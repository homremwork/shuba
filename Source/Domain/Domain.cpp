#include "Domain/Domain.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace shuba::domain {
bool is_ascii_space(char Value) noexcept {
	return std::isspace(static_cast<unsigned char>(Value)) != 0;
}

bool has_non_whitespace(std::string_view Text) noexcept {
	for (const auto value : Text)
		if (!is_ascii_space(value))
			return true;

	return false;
}

std::string trim_ascii_copy(std::string_view Text) {
	auto first = std::size_t{0};
	while (first < Text.size() && is_ascii_space(Text[first]))
		++first;

	auto last = Text.size();
	while (last > first && is_ascii_space(Text[last - 1]))
		--last;

	return std::string(Text.substr(first, last - first));
}

std::string_view to_string(ItemStatus Status) noexcept {
	switch (Status) {
		case ItemStatus::Draft:
			return "draft";
		case ItemStatus::Planned:
			return "planned";
		case ItemStatus::Listed:
			return "listed";
		case ItemStatus::Sold:
			return "sold";
		case ItemStatus::Archived:
			return "archived";
	}

	return "unknown item status";
}

std::optional<ItemStatus> parse_item_status(std::string_view Text) noexcept {
	if (Text == "draft")
		return ItemStatus::Draft;
	if (Text == "planned")
		return ItemStatus::Planned;
	if (Text == "listed")
		return ItemStatus::Listed;
	if (Text == "sold")
		return ItemStatus::Sold;
	if (Text == "archived")
		return ItemStatus::Archived;

	return std::nullopt;
}

std::string_view to_string(StorageLifecycleStatus Status) noexcept {
	switch (Status) {
		case StorageLifecycleStatus::Active:
			return "active";
		case StorageLifecycleStatus::Archived:
			return "archived";
	}

	return "unknown storage lifecycle status";
}

std::optional<StorageLifecycleStatus> parse_storage_lifecycle_status(
	std::string_view Text) noexcept {
	if (Text == "active")
		return StorageLifecycleStatus::Active;
	if (Text == "archived")
		return StorageLifecycleStatus::Archived;

	return std::nullopt;
}

std::string_view to_string(PhotoOwnerType OwnerType) noexcept {
	switch (OwnerType) {
		case PhotoOwnerType::Item:
			return "item";
		case PhotoOwnerType::Storage:
			return "storage";
	}

	return "unknown photo owner type";
}

std::optional<PhotoOwnerType> parse_photo_owner_type(
	std::string_view Text) noexcept {
	if (Text == "item")
		return PhotoOwnerType::Item;
	if (Text == "storage")
		return PhotoOwnerType::Storage;

	return std::nullopt;
}

std::string_view to_string(PhotoMediaFormat Format) noexcept {
	switch (Format) {
		case PhotoMediaFormat::JpegXl:
			return "jxl";
	}

	return "unknown photo media format";
}

std::optional<PhotoMediaFormat> parse_photo_media_format(
	std::string_view Text) noexcept {
	if (Text == "jxl")
		return PhotoMediaFormat::JpegXl;

	return std::nullopt;
}

std::string_view to_string(MoneyParseIssue Issue) noexcept {
	switch (Issue) {
		case MoneyParseIssue::None:
			return "none";
		case MoneyParseIssue::EmptyAmount:
			return "empty amount";
		case MoneyParseIssue::EmptyCurrency:
			return "empty currency";
		case MoneyParseIssue::MissingDigits:
			return "missing digits";
		case MoneyParseIssue::InvalidCharacter:
			return "invalid character";
		case MoneyParseIssue::MultipleDecimalSeparators:
			return "multiple decimal separators";
		case MoneyParseIssue::TooManyFractionDigits:
			return "too many fraction digits";
		case MoneyParseIssue::AmountOverflow:
			return "amount overflow";
	}

	return "unknown money parse issue";
}

