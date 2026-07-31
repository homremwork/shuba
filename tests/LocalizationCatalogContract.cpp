#include "Localization/Catalog.hpp"
#include "Localization/CatalogDefinition.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
struct PoEntry final {
	std::string context;
	std::string singular;
	std::string plural;
	std::vector<std::string> translations;
	bool fuzzy{};
};

using PoEntryMap		   = std::unordered_map<std::string, PoEntry>;
using PlaceholderSignature = std::unordered_map<std::string, std::size_t>;
using PluralTranslationMap = std::unordered_map<std::size_t, std::string>;

struct ValidationResult final {
	bool valid{};
	std::string error;
};

enum class ActivePoField : std::uint8_t {
	None,
	Context,
	Singular,
	Plural,
	StaticTranslation,
	PluralTranslation,
};

[[nodiscard]] std::string make_identity(std::string_view context,
										std::string_view singular) {
	std::string identity;
	identity.reserve(context.size() + singular.size() + 1U);
	identity.append(context);
	identity.push_back('\x04');
	identity.append(singular);
	return identity;
}

[[nodiscard]] bool has_valid_utf8(std::string_view text) noexcept {
	std::size_t index{};
	while (index < text.size()) {
		const unsigned char first = static_cast<unsigned char>(text[index]);
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
		if (index + continuation_count >= text.size())
			return false;
		for (std::size_t offset = 1U; offset <= continuation_count; ++offset) {
			const unsigned char continuation =
				static_cast<unsigned char>(text[index + offset]);
			if ((continuation & 0xC0U) != 0x80U)
				return false;
			code_point = (code_point << 6U) | (continuation & 0x3FU);
		}
		if ((continuation_count == 2U && code_point < 0x800U)
			|| (continuation_count == 3U && code_point < 0x10000U)
			|| (code_point >= 0xD800U && code_point <= 0xDFFFU)
			|| code_point > 0x10FFFFU) {
			return false;
		}
		index += continuation_count + 1U;
	}
	return true;
}

[[nodiscard]] bool parse_po_quoted(std::string_view line,
								   std::string_view prefix,
								   std::string& value) {
	if (!line.starts_with(prefix) || line.size() < prefix.size() + 2U
		|| line[prefix.size()] != '"' || line.back() != '"') {
		return false;
	}
	value.clear();
	for (std::size_t index = prefix.size() + 1U; index + 1U < line.size();
		 ++index) {
		const char character = line[index];
		if (character != '\\') {
			value.push_back(character);
			continue;
		}
		if (++index + 1U >= line.size())
			return false;
		switch (line[index]) {
			case '\\':
				value.push_back('\\');
				break;
			case '"':
				value.push_back('"');
				break;
			case 'n':
				value.push_back('\n');
				break;
			default:
				return false;
		}
	}
	return true;
}

