#include "Catalog/Search.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {
[[nodiscard]] shuba::core::StableIdentifier make_id(std::string text) {
	std::optional<shuba::core::StableIdentifier> identifier =
		shuba::core::StableIdentifier::try_create_file_safe(std::move(text));
	REQUIRE(identifier.has_value());
	return *identifier;
}

[[nodiscard]] shuba::domain::RecordTimestamps make_timestamps(
	std::int64_t created_at, std::int64_t updated_at) {
	return {.created_at = shuba::core::EpochMilliseconds{created_at},
			.updated_at = shuba::core::EpochMilliseconds{updated_at}};
}

[[nodiscard]] shuba::domain::MoneyAmount make_money(std::string_view amount,
													std::string_view currency) {
	shuba::domain::MoneyParseResult parsed =
		shuba::domain::parse_money_amount(amount, currency);
	REQUIRE(parsed.valid());
	return parsed.value();
}

[[nodiscard]] shuba::persistence::ItemEnvelope make_item(
	std::string id, std::string display_name, std::string category,
	std::optional<shuba::core::StableIdentifier> storage_id = std::nullopt,
	shuba::domain::ItemStatus status = shuba::domain::ItemStatus::Draft,
	std::int64_t updated_at			 = 10) {
	return shuba::persistence::ItemEnvelope{
		.record = shuba::domain::ItemRecord{
			.id			  = make_id(std::move(id)),
			.display_name = std::move(display_name),
			.category	  = std::move(category),
			.storage_id	  = std::move(storage_id),
			.status		  = status,
			.timestamps	  = make_timestamps(1, updated_at)}};
}