MoneyAmount normalize_money_amount(MoneyAmount Amount) {
	if (Amount.units == 0) {
		Amount.scale = 0;
		return Amount;
	}

	while (Amount.scale > 0 && Amount.units % 10 == 0) {
		Amount.units /= 10;
		--Amount.scale;
	}

	return Amount;
}

bool MoneyParseResult::valid() const noexcept {
	return amount.has_value();
}

MoneyParseResult::operator bool() const noexcept {
	return valid();
}

const MoneyAmount& MoneyParseResult::value() const {
	return *amount;
}

MoneyParseResult parse_money_amount(std::string_view DecimalText,
									std::string_view CurrencyLabel) {
	const auto amount_text = trim_ascii_copy(DecimalText);
	auto currency		   = trim_ascii_copy(CurrencyLabel);

	if (amount_text.empty())
		return {.issue = MoneyParseIssue::EmptyAmount};
	if (currency.empty())
		return {.issue = MoneyParseIssue::EmptyCurrency};

	auto negative = false;
	auto index	  = std::size_t{0};
	if (amount_text[index] == '+' || amount_text[index] == '-') {
		negative = amount_text[index] == '-';
		++index;
	}

	auto units		 = std::int64_t{0};
	auto scale		 = std::uint8_t{0};
	auto saw_digit	 = false;
	auto saw_decimal = false;

	for (; index < amount_text.size(); ++index) {
		const auto value = amount_text[index];

		if (value == '.') {
			if (saw_decimal)
				return {.issue = MoneyParseIssue::MultipleDecimalSeparators};

			saw_decimal = true;
			continue;
		}

		if (value < '0' || value > '9')
			return {.issue = MoneyParseIssue::InvalidCharacter};

		saw_digit = true;

		if (saw_decimal) {
			if (scale == max_money_scale)
				return {.issue = MoneyParseIssue::TooManyFractionDigits};
			++scale;
		}

		const auto digit = static_cast<std::int64_t>(value - '0');
		if (units > (std::numeric_limits<std::int64_t>::max() - digit) / 10)
			return {.issue = MoneyParseIssue::AmountOverflow};

		units = (units * 10) + digit;
	}

	if (!saw_digit)
		return {.issue = MoneyParseIssue::MissingDigits};

	if (negative)
		units = -units;

	return {
		.amount = normalize_money_amount(MoneyAmount{
			.units = units, .scale = scale, .currency = std::move(currency)}),
		.issue	= MoneyParseIssue::None};
}

namespace {
[[nodiscard]] std::uint64_t absolute_units(std::int64_t Units) noexcept {
	if (Units >= 0)
		return static_cast<std::uint64_t>(Units);

	return static_cast<std::uint64_t>(-(Units + 1)) + 1U;
}

[[nodiscard]] std::optional<std::int64_t> scale_units(
	MoneyAmount Amount, std::uint8_t TargetScale) {
	if (Amount.scale > TargetScale)
		return std::nullopt;

	auto units = Amount.units;
	while (Amount.scale < TargetScale) {
		if (units > std::numeric_limits<std::int64_t>::max() / 10
			|| units < std::numeric_limits<std::int64_t>::min() / 10) {
			return std::nullopt;
		}

		units *= 10;
		++Amount.scale;
	}

	return units;
}

[[nodiscard]] std::optional<std::int64_t> checked_subtract(
	std::int64_t Left, std::int64_t Right) noexcept {
	if ((Right > 0 && Left < std::numeric_limits<std::int64_t>::min() + Right)
		|| (Right < 0
			&& Left > std::numeric_limits<std::int64_t>::max() + Right)) {
		return std::nullopt;
	}

	return Left - Right;
}

void append_tag_key_hint(std::vector<std::string>& Hints,
						 std::unordered_set<std::string>& SeenKeys,
						 const TagRow& Tag) {
	if (!is_tag_key_hint_candidate(Tag))
		return;

	if (SeenKeys.insert(Tag.key).second)
		Hints.push_back(Tag.key);
}
}	 // namespace