[[nodiscard]] bool parse_po_entries(std::string_view text, PoEntryMap& entries,
									std::string& error) {
	std::unordered_set<std::string> contexts;
	std::size_t offset{};
	while (offset < text.size()) {
		const std::size_t next		 = text.find("\n\n", offset);
		const std::string_view block = text.substr(
			offset, next == std::string_view::npos ? text.size() - offset
												   : next - offset);
		offset = next == std::string_view::npos ? text.size() : next + 2U;
		if (block.empty())
			continue;
		PoEntry entry;
		std::optional<std::string> static_translation;
		PluralTranslationMap plural_translations;
		ActivePoField active_field{ActivePoField::None};
		std::size_t active_plural_index{};
		bool has_singular_directive{};
		std::size_t line_offset{};
		while (line_offset < block.size()) {
			const std::size_t line_end = block.find('\n', line_offset);
			const std::string_view line =
				block.substr(line_offset, line_end == std::string_view::npos
											  ? block.size() - line_offset
											  : line_end - line_offset);
			line_offset = line_end == std::string_view::npos ? block.size()
															 : line_end + 1U;
			if (line.starts_with("#~")) {
				error = "PO file contains an obsolete entry.";
				return false;
			}
			if (line.starts_with("#,")
				&& line.find("fuzzy") != std::string_view::npos)
				entry.fuzzy = true;
			if (line.empty() || line.starts_with('#'))
				continue;

			std::string value;
			if (line.starts_with("msgctxt")) {
				if (!parse_po_quoted(line, "msgctxt ", value)) {
					error = "Unsupported PO context syntax.";
					return false;
				}
				entry.context = std::move(value);
				active_field  = ActivePoField::Context;
			} else if (line.starts_with("msgid_plural")) {
				if (!parse_po_quoted(line, "msgid_plural ", value)) {
					error = "Unsupported PO plural-source syntax.";
					return false;
				}
				entry.plural = std::move(value);
				active_field = ActivePoField::Plural;
			} else if (line.starts_with("msgid")) {
				if (!parse_po_quoted(line, "msgid ", value)
					|| has_singular_directive) {
					error = "Unsupported PO singular-source syntax.";
					return false;
				}
				entry.singular		   = std::move(value);
				has_singular_directive = true;
				active_field		   = ActivePoField::Singular;
			} else if (line.starts_with("msgstr[")) {
				const std::size_t close = line.find("] ");
				std::size_t plural_index{};
				const std::string_view index_text =
					close == std::string_view::npos
						? std::string_view{}
						: line.substr(7U, close - 7U);
				const std::from_chars_result parsed_index = std::from_chars(
					index_text.data(), index_text.data() + index_text.size(),
					plural_index);
				if (close == std::string_view::npos || index_text.empty()
					|| parsed_index.ec != std::errc{}
					|| parsed_index.ptr != index_text.data() + index_text.size()
					|| !parse_po_quoted(line.substr(close + 2U), "", value)) {
					error = "Unsupported PO plural translation syntax.";
					return false;
				}
				if (!plural_translations.emplace(plural_index, std::move(value))
						 .second) {
					error = "PO plural translation index is duplicated.";
					return false;
				}
				active_field		= ActivePoField::PluralTranslation;
				active_plural_index = plural_index;
			} else if (line.starts_with("msgstr")) {
				if (!parse_po_quoted(line, "msgstr ", value)
					|| static_translation.has_value()) {
					error = "Unsupported PO static translation syntax.";
					return false;
				}
				static_translation = std::move(value);
				active_field	   = ActivePoField::StaticTranslation;
			} else if (line.starts_with('"')) {
				if (!parse_po_quoted(line, "", value)) {
					error = "Unsupported PO continuation syntax.";
					return false;
				}
				switch (active_field) {
					case ActivePoField::Context:
						entry.context.append(value);
						break;
					case ActivePoField::Singular:
						entry.singular.append(value);
						break;
					case ActivePoField::Plural:
						entry.plural.append(value);
						break;
					case ActivePoField::StaticTranslation:
						static_translation->append(value);
						break;
					case ActivePoField::PluralTranslation:
						plural_translations.at(active_plural_index)
							.append(value);
						break;
					case ActivePoField::None:
						error = "PO continuation has no preceding field.";
						return false;
				}
			} else {
				error = "Unsupported PO directive: " + std::string{line};
				return false;
			}
		}
		if (!has_singular_directive)
			continue;
		if (entry.singular.empty() && entry.context.empty())
			continue;
		if (entry.singular.empty()) {
			error = "PO entry has no singular source identity: "
					+ std::string{block};
			return false;
		}
		if (entry.fuzzy) {
			error = "PO file contains a fuzzy entry.";
			return false;
		}
		if (entry.context.empty()) {
			error = "PO translation entry has no context.";
			return false;
		}
		if (entry.plural.empty()) {
			if (!static_translation.has_value()
				|| !plural_translations.empty()) {
				error = "PO static entry has an invalid translation shape.";
				return false;
			}
			entry.translations.push_back(std::move(*static_translation));
		} else {
			if (static_translation.has_value()
				|| plural_translations.size() != 3U) {
				error =
					"PO plural entry must have exactly indexes 0, 1, and 2.";
				return false;
			}
			for (std::size_t plural_index{}; plural_index < 3U;
				 ++plural_index) {
				PluralTranslationMap::iterator translation =
					plural_translations.find(plural_index);
				if (translation == plural_translations.end()) {
					error =
						"PO plural entry must have exactly indexes 0, 1, and "
						"2.";
					return false;
				}
				entry.translations.push_back(std::move(translation->second));
			}
		}
		if (!contexts.emplace(entry.context).second) {
			error = "PO file contains a duplicate context.";
			return false;
		}
		const std::string identity =
			make_identity(entry.context, entry.singular);
		if (!entries.emplace(identity, std::move(entry)).second) {
			error =
				"PO file contains a duplicate context and singular identity.";
			return false;
		}
	}
	return true;
}

