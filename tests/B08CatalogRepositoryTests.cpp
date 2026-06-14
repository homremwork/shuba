#include "Catalog/CatalogRepository.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {
[[nodiscard]] shuba::core::StableIdentifier make_id(std::string text) {
	auto identifier =
		shuba::core::StableIdentifier::try_create_file_safe(std::move(text));
	REQUIRE(identifier.has_value());
	return *identifier;
}

[[nodiscard]] shuba::domain::RecordTimestamps make_timestamps(
	std::int64_t created_at, std::int64_t updated_at) {
	return {.created_at = shuba::core::EpochMilliseconds{created_at},
			.updated_at = shuba::core::EpochMilliseconds{updated_at}};
}

[[nodiscard]] shuba::persistence::ItemEnvelope make_item(
	std::string id, std::string display_name,
	std::optional<shuba::core::StableIdentifier> storage_id = std::nullopt,
	shuba::domain::ItemStatus status = shuba::domain::ItemStatus::Draft) {
	return shuba::persistence::ItemEnvelope{
		.record =
			shuba::domain::ItemRecord{.id			= make_id(std::move(id)),
									  .display_name = std::move(display_name),
									  .category		= "other",
									  .storage_id	= std::move(storage_id),
									  .status		= status,
									  .timestamps	= make_timestamps(1, 2)}};
}

[[nodiscard]] shuba::persistence::StorageEnvelope make_storage(
	std::string id, std::string display_name,
	std::optional<shuba::core::StableIdentifier> parent_id = std::nullopt,
	shuba::domain::StorageLifecycleStatus lifecycle_status =
		shuba::domain::StorageLifecycleStatus::Active) {
	return shuba::persistence::StorageEnvelope{
		.record = shuba::domain::StorageRecord{
			.id				   = make_id(std::move(id)),
			.display_name	   = std::move(display_name),
			.storage_type	   = "box",
			.parent_storage_id = std::move(parent_id),
			.lifecycle_status  = lifecycle_status,
			.timestamps		   = make_timestamps(3, 4)}};
}

[[nodiscard]] shuba::persistence::PhotoEnvelope make_photo(
	std::string id, shuba::domain::PhotoOwnerType owner_type,
	shuba::core::StableIdentifier owner_id, std::int64_t sort_order,
	bool is_main, std::int64_t created_at) {
	return shuba::persistence::PhotoEnvelope{
		.record = shuba::domain::PhotoRecord{
			.id			= make_id(std::move(id)),
			.owner_type = owner_type,
			.owner_id	= std::move(owner_id),
			.sort_order = sort_order,
			.is_main	= is_main,
			.timestamps = make_timestamps(created_at, created_at)}};
}

[[nodiscard]] bool has_diagnostic_code(
	const std::vector<shuba::catalog::DerivedDiagnostic>& diagnostics,
	std::string_view code) {
	return std::ranges::any_of(diagnostics, [code](const auto& diagnostic) {
		return diagnostic.code == code;
	});
}

[[nodiscard]] std::size_t count_diagnostic_code(
	const std::vector<shuba::catalog::DerivedDiagnostic>& diagnostics,
	std::string_view code) {
	return static_cast<std::size_t>(std::ranges::count_if(
		diagnostics,
		[code](const auto& diagnostic) { return diagnostic.code == code; }));
}

[[nodiscard]] const shuba::catalog::ItemSearchProjection& find_item_search(
	const shuba::catalog::CatalogRepositoryState& state, std::string_view id) {
	const std::vector<shuba::catalog::ItemSearchProjection>::const_iterator
		found = std::ranges::find_if(
			state.search_projection.items,
			[id](const auto& item) { return item.id.value() == id; });
	REQUIRE(found != state.search_projection.items.end());
	return *found;
}

[[nodiscard]] const shuba::catalog::StorageSearchProjection&
find_storage_search(const shuba::catalog::CatalogRepositoryState& state,
					std::string_view id) {
	const std::vector<shuba::catalog::StorageSearchProjection>::const_iterator
		found = std::ranges::find_if(
			state.search_projection.storages,
			[id](const auto& storage) { return storage.id.value() == id; });
	REQUIRE(found != state.search_projection.storages.end());
	return *found;
}

[[nodiscard]] const shuba::catalog::ItemProjection& item_projection(
	const shuba::catalog::CatalogRepositoryState& state, std::string_view id) {
	const auto found = state.item_projections.find(std::string{id});
	REQUIRE(found != state.item_projections.end());
	return found->second;
}

