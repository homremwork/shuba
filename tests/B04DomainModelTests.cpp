#include "Domain/Domain.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace {
[[nodiscard]] shuba::core::StableIdentifier make_id(std::string Text) {
	auto identifier =
		shuba::core::StableIdentifier::try_create_file_safe(std::move(Text));
	REQUIRE(identifier.has_value());
	return *identifier;
}

[[nodiscard]] shuba::domain::RecordTimestamps make_timestamps(
	std::int64_t CreatedAt, std::int64_t UpdatedAt) {
	return {.created_at = shuba::core::EpochMilliseconds{CreatedAt},
			.updated_at = shuba::core::EpochMilliseconds{UpdatedAt}};
}
}	 // namespace

TEST_CASE("B04 item statuses and archive rules use the first-version lifecycle",
		  "[b04][domain][status]") {
	using namespace shuba::domain;

	REQUIRE(parse_item_status("draft").value() == ItemStatus::Draft);
	REQUIRE(parse_item_status("planned").value() == ItemStatus::Planned);
	REQUIRE(parse_item_status("listed").value() == ItemStatus::Listed);
	REQUIRE(parse_item_status("sold").value() == ItemStatus::Sold);
	REQUIRE(parse_item_status("archived").value() == ItemStatus::Archived);
	REQUIRE_FALSE(parse_item_status("reserved").has_value());
	REQUIRE(to_string(ItemStatus::Archived) == "archived");

	auto item = ItemRecord{.id			 = make_id("item-001"),
						   .display_name = "Winter boots",
						   .category	 = "footwear",
						   .timestamps	 = make_timestamps(1, 2)};
	REQUIRE(item.status == ItemStatus::Draft);
	REQUIRE_FALSE(is_archived(item));

	archive_item(item);
	REQUIRE(item.status == ItemStatus::Archived);
	REQUIRE(is_archived(item));

	auto storage = StorageRecord{.id		   = make_id("storage-001"),
								 .display_name = "Box A",
								 .storage_type = "box",
								 .timestamps   = make_timestamps(3, 4)};
	REQUIRE(storage.lifecycle_status == StorageLifecycleStatus::Active);
	archive_storage(storage);
	REQUIRE(storage.lifecycle_status == StorageLifecycleStatus::Archived);
	REQUIRE(is_archived(storage));

	REQUIRE(is_normal_lifecycle_action(RecordLifecycleAction::Archive));
	REQUIRE_FALSE(
		is_normal_lifecycle_action(RecordLifecycleAction::HardDelete));
	REQUIRE(requires_hard_delete_gate(RecordLifecycleAction::HardDelete));
	REQUIRE_FALSE(owner_hard_delete_visible(false));
	REQUIRE(owner_hard_delete_visible(true));
}

TEST_CASE("B04 tags preserve existing blank keys but block new UI blank keys",
		  "[b04][domain][tags]") {
	using namespace shuba::domain;

	const auto blank_tag = TagRow{.key = "  ", .value = "legacy value"};
	const auto ui_result = validate_tag_for_ui_save(blank_tag);
	REQUIRE_FALSE(ui_result.accepted);
	REQUIRE(ui_result.issue == TagKeyValidationIssue::BlankKey);
	REQUIRE_FALSE(ui_result.contributes_to_key_hints);

	const auto existing_result = validate_existing_tag_row(blank_tag);
	REQUIRE(existing_result.accepted);
	REQUIRE(existing_result.has_warning());
	REQUIRE(existing_result.issue == TagKeyValidationIssue::BlankKey);
	REQUIRE_FALSE(existing_result.contributes_to_key_hints);

	const auto item = ItemRecord{
		.id			  = make_id("item-tags"),
		.display_name = "Sneakers",
		.category	  = "footwear",
		.tags		  = {TagRow{.key = "brand", .value = "Nike"},
						 TagRow{.key = "brand", .value = "duplicate allowed"},
						 blank_tag, TagRow{.key = "season", .value = "winter"}},
		.timestamps	  = make_timestamps(1, 2)};
	const auto storage = StorageRecord{
		.id			  = make_id("storage-tags"),
		.display_name = "Hall shelf",
		.storage_type = "shelf",
		.tags		  = {TagRow{.key = "brand", .value = "ignored duplicate"},
						 TagRow{.key = "room", .value = "hall"}},
		.timestamps	  = make_timestamps(3, 4)};

	const auto hints = derive_tag_key_hints(
		std::vector<ItemRecord>{item}, std::vector<StorageRecord>{storage});
	REQUIRE(hints == std::vector<std::string>{"brand", "season", "room"});
}