void write_po_string(std::ostream& output, std::string_view field,
					 std::string_view value) {
	output << field << " \"";
	for (const char character : value) {
		switch (character) {
			case '\\':
				output << "\\\\";
				break;
			case '\"':
				output << "\\\"";
				break;
			case '\n':
				output << "\\n";
				break;
			default:
				output << character;
				break;
		}
	}
	output << "\"\n";
}

[[nodiscard]] bool write_pot(std::string_view path) {
	std::ofstream output{std::string{path}, std::ios::binary | std::ios::trunc};
	if (!output) {
		std::cerr << "Could not open the generated POT path.\n";
		return false;
	}

	const shuba::localization::detail::CatalogDefinitionsResult definitions =
		shuba::localization::detail::catalog_definitions();
	if (!definitions.has_value()) {
		std::cerr << definitions.error().technical_details << '\n';
		return false;
	}

	output << "# Shuba UI English source catalog.\n"
		   << "# Generated from source-owned localization definitions; do not "
			  "edit manually.\n"
		   << "msgid \"\"\n"
		   << "msgstr \"\"\n"
		   << "\"Project-Id-Version: Shuba UI\\n\"\n"
		   << "\"Content-Type: text/plain; charset=UTF-8\\n\"\n"
		   << "\"X-Shuba-Catalog-Id: shuba-production-v1\\n\"\n\n";
	for (const shuba::localization::detail::CatalogDefinition& definition :
		 *definitions) {
		write_po_string(output, "msgctxt", definition.context);
		write_po_string(output, "msgid", definition.singular);
		if (definition.is_plural()) {
			write_po_string(output, "msgid_plural", definition.plural);
			output << "msgstr[0] \"\"\nmsgstr[1] \"\"\nmsgstr[2] \"\"\n";
		} else {
			output << "msgstr \"\"\n";
		}
		if (&definition != &definitions->back())
			output << '\n';
	}
	output.flush();
	return output.good();
}

[[nodiscard]] bool is_placeholder_character(char character) noexcept {
	return (character >= 'a' && character <= 'z')
		   || (character >= 'A' && character <= 'Z')
		   || (character >= '0' && character <= '9') || character == '_';
}

[[nodiscard]] std::optional<PlaceholderSignature> placeholder_signature(
	std::string_view message) {
	PlaceholderSignature placeholders;
	std::size_t offset{};
	while (offset < message.size()) {
		if (message[offset] == '}')
			return std::nullopt;
		if (message[offset] != '{') {
			++offset;
			continue;
		}
		const std::size_t end = message.find('}', offset + 1U);
		if (end == std::string_view::npos)
			return std::nullopt;
		const std::string_view name =
			message.substr(offset + 1U, end - offset - 1U);
		if (name.empty())
			return std::nullopt;
		for (const char character : name)
			if (!is_placeholder_character(character))
				return std::nullopt;
		++placeholders[std::string{name}];
		offset = end + 1U;
	}
	return placeholders;
}

