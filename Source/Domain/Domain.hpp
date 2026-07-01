#pragma once

#include "Core/Clock.hpp"
#include "Core/Identifier.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace shuba::domain {
inline constexpr auto first_record_schema_version = 1;
inline constexpr auto max_money_scale			  = std::uint8_t{9};

[[nodiscard]] bool is_ascii_space(char Value) noexcept;
[[nodiscard]] bool has_non_whitespace(std::string_view Text) noexcept;
[[nodiscard]] std::string trim_ascii_copy(std::string_view Text);

enum class ItemStatus : std::uint8_t {
	Draft,
	Planned,
	Listed,
	Sold,
	Archived,
};

[[nodiscard]] std::string_view to_string(ItemStatus Status) noexcept;
[[nodiscard]] std::optional<ItemStatus> parse_item_status(
	std::string_view Text) noexcept;

enum class StorageLifecycleStatus : std::uint8_t {
	Active,
	Archived,
};

[[nodiscard]] std::string_view to_string(
	StorageLifecycleStatus Status) noexcept;
[[nodiscard]] std::optional<StorageLifecycleStatus>
parse_storage_lifecycle_status(std::string_view Text) noexcept;

enum class PhotoOwnerType : std::uint8_t {
	Item,
	Storage,
};

[[nodiscard]] std::string_view to_string(PhotoOwnerType OwnerType) noexcept;
[[nodiscard]] std::optional<PhotoOwnerType> parse_photo_owner_type(
	std::string_view Text) noexcept;

enum class PhotoMediaFormat : std::uint8_t {
	JpegXl,
};

[[nodiscard]] std::string_view to_string(PhotoMediaFormat Format) noexcept;
[[nodiscard]] std::optional<PhotoMediaFormat> parse_photo_media_format(
	std::string_view Text) noexcept;

struct BusinessDate final {
	std::string value;

	friend bool operator==(const BusinessDate&, const BusinessDate&) = default;
};

struct RecordTimestamps final {
	core::EpochMilliseconds created_at{};
	core::EpochMilliseconds updated_at{};

	friend bool operator==(const RecordTimestamps&,
						   const RecordTimestamps&) = default;
};

enum class MoneyParseIssue : std::uint8_t {
	None,
	EmptyAmount,
	EmptyCurrency,
	MissingDigits,
	InvalidCharacter,
	MultipleDecimalSeparators,
	TooManyFractionDigits,
	AmountOverflow,
};

[[nodiscard]] std::string_view to_string(MoneyParseIssue Issue) noexcept;

struct MoneyAmount final {
	std::int64_t units{};
	std::uint8_t scale{};
	std::string currency;

	friend bool operator==(const MoneyAmount&, const MoneyAmount&) = default;
};

[[nodiscard]] MoneyAmount normalize_money_amount(MoneyAmount Amount);

struct MoneyParseResult final {
	std::optional<MoneyAmount> amount;
	MoneyParseIssue issue{MoneyParseIssue::None};

	[[nodiscard]] bool valid() const noexcept;
	[[nodiscard]] explicit operator bool() const noexcept;
	[[nodiscard]] const MoneyAmount& value() const;
};

[[nodiscard]] MoneyParseResult parse_money_amount(
	std::string_view DecimalText, std::string_view CurrencyLabel);
[[nodiscard]] std::string canonical_decimal_text(const MoneyAmount& Amount);
[[nodiscard]] std::optional<MoneyAmount> calculate_profit(
	const MoneyAmount& SalePrice, const MoneyAmount& AcquisitionCost,
	const MoneyAmount& ExpensesTotal);

struct TagRow final {
	std::string key;
	std::string value;

	friend bool operator==(const TagRow&, const TagRow&) = default;
};

struct ListingData final {
	std::string marketplace;
	std::string url;
	std::optional<BusinessDate> listed_on;
	std::optional<MoneyAmount> price;
	std::string note;

	[[nodiscard]] bool empty() const noexcept;

	friend bool operator==(const ListingData&, const ListingData&) = default;
};

struct AcquisitionData final {
	std::string source;
	std::optional<BusinessDate> acquired_on;
	std::optional<MoneyAmount> cost;

	[[nodiscard]] bool empty() const noexcept;

	friend bool operator==(const AcquisitionData&,
						   const AcquisitionData&) = default;
};

struct FinanceData final {
	std::optional<MoneyAmount> original_price;
	std::optional<MoneyAmount> real_sale_price;
	std::optional<MoneyAmount> expenses_total;

	[[nodiscard]] bool empty() const noexcept;

	friend bool operator==(const FinanceData&, const FinanceData&) = default;
};

[[nodiscard]] std::optional<MoneyAmount> calculate_profit(
	const AcquisitionData& Acquisition, const FinanceData& Finance);

struct ItemRecord final {
	core::StableIdentifier id;
	int schema_version{first_record_schema_version};
	std::string display_name;
	std::string category;
	std::optional<core::StableIdentifier> storage_id;
	std::vector<TagRow> tags;
	std::string notes;
	ItemStatus status{ItemStatus::Draft};
	ListingData listing;
	AcquisitionData acquisition;
	FinanceData finance;
	RecordTimestamps timestamps;

	friend bool operator==(const ItemRecord&, const ItemRecord&) = default;
};

struct StorageRecord final {
	core::StableIdentifier id;
	int schema_version{first_record_schema_version};
	std::string display_name;
	std::string storage_type;
	std::optional<core::StableIdentifier> parent_storage_id;
	std::string location;
	std::vector<TagRow> tags;
	std::string notes;
	StorageLifecycleStatus lifecycle_status{StorageLifecycleStatus::Active};
	RecordTimestamps timestamps;