std::string canonical_decimal_text(const MoneyAmount& Amount) {
	const auto normalized = normalize_money_amount(Amount);
	auto digits			  = std::to_string(absolute_units(normalized.units));

	if (normalized.scale > 0) {
		const auto scale = static_cast<std::size_t>(normalized.scale);
		if (digits.size() <= scale)
			digits.insert(0, (scale - digits.size()) + 1, '0');

		digits.insert(digits.end() - static_cast<std::ptrdiff_t>(scale), '.');
	}

	if (normalized.units < 0 && absolute_units(normalized.units) != 0)
		digits.insert(digits.begin(), '-');

	return digits;
}

std::optional<MoneyAmount> calculate_profit(const MoneyAmount& SalePrice,
											const MoneyAmount& AcquisitionCost,
											const MoneyAmount& ExpensesTotal) {
	if (SalePrice.currency != AcquisitionCost.currency
		|| SalePrice.currency != ExpensesTotal.currency) {
		return std::nullopt;
	}

	const auto target_scale =
		std::max({SalePrice.scale, AcquisitionCost.scale, ExpensesTotal.scale});
	const auto sale_units	 = scale_units(SalePrice, target_scale);
	const auto cost_units	 = scale_units(AcquisitionCost, target_scale);
	const auto expense_units = scale_units(ExpensesTotal, target_scale);

	if (!sale_units || !cost_units || !expense_units)
		return std::nullopt;

	const auto after_cost = checked_subtract(*sale_units, *cost_units);
	if (!after_cost)
		return std::nullopt;

	const auto after_expenses = checked_subtract(*after_cost, *expense_units);
	if (!after_expenses)
		return std::nullopt;

	return normalize_money_amount(MoneyAmount{.units	= *after_expenses,
											  .scale	= target_scale,
											  .currency = SalePrice.currency});
}

bool ListingData::empty() const noexcept {
	return marketplace.empty() && url.empty() && !listed_on.has_value()
		   && !price.has_value() && note.empty();
}

bool AcquisitionData::empty() const noexcept {
	return source.empty() && !acquired_on.has_value() && !cost.has_value();
}

bool FinanceData::empty() const noexcept {
	return !original_price.has_value() && !real_sale_price.has_value()
		   && !expenses_total.has_value();
}

std::optional<MoneyAmount> calculate_profit(const AcquisitionData& Acquisition,
											const FinanceData& Finance) {
	if (!Finance.real_sale_price || !Acquisition.cost
		|| !Finance.expenses_total)
		return std::nullopt;

	return calculate_profit(*Finance.real_sale_price, *Acquisition.cost,
							*Finance.expenses_total);
}

bool TagValidationResult::has_warning() const noexcept {
	return accepted && issue != TagKeyValidationIssue::None;
}

bool is_blank_tag_key(std::string_view Key) noexcept {
	return !has_non_whitespace(Key);
}

TagValidationResult validate_tag_for_ui_save(const TagRow& Tag) noexcept {
	if (is_blank_tag_key(Tag.key))
		return {.issue					  = TagKeyValidationIssue::BlankKey,
				.accepted				  = false,
				.contributes_to_key_hints = false};

	return {.issue					  = TagKeyValidationIssue::None,
			.accepted				  = true,
			.contributes_to_key_hints = true};
}

TagValidationResult validate_existing_tag_row(const TagRow& Tag) noexcept {
	if (is_blank_tag_key(Tag.key))
		return {.issue					  = TagKeyValidationIssue::BlankKey,
				.accepted				  = true,
				.contributes_to_key_hints = false};

	return {.issue					  = TagKeyValidationIssue::None,
			.accepted				  = true,
			.contributes_to_key_hints = true};
}

bool is_tag_key_hint_candidate(const TagRow& Tag) noexcept {
	return validate_existing_tag_row(Tag).contributes_to_key_hints;
}

