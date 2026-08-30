#include "Localization/Facade.hpp"

#include "Localization/MessageCatalog.hpp"

#include <algorithm>
#include <array>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace shuba::localization {
namespace {
using detail::FormatterMessage;
using detail::PresentationMessage;

[[nodiscard]] std::string replace_placeholder(std::string source,
											  std::string_view placeholder,
											  std::string_view value) {
	std::size_t position{};
	while ((position = source.find(placeholder, position))
		   != std::string::npos) {
		source.replace(position, placeholder.size(), value);
		position += value.size();
	}
	return source;
}

[[nodiscard]] std::string join_nonempty_strings(
	std::span<const std::string> values, std::string_view separator) {
	std::string result;
	for (const std::string& value : values) {
		if (value.empty())
			continue;
		if (!result.empty())
			result.append(separator);
		result.append(value);
	}
	return result;
}

constexpr std::string_view omitted_clause_marker{"\x1fshuba-omit-clause\x1f"};

[[nodiscard]] std::string omit_empty_semicolon_clauses(
	std::string text,
	std::initializer_list<std::pair<std::string_view, std::string_view>>
		fields) {
	bool has_omitted_clause{};
	for (const auto& [placeholder, value] : fields) {
		if (!value.empty())
			continue;
		text			   = replace_placeholder(std::move(text), placeholder,
												 omitted_clause_marker);
		has_omitted_clause = true;
	}
	if (!has_omitted_clause)
		return text;

	std::string result;
	std::size_t clause_start{};
	while (clause_start <= text.size()) {
		const std::size_t separator = text.find(';', clause_start);
		const std::size_t clause_end =
			separator == std::string::npos ? text.size() : separator;
		std::string_view clause{text};
		clause = clause.substr(clause_start, clause_end - clause_start);
		while (!clause.empty() && clause.front() == ' ')
			clause.remove_prefix(1U);
		while (!clause.empty() && clause.back() == ' ')
			clause.remove_suffix(1U);
		if (!clause.empty()
			&& clause.find(omitted_clause_marker) == std::string_view::npos) {
			if (!result.empty())
				result += "; ";
			result.append(clause);
		}
		if (separator == std::string::npos)
			break;
		clause_start = separator + 1U;
	}
	return result;
}

[[nodiscard]] std::string format_zoom_scale(float scale) {
	std::ostringstream stream;
	stream << std::fixed << std::setprecision(2) << scale;
	return stream.str();
}

[[nodiscard]] const PresentationMessage& local_template_message(
	detail::TemplateMessageIndex index) noexcept {
	return detail::template_message(index);
}

}	 // namespace

std::string Localization::translate_template(
	const PresentationMessage& message) const {
	return translate_message(message.context, message.english);
}

std::string Localization::format_plural(const FormatterMessage& message,
										std::uint64_t count) const {
	return replace_placeholder(
		translate_plural_message(message.context, message.singular,
								 message.plural, count),
		"{count}", std::to_string(count));
}

std::string Localization::recovery_actions(
	std::span<const ui::RecoveryAction> actions) const {
	std::string labels;
	for (const ui::RecoveryAction action : actions) {
		if (!labels.empty())
			labels += " · ";
		labels += recovery_action_label(action);
	}
	if (labels.empty())
		labels = "—";
	return replace_placeholder(text(MessageId::RecoveryActions), "{actions}",
							   labels);
}

std::string Localization::recovery_counts(
	const RecoveryCountsFields& fields) const {
	std::string result = text(MessageId::RecoveryCounts);
	const std::array values{
		std::pair{"{accepted_items}", std::to_string(fields.accepted_items)},
		std::pair{"{accepted_storages}",
				  std::to_string(fields.accepted_storages)},
		std::pair{"{accepted_photos}", std::to_string(fields.accepted_photos)},
		std::pair{"{skipped_items}", std::to_string(fields.skipped_items)},
		std::pair{"{skipped_storages}",
				  std::to_string(fields.skipped_storages)},
		std::pair{"{skipped_photos}", std::to_string(fields.skipped_photos)},
		std::pair{"{broken_references}",
				  std::to_string(fields.broken_references)},
		std::pair{"{orphan_media}", std::to_string(fields.orphan_media)},
	};
	for (const auto& [placeholder, value] : values)
		result = replace_placeholder(std::move(result), placeholder, value);
	return result;
}

