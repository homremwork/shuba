#pragma once

#include "Catalog/BackupArchive.hpp"
#include "Catalog/CatalogRepository.hpp"
#include "Catalog/Search.hpp"
#include "UI/Session/CatalogStartupSession.hpp"
#include "UI/Session/EntityEditTypes.hpp"
#include "UI/Session/PhotoSessionTypes.hpp"
#include "UI/View/Primitives/Palette.hpp"

#include <cstddef>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace shuba::ui {
struct TagKeyCandidateGroups final {
	std::vector<std::string> item_keys;
	std::vector<std::string> storage_keys;
};

[[nodiscard]] std::string status_text(domain::ItemStatus status);
[[nodiscard]] std::string storage_lifecycle_text(
	domain::StorageLifecycleStatus status);
[[nodiscard]] std::string photo_presence_label(
	catalog::PhotoPresenceState state);
[[nodiscard]] std::string photo_filter_label(
	catalog::SearchPhotoPresenceFilter filter);
[[nodiscard]] std::string field_value_summary(std::string_view label,
											  std::string_view value);
[[nodiscard]] std::string tags_summary(std::span<const domain::TagRow> tags);
[[nodiscard]] std::string money_summary(
	const std::optional<domain::MoneyAmount>& amount);
[[nodiscard]] std::string listing_summary(const domain::ListingData& listing);
[[nodiscard]] std::string finance_summary(
	const domain::AcquisitionData& acquisition,
	const domain::FinanceData& finance);
[[nodiscard]] std::string storage_label(
	const catalog::CatalogRepositoryState& repository,
	const std::optional<core::StableIdentifier>& storage_id);
[[nodiscard]] std::string storage_choice_label(
	const catalog::CatalogRepositoryState& repository,
	const persistence::StorageEnvelope& storage);
[[nodiscard]] std::optional<core::StableIdentifier> next_storage_choice(
	const catalog::CatalogRepositoryState& repository,
	const std::optional<core::StableIdentifier>& current,
	std::optional<core::StableIdentifier> excluded = std::nullopt);
[[nodiscard]] std::string diagnostic_summary(
	std::span<const EntityEditDiagnostic> diagnostics);
[[nodiscard]] std::string core_diagnostic_summary(
	std::span<const core::Diagnostic> diagnostics);
[[nodiscard]] std::string progress_summary(
	std::span<const platform::ProgressEvent> events);
[[nodiscard]] std::string pending_photo_summary(
	std::span<const PendingPhotoSource> pending_sources);
[[nodiscard]] std::string pending_photo_source_summary(
	const PendingPhotoSource& source, std::size_t display_index);
[[nodiscard]] std::string tag_row_count_summary(
	std::span<const domain::TagRow> tags);
void append_tag_key_candidate(std::vector<std::string>& keys,
							  std::set<std::string>& seen,
							  const domain::TagRow& tag);
[[nodiscard]] TagKeyCandidateGroups derive_tag_key_candidate_groups(
	const catalog::CatalogRepositoryState& repository);
void apply_tag_key_candidate(std::vector<domain::TagRow>& tags,
							 std::string key);
[[nodiscard]] bool has_ready_pending_photo(
	std::span<const PendingPhotoSource> pending_sources) noexcept;
[[nodiscard]] std::string recovery_action_summary(
	std::span<const std::string> actions);
[[nodiscard]] std::string recovery_counts_summary(
	const CatalogRecoveryUiSummary& summary);
[[nodiscard]] std::string import_validation_summary(
	const catalog::StagedCatalogValidationResult& validation);
[[nodiscard]] bool has_diagnostics(
	std::span<const core::Diagnostic> diagnostics) noexcept;
[[nodiscard]] std::string photo_summary(const persistence::PhotoEnvelope& photo,
										std::size_t position,
										std::size_t total);
[[nodiscard]] std::string owner_caption(
	const catalog::CatalogRepositoryState& repository,
	const domain::PhotoOwner& owner);
[[nodiscard]] const catalog::OwnerPhotoProjection* owner_photo_projection(
	const catalog::CatalogRepositoryState& repository,
	const domain::PhotoOwner& owner);
[[nodiscard]] std::optional<std::size_t> find_photo_index_in_order(
	std::span<const core::StableIdentifier> ordered_photo_ids,
	const core::StableIdentifier& photo_id);
[[nodiscard]] std::optional<core::StableIdentifier> first_viewable_photo_id(
	const catalog::CatalogRepositoryState& repository,
	const domain::PhotoOwner& owner);
[[nodiscard]] std::optional<core::StableIdentifier> adjacent_photo_id(
	const catalog::CatalogRepositoryState& repository,
	const domain::PhotoOwner& owner, const core::StableIdentifier& photo_id,
	int direction);
[[nodiscard]] juce::Image juce_image_from_pixels(
	const platform::ImagePixels& pixels);
[[nodiscard]] bool contains_string(std::span<const std::string> values,
								   std::string_view value);
void toggle_string(std::vector<std::string>& values, std::string value);
[[nodiscard]] bool contains_status(std::span<const domain::ItemStatus> values,
								   domain::ItemStatus status);
void toggle_status(std::vector<domain::ItemStatus>& values,
				   domain::ItemStatus status);
[[nodiscard]] bool has_catalog_filters(
	const catalog::CatalogSearchFilters& filters) noexcept;
[[nodiscard]] std::string active_filter_summary(
	const catalog::CatalogSearchFilters& filters,
	const catalog::CatalogRepositoryState& repository);
[[nodiscard]] std::string first_note_or_tag_summary(
	const persistence::ItemEnvelope* item);
[[nodiscard]] std::string first_storage_note_or_tag_summary(
	const persistence::StorageEnvelope* storage);
[[nodiscard]] std::string warning_summary(
	const catalog::SearchWarningMarkers& warnings);
[[nodiscard]] juce::String item_result_text(const catalog::SearchResult& result,
											const CatalogSessionState& session);
[[nodiscard]] juce::String storage_result_text(
	const catalog::SearchResult& result, const CatalogSessionState& session);
[[nodiscard]] std::vector<std::string> distinct_categories(
	const catalog::SearchIndex& index);
[[nodiscard]] std::set<std::string> storage_filter_id_set(
	const catalog::CatalogRepositoryState& repository,
	const core::StableIdentifier& selected_storage_id, bool include_nested);
}	 // namespace shuba::ui