[[nodiscard]] const shuba::catalog::StorageProjection& storage_projection(
	const shuba::catalog::CatalogRepositoryState& state, std::string_view id) {
	const auto found = state.storage_projections.find(std::string{id});
	REQUIRE(found != state.storage_projections.end());
	return found->second;
}
}	 // namespace

TEST_CASE("B08 classifies owner photo presence without filter gaps",
		  "[b08][catalog][photos]") {
	using namespace shuba::catalog;
	using namespace shuba::domain;

	auto usable_item   = make_item("item-usable", "Has usable");
	auto no_photo_item = make_item("item-no-photos", "No photos");
	auto broken_item   = make_item("item-broken-only", "Broken only");
	auto mixed_item	   = make_item("item-mixed", "Mixed photos");

	const shuba::core::StableIdentifier usable_item_id = usable_item.record.id;
	const shuba::core::StableIdentifier broken_item_id = broken_item.record.id;
	const shuba::core::StableIdentifier mixed_item_id  = mixed_item.record.id;

	CatalogRepositoryState state =
		build_catalog_repository(CatalogRepositoryInput{
			.items	= {usable_item, no_photo_item, broken_item, mixed_item},
			.photos = {make_photo("photo-usable", PhotoOwnerType::Item,
								  usable_item_id, 10, true, 10),
					   make_photo("photo-broken", PhotoOwnerType::Item,
								  broken_item_id, 10, true, 10),
					   make_photo("photo-mixed-broken", PhotoOwnerType::Item,
								  mixed_item_id, 5, true, 1),
					   make_photo("photo-mixed-usable", PhotoOwnerType::Item,
								  mixed_item_id, 5, true, 2)},
			.media =
				CatalogMediaSnapshot{.complete_scan_available	 = true,
									 .readable_photo_media_files = {
										 "media/photos/photo-usable.jxl",
										 "media/photos/photo-mixed-usable.jxl",
										 "media/photos/orphan-media.jxl",
										 "media/photos/readme.txt"}}});

	REQUIRE(item_projection(state, "item-usable").photo_presence
			== PhotoPresenceState::HasUsablePhotos);
	REQUIRE(item_projection(state, "item-no-photos").photo_presence
			== PhotoPresenceState::NoPhotoRecords);
	REQUIRE(item_projection(state, "item-broken-only").photo_presence
			== PhotoPresenceState::OnlyBrokenPhotos);
	REQUIRE(item_projection(state, "item-mixed").photo_presence
			== PhotoPresenceState::MixedUsableAndBrokenPhotos);

	REQUIRE(item_projection(state, "item-broken-only")
				.representative_photo_id->value()
			== "photo-broken");
	REQUIRE_FALSE(item_projection(state, "item-broken-only")
					  .representative_usable_photo_id.has_value());
	REQUIRE(
		item_projection(state, "item-mixed").representative_photo_id->value()
		== "photo-mixed-broken");
	REQUIRE(item_projection(state, "item-mixed")
				.representative_usable_photo_id->value()
			== "photo-mixed-usable");

	REQUIRE(find_item_search(state, "item-mixed").has_broken_photos);
	REQUIRE(find_item_search(state, "item-no-photos").photo_presence
			== PhotoPresenceState::NoPhotoRecords);
	REQUIRE(has_diagnostic_code(state.diagnostics, "missing_photo_media"));
	REQUIRE(has_diagnostic_code(state.diagnostics, "orphan_photo_media"));
	REQUIRE(
		has_diagnostic_code(state.diagnostics, "unexpected_photo_media_file"));
	REQUIRE(has_diagnostic_code(state.diagnostics, "multiple_main_photos"));
	REQUIRE(
		has_diagnostic_code(state.diagnostics, "duplicate_photo_sort_order"));
	REQUIRE(state.orphan_photo_media_files
			== std::vector<std::string>{"orphan-media.jxl"});
	REQUIRE(state.recovery_summary.orphan_media_count == 1);
	REQUIRE(state.recovery_summary.broken_reference_count == 2);
}

TEST_CASE("B08 incomplete media scans do not mark accepted photos broken",
		  "[b08][catalog][photos]") {
	using namespace shuba::catalog;
	using namespace shuba::domain;

	const shuba::persistence::ItemEnvelope item =
		make_item("item-unscanned", "Unscanned media owner");
	CatalogRepositoryState state =
		build_catalog_repository(CatalogRepositoryInput{
			.items	= {item},
			.photos = {make_photo("photo-unscanned", PhotoOwnerType::Item,
								  item.record.id, 1000, true, 10)},
			.media	= CatalogMediaSnapshot{.complete_scan_available = false}});

	REQUIRE(item_projection(state, "item-unscanned").photo_presence
			== PhotoPresenceState::HasUsablePhotos);
	REQUIRE(item_projection(state, "item-unscanned")
				.representative_usable_photo_id->value()
			== "photo-unscanned");
	REQUIRE_FALSE(
		has_diagnostic_code(state.diagnostics, "missing_photo_media"));
	REQUIRE(state.recovery_summary.broken_reference_count == 0);
}