[[nodiscard]] shuba::persistence::StorageEnvelope make_storage(
	std::string id, std::string display_name, std::string storage_type,
	std::optional<shuba::core::StableIdentifier> parent_id = std::nullopt,
	shuba::domain::StorageLifecycleStatus lifecycle_status =
		shuba::domain::StorageLifecycleStatus::Active,
	std::int64_t updated_at = 10) {
	return shuba::persistence::StorageEnvelope{
		.record = shuba::domain::StorageRecord{
			.id				   = make_id(std::move(id)),
			.display_name	   = std::move(display_name),
			.storage_type	   = std::move(storage_type),
			.parent_storage_id = std::move(parent_id),
			.lifecycle_status  = lifecycle_status,
			.timestamps		   = make_timestamps(1, updated_at)}};
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

struct SearchFixture final {
	shuba::core::StableIdentifier root_storage_id;
	shuba::core::StableIdentifier box_storage_id;
	shuba::core::StableIdentifier shelf_storage_id;
	shuba::core::StableIdentifier archived_storage_id;
	shuba::catalog::CatalogRepositoryState repository;
	shuba::catalog::SearchIndex index;
};

[[nodiscard]] SearchFixture make_search_fixture() {
	using namespace shuba::catalog;
	using namespace shuba::domain;

	const shuba::core::StableIdentifier root_id	 = make_id("storage-root");
	const shuba::core::StableIdentifier box_id	 = make_id("storage-box");
	const shuba::core::StableIdentifier shelf_id = make_id("storage-shelf");
	const shuba::core::StableIdentifier archived_storage_id =
		make_id("storage-archived");

	shuba::persistence::StorageEnvelope root =
		make_storage("storage-root", "Главный шкаф", "room", std::nullopt,
					 StorageLifecycleStatus::Active, 50);
	root.record.location = "Минск прихожая";
	root.record.tags	 = {TagRow{.key = "room", .value = "прихожая"}};
	root.record.notes	 = "верхний уровень";

	shuba::persistence::StorageEnvelope box =
		make_storage("storage-box", "Зимняя коробка", "box", root_id,
					 StorageLifecycleStatus::Active, 40);
	box.record.location = "полка А";
	box.record.tags		= {TagRow{.key = "season", .value = "winter"}};

	shuba::persistence::StorageEnvelope shelf =
		make_storage("storage-shelf", "Дальняя полка", "shelf", box_id,
					 StorageLifecycleStatus::Active, 30);

	shuba::persistence::StorageEnvelope archived_storage =
		make_storage("storage-archived", "Архивный склад", "box", root_id,
					 StorageLifecycleStatus::Archived, 20);

	shuba::persistence::ItemEnvelope boots =
		make_item("item-boots", "БОТИНКИ Nike 42", "footwear", box_id,
				  ItemStatus::Listed, 100);
	boots.record.tags  = {TagRow{.key = "brand", .value = "Nike"},
						  TagRow{.key = "season", .value = "зима"}};
	boots.record.notes = "Тёплая пара из Wildberries";
	boots.record.listing =
		ListingData{.marketplace = "Wildberries",
					.url		 = "https://wildberries.by/catalog/boots-42",
					.listed_on	 = BusinessDate{.value = "2026-01-02"},
					.price		 = make_money("80", "BYN"),
					.note		 = "листинг для зимы"};
	boots.record.acquisition =
		AcquisitionData{.source		 = "second hand",
						.acquired_on = BusinessDate{.value = "2025-12-01"},
						.cost		 = make_money("30", "BYN")};
	boots.record.finance =
		FinanceData{.original_price	 = make_money("120", "BYN"),
					.real_sale_price = make_money("70", "BYN"),
					.expenses_total	 = make_money("5", "BYN")};

	shuba::persistence::ItemEnvelope jacket =
		make_item("item-jacket", "Куртка синяя", "outerwear", root_id,
				  ItemStatus::Draft, 90);
	jacket.record.tags = {TagRow{.key = "color", .value = "синий"}};

	shuba::persistence::ItemEnvelope broken_photo_item =
		make_item("item-broken-photo", "Broken photo diagnostic", "other",
				  root_id, ItemStatus::Draft, 80);

	shuba::persistence::ItemEnvelope mixed_photo_item =
		make_item("item-mixed-photo", "Mixed photo toy", "toys", root_id,
				  ItemStatus::Draft, 70);

	shuba::persistence::ItemEnvelope nested_item =
		make_item("item-nested-scarf", "Шарф nested", "accessory", shelf_id,
				  ItemStatus::Planned, 60);

	shuba::persistence::ItemEnvelope archived_item =
		make_item("item-archived", "Archived old shoes", "footwear", root_id,
				  ItemStatus::Archived, 110);

	shuba::persistence::ItemEnvelope active_in_archived =
		make_item("item-active-archived-storage", "Active visible history",
				  "other", archived_storage_id, ItemStatus::Draft, 55);

	CatalogRepositoryState repository =
		build_catalog_repository(CatalogRepositoryInput{
			.items	  = {boots, jacket, broken_photo_item, mixed_photo_item,
						 nested_item, archived_item, active_in_archived},
			.storages = {root, box, shelf, archived_storage},
			.photos = {make_photo("photo-boots", PhotoOwnerType::Item,
								  boots.record.id, 10, true, 10),
					   make_photo("photo-broken", PhotoOwnerType::Item,
								  broken_photo_item.record.id, 10, true, 10),
					   make_photo("photo-mixed-broken", PhotoOwnerType::Item,
								  mixed_photo_item.record.id, 5, true, 10),
					   make_photo("photo-mixed-usable", PhotoOwnerType::Item,
								  mixed_photo_item.record.id, 20, false, 20),
					   make_photo("photo-storage-box", PhotoOwnerType::Storage,
								  box.record.id, 10, true, 10)},
			.media	= CatalogMediaSnapshot{
				.complete_scan_available	= true,
				.readable_photo_media_files = {
					"media/photos/photo-boots.jxl",
					"media/photos/photo-mixed-usable.jxl",
					"media/photos/photo-storage-box.jxl"}}});

	SearchIndex index = build_search_index(repository);
	return SearchFixture{.root_storage_id	  = root_id,
						 .box_storage_id	  = box_id,
						 .shelf_storage_id	  = shelf_id,
						 .archived_storage_id = archived_storage_id,
						 .repository		  = std::move(repository),
						 .index				  = std::move(index)};
}

[[nodiscard]] std::vector<std::string> item_ids(
	const shuba::catalog::CatalogSearchResultSet& results) {
	std::vector<std::string> ids;
	ids.reserve(results.item_results.size());
	for (const shuba::catalog::SearchResult& result : results.item_results)
		ids.push_back(result.record_id.value());
	return ids;
}

[[nodiscard]] std::vector<std::string> storage_ids(
	const shuba::catalog::CatalogSearchResultSet& results) {
	std::vector<std::string> ids;
	ids.reserve(results.storage_results.size());
	for (const shuba::catalog::SearchResult& result : results.storage_results)
		ids.push_back(result.record_id.value());
	return ids;
}

[[nodiscard]] bool contains_id(const std::vector<std::string>& ids,
							   std::string_view id) {
	return std::ranges::find(ids, id) != ids.end();
}

[[nodiscard]] const shuba::catalog::SearchResult& find_item_result(
	const shuba::catalog::CatalogSearchResultSet& results,
	std::string_view id) {
	const std::vector<shuba::catalog::SearchResult>::const_iterator found =
		std::ranges::find_if(results.item_results,
							 [id](const shuba::catalog::SearchResult& result) {
		return result.record_id.value() == id;
	});
	REQUIRE(found != results.item_results.end());
	return *found;
}
}	 // namespace

