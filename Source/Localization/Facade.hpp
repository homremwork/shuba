#pragma once

#include "Localization/Language.hpp"
#include "Localization/MessageId.hpp"
#include "Localization/PhotoWorkflowLocalization.hpp"
#include "Localization/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace shuba::localization {
namespace detail {
struct FormatterMessage;
struct PresentationMessage;
}	 // namespace detail

class Localization final {
public:
	Localization(const Localization&)			 = delete;
	Localization& operator=(const Localization&) = delete;
	Localization(Localization&&) noexcept;
	Localization& operator=(Localization&&) noexcept;
	~Localization();

	[[nodiscard]] const LocalizationInitialization& initialization()
		const noexcept;
	[[nodiscard]] std::string text(MessageId message) const;
	[[nodiscard]] std::string recovery_action_label(
		ui::RecoveryAction action) const;
	[[nodiscard]] std::string recovery_actions(
		std::span<const ui::RecoveryAction> actions) const;
	[[nodiscard]] std::string recovery_counts(
		const RecoveryCountsFields& fields) const;
	[[nodiscard]] std::string import_validation_summary(
		const ImportValidationFields& fields) const;
	[[nodiscard]] std::string shell_status(
		const ShellStatusFields& fields) const;
	[[nodiscard]] std::string technical_information_heading() const;
	[[nodiscard]] std::string photo_count(std::uint64_t count) const;
	[[nodiscard]] std::string catalog_result_count(std::uint64_t count) const;
	[[nodiscard]] std::string catalog_filter_clauses(
		const CatalogFilterSummaryFields& fields) const;
	[[nodiscard]] std::string catalog_filter_summary(
		CatalogFilterSummaryKind kind, std::string_view filters) const;
	[[nodiscard]] std::string field_value(std::string_view label,
										  std::string_view value) const;
	[[nodiscard]] std::string tags_summary(std::string_view tag_list) const;
	[[nodiscard]] std::string listing_summary(std::string_view marketplace,
											  std::string_view url,
											  std::string_view price,
											  std::string_view note) const;
	[[nodiscard]] std::string finance_summary(std::string_view source,
											  std::string_view acquisition_cost,
											  std::string_view sale_price,
											  std::string_view expenses,
											  std::string_view profit) const;
	[[nodiscard]] std::string storage_choice(std::string_view display_name,
											 std::string_view type,
											 std::string_view location) const;
	[[nodiscard]] std::string catalog_warning_label(
		CatalogWarning warning) const;
	[[nodiscard]] std::string item_header(const ItemHeaderFields& fields) const;
	[[nodiscard]] std::string storage_header(
		const StorageHeaderFields& fields) const;
	[[nodiscard]] std::string item_result_card(
		const ItemResultFields& fields) const;
	[[nodiscard]] std::string storage_result_card(
		const StorageResultFields& fields) const;
	[[nodiscard]] std::string item_storage_field(
		std::string_view storage) const;
	[[nodiscard]] std::string open_storage_action(
		std::string_view storage) const;
	[[nodiscard]] std::string parent_storage_field(
		std::string_view storage) const;
	[[nodiscard]] std::string missing_storage_label(
		std::string_view identifier) const;
	[[nodiscard]] std::string draft_result_count(std::uint64_t count) const;
	[[nodiscard]] std::string photo_deck_tab(bool staged,
											 std::uint64_t count) const;
	[[nodiscard]] std::string photo_deck_selection_summary(
		bool staged, std::size_t position, std::size_t count,
		std::size_t total) const;
	[[nodiscard]] std::string photo_position(bool staged, std::size_t position,
											 std::size_t total) const;
	[[nodiscard]] std::string preview_viewer_zoom_hint(float scale) const;
	[[nodiscard]] std::string photo_workflow_text(
		PhotoWorkflowMessageId message) const;
	[[nodiscard]] std::string photo_import_completed(
		const PhotoImportCompletion& completion) const;
	[[nodiscard]] std::string pending_photo_staging_completed(
		const PendingPhotoStagingCompletion& completion) const;
	[[nodiscard]] std::string jpeg_export_completed(
		std::uint64_t bytes_written) const;
	[[nodiscard]] std::string pending_save_photo_outcome(
		const PendingSavePhotoOutcome& outcome) const;
	[[nodiscard]] std::string photo_deletion_outcome(
		const PhotoDeletionOutcome& outcome) const;

	[[nodiscard]] std::string item_status_label(
		domain::ItemStatus status) const;
	[[nodiscard]] std::string storage_lifecycle_label(
		domain::StorageLifecycleStatus status) const;
	[[nodiscard]] std::string photo_presence_label(
		catalog::PhotoPresenceState state) const;
	[[nodiscard]] std::string photo_filter_label(
		catalog::SearchPhotoPresenceFilter filter) const;
	[[nodiscard]] std::string pending_photo_status_label(
		ui::PendingPhotoStatus status) const;
	[[nodiscard]] std::string catalog_load_status_label(
		persistence::CatalogLoadStatus status) const;
	[[nodiscard]] std::string startup_source_label(
		ui::CatalogSessionStartupSource source) const;

	[[nodiscard]] std::string result_count(std::uint64_t count) const;
	[[nodiscard]] std::string item_count(std::uint64_t count) const;
	[[nodiscard]] std::string staged_photo_count(std::uint64_t count) const;
	[[nodiscard]] std::string progress_summary(
		const ProgressSummary& summary) const;
	[[nodiscard]] std::string progress_summary(
		const platform::ProgressEvent& event) const;
	[[nodiscard]] std::string progress_no_events() const;

private:
	class Impl;

	explicit Localization(std::unique_ptr<Impl> implementation);
	[[nodiscard]] std::string translate_message(std::string_view context,
												std::string_view english) const;
	[[nodiscard]] std::string translate_plural_message(
		std::string_view context, std::string_view singular,
		std::string_view plural, std::uint64_t count) const;
	[[nodiscard]] std::string translate_template(
		const detail::PresentationMessage& message) const;
	[[nodiscard]] std::string format_plural(
		const detail::FormatterMessage& message, std::uint64_t count) const;
	[[nodiscard]] std::string translate_photo_workflow_message(
		const PhotoWorkflowMessage& message) const;
	[[nodiscard]] static Localization make_english(
		Language requested_language,
		std::vector<LocalizationIssue> issues = {});

	std::unique_ptr<Impl> implementation;

	friend Localization make_localization(
		Language language, std::string_view russian_catalog_bytes);
};

[[nodiscard]] Localization make_localization(
	Language language, std::string_view russian_catalog_bytes);
}	 // namespace shuba::localization