	friend bool operator==(const StorageRecord&,
						   const StorageRecord&) = default;
};

struct PhotoOwner final {
	PhotoOwnerType type{PhotoOwnerType::Item};
	core::StableIdentifier id;

	friend bool operator==(const PhotoOwner&, const PhotoOwner&) = default;
};

struct PhotoRecord final {
	core::StableIdentifier id;
	int schema_version{first_record_schema_version};
	PhotoOwnerType owner_type{PhotoOwnerType::Item};
	core::StableIdentifier owner_id;
	PhotoMediaFormat media_format{PhotoMediaFormat::JpegXl};
	std::int64_t sort_order{};
	bool is_main{};
	std::optional<std::int32_t> width;
	std::optional<std::int32_t> height;
	std::optional<std::uint64_t> encoded_bytes;
	std::string source_mime_type;
	std::string source_md5;
	RecordTimestamps timestamps;

	friend bool operator==(const PhotoRecord&, const PhotoRecord&) = default;
};

enum class TagKeyValidationIssue : std::uint8_t {
	None,
	BlankKey,
};

struct TagValidationResult final {
	TagKeyValidationIssue issue{TagKeyValidationIssue::None};
	bool accepted{};
	bool contributes_to_key_hints{};

	[[nodiscard]] bool has_warning() const noexcept;
};

[[nodiscard]] bool is_blank_tag_key(std::string_view Key) noexcept;
[[nodiscard]] TagValidationResult validate_tag_for_ui_save(
	const TagRow& Tag) noexcept;
[[nodiscard]] TagValidationResult validate_existing_tag_row(
	const TagRow& Tag) noexcept;
[[nodiscard]] bool is_tag_key_hint_candidate(const TagRow& Tag) noexcept;
[[nodiscard]] std::vector<std::string> derive_tag_key_hints(
	std::span<const ItemRecord> Items, std::span<const StorageRecord> Storages);

enum class RecordRequiredFieldIssue : std::uint8_t {
	EmptyDisplayName,
	EmptyCategory,
	EmptyStorageType,
};

[[nodiscard]] std::vector<RecordRequiredFieldIssue> validate_required_fields(
	const ItemRecord& Item);
[[nodiscard]] std::vector<RecordRequiredFieldIssue> validate_required_fields(
	const StorageRecord& Storage);

enum class ReferenceState : std::uint8_t {
	Absent,
	Resolved,
	Broken,
};

[[nodiscard]] bool contains_storage_id(std::span<const StorageRecord> Storages,
									   const core::StableIdentifier& StorageId);
[[nodiscard]] ReferenceState classify_item_storage_reference(
	const ItemRecord& Item, std::span<const StorageRecord> Storages);
[[nodiscard]] ReferenceState classify_storage_parent_reference(
	const StorageRecord& Storage, std::span<const StorageRecord> Storages);

struct StorageCycleCheck final {
	bool has_cycle{};
	std::vector<std::string> path;

	[[nodiscard]] explicit operator bool() const noexcept;
};

[[nodiscard]] StorageCycleCheck would_create_storage_parent_cycle(
	const core::StableIdentifier& StorageId,
	const std::optional<core::StableIdentifier>& ParentStorageId,
	std::span<const StorageRecord> Storages);
[[nodiscard]] StorageCycleCheck check_storage_parent_cycle(
	const StorageRecord& Storage, std::span<const StorageRecord> Storages);

[[nodiscard]] bool is_archived(const ItemRecord& Item) noexcept;
[[nodiscard]] bool is_archived(const StorageRecord& Storage) noexcept;
void archive_item(ItemRecord& Item) noexcept;
void archive_storage(StorageRecord& Storage) noexcept;

enum class RecordLifecycleAction : std::uint8_t {
	Archive,
	HardDelete,
};

[[nodiscard]] bool is_normal_lifecycle_action(
	RecordLifecycleAction Action) noexcept;
[[nodiscard]] bool requires_hard_delete_gate(
	RecordLifecycleAction Action) noexcept;
[[nodiscard]] bool owner_hard_delete_visible(
	bool MultiFileDeletionSequenceTestsProven) noexcept;

[[nodiscard]] PhotoOwner owner_of(const PhotoRecord& Photo);
[[nodiscard]] bool photo_owner_matches(const PhotoRecord& Photo,
									   const PhotoOwner& Owner) noexcept;
[[nodiscard]] std::vector<const PhotoRecord*> ordered_photos_for_owner(
	std::span<const PhotoRecord> Photos, const PhotoOwner& Owner);
[[nodiscard]] const PhotoRecord* representative_photo_for_owner(
	std::span<const PhotoRecord> Photos, const PhotoOwner& Owner);
[[nodiscard]] std::size_t count_main_photos_for_owner(
	std::span<const PhotoRecord> Photos, const PhotoOwner& Owner);
[[nodiscard]] bool has_duplicate_photo_sort_order(
	std::span<const PhotoRecord> Photos, const PhotoOwner& Owner);
[[nodiscard]] std::int64_t next_photo_sort_order(
	std::span<const PhotoRecord> Photos, const PhotoOwner& Owner,
	std::int64_t Step = 1000);
bool select_main_photo(std::span<PhotoRecord> Photos,
					   const core::StableIdentifier& SelectedPhotoId);
}	 // namespace shuba::domain