TEST_CASE(
	"B04 money parsing and profit use exact decimal values and currencies",
	"[b04][domain][money]") {
	using namespace shuba::domain;

	const auto sale		= parse_money_amount("100.00", "BYN");
	const auto cost		= parse_money_amount("60.25", "BYN");
	const auto expenses = parse_money_amount("5.75", "BYN");
	REQUIRE(sale);
	REQUIRE(cost);
	REQUIRE(expenses);
	REQUIRE(
		canonical_decimal_text(parse_money_amount("0012.3400", " BYN ").value())
		== "12.34");

	const auto profit =
		calculate_profit(sale.value(), cost.value(), expenses.value());
	REQUIRE(profit.has_value());
	REQUIRE(profit->currency == "BYN");
	REQUIRE(canonical_decimal_text(*profit) == "34");

	const auto acquisition = AcquisitionData{.cost = cost.value()};
	const auto finance	   = FinanceData{.real_sale_price = sale.value(),
										 .expenses_total  = expenses.value()};
	REQUIRE(calculate_profit(acquisition, finance).has_value());

	const auto missing_expenses = FinanceData{.real_sale_price = sale.value()};
	REQUIRE_FALSE(calculate_profit(acquisition, missing_expenses).has_value());

	const auto zero_expenses = parse_money_amount("0.00", "BYN");
	REQUIRE(zero_expenses);
	const auto explicit_zero_profit = calculate_profit(
		acquisition, FinanceData{.real_sale_price = sale.value(),
								 .expenses_total  = zero_expenses.value()});
	REQUIRE(explicit_zero_profit.has_value());
	REQUIRE(canonical_decimal_text(*explicit_zero_profit) == "39.75");

	const auto usd_expenses = parse_money_amount("5.75", "USD");
	REQUIRE(usd_expenses);
	REQUIRE_FALSE(
		calculate_profit(sale.value(), cost.value(), usd_expenses.value())
			.has_value());
	REQUIRE(parse_money_amount("12,34", "BYN").issue
			== MoneyParseIssue::InvalidCharacter);
}

TEST_CASE("B04 storage references classify missing links and detect cycles",
		  "[b04][domain][storage]") {
	using namespace shuba::domain;

	const auto root_id	  = make_id("storage-root");
	const auto child_id	  = make_id("storage-child");
	const auto missing_id = make_id("storage-missing");

	const auto root = StorageRecord{.id			  = root_id,
									.display_name = "Root box",
									.storage_type = "box",
									.timestamps	  = make_timestamps(1, 2)};
	const auto child =
		StorageRecord{.id				 = child_id,
					  .display_name		 = "Child bag",
					  .storage_type		 = "bag",
					  .parent_storage_id = std::optional{root_id},
					  .timestamps		 = make_timestamps(3, 4)};
	const auto storages = std::vector<StorageRecord>{root, child};

	const auto unassigned_item =
		ItemRecord{.id			 = make_id("item-unassigned"),
				   .display_name = "Unassigned item",
				   .category	 = "other",
				   .timestamps	 = make_timestamps(5, 6)};
	REQUIRE(classify_item_storage_reference(unassigned_item, storages)
			== ReferenceState::Absent);

	const auto assigned_item = ItemRecord{.id = make_id("item-assigned"),
										  .display_name = "Assigned item",
										  .category		= "other",
										  .storage_id = std::optional{child_id},
										  .timestamps = make_timestamps(7, 8)};
	REQUIRE(classify_item_storage_reference(assigned_item, storages)
			== ReferenceState::Resolved);

	const auto broken_item = ItemRecord{.id			  = make_id("item-broken"),
										.display_name = "Broken item",
										.category	  = "other",
										.storage_id = std::optional{missing_id},
										.timestamps = make_timestamps(9, 10)};
	REQUIRE(classify_item_storage_reference(broken_item, storages)
			== ReferenceState::Broken);
	REQUIRE(classify_storage_parent_reference(child, storages)
			== ReferenceState::Resolved);

	const auto broken_parent =
		StorageRecord{.id				 = make_id("storage-broken"),
					  .display_name		 = "Broken parent",
					  .storage_type		 = "box",
					  .parent_storage_id = std::optional{missing_id},
					  .timestamps		 = make_timestamps(11, 12)};
	REQUIRE(classify_storage_parent_reference(broken_parent, storages)
			== ReferenceState::Broken);

	REQUIRE_FALSE(check_storage_parent_cycle(child, storages));
	const auto proposed_cycle = would_create_storage_parent_cycle(
		root_id, std::optional{child_id}, storages);
	REQUIRE(proposed_cycle.has_cycle);
	REQUIRE(proposed_cycle.path
			== std::vector<std::string>{"storage-child", "storage-root"});

	const auto self_parent = StorageRecord{
		.id				   = make_id("storage-self"),
		.display_name	   = "Self parent",
		.storage_type	   = "box",
		.parent_storage_id = std::optional{make_id("storage-self")},
		.timestamps		   = make_timestamps(13, 14)};
	REQUIRE(check_storage_parent_cycle(self_parent,
									   std::vector<StorageRecord>{self_parent})
				.has_cycle);
}

