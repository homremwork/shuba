#include "Localization/Catalog.hpp"

#include "Localization/CatalogDefinition.hpp"

// The upstream header declares an inline stream overload but includes only
// iosfwd. The production adapter uses the iterator-range API, while this
// complete declaration keeps the upstream public header portable.
#include <istream>

#include <spiritless_po.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

namespace shuba::localization::detail {
namespace {
constexpr std::string_view production_catalog_id{"shuba-production-v1"};
constexpr std::string_view russian_plural_forms{
	"nplurals=3; plural=(n%10==1 && n%100!=11 ? 0 : n%10>=2 && n%10<=4 "
	"&& (n%100<10 || n%100>=20) ? 1 : 2);"};
constexpr std::string_view context_separator{"\x04"};

[[nodiscard]] CatalogLoadResult rejection(std::string code,
										  std::string technical_details) {
	return CatalogLoadResult{
		.issues = {{.code			   = std::move(code),
					.technical_details = std::move(technical_details)}}};
}

[[nodiscard]] bool has_embedded_nul(std::string_view value) noexcept {
	return std::ranges::find(value, '\0') != value.end();
}

[[nodiscard]] bool is_valid_utf8(std::string_view value) noexcept {
	std::size_t index{};
	while (index < value.size()) {
		const std::uint8_t first = static_cast<std::uint8_t>(value[index]);
		if (first <= 0x7FU) {
			++index;
			continue;
		}

		std::size_t continuation_count{};
		std::uint32_t code_point{};
		if (first >= 0xC2U && first <= 0xDFU) {
			continuation_count = 1U;
			code_point		   = first & 0x1FU;
		} else if (first >= 0xE0U && first <= 0xEFU) {
			continuation_count = 2U;
			code_point		   = first & 0x0FU;
		} else if (first >= 0xF0U && first <= 0xF4U) {
			continuation_count = 3U;
			code_point		   = first & 0x07U;
		} else {
			return false;
		}

		if (index + continuation_count >= value.size())
			return false;
		for (std::size_t offset = 1U; offset <= continuation_count; ++offset) {
			const std::uint8_t continuation =
				static_cast<std::uint8_t>(value[index + offset]);
			if ((continuation & 0xC0U) != 0x80U)
				return false;
			code_point = (code_point << 6U) | (continuation & 0x3FU);
		}

		if ((continuation_count == 2U && code_point < 0x800U)
			|| (continuation_count == 3U && code_point < 0x10000U)
			|| (code_point >= 0xD800U && code_point <= 0xDFFFU)
			|| code_point > 0x10FFFFU)
			return false;
		index += continuation_count + 1U;
	}
	return true;
}

[[nodiscard]] std::string combined_id(std::string_view context,
									  std::string_view message_id) {
	std::string result;
	result.reserve(context.size() + context_separator.size()
				   + message_id.size());
	result.append(context);
	result.append(context_separator);
	result.append(message_id);
	return result;
}

[[nodiscard]] bool has_exact_metadata(const spiritless_po::Catalog& catalog) {
	const spiritless_po::MetadataParser::MapT& metadata = catalog.GetMetadata();
	const auto content_type = metadata.find("Content-Type");
	const auto plural_forms = metadata.find("Plural-Forms");
	const auto language		= metadata.find("Language");
	const auto catalog_id	= metadata.find("X-Shuba-Catalog-Id");
	return content_type != metadata.end()
		   && content_type->second == "text/plain; charset=UTF-8"
		   && plural_forms != metadata.end()
		   && plural_forms->second == russian_plural_forms
		   && language != metadata.end() && language->second == "ru"
		   && catalog_id != metadata.end()
		   && catalog_id->second == production_catalog_id;
}

[[nodiscard]] bool has_static_translation(const spiritless_po::Catalog& catalog,
										  std::string_view context,
										  std::string_view english) {
	const auto& index	= catalog.GetIndex();
	const auto& strings = catalog.GetStringTable();
	const auto entry	= index.find(combined_id(context, english));
	return entry != index.end() && entry->second.totalPlurals == 1U
		   && entry->second.stringTableIndex < strings.size()
		   && !strings[entry->second.stringTableIndex].empty();
}

[[nodiscard]] bool has_plural_translation(const spiritless_po::Catalog& catalog,
										  std::string_view context,
										  std::string_view singular) {
	const auto& index	= catalog.GetIndex();
	const auto& strings = catalog.GetStringTable();
	const auto entry	= index.find(combined_id(context, singular));
	if (entry == index.end() || entry->second.totalPlurals != 3U)
		return false;
	const std::size_t start = entry->second.stringTableIndex;
	if (start > strings.size() || strings.size() - start < 3U)
		return false;
	for (std::size_t offset{}; offset < 3U; ++offset)
		if (strings[start + offset].empty())
			return false;
	return true;
}

[[nodiscard]] bool has_exact_inventory(const spiritless_po::Catalog& catalog) {
	const auto& index		  = catalog.GetIndex();
	const auto metadata_entry = index.find("");
	if (metadata_entry == index.end()
		|| metadata_entry->second.totalPlurals != 1U)
		return false;

	const CatalogDefinitionsResult definitions = catalog_definitions();
	if (!definitions.has_value() || index.size() != definitions->size() + 1U)
		return false;
	for (const CatalogDefinition& definition : *definitions) {
		if (definition.is_plural()) {
			if (!has_plural_translation(catalog, definition.context,
										definition.singular))
				return false;
		} else if (!has_static_translation(catalog, definition.context,
										   definition.singular)) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] unsigned long checked_count(std::uint64_t count) noexcept {
	constexpr std::uint64_t maximum =
		static_cast<std::uint64_t>(std::numeric_limits<unsigned long>::max());
	return static_cast<unsigned long>(std::min(count, maximum));
}
}	 // namespace

struct Catalog::State final {
	spiritless_po::Catalog catalog;
	mutable std::mutex mutex;
};

bool CatalogLoadResult::accepted() const noexcept {
	return catalog != nullptr && issues.empty();
}

Catalog::Catalog(std::shared_ptr<State> state_value)
	: state(std::move(state_value)) {}
Catalog::Catalog(Catalog&&) noexcept			= default;
Catalog& Catalog::operator=(Catalog&&) noexcept = default;
Catalog::~Catalog()								= default;

std::string Catalog::pgettext(std::string_view context,
							  std::string_view message_id) const {
	const std::lock_guard lock{state->mutex};
	return state->catalog.pgettext(std::string{context},
								   std::string{message_id});
}

std::string Catalog::npgettext(std::string_view context,
							   std::string_view singular_message_id,
							   std::string_view plural_message_id,
							   std::uint64_t count) const {
	const std::lock_guard lock{state->mutex};
	return state->catalog.npgettext(
		std::string{context}, std::string{singular_message_id},
		std::string{plural_message_id}, checked_count(count));
}

CatalogLoadResult load_catalog(std::string_view catalog_bytes) {
	if (catalog_bytes.empty())
		return rejection("localization-catalog-empty",
						 "The PO byte range is empty.");
	if (has_embedded_nul(catalog_bytes))
		return rejection("localization-catalog-embedded-nul",
						 "The PO byte range contains an embedded NUL byte.");
	if (!is_valid_utf8(catalog_bytes))
		return rejection("localization-catalog-invalid-utf8",
						 "The PO byte range is not valid UTF-8.");

	try {
		std::shared_ptr<Catalog::State> state =
			std::make_shared<Catalog::State>();
		const bool parsed =
			state->catalog.Add(catalog_bytes.begin(), catalog_bytes.end());
		if (!parsed || !state->catalog.GetError().empty())
			return rejection("localization-catalog-parse-failed",
							 "spiritless_po reported a PO parse or "
							 "plural-expression error.");
		const spiritless_po::Catalog::StatisticsT& statistics =
			state->catalog.GetStatistics();
		if (statistics.metadataCount != 1U || statistics.discardedCount != 0U)
			return rejection(
				"localization-catalog-duplicate-or-metadata-invalid",
				"The catalog must contain one metadata entry and no discarded "
				"IDs.");
		if (!has_exact_metadata(state->catalog))
			return rejection("localization-catalog-metadata-invalid",
							 "The catalog metadata does not match Shuba's "
							 "Russian UTF-8 contract.");
		if (!has_exact_inventory(state->catalog))
			return rejection("localization-catalog-source-definition-mismatch",
							 "The catalog does not exactly match the "
							 "source-owned message definitions.");
		return CatalogLoadResult{.catalog = std::shared_ptr<const Catalog>{
									 new Catalog{std::move(state)}}};
	} catch (const std::exception& exception) {
		return rejection("localization-catalog-initialization-exception",
						 exception.what());
	} catch (...) {
		return rejection(
			"localization-catalog-initialization-exception",
			"A non-standard exception escaped catalog initialization.");
	}
}
}	 // namespace shuba::localization::detail
