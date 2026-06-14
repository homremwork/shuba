#pragma once

#include "Core/Identifier.hpp"
#include "Domain/Domain.hpp"
#include "Persistence/JsonlCatalog.hpp"
#include "Persistence/MetadataSchema.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace shuba::catalog {
enum class PhotoPresenceState : std::uint8_t {
	HasUsablePhotos,
	NoPhotoRecords,
	OnlyBrokenPhotos,
	MixedUsableAndBrokenPhotos,
};

[[nodiscard]] std::string_view to_string(PhotoPresenceState state) noexcept;

enum class DerivedDiagnosticSeverity : std::uint8_t {
	Info,
	Warning,
	Error,
};

[[nodiscard]] std::string_view to_string(
	DerivedDiagnosticSeverity severity) noexcept;

struct DerivedDiagnostic final {
	DerivedDiagnosticSeverity severity{DerivedDiagnosticSeverity::Info};
	std::string code;
	std::string subject_id;
	std::string message;
	std::string details;

	friend bool operator==(const DerivedDiagnostic&,
						   const DerivedDiagnostic&) = default;
};

struct CatalogMediaSnapshot final {
	bool complete_scan_available{true};
	std::vector<std::string> readable_photo_media_files;

	friend bool operator==(const CatalogMediaSnapshot&,
						   const CatalogMediaSnapshot&) = default;
};

struct CatalogRepositoryInput final {
	std::vector<persistence::ItemEnvelope> items;
	std::vector<persistence::StorageEnvelope> storages;
	std::vector<persistence::PhotoEnvelope> photos;
	CatalogMediaSnapshot media;

	friend bool operator==(const CatalogRepositoryInput&,
						   const CatalogRepositoryInput&) = default;
};

struct OwnerPhotoProjection final {
	domain::PhotoOwnerType owner_type{domain::PhotoOwnerType::Item};
	core::StableIdentifier owner_id;
	std::vector<core::StableIdentifier> ordered_photo_ids;
	std::vector<core::StableIdentifier> usable_photo_ids;
	std::vector<core::StableIdentifier> broken_photo_ids;
	std::optional<core::StableIdentifier> representative_photo_id;
	std::optional<core::StableIdentifier> representative_usable_photo_id;
	PhotoPresenceState presence{PhotoPresenceState::NoPhotoRecords};
	bool has_multiple_main_photos{};
	bool has_duplicate_sort_orders{};

	friend bool operator==(const OwnerPhotoProjection&,
						   const OwnerPhotoProjection&) = default;
};

struct ItemProjection final {
	core::StableIdentifier id;
	domain::ReferenceState storage_reference_state{
		domain::ReferenceState::Absent};
	std::optional<core::StableIdentifier> storage_id;
	std::string storage_display_name;
	std::string storage_path_label;
	bool storage_archived{};
	bool broken_storage_reference{};
	PhotoPresenceState photo_presence{PhotoPresenceState::NoPhotoRecords};
	std::optional<core::StableIdentifier> representative_photo_id;
	std::optional<core::StableIdentifier> representative_usable_photo_id;
	bool visible_by_default{};

	friend bool operator==(const ItemProjection&,
						   const ItemProjection&) = default;
};

struct StorageProjection final {
	core::StableIdentifier id;
	domain::ReferenceState parent_reference_state{
		domain::ReferenceState::Absent};
	std::optional<core::StableIdentifier> parent_storage_id;
	std::vector<core::StableIdentifier> direct_child_storage_ids;
	std::vector<core::StableIdentifier> nested_descendant_storage_ids;
	std::vector<std::string> path_segments;
	std::string path_label;
	std::uint64_t direct_item_count{};
	std::uint64_t nested_item_count{};
	PhotoPresenceState photo_presence{PhotoPresenceState::NoPhotoRecords};
	std::optional<core::StableIdentifier> representative_photo_id;
	std::optional<core::StableIdentifier> representative_usable_photo_id;
	bool has_parent_cycle{};
	bool visible_by_default{};

	friend bool operator==(const StorageProjection&,
						   const StorageProjection&) = default;
};