std::vector<std::string> derive_tag_key_hints(
	std::span<const ItemRecord> Items,
	std::span<const StorageRecord> Storages) {
	auto hints	   = std::vector<std::string>{};
	auto seen_keys = std::unordered_set<std::string>{};

	for (const auto& item : Items)
		for (const auto& tag : item.tags)
			append_tag_key_hint(hints, seen_keys, tag);

	for (const auto& storage : Storages)
		for (const auto& tag : storage.tags)
			append_tag_key_hint(hints, seen_keys, tag);

	return hints;
}

std::vector<RecordRequiredFieldIssue> validate_required_fields(
	const ItemRecord& Item) {
	auto issues = std::vector<RecordRequiredFieldIssue>{};
	if (!has_non_whitespace(Item.display_name))
		issues.push_back(RecordRequiredFieldIssue::EmptyDisplayName);
	if (!has_non_whitespace(Item.category))
		issues.push_back(RecordRequiredFieldIssue::EmptyCategory);
	return issues;
}

std::vector<RecordRequiredFieldIssue> validate_required_fields(
	const StorageRecord& Storage) {
	auto issues = std::vector<RecordRequiredFieldIssue>{};
	if (!has_non_whitespace(Storage.display_name))
		issues.push_back(RecordRequiredFieldIssue::EmptyDisplayName);
	if (!has_non_whitespace(Storage.storage_type))
		issues.push_back(RecordRequiredFieldIssue::EmptyStorageType);
	return issues;
}

bool contains_storage_id(std::span<const StorageRecord> Storages,
						 const core::StableIdentifier& StorageId) {
	return std::any_of(
		Storages.begin(), Storages.end(),
		[&](const auto& Storage) { return Storage.id == StorageId; });
}

ReferenceState classify_item_storage_reference(
	const ItemRecord& Item, std::span<const StorageRecord> Storages) {
	if (!Item.storage_id)
		return ReferenceState::Absent;

	return contains_storage_id(Storages, *Item.storage_id)
			   ? ReferenceState::Resolved
			   : ReferenceState::Broken;
}

ReferenceState classify_storage_parent_reference(
	const StorageRecord& Storage, std::span<const StorageRecord> Storages) {
	if (!Storage.parent_storage_id)
		return ReferenceState::Absent;

	return contains_storage_id(Storages, *Storage.parent_storage_id)
			   ? ReferenceState::Resolved
			   : ReferenceState::Broken;
}

StorageCycleCheck::operator bool() const noexcept {
	return has_cycle;
}

StorageCycleCheck would_create_storage_parent_cycle(
	const core::StableIdentifier& StorageId,
	const std::optional<core::StableIdentifier>& ParentStorageId,
	std::span<const StorageRecord> Storages) {
	if (!ParentStorageId)
		return {};

	auto parent_by_storage_id = std::unordered_map<std::string, std::string>{};
	for (const auto& storage : Storages)
		if (storage.parent_storage_id)
			parent_by_storage_id[storage.id.value()] =
				storage.parent_storage_id->value();

	parent_by_storage_id[StorageId.value()] = ParentStorageId->value();

	auto visited = std::unordered_set<std::string>{};
	auto path	 = std::vector<std::string>{};
	auto current = ParentStorageId->value();

	while (true) {
		path.push_back(current);

		if (current == StorageId.value())
			return {.has_cycle = true, .path = std::move(path)};

		if (!visited.insert(current).second)
			return {.has_cycle = true, .path = std::move(path)};

		const auto next = parent_by_storage_id.find(current);
		if (next == parent_by_storage_id.end())
			return {};

		current = next->second;
	}
}

StorageCycleCheck check_storage_parent_cycle(
	const StorageRecord& Storage, std::span<const StorageRecord> Storages) {
	return would_create_storage_parent_cycle(
		Storage.id, Storage.parent_storage_id, Storages);
}

bool is_archived(const ItemRecord& Item) noexcept {
	return Item.status == ItemStatus::Archived;
}

bool is_archived(const StorageRecord& Storage) noexcept {
	return Storage.lifecycle_status == StorageLifecycleStatus::Archived;
}

void archive_item(ItemRecord& Item) noexcept {
	Item.status = ItemStatus::Archived;
}