TEST_CASE(
	"B08 derives storage paths counts archive visibility and broken links",
	"[b08][catalog][storage]") {
	using namespace shuba::catalog;
	using namespace shuba::domain;

	const shuba::core::StableIdentifier root_id = make_id("storage-root");
	const shuba::core::StableIdentifier archived_id =
		make_id("storage-archived");
	const shuba::core::StableIdentifier grandchild_id =
		make_id("storage-grandchild");
	const shuba::core::StableIdentifier missing_id = make_id("storage-missing");
	const shuba::core::StableIdentifier cycle_a_id = make_id("storage-cycle-a");
	const shuba::core::StableIdentifier cycle_b_id = make_id("storage-cycle-b");

	CatalogRepositoryState state =
		build_catalog_repository(CatalogRepositoryInput{
			.items = {make_item("item-active-in-archived", "Active in archived",
								archived_id, ItemStatus::Listed),
					  make_item("item-archived-in-root", "Archived item",
								root_id, ItemStatus::Archived),
					  make_item("item-in-grandchild", "Nested item",
								grandchild_id, ItemStatus::Draft),
					  make_item("item-broken-storage", "Broken storage",
								missing_id, ItemStatus::Draft),
					  make_item("item-unassigned", "Unassigned")},
			.storages = {
				make_storage("storage-root", "Root"),
				make_storage("storage-archived", "Archived box", root_id,
							 StorageLifecycleStatus::Archived),
				make_storage("storage-grandchild", "Grandchild", archived_id),
				make_storage("storage-broken-parent", "Broken parent",
							 missing_id),
				make_storage("storage-cycle-a", "Cycle A", cycle_b_id),
				make_storage("storage-cycle-b", "Cycle B", cycle_a_id)}});

	REQUIRE(storage_projection(state, "storage-root").path_label == "Root");
	REQUIRE(storage_projection(state, "storage-archived").path_label
			== "Root / Archived box");
	REQUIRE(storage_projection(state, "storage-grandchild").path_label
			== "Root / Archived box / Grandchild");
	REQUIRE(storage_projection(state, "storage-broken-parent").path_label
			== "Broken parent");
	REQUIRE(storage_projection(state, "storage-cycle-a").path_label
			== "Cycle A");
	REQUIRE(storage_projection(state, "storage-cycle-b").has_parent_cycle);

	REQUIRE(storage_projection(state, "storage-root").direct_child_storage_ids
			== std::vector<shuba::core::StableIdentifier>{archived_id});
	REQUIRE(
		storage_projection(state, "storage-root").nested_descendant_storage_ids
		== std::vector<shuba::core::StableIdentifier>{archived_id,
													  grandchild_id});
	REQUIRE(storage_projection(state, "storage-root").direct_item_count == 1);
	REQUIRE(storage_projection(state, "storage-root").nested_item_count == 3);
	REQUIRE(storage_projection(state, "storage-archived").direct_item_count
			== 1);
	REQUIRE(storage_projection(state, "storage-archived").nested_item_count
			== 2);

	REQUIRE(
		item_projection(state, "item-active-in-archived").visible_by_default);
	REQUIRE(item_projection(state, "item-active-in-archived").storage_archived);
	REQUIRE(
		find_item_search(state, "item-active-in-archived").visible_by_default);
	REQUIRE(
		find_item_search(state, "item-active-in-archived").storage_archived);
	REQUIRE_FALSE(
		item_projection(state, "item-archived-in-root").visible_by_default);
	REQUIRE_FALSE(
		storage_projection(state, "storage-archived").visible_by_default);
	REQUIRE_FALSE(
		find_storage_search(state, "storage-archived").visible_by_default);

	REQUIRE(
		item_projection(state, "item-broken-storage").broken_storage_reference);
	REQUIRE(storage_projection(state, "storage-broken-parent")
				.parent_reference_state
			== ReferenceState::Broken);
	REQUIRE(count_diagnostic_code(state.diagnostics, "broken_item_storage")
			== 1);
	REQUIRE(count_diagnostic_code(state.diagnostics, "broken_storage_parent")
			== 1);
	REQUIRE(count_diagnostic_code(state.diagnostics, "storage_parent_cycle")
			== 2);
	REQUIRE(state.recovery_summary.broken_reference_count == 4);
	REQUIRE(state.search_projection.tag_key_hints.empty());
}