TEST_CASE("B04 photo helpers derive owner ordering and main photo state",
		  "[b04][domain][photos]") {
	using namespace shuba::domain;

	const auto owner_id		  = make_id("item-photo-owner");
	const auto other_owner_id = make_id("storage-photo-owner");
	const auto owner = PhotoOwner{.type = PhotoOwnerType::Item, .id = owner_id};
	const auto other_owner =
		PhotoOwner{.type = PhotoOwnerType::Storage, .id = other_owner_id};
	const auto photo_1_id = make_id("photo-001");
	const auto photo_2_id = make_id("photo-002");
	const auto photo_3_id = make_id("photo-003");

	auto photos = std::vector<PhotoRecord>{
		PhotoRecord{.id			= photo_1_id,
					.owner_type = PhotoOwnerType::Item,
					.owner_id	= owner_id,
					.sort_order = 100,
					.is_main	= true,
					.timestamps = make_timestamps(10, 10)},
		PhotoRecord{.id			= photo_2_id,
					.owner_type = PhotoOwnerType::Item,
					.owner_id	= owner_id,
					.sort_order = 100,
					.is_main	= true,
					.timestamps = make_timestamps(5, 5)},
		PhotoRecord{.id			= photo_3_id,
					.owner_type = PhotoOwnerType::Item,
					.owner_id	= owner_id,
					.sort_order = 200,
					.timestamps = make_timestamps(1, 1)},
		PhotoRecord{.id			= make_id("photo-004"),
					.owner_type = PhotoOwnerType::Storage,
					.owner_id	= other_owner_id,
					.sort_order = 50,
					.is_main	= true,
					.timestamps = make_timestamps(1, 1)}};

	const auto ordered = ordered_photos_for_owner(photos, owner);
	REQUIRE(ordered.size() == 3);
	REQUIRE(ordered[0]->id == photo_2_id);
	REQUIRE(ordered[1]->id == photo_1_id);
	REQUIRE(ordered[2]->id == photo_3_id);
	REQUIRE(has_duplicate_photo_sort_order(photos, owner));
	REQUIRE_FALSE(has_duplicate_photo_sort_order(photos, other_owner));

	const auto representative = representative_photo_for_owner(photos, owner);
	REQUIRE(representative != nullptr);
	REQUIRE(representative->id == photo_2_id);
	REQUIRE(count_main_photos_for_owner(photos, owner) == 2);
	REQUIRE(next_photo_sort_order(photos, owner) == 1200);

	REQUIRE(select_main_photo(photos, photo_3_id));
	const auto selected_representative =
		representative_photo_for_owner(photos, owner);
	REQUIRE(selected_representative != nullptr);
	REQUIRE(selected_representative->id == photo_3_id);
	REQUIRE(count_main_photos_for_owner(photos, owner) == 1);
	REQUIRE(representative_photo_for_owner(photos, other_owner)->is_main);
	REQUIRE_FALSE(select_main_photo(photos, make_id("photo-missing")));
}