void archive_storage(StorageRecord& Storage) noexcept {
	Storage.lifecycle_status = StorageLifecycleStatus::Archived;
}

bool is_normal_lifecycle_action(RecordLifecycleAction Action) noexcept {
	return Action == RecordLifecycleAction::Archive;
}

bool requires_hard_delete_gate(RecordLifecycleAction Action) noexcept {
	return Action == RecordLifecycleAction::HardDelete;
}

bool owner_hard_delete_visible(
	bool MultiFileDeletionSequenceTestsProven) noexcept {
	return MultiFileDeletionSequenceTestsProven;
}

PhotoOwner owner_of(const PhotoRecord& Photo) {
	return PhotoOwner{.type = Photo.owner_type, .id = Photo.owner_id};
}

bool photo_owner_matches(const PhotoRecord& Photo,
						 const PhotoOwner& Owner) noexcept {
	return Photo.owner_type == Owner.type && Photo.owner_id == Owner.id;
}

std::vector<const PhotoRecord*> ordered_photos_for_owner(
	std::span<const PhotoRecord> Photos, const PhotoOwner& Owner) {
	auto ordered = std::vector<const PhotoRecord*>{};
	for (const auto& photo : Photos)
		if (photo_owner_matches(photo, Owner))
			ordered.push_back(&photo);

	std::sort(ordered.begin(), ordered.end(),
			  [](const auto* Left, const auto* Right) {
		if (Left->sort_order != Right->sort_order)
			return Left->sort_order < Right->sort_order;
		if (Left->timestamps.created_at != Right->timestamps.created_at)
			return Left->timestamps.created_at < Right->timestamps.created_at;
		return Left->id.value() < Right->id.value();
	});

	return ordered;
}

const PhotoRecord* representative_photo_for_owner(
	std::span<const PhotoRecord> Photos, const PhotoOwner& Owner) {
	const auto ordered = ordered_photos_for_owner(Photos, Owner);
	if (ordered.empty())
		return nullptr;

	for (const auto* photo : ordered)
		if (photo->is_main)
			return photo;

	return ordered.front();
}

std::size_t count_main_photos_for_owner(std::span<const PhotoRecord> Photos,
										const PhotoOwner& Owner) {
	return static_cast<std::size_t>(
		std::count_if(Photos.begin(), Photos.end(), [&](const auto& Photo) {
		return Photo.is_main && photo_owner_matches(Photo, Owner);
	}));
}

bool has_duplicate_photo_sort_order(std::span<const PhotoRecord> Photos,
									const PhotoOwner& Owner) {
	auto seen_orders = std::unordered_set<std::int64_t>{};
	for (const auto& photo : Photos) {
		if (!photo_owner_matches(photo, Owner))
			continue;

		if (!seen_orders.insert(photo.sort_order).second)
			return true;
	}

	return false;
}

std::int64_t next_photo_sort_order(std::span<const PhotoRecord> Photos,
								   const PhotoOwner& Owner, std::int64_t Step) {
	auto has_owner_photo = false;
	auto max_order		 = std::int64_t{0};

	for (const auto& photo : Photos) {
		if (!photo_owner_matches(photo, Owner))
			continue;

		has_owner_photo = true;
		max_order		= std::max(max_order, photo.sort_order);
	}

	if (!has_owner_photo)
		return Step;

	if (max_order > std::numeric_limits<std::int64_t>::max() - Step)
		return max_order;

	return max_order + Step;
}

bool select_main_photo(std::span<PhotoRecord> Photos,
					   const core::StableIdentifier& SelectedPhotoId) {
	const auto selected = std::find_if(
		Photos.begin(), Photos.end(),
		[&](const auto& Photo) { return Photo.id == SelectedPhotoId; });

	if (selected == Photos.end())
		return false;

	const auto selected_owner = owner_of(*selected);
	for (auto& photo : Photos)
		if (photo_owner_matches(photo, selected_owner))
			photo.is_main = photo.id == SelectedPhotoId;

	return true;
}
}	 // namespace shuba::domain