[[nodiscard]] ValidationResult validate_catalog_bytes(
	std::string_view catalog_bytes) {
	if (!has_valid_utf8(catalog_bytes))
		return {.error = "Tracked Russian PO is not valid UTF-8."};

	PoEntryMap entries;
	std::string parse_error;
	if (!parse_po_entries(catalog_bytes, entries, parse_error))
		return {.error = std::move(parse_error)};
	const shuba::localization::detail::CatalogDefinitionsResult definitions =
		shuba::localization::detail::catalog_definitions();
	if (!definitions.has_value())
		return {.error = definitions.error().technical_details};
	if (entries.size() != definitions->size())
		return {.error =
					"Tracked Russian PO does not have the exact source entry "
					"count."};
	for (const shuba::localization::detail::CatalogDefinition& definition :
		 *definitions) {
		const PoEntryMap::const_iterator entry = entries.find(
			make_identity(definition.context, definition.singular));
		if (entry == entries.end() || entry->second.plural != definition.plural)
			return {.error =
						"Tracked Russian PO differs from a source identity."};
		const std::size_t expected_translations =
			definition.is_plural() ? 3U : 1U;
		if (entry->second.translations.size() != expected_translations) {
			return {.error =
						"Tracked Russian PO has an invalid translation-form "
						"count."};
		}
		const std::optional<PlaceholderSignature> source_placeholders =
			placeholder_signature(definition.singular);
		if (!source_placeholders.has_value())
			return {
				.error =
					"A source localization message has invalid placeholders."};
		if (definition.is_plural()) {
			const std::optional<PlaceholderSignature> plural_placeholders =
				placeholder_signature(definition.plural);
			if (!plural_placeholders.has_value()
				|| *plural_placeholders != *source_placeholders) {
				return {.error =
							"A source localization plural pair has placeholder "
							"drift."};
			}
		}
		for (const std::string& translation : entry->second.translations) {
			if (translation.empty())
				return {.error =
							"Tracked Russian PO has an empty translation."};
			const std::optional<PlaceholderSignature> translation_placeholders =
				placeholder_signature(translation);
			if (!translation_placeholders.has_value()
				|| *translation_placeholders != *source_placeholders) {
				return {.error = "Tracked Russian PO has placeholder drift."};
			}
		}
	}

	const shuba::localization::detail::CatalogLoadResult loaded =
		shuba::localization::detail::load_catalog(catalog_bytes);
	if (loaded.accepted())
		return {.valid = true};
	if (!loaded.issues.empty()) {
		return {.error = loaded.issues.front().code + ": "
						 + loaded.issues.front().technical_details};
	}
	return {.error = "The production parser rejected the tracked Russian PO."};
}

[[nodiscard]] bool read_catalog(std::string_view path,
								std::string& catalog_bytes) {
	std::ifstream input{std::string{path}, std::ios::binary};
	if (!input) {
		std::cerr << "Could not open the tracked Russian PO path.\n";
		return false;
	}
	catalog_bytes.assign(std::istreambuf_iterator<char>{input}, {});
	return true;
}

[[nodiscard]] bool validate_catalog(std::string_view path) {
	std::string catalog_bytes;
	if (!read_catalog(path, catalog_bytes))
		return false;
	const ValidationResult validation = validate_catalog_bytes(catalog_bytes);
	if (validation.valid)
		return true;
	std::cerr << validation.error << '\n';
	return false;
}