TEST_CASE("B09 normalizes representative Russian and English search text",
		  "[b09][search][normalization]") {
	using namespace shuba::catalog;

	const SearchNormalizationDecision decision =
		search_normalization_decision();
	REQUIRE(decision.dependency_name == "custom minimal UTF-8 normalizer");
	REQUIRE_FALSE(decision.standard_byte_lowercase_sufficient);
	REQUIRE_FALSE(decision.juce_lowercase_sufficient);
	REQUIRE(normalize_search_text("БОТИНКИ Ёлка Nike-42")
			== "ботинки елка nike 42");
	REQUIRE(tokenize_normalized_search_text("ботинки елка nike 42")
			== std::vector<std::string>{"ботинки", "елка", "nike", "42"});
}

TEST_CASE("B09 catalog search matches Russian English tags paths and tokens",
		  "[b09][search][catalog]") {
	using namespace shuba::catalog;

	const SearchFixture fixture = make_search_fixture();

	CatalogSearchResultSet russian_english =
		search_catalog(fixture.index, "ботинки NIKE");
	REQUIRE(item_ids(russian_english)
			== std::vector<std::string>{"item-boots"});
	REQUIRE(find_item_result(russian_english, "item-boots").match_tier
			== SearchMatchTier::DisplayNamePrefix);

	CatalogSearchResultSet folded_query =
		search_catalog(fixture.index, "теплая wildberries");
	REQUIRE(item_ids(folded_query) == std::vector<std::string>{"item-boots"});

	CatalogSearchResultSet tag_query =
		search_catalog(fixture.index, "brand nike");
	REQUIRE(item_ids(tag_query) == std::vector<std::string>{"item-boots"});
	REQUIRE(find_item_result(tag_query, "item-boots").match_tier
			== SearchMatchTier::Tag);

	CatalogSearchResultSet path_query =
		search_catalog(fixture.index, "главный зимняя");
	REQUIRE(contains_id(item_ids(path_query), "item-boots"));
	REQUIRE(contains_id(storage_ids(path_query), "storage-box"));
}

TEST_CASE(
	"B09 archive visibility keeps active items in archived storage visible",
	"[b09][search][archive]") {
	using namespace shuba::catalog;

	const SearchFixture fixture = make_search_fixture();

	CatalogSearchResultSet default_browse = search_catalog(fixture.index, "");
	const std::vector<std::string> default_item_ids = item_ids(default_browse);
	REQUIRE_FALSE(contains_id(default_item_ids, "item-archived"));
	REQUIRE(contains_id(default_item_ids, "item-active-archived-storage"));
	REQUIRE(find_item_result(default_browse, "item-active-archived-storage")
				.warnings.archived_storage);

	CatalogSearchFilters include_archived_items;
	include_archived_items.include_archived = true;
	CatalogSearchResultSet archived_browse =
		search_catalog(fixture.index, "", include_archived_items);
	REQUIRE(contains_id(item_ids(archived_browse), "item-archived"));

	CatalogSearchResultSet default_storages =
		search_storages(fixture.index, "");
	REQUIRE_FALSE(
		contains_id(storage_ids(default_storages), "storage-archived"));

	StorageSearchFilters include_archived_storages;
	include_archived_storages.include_archived = true;
	CatalogSearchResultSet archived_storages =
		search_storages(fixture.index, "", include_archived_storages);
	REQUIRE(contains_id(storage_ids(archived_storages), "storage-archived"));
}

TEST_CASE("B09 storage filters include nested contents by default",
		  "[b09][search][storage-filter]") {
	using namespace shuba::catalog;

	const SearchFixture fixture = make_search_fixture();

	CatalogSearchFilters nested_filter;
	nested_filter.storage_id = fixture.root_storage_id;
	CatalogSearchResultSet nested_results =
		search_catalog(fixture.index, "", nested_filter);
	const std::vector<std::string> nested_ids = item_ids(nested_results);
	REQUIRE(contains_id(nested_ids, "item-jacket"));
	REQUIRE(contains_id(nested_ids, "item-boots"));
	REQUIRE(contains_id(nested_ids, "item-nested-scarf"));
	REQUIRE(contains_id(nested_ids, "item-active-archived-storage"));

	CatalogSearchFilters direct_filter;
	direct_filter.storage_id			 = fixture.root_storage_id;
	direct_filter.include_nested_storage = false;
	CatalogSearchResultSet direct_results =
		search_catalog(fixture.index, "", direct_filter);
	const std::vector<std::string> direct_ids = item_ids(direct_results);
	REQUIRE(contains_id(direct_ids, "item-jacket"));
	REQUIRE_FALSE(contains_id(direct_ids, "item-boots"));
	REQUIRE_FALSE(contains_id(direct_ids, "item-nested-scarf"));
}