struct ItemSearchProjection final {
	core::StableIdentifier id;
	std::string display_name;
	std::string category;
	domain::ItemStatus status{domain::ItemStatus::Draft};
	std::optional<core::StableIdentifier> storage_id;
	std::string storage_display_name;
	std::string storage_path_label;
	bool storage_archived{};
	bool broken_storage_reference{};
	PhotoPresenceState photo_presence{PhotoPresenceState::NoPhotoRecords};
	bool has_broken_photos{};
	std::optional<core::StableIdentifier> representative_photo_id;
	std::optional<core::StableIdentifier> representative_usable_photo_id;
	bool visible_by_default{};

	friend bool operator==(const ItemSearchProjection&,
						   const ItemSearchProjection&) = default;
};

struct StorageSearchProjection final {
	core::StableIdentifier id;
	std::string display_name;
	std::string storage_type;
	domain::StorageLifecycleStatus lifecycle_status{
		domain::StorageLifecycleStatus::Active};
	std::string parent_path_label;
	std::uint64_t direct_child_count{};
	std::uint64_t direct_item_count{};
	std::uint64_t nested_item_count{};
	PhotoPresenceState photo_presence{PhotoPresenceState::NoPhotoRecords};
	bool has_broken_photos{};
	std::optional<core::StableIdentifier> representative_photo_id;
	std::optional<core::StableIdentifier> representative_usable_photo_id;
	bool visible_by_default{};

	friend bool operator==(const StorageSearchProjection&,
						   const StorageSearchProjection&) = default;
};

struct SearchRebuildProjection final {
	std::vector<ItemSearchProjection> items;
	std::vector<StorageSearchProjection> storages;
	std::vector<std::string> tag_key_hints;

	friend bool operator==(const SearchRebuildProjection&,
						   const SearchRebuildProjection&) = default;
};

struct DerivedRecoverySummary final {
	std::uint64_t accepted_item_count{};
	std::uint64_t accepted_storage_count{};
	std::uint64_t accepted_photo_count{};
	std::uint64_t broken_reference_count{};
	std::uint64_t orphan_media_count{};
	std::uint64_t diagnostic_count{};

	friend bool operator==(const DerivedRecoverySummary&,
						   const DerivedRecoverySummary&) = default;
};

struct CatalogRepositoryState final {
	std::vector<persistence::ItemEnvelope> items;
	std::vector<persistence::StorageEnvelope> storages;
	std::vector<persistence::PhotoEnvelope> photos;
	std::map<std::string, std::size_t> item_index_by_id;
	std::map<std::string, std::size_t> storage_index_by_id;
	std::map<std::string, std::size_t> photo_index_by_id;
	std::map<std::string, ItemProjection> item_projections;
	std::map<std::string, StorageProjection> storage_projections;
	std::map<std::string, OwnerPhotoProjection> item_photo_projections;
	std::map<std::string, OwnerPhotoProjection> storage_photo_projections;
	std::vector<std::string> orphan_photo_media_files;
	std::vector<std::string> unexpected_photo_media_files;
	std::vector<DerivedDiagnostic> diagnostics;
	SearchRebuildProjection search_projection;
	DerivedRecoverySummary recovery_summary;
};

[[nodiscard]] std::string expected_photo_media_file_name(
	const core::StableIdentifier& photo_id);
[[nodiscard]] std::string expected_photo_media_relative_path(
	const core::StableIdentifier& photo_id);

[[nodiscard]] CatalogRepositoryInput make_catalog_repository_input(
	const persistence::CatalogJsonlLoadResult& load_result,
	CatalogMediaSnapshot media = {});
[[nodiscard]] CatalogRepositoryState build_catalog_repository(
	CatalogRepositoryInput input);

[[nodiscard]] const persistence::ItemEnvelope* find_item_envelope(
	const CatalogRepositoryState& state, const core::StableIdentifier& id);
[[nodiscard]] const persistence::StorageEnvelope* find_storage_envelope(
	const CatalogRepositoryState& state, const core::StableIdentifier& id);
[[nodiscard]] const persistence::PhotoEnvelope* find_photo_envelope(
	const CatalogRepositoryState& state, const core::StableIdentifier& id);
}	 // namespace shuba::catalog