[[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>>
find_nonempty_directive_value(std::string_view catalog,
							  std::string_view directive) {
	std::size_t line_begin{};
	while (line_begin < catalog.size()) {
		const std::size_t line_end = catalog.find('\n', line_begin);
		const std::size_t bounded_end =
			line_end == std::string_view::npos ? catalog.size() : line_end;
		const std::string_view line =
			catalog.substr(line_begin, bounded_end - line_begin);
		if (line.starts_with(directive) && line.size() > directive.size() + 2U
			&& line[directive.size()] == '"' && line.back() == '"') {
			return std::pair{line_begin + directive.size() + 1U,
							 line_begin + line.size() - 1U};
		}
		if (line_end == std::string_view::npos)
			break;
		line_begin = line_end + 1U;
	}
	return std::nullopt;
}

[[nodiscard]] bool expect_rejection(std::string_view probe,
									std::string_view catalog,
									std::string_view expected_error) {
	const ValidationResult validation = validate_catalog_bytes(catalog);
	if (!validation.valid && validation.error == expected_error)
		return true;
	std::cerr << "Localization failure probe '" << probe << "' produced '"
			  << (validation.valid ? "accepted" : validation.error)
			  << "' instead of '" << expected_error << "'.\n";
	return false;
}

[[nodiscard]] bool run_failure_probes(std::string_view path) {
	std::string baseline;
	if (!read_catalog(path, baseline))
		return false;
	const ValidationResult baseline_validation =
		validate_catalog_bytes(baseline);
	if (!baseline_validation.valid) {
		std::cerr << "Failure-probe baseline is invalid: "
				  << baseline_validation.error << '\n';
		return false;
	}

	std::string obsolete = baseline;
	obsolete.append(
		"\n# Translator note before an obsolete record.\n"
		"#~ msgctxt \"obsolete.test\"\n#~ msgid \"Obsolete\"\n"
		"#~ msgstr \"Устаревшее\"\n");
	if (!expect_rejection("obsolete entry", std::move(obsolete),
						  "PO file contains an obsolete entry.")) {
		return false;
	}

	std::string fuzzy				= baseline;
	const std::size_t first_context = fuzzy.find("msgctxt ");
	if (first_context == std::string::npos)
		return false;
	fuzzy.insert(first_context, "#,fuzzy\n");
	if (!expect_rejection("fuzzy entry", std::move(fuzzy),
						  "PO file contains a fuzzy entry.")) {
		return false;
	}

	std::string source_identity = baseline;
	const std::optional<std::pair<std::size_t, std::size_t>> source_range =
		find_nonempty_directive_value(source_identity, "msgid ");
	if (!source_range.has_value())
		return false;
	source_identity.insert(source_range->second, " changed");
	if (!expect_rejection(
			"source identity", std::move(source_identity),
			"Tracked Russian PO differs from a source identity.")) {
		return false;
	}

	std::string empty_translation = baseline;
	const std::optional<std::pair<std::size_t, std::size_t>> translation_range =
		find_nonempty_directive_value(empty_translation, "msgstr ");
	if (!translation_range.has_value())
		return false;
	empty_translation.erase(
		translation_range->first,
		translation_range->second - translation_range->first);
	if (!expect_rejection("empty translation", std::move(empty_translation),
						  "Tracked Russian PO has an empty translation.")) {
		return false;
	}

	std::string placeholder_drift = baseline;
	std::size_t placeholder_line  = placeholder_drift.find("msgstr");
	while (placeholder_line != std::string::npos) {
		const std::size_t line_end =
			placeholder_drift.find('\n', placeholder_line);
		const std::size_t placeholder_begin =
			placeholder_drift.find('{', placeholder_line);
		if (placeholder_begin != std::string::npos
			&& (line_end == std::string::npos
				|| placeholder_begin < line_end)) {
			const std::size_t placeholder_end =
				placeholder_drift.find('}', placeholder_begin);
			if (placeholder_end == std::string::npos)
				return false;
			const std::string duplicate =
				" "
				+ placeholder_drift.substr(
					placeholder_begin,
					placeholder_end - placeholder_begin + 1U);
			placeholder_drift.insert(placeholder_end + 1U, duplicate);
			break;
		}
		placeholder_line = placeholder_drift.find("msgstr", line_end);
	}
	if (placeholder_line == std::string::npos
		|| !expect_rejection("placeholder count", std::move(placeholder_drift),
							 "Tracked Russian PO has placeholder drift.")) {
		return false;
	}

	std::string plural_index	= baseline;
	const std::size_t index_one = plural_index.find("msgstr[1]");
	if (index_one == std::string::npos)
		return false;
	plural_index.replace(index_one, std::string_view{"msgstr[1]"}.size(),
						 "msgstr[0]");
	return expect_rejection("plural indexes", std::move(plural_index),
							"PO plural translation index is duplicated.");
}
}	 // namespace

int main(int argc, char* argv[]) {
	if (argc == 3 && std::string_view{argv[1]} == "--write-pot")
		return write_pot(argv[2]) ? 0 : 1;
	if (argc == 3 && std::string_view{argv[1]} == "--validate-catalog")
		return validate_catalog(argv[2]) ? 0 : 1;
	if (argc == 3 && std::string_view{argv[1]} == "--run-failure-probes")
		return run_failure_probes(argv[2]) ? 0 : 1;
	std::cerr
		<< "Usage: shuba_localization_contract --write-pot <path>\n"
		<< "       shuba_localization_contract --validate-catalog <path>\n"
		<< "       shuba_localization_contract --run-failure-probes <path>\n";
	return 2;
}