TEST_CASE("B09 item filters compose categories statuses and browse options",
		  "[b09][search][filters]") {
	using namespace shuba::catalog;
	using namespace shuba::domain;

	const SearchFixture fixture = make_search_fixture();

	CatalogSearchFilters listed_footwear;
	listed_footwear.categories = {"footwear"};
	listed_footwear.statuses   = {ItemStatus::Listed};
	CatalogSearchResultSet listed_results =
		search_catalog(fixture.index, "", listed_footwear);
	REQUIRE(item_ids(listed_results) == std::vector<std::string>{"item-boots"});

	CatalogSearchFilters quick_sale_state;
	quick_sale_state.listed_only = true;
	quick_sale_state.sold_only	 = true;
	CatalogSearchResultSet quick_sale_results =
		search_catalog(fixture.index, "", quick_sale_state);
	REQUIRE(item_ids(quick_sale_results)
			== std::vector<std::string>{"item-boots"});
	REQUIRE(quick_sale_results.storage_results.empty());
	REQUIRE(quick_sale_results.total_count == 1U);

	CatalogSearchOptions include_storage_browse;
	include_storage_browse.include_storage_results_for_empty_query = true;
	CatalogSearchResultSet browse_with_storages = search_catalog(
		fixture.index, "", CatalogSearchFilters{}, include_storage_browse);
	const std::vector<std::string> visible_storage_ids =
		storage_ids(browse_with_storages);
	REQUIRE(contains_id(visible_storage_ids, "storage-root"));
	REQUIRE(contains_id(visible_storage_ids, "storage-box"));
	REQUIRE(contains_id(visible_storage_ids, "storage-shelf"));
	REQUIRE_FALSE(contains_id(visible_storage_ids, "storage-archived"));
}

TEST_CASE("B09 photo presence filters cover all internal edge states",
		  "[b09][search][photos]") {
	using namespace shuba::catalog;

	const SearchFixture fixture = make_search_fixture();

	CatalogSearchFilters has_photos;
	has_photos.photo_presence = SearchPhotoPresenceFilter::HasPhotos;
	CatalogSearchResultSet has_photo_results =
		search_catalog(fixture.index, "", has_photos);
	const std::vector<std::string> has_photo_ids = item_ids(has_photo_results);
	REQUIRE(contains_id(has_photo_ids, "item-boots"));
	REQUIRE(contains_id(has_photo_ids, "item-mixed-photo"));
	REQUIRE_FALSE(contains_id(has_photo_ids, "item-broken-photo"));
	REQUIRE_FALSE(contains_id(has_photo_ids, "item-jacket"));

	CatalogSearchFilters no_photos;
	no_photos.photo_presence = SearchPhotoPresenceFilter::NoPhotos;
	CatalogSearchResultSet no_photo_results =
		search_catalog(fixture.index, "", no_photos);
	const std::vector<std::string> no_photo_ids = item_ids(no_photo_results);
	REQUIRE(contains_id(no_photo_ids, "item-jacket"));
	REQUIRE(contains_id(no_photo_ids, "item-nested-scarf"));
	REQUIRE_FALSE(contains_id(no_photo_ids, "item-broken-photo"));

	CatalogSearchFilters broken_photos;
	broken_photos.photo_presence = SearchPhotoPresenceFilter::BrokenPhotos;
	CatalogSearchResultSet broken_photo_results =
		search_catalog(fixture.index, "", broken_photos);
	const std::vector<std::string> broken_photo_ids =
		item_ids(broken_photo_results);
	REQUIRE(contains_id(broken_photo_ids, "item-broken-photo"));
	REQUIRE(contains_id(broken_photo_ids, "item-mixed-photo"));
	REQUIRE_FALSE(contains_id(broken_photo_ids, "item-jacket"));
}

TEST_CASE("B09 storage search applies storage-specific filters",
		  "[b09][search][storages]") {
	using namespace shuba::catalog;

	const SearchFixture fixture = make_search_fixture();

	CatalogSearchResultSet path_results =
		search_storages(fixture.index, "главный");
	REQUIRE(contains_id(storage_ids(path_results), "storage-box"));
	REQUIRE(contains_id(storage_ids(path_results), "storage-shelf"));

	StorageSearchFilters type_filter;
	type_filter.storage_types = {"shelf"};
	CatalogSearchResultSet shelf_results =
		search_storages(fixture.index, "", type_filter);
	REQUIRE(storage_ids(shelf_results)
			== std::vector<std::string>{"storage-shelf"});

	StorageSearchFilters parent_filter;
	parent_filter.parent_storage_id = fixture.box_storage_id;
	CatalogSearchResultSet child_results =
		search_storages(fixture.index, "", parent_filter);
	REQUIRE(storage_ids(child_results)
			== std::vector<std::string>{"storage-shelf"});
}