std::string Localization::import_validation_summary(
	const ImportValidationFields& fields) const {
	std::string result = text(MessageId::ImportValidationSummary);
	result			   = replace_placeholder(std::move(result), "{load_status}",
											 fields.load_status);
	result = replace_placeholder(std::move(result), "{accepted_items}",
								 std::to_string(fields.accepted_items));
	result = replace_placeholder(std::move(result), "{accepted_storages}",
								 std::to_string(fields.accepted_storages));
	result = replace_placeholder(std::move(result), "{accepted_photos}",
								 std::to_string(fields.accepted_photos));
	result = replace_placeholder(std::move(result), "{broken_references}",
								 std::to_string(fields.broken_references));
	return replace_placeholder(std::move(result), "{orphan_media}",
							   std::to_string(fields.orphan_media));
}

std::string Localization::shell_status(const ShellStatusFields& fields) const {
	const detail::TemplateMessageIndex index =
		fields.demo_catalog_active
			? detail::TemplateMessageIndex::ShellStatusDemo
			: detail::TemplateMessageIndex::ShellStatus;
	std::string result = translate_template(local_template_message(index));
	result = replace_placeholder(std::move(result), "{load_status}",
								 catalog_load_status_label(fields.load_status));
	result = replace_placeholder(std::move(result), "{source}",
								 startup_source_label(fields.source));
	result = replace_placeholder(std::move(result), "{items}",
								 std::to_string(fields.item_count));
	return replace_placeholder(std::move(result), "{storages}",
							   std::to_string(fields.storage_count));
}

std::string Localization::technical_information_heading() const {
	return translate_template(local_template_message(
		detail::TemplateMessageIndex::TechnicalInformationHeading));
}

std::string Localization::photo_count(std::uint64_t count) const {
	return translate_plural_message("common.photo_count", "photo", "photos",
									count);
}

std::string Localization::catalog_result_count(std::uint64_t count) const {
	return format_plural(
		detail::plural_message(detail::PluralMessageIndex::CatalogResultCount),
		count);
}

std::string Localization::catalog_filter_clauses(
	const CatalogFilterSummaryFields& fields) const {
	std::vector<std::string> clauses;
	const std::string categories =
		join_nonempty_strings(fields.categories, ", ");
	const std::string statuses = join_nonempty_strings(fields.statuses, ", ");
	if (!categories.empty())
		clauses.push_back(replace_placeholder(
			translate_template(local_template_message(
				detail::TemplateMessageIndex::CatalogFilterSummaryCategories)),
			"{categories}", categories));
	if (!statuses.empty())
		clauses.push_back(replace_placeholder(
			translate_template(local_template_message(
				detail::TemplateMessageIndex::CatalogFilterSummaryStatuses)),
			"{statuses}", statuses));
	if (fields.storage.has_value() && !fields.storage->empty()) {
		clauses.push_back(replace_placeholder(
			translate_template(local_template_message(
				detail::TemplateMessageIndex::CatalogFilterSummaryStorage)),
			"{storage}", *fields.storage));
		if (fields.include_nested_storage)
			clauses.push_back(translate_template(local_template_message(
				detail::TemplateMessageIndex::CatalogFilterSummaryNested)));
	} else if (fields.storage_unassigned) {
		clauses.push_back(translate_template(
			local_template_message(detail::TemplateMessageIndex::
									   CatalogFilterSummaryStorageUnassigned)));
	}
	if (fields.photo_presence.has_value() && !fields.photo_presence->empty())
		clauses.push_back(replace_placeholder(
			translate_template(local_template_message(
				detail::TemplateMessageIndex::CatalogFilterSummaryPhotos)),
			"{photo_presence}", *fields.photo_presence));
	if (fields.listed_shortcut)
		clauses.push_back(translate_template(local_template_message(
			detail::TemplateMessageIndex::CatalogFilterSummaryListedShortcut)));
	if (fields.sold_shortcut)
		clauses.push_back(translate_template(local_template_message(
			detail::TemplateMessageIndex::CatalogFilterSummarySoldShortcut)));
	if (fields.include_archived)
		clauses.push_back(translate_template(
			local_template_message(detail::TemplateMessageIndex::
									   CatalogFilterSummaryIncludeArchived)));
	if (clauses.empty())
		return translate_template(local_template_message(
			detail::TemplateMessageIndex::CatalogFilterSummaryNone));
	return join_nonempty_strings(clauses, " · ");
}

std::string Localization::catalog_filter_summary(
	CatalogFilterSummaryKind kind, std::string_view filters) const {
	const detail::TemplateMessageIndex index =
		kind == CatalogFilterSummaryKind::Applied
			? detail::TemplateMessageIndex::AppliedCatalogFilterSummary
			: detail::TemplateMessageIndex::DraftCatalogFilterSummary;
	return replace_placeholder(
		translate_template(local_template_message(index)), "{filters}",
		filters);
}

std::string Localization::field_value(std::string_view label,
									  std::string_view value) const {
	const detail::StaticMessage& message =
		detail::static_template_message(MessageId::ScreenFieldValue);
	std::string result = translate_message(message.context, message.english);
	result = replace_placeholder(std::move(result), "{label}", label);
	return replace_placeholder(std::move(result), "{value}", value);
}

std::string Localization::tags_summary(std::string_view tag_list) const {
	const detail::StaticMessage& message =
		detail::static_template_message(MessageId::ScreenTagsSummary);
	return replace_placeholder(
		translate_message(message.context, message.english), "{tag_list}",
		tag_list);
}

std::string Localization::listing_summary(std::string_view marketplace,
										  std::string_view url,
										  std::string_view price,
										  std::string_view note) const {
	std::string result = omit_empty_semicolon_clauses(
		translate_template(local_template_message(
			detail::TemplateMessageIndex::ScreenListingSummary)),
		{{"{marketplace}", marketplace},
		 {"{url}", url},
		 {"{price}", price},
		 {"{note}", note}});
	result =
		replace_placeholder(std::move(result), "{marketplace}", marketplace);
	result = replace_placeholder(std::move(result), "{url}", url);
	result = replace_placeholder(std::move(result), "{price}", price);
	return replace_placeholder(std::move(result), "{note}", note);
}

std::string Localization::finance_summary(std::string_view source,
										  std::string_view acquisition_cost,
										  std::string_view sale_price,
										  std::string_view expenses,
										  std::string_view profit) const {
	std::string result = omit_empty_semicolon_clauses(
		translate_template(local_template_message(
			detail::TemplateMessageIndex::ScreenFinanceSummary)),
		{{"{source}", source},
		 {"{acquisition_cost}", acquisition_cost},
		 {"{sale_price}", sale_price},
		 {"{expenses}", expenses},
		 {"{profit}", profit}});
	result = replace_placeholder(std::move(result), "{source}", source);
	result = replace_placeholder(std::move(result), "{acquisition_cost}",
								 acquisition_cost);
	result = replace_placeholder(std::move(result), "{sale_price}", sale_price);
	result = replace_placeholder(std::move(result), "{expenses}", expenses);
	return replace_placeholder(std::move(result), "{profit}", profit);
}

std::string Localization::storage_choice(std::string_view display_name,
										 std::string_view type,
										 std::string_view location) const {
	std::string result = omit_empty_semicolon_clauses(
		translate_template(local_template_message(
			detail::TemplateMessageIndex::StorageChoice)),
		{{"{type}", type}, {"{path_or_location}", location}});
	result =
		replace_placeholder(std::move(result), "{display_name}", display_name);
	result = replace_placeholder(std::move(result), "{type}", type);
	return replace_placeholder(std::move(result), "{path_or_location}",
							   location);
}

std::string Localization::catalog_warning_label(CatalogWarning warning) const {
	const std::span<const PresentationMessage> messages =
		detail::catalog_warning_messages();
	const std::size_t index = static_cast<std::size_t>(warning);
	return translate_template(messages[index < messages.size() ? index : 0U]);
}

std::string Localization::item_header(const ItemHeaderFields& fields) const {
	std::string result = omit_empty_semicolon_clauses(
		translate_template(
			local_template_message(detail::TemplateMessageIndex::ItemHeader)),
		{{"{storage_path}", fields.storage_path},
		 {"{warnings}", fields.warnings}});
	const std::array values{std::pair{"{name}", fields.name},
							std::pair{"{photo_state}", fields.photo_state},
							std::pair{"{category}", fields.category},
							std::pair{"{status}", fields.status},
							std::pair{"{storage_path}", fields.storage_path},
							std::pair{"{warnings}", fields.warnings}};
	for (const auto& [placeholder, value] : values)
		result = replace_placeholder(std::move(result), placeholder, value);
	return result;
}

std::string Localization::storage_header(
	const StorageHeaderFields& fields) const {
	std::string result = omit_empty_semicolon_clauses(
		translate_template(local_template_message(
			detail::TemplateMessageIndex::StorageHeader)),
		{{"{type}", fields.type},
		 {"{path}", fields.path},
		 {"{location}", fields.location},
		 {"{notes}", fields.notes},
		 {"{warnings}", fields.warnings}});
	const std::array values{std::pair{"{name}", fields.name},
							std::pair{"{type}", fields.type},
							std::pair{"{path}", fields.path},
							std::pair{"{location}", fields.location},
							std::pair{"{notes}", fields.notes},
							std::pair{"{warnings}", fields.warnings}};
	for (const auto& [placeholder, value] : values)
		result = replace_placeholder(std::move(result), placeholder, value);
	return result;
}

std::string Localization::item_result_card(
	const ItemResultFields& fields) const {
	std::string result = omit_empty_semicolon_clauses(
		translate_template(local_template_message(
			detail::TemplateMessageIndex::ItemResultCard)),
		{{"{category}", fields.category},
		 {"{status}", fields.status},
		 {"{location}", fields.location},
		 {"{details}", fields.details},
		 {"{warnings}", fields.warnings}});
	const std::array values{std::pair{"{title}", fields.title},
							std::pair{"{photo_state}", fields.photo_state},
							std::pair{"{category}", fields.category},
							std::pair{"{status}", fields.status},
							std::pair{"{location}", fields.location},
							std::pair{"{details}", fields.details},
							std::pair{"{warnings}", fields.warnings}};
	for (const auto& [placeholder, value] : values)
		result = replace_placeholder(std::move(result), placeholder, value);
	return result;
}

std::string Localization::storage_result_card(
	const StorageResultFields& fields) const {
	std::string result = omit_empty_semicolon_clauses(
		translate_template(local_template_message(
			detail::TemplateMessageIndex::StorageResultCard)),
		{{"{type}", fields.type},
		 {"{lifecycle}", fields.lifecycle},
		 {"{location}", fields.location},
		 {"{details}", fields.details},
		 {"{warnings}", fields.warnings}});
	result = replace_placeholder(std::move(result), "{title}", fields.title);
	result = replace_placeholder(std::move(result), "{type}", fields.type);
	result =
		replace_placeholder(std::move(result), "{lifecycle}", fields.lifecycle);
	result =
		replace_placeholder(std::move(result), "{location}", fields.location);
	result = replace_placeholder(std::move(result), "{direct_children}",
								 std::to_string(fields.direct_children));
	result = replace_placeholder(std::move(result), "{direct_items}",
								 std::to_string(fields.direct_items));
	result = replace_placeholder(std::move(result), "{nested_items}",
								 std::to_string(fields.nested_items));
	result =
		replace_placeholder(std::move(result), "{details}", fields.details);
	return replace_placeholder(std::move(result), "{warnings}",
							   fields.warnings);
}

std::string Localization::item_storage_field(std::string_view storage) const {
	return replace_placeholder(
		translate_template(local_template_message(
			detail::TemplateMessageIndex::ItemStorageField)),
		"{storage}", storage);
}

std::string Localization::open_storage_action(std::string_view storage) const {
	return replace_placeholder(
		translate_template(local_template_message(
			detail::TemplateMessageIndex::OpenStorageAction)),
		"{storage}", storage);
}

std::string Localization::parent_storage_field(std::string_view storage) const {
	return replace_placeholder(
		translate_template(local_template_message(
			detail::TemplateMessageIndex::ParentStorageField)),
		"{storage}", storage);
}

std::string Localization::missing_storage_label(
	std::string_view identifier) const {
	return replace_placeholder(
		translate_template(local_template_message(
			detail::TemplateMessageIndex::MissingStorageLabel)),
		"{id}", identifier);
}

std::string Localization::item_status_label(domain::ItemStatus status) const {
	return translate_template(detail::presentation_message(
		detail::presentation_message_index(status)));
}

std::string Localization::storage_lifecycle_label(
	domain::StorageLifecycleStatus status) const {
	return translate_template(detail::presentation_message(
		detail::presentation_message_index(status)));
}

std::string Localization::photo_presence_label(
	catalog::PhotoPresenceState state) const {
	return translate_template(detail::presentation_message(
		detail::presentation_message_index(state)));
}

std::string Localization::photo_filter_label(
	catalog::SearchPhotoPresenceFilter filter) const {
	return translate_template(detail::presentation_message(
		detail::presentation_message_index(filter)));
}

std::string Localization::pending_photo_status_label(
	ui::PendingPhotoStatus status) const {
	return translate_template(detail::presentation_message(
		detail::presentation_message_index(status)));
}

std::string Localization::catalog_load_status_label(
	persistence::CatalogLoadStatus status) const {
	return translate_template(detail::presentation_message(
		detail::presentation_message_index(status)));
}

std::string Localization::startup_source_label(
	ui::CatalogSessionStartupSource source) const {
	return translate_template(detail::presentation_message(
		detail::presentation_message_index(source)));
}

std::string Localization::result_count(std::uint64_t count) const {
	return format_plural(
		detail::plural_message(detail::PluralMessageIndex::ResultCount), count);
}

std::string Localization::item_count(std::uint64_t count) const {
	return format_plural(
		detail::plural_message(detail::PluralMessageIndex::ItemCount), count);
}

std::string Localization::staged_photo_count(std::uint64_t count) const {
	return format_plural(
		detail::plural_message(detail::PluralMessageIndex::StagedPhotoCount),
		count);
}

std::string Localization::draft_result_count(std::uint64_t count) const {
	return format_plural(
		detail::plural_message(detail::PluralMessageIndex::DraftResultCount),
		count);
}

std::string Localization::photo_deck_tab(bool staged,
										 std::uint64_t count) const {
	return format_plural(
		detail::plural_message(
			staged ? detail::PluralMessageIndex::PhotoDeckStaged
				   : detail::PluralMessageIndex::PhotoDeckCurrent),
		count);
}

std::string Localization::photo_deck_selection_summary(
	bool staged, std::size_t position, std::size_t count,
	std::size_t total) const {
	std::string result = translate_template(local_template_message(
		detail::TemplateMessageIndex::PhotoDeckSelectionSummary));
	result			   = replace_placeholder(std::move(result), "{scope}",
											 staged ? "Staged" : "Current");
	result			   = replace_placeholder(std::move(result), "{position}",
											 std::to_string(position));
	result			   = replace_placeholder(std::move(result), "{count}",
											 std::to_string(count));
	return replace_placeholder(std::move(result), "{total}",
							   std::to_string(total));
}

std::string Localization::photo_position(bool staged, std::size_t position,
										 std::size_t total) const {
	const detail::TemplateMessageIndex index =
		staged ? detail::TemplateMessageIndex::StagedPhotoCaption
			   : detail::TemplateMessageIndex::StoredPhotoCaption;
	std::string result = translate_template(local_template_message(index));
	result			   = replace_placeholder(std::move(result), "{position}",
											 std::to_string(position));
	return replace_placeholder(std::move(result), "{total}",
							   std::to_string(total));
}

std::string Localization::preview_viewer_zoom_hint(float scale) const {
	return replace_placeholder(
		translate_template(local_template_message(
			detail::TemplateMessageIndex::PreviewViewerZoomHint)),
		"{scale}", format_zoom_scale(scale));
}

std::string Localization::progress_summary(
	const ProgressSummary& summary) const {
	std::string units;
	if (summary.has_current_units && summary.has_total_units) {
		units = translate_template(local_template_message(
			detail::TemplateMessageIndex::ProgressUnits));
		units = replace_placeholder(std::move(units), "{current}",
									std::to_string(summary.current_units));
		units = replace_placeholder(std::move(units), "{total}",
									std::to_string(summary.total_units));
	} else if (summary.has_current_units) {
		units = replace_placeholder(
			translate_template(local_template_message(
				detail::TemplateMessageIndex::ProgressCurrentUnits)),
			"{current}", std::to_string(summary.current_units));
	}
	std::string result = translate_template(
		local_template_message(detail::TemplateMessageIndex::ProgressTemplate));
	result = replace_placeholder(std::move(result), "{phase}", summary.phase);
	result =
		replace_placeholder(std::move(result), "{message}", summary.message);
	result = replace_placeholder(std::move(result), "{units}", units);
	return replace_placeholder(
		std::move(result), "{cancellability}",
		translate_template(local_template_message(
			summary.cancellable
				? detail::TemplateMessageIndex::ProgressCancellable
				: detail::TemplateMessageIndex::ProgressNotCancellable)));
}

std::string Localization::progress_summary(
	const platform::ProgressEvent& event) const {
	ProgressSummary summary{
		.phase			   = event.phase,
		.message		   = event.message,
		.current_units	   = event.current_units.value_or(0U),
		.total_units	   = event.total_units.value_or(0U),
		.has_current_units = event.current_units.has_value(),
		.has_total_units   = event.total_units.has_value(),
		.cancellable	   = event.cancellable};
	if (!event.message_id.has_value())
		return progress_summary(summary);

	for (const ProgressMessageDefinition& definition :
		 detail::progress_message_definitions()) {
		if (definition.id != *event.message_id)
			continue;
		const std::string context_prefix =
			"progress." + std::string{definition.phase_code};
		summary.phase	= translate_message(context_prefix + ".phase",
											definition.english_phase);
		summary.message = translate_message(context_prefix + ".message",
											definition.english_message);
		break;
	}
	return progress_summary(summary);
}

std::string Localization::progress_no_events() const {
	return translate_template(
		local_template_message(detail::TemplateMessageIndex::ProgressNoEvents));
}
}	 // namespace shuba::localization
