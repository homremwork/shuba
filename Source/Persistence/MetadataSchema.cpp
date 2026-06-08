#include "Persistence/MetadataSchema.hpp"

#include <glaze/json.hpp>
#include <glaze/json/read.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace shuba::persistence::schema_detail {
struct MoneyDto final {
	std::optional<std::string> amount;
	std::optional<std::string> currency;
};

struct TagRowDto final {
	std::optional<std::string> key;
	std::optional<std::string> value;
};

struct ListingDto final {
	std::optional<std::string> marketplace;
	std::optional<std::string> url;
	std::optional<std::string> listed_on;
	std::optional<MoneyDto> price;
	std::optional<std::string> note;
};

struct AcquisitionDto final {
	std::optional<std::string> source;
	std::optional<std::string> acquired_on;
	std::optional<MoneyDto> cost;
};

struct FinanceDto final {
	std::optional<MoneyDto> original_price;
	std::optional<MoneyDto> real_sale_price;
	std::optional<MoneyDto> expenses_total;
};

struct DataFilesDto final {
	std::optional<std::string> items;
	std::optional<std::string> storages;
	std::optional<std::string> photos;
};

struct MediaDto final {
	std::optional<std::string> photo_directory;
	std::optional<std::string> photo_format;
	std::optional<std::string> photo_extension;
};

struct FeaturesDto final {
	std::optional<bool> photo_owner_records;
	std::optional<bool> unknown_entity_field_preservation;
	std::optional<bool> entity_jsonl_current_state_tables;
};

struct ManifestDto final {
	std::optional<int> schema_version;
	std::optional<std::string> catalog_id;
	std::optional<std::int64_t> created_at;
	glz::raw_json last_migration_at;
	std::optional<DataFilesDto> data_files;
	std::optional<MediaDto> media;
	std::optional<FeaturesDto> features;
};

struct JpegExportDto final {
	std::optional<int> quality;
};

struct SettingsDto final {
	std::optional<int> schema_version;
	std::optional<std::string> default_currency;
	std::optional<JpegExportDto> jpeg_export;
};

struct ItemDto final {
	std::optional<std::string> id;
	std::optional<int> schema_version;
	std::optional<std::string> display_name;
	std::optional<std::string> category;
	std::optional<std::string> storage_id;
	std::vector<TagRowDto> tags;
	std::optional<std::string> notes;
	std::optional<std::string> status;
	std::optional<ListingDto> listing;
	std::optional<AcquisitionDto> acquisition;
	std::optional<FinanceDto> finance;
	std::optional<std::int64_t> created_at;
	std::optional<std::int64_t> updated_at;
	std::map<std::string, glz::raw_json> unknown_fields;
};

struct StorageDto final {
	std::optional<std::string> id;
	std::optional<int> schema_version;
	std::optional<std::string> display_name;
	std::optional<std::string> storage_type;
	std::optional<std::string> parent_storage_id;
	std::optional<std::string> location;
	std::vector<TagRowDto> tags;
	std::optional<std::string> notes;
	std::optional<std::string> lifecycle_status;
	std::optional<std::int64_t> created_at;
	std::optional<std::int64_t> updated_at;
	std::map<std::string, glz::raw_json> unknown_fields;
};

struct PhotoDto final {
	std::optional<std::string> id;
	std::optional<int> schema_version;
	std::optional<std::string> owner_type;
	std::optional<std::string> owner_id;
	std::optional<std::string> media_format;
	std::optional<std::int64_t> sort_order;
	std::optional<bool> is_main;
	std::optional<std::int32_t> width;
	std::optional<std::int32_t> height;
	std::optional<std::uint64_t> encoded_bytes;
	std::optional<std::string> source_mime_type;
	std::optional<std::int64_t> created_at;
	std::optional<std::int64_t> updated_at;
	std::map<std::string, glz::raw_json> unknown_fields;
};
}	 // namespace shuba::persistence::schema_detail

template<>
struct glz::meta<shuba::persistence::schema_detail::MoneyDto> {
	using T = shuba::persistence::schema_detail::MoneyDto;
	static constexpr auto value =
		object("amount", &T::amount, "currency", &T::currency);
};

template<>
struct glz::meta<shuba::persistence::schema_detail::TagRowDto> {
	using T						= shuba::persistence::schema_detail::TagRowDto;
	static constexpr auto value = object("key", &T::key, "value", &T::value);
};

template<>
struct glz::meta<shuba::persistence::schema_detail::ListingDto> {
	using T = shuba::persistence::schema_detail::ListingDto;
	static constexpr auto value =
		object("marketplace", &T::marketplace, "url", &T::url, "listedOn",
			   &T::listed_on, "price", &T::price, "note", &T::note);
};

template<>
struct glz::meta<shuba::persistence::schema_detail::AcquisitionDto> {
	using T = shuba::persistence::schema_detail::AcquisitionDto;
	static constexpr auto value = object("source", &T::source, "acquiredOn",
										 &T::acquired_on, "cost", &T::cost);
};

template<>
struct glz::meta<shuba::persistence::schema_detail::FinanceDto> {
	using T = shuba::persistence::schema_detail::FinanceDto;
	static constexpr auto value =
		object("originalPrice", &T::original_price, "realSalePrice",
			   &T::real_sale_price, "expensesTotal", &T::expenses_total);
};

template<>
struct glz::meta<shuba::persistence::schema_detail::DataFilesDto> {
	using T = shuba::persistence::schema_detail::DataFilesDto;
	static constexpr auto value = object("items", &T::items, "storages",
										 &T::storages, "photos", &T::photos);
};

template<>
struct glz::meta<shuba::persistence::schema_detail::MediaDto> {
	using T = shuba::persistence::schema_detail::MediaDto;
	static constexpr auto value =
		object("photoDirectory", &T::photo_directory, "photoFormat",
			   &T::photo_format, "photoExtension", &T::photo_extension);
};

template<>
struct glz::meta<shuba::persistence::schema_detail::FeaturesDto> {
	using T = shuba::persistence::schema_detail::FeaturesDto;
	static constexpr auto value = object(
		"photoOwnerRecords", &T::photo_owner_records,
		"unknownEntityFieldPreservation", &T::unknown_entity_field_preservation,
		"entityJsonlCurrentStateTables", &T::entity_jsonl_current_state_tables);
};

template<>
struct glz::meta<shuba::persistence::schema_detail::ManifestDto> {
	using T = shuba::persistence::schema_detail::ManifestDto;
	static constexpr auto value =
		object("schemaVersion", &T::schema_version, "catalogId", &T::catalog_id,
			   "createdAt", &T::created_at, "lastMigrationAt",
			   &T::last_migration_at, "dataFiles", &T::data_files, "media",
			   &T::media, "features", &T::features);
};

template<>
struct glz::meta<shuba::persistence::schema_detail::JpegExportDto> {
	using T = shuba::persistence::schema_detail::JpegExportDto;
	static constexpr auto value = object("quality", &T::quality);
};

template<>
struct glz::meta<shuba::persistence::schema_detail::SettingsDto> {
	using T = shuba::persistence::schema_detail::SettingsDto;
	static constexpr auto value =
		object("schemaVersion", &T::schema_version, "defaultCurrency",
			   &T::default_currency, "jpegExport", &T::jpeg_export);
};

template<>
struct glz::meta<shuba::persistence::schema_detail::ItemDto> {
	using T						= shuba::persistence::schema_detail::ItemDto;
	static constexpr auto value = object(
		"id", &T::id, "schemaVersion", &T::schema_version, "displayName",
		&T::display_name, "category", &T::category, "storageId", &T::storage_id,
		"tags", &T::tags, "notes", &T::notes, "status", &T::status, "listing",
		&T::listing, "acquisition", &T::acquisition, "finance", &T::finance,
		"createdAt", &T::created_at, "updatedAt", &T::updated_at);
	static constexpr auto unknown_write{&T::unknown_fields};
	static constexpr auto unknown_read{&T::unknown_fields};
};

template<>
struct glz::meta<shuba::persistence::schema_detail::StorageDto> {
	using T						= shuba::persistence::schema_detail::StorageDto;
	static constexpr auto value = object(
		"id", &T::id, "schemaVersion", &T::schema_version, "displayName",
		&T::display_name, "storageType", &T::storage_type, "parentStorageId",
		&T::parent_storage_id, "location", &T::location, "tags", &T::tags,
		"notes", &T::notes, "lifecycleStatus", &T::lifecycle_status,
		"createdAt", &T::created_at, "updatedAt", &T::updated_at);
	static constexpr auto unknown_write{&T::unknown_fields};
	static constexpr auto unknown_read{&T::unknown_fields};
};

template<>
struct glz::meta<shuba::persistence::schema_detail::PhotoDto> {
	using T						= shuba::persistence::schema_detail::PhotoDto;
	static constexpr auto value = object(
		"id", &T::id, "schemaVersion", &T::schema_version, "ownerType",
		&T::owner_type, "ownerId", &T::owner_id, "mediaFormat",
		&T::media_format, "sortOrder", &T::sort_order, "isMain", &T::is_main,
		"width", &T::width, "height", &T::height, "encodedBytes",
		&T::encoded_bytes, "sourceMimeType", &T::source_mime_type, "createdAt",
		&T::created_at, "updatedAt", &T::updated_at);
	static constexpr auto unknown_write{&T::unknown_fields};
	static constexpr auto unknown_read{&T::unknown_fields};
};

namespace shuba::persistence {
namespace {
using namespace schema_detail;

constexpr auto item_known_fields = std::array{
	std::string_view{"id"},			 std::string_view{"schemaVersion"},
	std::string_view{"displayName"}, std::string_view{"category"},
	std::string_view{"storageId"},	 std::string_view{"tags"},
	std::string_view{"notes"},		 std::string_view{"status"},
	std::string_view{"listing"},	 std::string_view{"acquisition"},
	std::string_view{"finance"},	 std::string_view{"createdAt"},
	std::string_view{"updatedAt"}};

constexpr auto storage_known_fields =
	std::array{std::string_view{"id"},
			   std::string_view{"schemaVersion"},
			   std::string_view{"displayName"},
			   std::string_view{"storageType"},
			   std::string_view{"parentStorageId"},
			   std::string_view{"location"},
			   std::string_view{"tags"},
			   std::string_view{"notes"},
			   std::string_view{"lifecycleStatus"},
			   std::string_view{"createdAt"},
			   std::string_view{"updatedAt"}};

constexpr auto photo_known_fields =
	std::array{std::string_view{"id"},
			   std::string_view{"schemaVersion"},
			   std::string_view{"ownerType"},
			   std::string_view{"ownerId"},
			   std::string_view{"mediaFormat"},
			   std::string_view{"sortOrder"},
			   std::string_view{"isMain"},
			   std::string_view{"width"},
			   std::string_view{"height"},
			   std::string_view{"encodedBytes"},
			   std::string_view{"sourceMimeType"},
			   std::string_view{"createdAt"},
			   std::string_view{"updatedAt"}};

[[nodiscard]] bool contains_known_field(
	std::string_view key,
	std::span<const std::string_view> known_fields) noexcept {
	return std::ranges::find(known_fields, key) != known_fields.end();
}

[[nodiscard]] SchemaDiagnostic make_diagnostic(
	SchemaIssue issue, std::string field, std::string message,
	std::string technical_details = {}) {
	return {.issue			   = issue,
			.field			   = std::move(field),
			.message		   = std::move(message),
			.technical_details = std::move(technical_details)};
}

template<class Value>
[[nodiscard]] SchemaReadResult<Value> read_success(Value value) {
	return {.value = std::move(value)};
}

template<class Value>
[[nodiscard]] SchemaReadResult<Value> read_failure(
	SchemaDiagnostic diagnostic) {
	return {.diagnostic = std::move(diagnostic)};
}

[[nodiscard]] SchemaWriteResult write_success(std::string json) {
	return {.json = std::move(json)};
}

[[nodiscard]] SchemaWriteResult write_failure(SchemaDiagnostic diagnostic) {
	return {.diagnostic = std::move(diagnostic)};
}

template<class Dto>
[[nodiscard]] std::optional<SchemaDiagnostic> read_json_dto(
	Dto& dto, std::string_view json, std::string_view schema_name) {
	auto input = std::string{json};
	const auto error =
		glz::read<glz::opts{.error_on_unknown_keys = false}>(dto, input);

	if (!error)
		return std::nullopt;

	return make_diagnostic(
		SchemaIssue::JsonParseError, std::string{schema_name},
		"JSON cannot be parsed into the first-version metadata schema.",
		glz::format_error(error, input));
}

template<class Dto>
[[nodiscard]] SchemaWriteResult write_json_dto(const Dto& dto,
											   std::string_view schema_name) {
	auto output		 = std::string{};
	const auto error = glz::write_json(dto, output);

	if (!error)
		return write_success(std::move(output));

	return write_failure(make_diagnostic(
		SchemaIssue::SerializationFailure, std::string{schema_name},
		"Metadata schema object cannot be serialized to JSON.",
		glz::format_error(error, output)));
}

[[nodiscard]] std::optional<int> schema_version_or_diagnostic(
	const std::optional<int>& schema_version, std::string_view field,
	SchemaDiagnostic& diagnostic) {
	const auto version = schema_version.value_or(first_catalog_schema_version);

	if (version <= 0) {
		diagnostic = make_diagnostic(
			SchemaIssue::MalformedKnownField, std::string{field},
			"Schema version must be a positive integer.");
		return std::nullopt;
	}

	if (version > first_catalog_schema_version) {
		diagnostic = make_diagnostic(SchemaIssue::UnsupportedSchemaVersion,
									 std::string{field},
									 "Metadata schema version is newer than "
									 "this implementation supports.");
		return std::nullopt;
	}

	return version;
}

[[nodiscard]] std::optional<int> required_schema_version_or_diagnostic(
	const std::optional<int>& schema_version, std::string_view field,
	SchemaDiagnostic& diagnostic) {
	if (!schema_version) {
		diagnostic =
			make_diagnostic(SchemaIssue::MissingRequiredField,
							std::string{field}, "Schema version is required.");
		return std::nullopt;
	}

	return schema_version_or_diagnostic(schema_version, field, diagnostic);
}

[[nodiscard]] std::optional<std::string> required_text(
	const std::optional<std::string>& value, std::string_view field,
	SchemaDiagnostic& diagnostic) {
	if (!value || !domain::has_non_whitespace(*value)) {
		diagnostic = make_diagnostic(
			SchemaIssue::MissingRequiredField, std::string{field},
			"Required text field is missing or blank.");
		return std::nullopt;
	}

	return *value;
}

[[nodiscard]] std::optional<core::StableIdentifier> required_identifier(
	const std::optional<std::string>& value, std::string_view field,
	SchemaDiagnostic& diagnostic) {
	if (!value || value->empty()) {
		diagnostic = make_diagnostic(
			SchemaIssue::MissingRequiredField, std::string{field},
			"Required identifier is missing or empty.");
		return std::nullopt;
	}

	auto identifier = core::StableIdentifier::try_create(*value);
	if (!identifier) {
		diagnostic = make_diagnostic(
			SchemaIssue::InvalidIdentifier, std::string{field},
			"Identifier is not safe for catalog metadata.", *value);
		return std::nullopt;
	}

	return identifier;
}

[[nodiscard]] std::optional<core::StableIdentifier> optional_identifier(
	const std::optional<std::string>& value, std::string_view field,
	SchemaDiagnostic& diagnostic) {
	if (!value)
		return std::nullopt;

	if (value->empty()) {
		diagnostic =
			make_diagnostic(SchemaIssue::InvalidIdentifier, std::string{field},
							"Optional identifier is present but empty.");
		return std::nullopt;
	}

	auto identifier = core::StableIdentifier::try_create(*value);
	if (!identifier) {
		diagnostic = make_diagnostic(
			SchemaIssue::InvalidIdentifier, std::string{field},
			"Identifier is not safe for catalog metadata.", *value);
		return std::nullopt;
	}

	return identifier;
}

[[nodiscard]] std::optional<domain::RecordTimestamps> required_timestamps(
	const std::optional<std::int64_t>& created_at,
	const std::optional<std::int64_t>& updated_at,
	SchemaDiagnostic& diagnostic) {
	if (!created_at) {
		diagnostic =
			make_diagnostic(SchemaIssue::MissingRequiredField, "createdAt",
							"Created timestamp is required.");
		return std::nullopt;
	}

	if (!updated_at) {
		diagnostic =
			make_diagnostic(SchemaIssue::MissingRequiredField, "updatedAt",
							"Updated timestamp is required.");
		return std::nullopt;
	}

	return domain::RecordTimestamps{
		.created_at = core::EpochMilliseconds{*created_at},
		.updated_at = core::EpochMilliseconds{*updated_at}};
}

[[nodiscard]] bool is_digit(char value) noexcept {
	return std::isdigit(static_cast<unsigned char>(value)) != 0;
}

[[nodiscard]] int two_digit_value(std::string_view text,
								  std::size_t offset) noexcept {
	return ((text[offset] - '0') * 10) + (text[offset + 1] - '0');
}

[[nodiscard]] bool is_valid_business_date(std::string_view text) noexcept {
	if (text.size() != 10 || text[4] != '-' || text[7] != '-')
		return false;

	for (const auto index : std::array<std::size_t, 8>{0, 1, 2, 3, 5, 6, 8, 9})
		if (!is_digit(text[index]))
			return false;

	const auto month = two_digit_value(text, 5);
	const auto day	 = two_digit_value(text, 8);
	return month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

[[nodiscard]] std::optional<domain::BusinessDate> optional_business_date(
	const std::optional<std::string>& value, std::string_view field,
	SchemaDiagnostic& diagnostic) {
	if (!value)
		return std::nullopt;

	if (!is_valid_business_date(*value)) {
		diagnostic = make_diagnostic(
			SchemaIssue::MalformedKnownField, std::string{field},
			"Optional business date is malformed and cannot be preserved raw "
			"by this schema adapter.",
			*value);
		return std::nullopt;
	}

	return domain::BusinessDate{.value = *value};
}

[[nodiscard]] std::optional<domain::MoneyAmount> optional_money(
	const std::optional<MoneyDto>& value, std::string_view field,
	SchemaDiagnostic& diagnostic) {
	if (!value)
		return std::nullopt;

	if (!value->amount || !value->currency) {
		diagnostic = make_diagnostic(
			SchemaIssue::MalformedKnownField, std::string{field},
			"Money object is missing amount or currency and cannot be "
			"preserved raw by this schema adapter.");
		return std::nullopt;
	}

	auto parsed = domain::parse_money_amount(*value->amount, *value->currency);
	if (!parsed) {
		diagnostic = make_diagnostic(
			SchemaIssue::MalformedKnownField, std::string{field},
			"Optional money field is malformed and cannot be preserved raw by "
			"this schema adapter.",
			std::string{domain::to_string(parsed.issue)});
		return std::nullopt;
	}

	return parsed.value();
}

[[nodiscard]] std::optional<std::vector<domain::TagRow>> tag_rows(
	const std::vector<TagRowDto>& rows, SchemaDiagnostic& diagnostic) {
	auto tags = std::vector<domain::TagRow>{};
	tags.reserve(rows.size());

	for (auto index = std::size_t{0}; index < rows.size(); ++index) {
		const auto& row = rows[index];
		if (!row.key) {
			diagnostic =
				make_diagnostic(SchemaIssue::MalformedKnownField,
								"tags[" + std::to_string(index) + "].key",
								"Tag row key is missing and cannot be "
								"preserved raw by this schema adapter.");
			return std::nullopt;
		}

		tags.push_back(domain::TagRow{
			.key = *row.key, .value = row.value.value_or(std::string{})});
	}

	return tags;
}

[[nodiscard]] std::optional<domain::ListingData> listing_data(
	const std::optional<ListingDto>& value, SchemaDiagnostic& diagnostic) {
	auto listing = domain::ListingData{};
	if (!value)
		return listing;

	listing.marketplace = value->marketplace.value_or(std::string{});
	listing.url			= value->url.value_or(std::string{});
	listing.listed_on = optional_business_date(value->listed_on,
											   "listing.listedOn", diagnostic);
	if (diagnostic.issue != SchemaIssue::None)
		return std::nullopt;
	listing.price = optional_money(value->price, "listing.price", diagnostic);
	if (diagnostic.issue != SchemaIssue::None)
		return std::nullopt;
	listing.note = value->note.value_or(std::string{});

	return listing;
}

[[nodiscard]] std::optional<domain::AcquisitionData> acquisition_data(
	const std::optional<AcquisitionDto>& value, SchemaDiagnostic& diagnostic) {
	auto acquisition = domain::AcquisitionData{};
	if (!value)
		return acquisition;

	acquisition.source		= value->source.value_or(std::string{});
	acquisition.acquired_on = optional_business_date(
		value->acquired_on, "acquisition.acquiredOn", diagnostic);
	if (diagnostic.issue != SchemaIssue::None)
		return std::nullopt;
	acquisition.cost =
		optional_money(value->cost, "acquisition.cost", diagnostic);
	if (diagnostic.issue != SchemaIssue::None)
		return std::nullopt;

	return acquisition;
}

[[nodiscard]] std::optional<domain::FinanceData> finance_data(
	const std::optional<FinanceDto>& value, SchemaDiagnostic& diagnostic) {
	auto finance = domain::FinanceData{};
	if (!value)
		return finance;

	finance.original_price = optional_money(
		value->original_price, "finance.originalPrice", diagnostic);
	if (diagnostic.issue != SchemaIssue::None)
		return std::nullopt;
	finance.real_sale_price = optional_money(
		value->real_sale_price, "finance.realSalePrice", diagnostic);
	if (diagnostic.issue != SchemaIssue::None)
		return std::nullopt;
	finance.expenses_total = optional_money(
		value->expenses_total, "finance.expensesTotal", diagnostic);
	if (diagnostic.issue != SchemaIssue::None)
		return std::nullopt;

	return finance;
}

[[nodiscard]] UnknownFields public_unknown_fields(
	const std::map<std::string, glz::raw_json>& unknown_fields) {
	auto result = UnknownFields{};
	for (const auto& [key, value] : unknown_fields)
		result.emplace(key, value.str);
	return result;
}

[[nodiscard]] std::optional<SchemaDiagnostic> validate_unknown_fields(
	const UnknownFields& unknown_fields,
	std::span<const std::string_view> known_fields) {
	for (const auto& [key, raw_json] : unknown_fields) {
		if (contains_known_field(key, known_fields)) {
			return make_diagnostic(
				SchemaIssue::InvalidKnownValue, key,
				"Preserved unknown field conflicts with a known schema field.");
		}

		if (key.empty() || raw_json.empty()) {
			return make_diagnostic(
				SchemaIssue::MalformedKnownField, key,
				"Preserved unknown field has an empty key or raw JSON value.");
		}

		auto generic_value = glz::generic_u64{};
		const auto error   = glz::read_json(generic_value, raw_json);
		if (error) {
			return make_diagnostic(
				SchemaIssue::MalformedKnownField, key,
				"Preserved unknown field raw value is not valid JSON.",
				glz::format_error(error, raw_json));
		}
	}

	return std::nullopt;
}

[[nodiscard]] std::map<std::string, glz::raw_json> glaze_unknown_fields(
	const UnknownFields& unknown_fields,
	std::span<const std::string_view> known_fields) {
	auto result = std::map<std::string, glz::raw_json>{};
	for (const auto& [key, raw_json] : unknown_fields) {
		if (contains_known_field(key, known_fields))
			continue;

		auto raw = glz::raw_json{};
		raw.str	 = raw_json;
		result.emplace(key, std::move(raw));
	}
	return result;
}

[[nodiscard]] std::optional<std::string> optional_non_empty(std::string value) {
	if (value.empty())
		return std::nullopt;

	return value;
}

[[nodiscard]] MoneyDto money_dto(const domain::MoneyAmount& value) {
	return {.amount	  = domain::canonical_decimal_text(value),
			.currency = value.currency};
}

[[nodiscard]] std::optional<MoneyDto> optional_money_dto(
	const std::optional<domain::MoneyAmount>& value) {
	if (!value)
		return std::nullopt;

	return money_dto(*value);
}

[[nodiscard]] std::vector<TagRowDto> tag_row_dtos(
	const std::vector<domain::TagRow>& tags) {
	auto result = std::vector<TagRowDto>{};
	result.reserve(tags.size());
	for (const auto& tag : tags)
		result.push_back(TagRowDto{.key = tag.key, .value = tag.value});
	return result;
}

[[nodiscard]] std::optional<ListingDto> listing_dto(
	const domain::ListingData& listing) {
	if (listing.empty())
		return std::nullopt;

	return ListingDto{.marketplace = optional_non_empty(listing.marketplace),
					  .url		   = optional_non_empty(listing.url),
					  .listed_on = listing.listed_on
									   ? std::optional{listing.listed_on->value}
									   : std::nullopt,
					  .price	 = optional_money_dto(listing.price),
					  .note		 = optional_non_empty(listing.note)};
}

[[nodiscard]] std::optional<AcquisitionDto> acquisition_dto(
	const domain::AcquisitionData& acquisition) {
	if (acquisition.empty())
		return std::nullopt;

	return AcquisitionDto{
		.source		 = optional_non_empty(acquisition.source),
		.acquired_on = acquisition.acquired_on
						   ? std::optional{acquisition.acquired_on->value}
						   : std::nullopt,
		.cost		 = optional_money_dto(acquisition.cost)};
}

[[nodiscard]] std::optional<FinanceDto> finance_dto(
	const domain::FinanceData& finance) {
	if (finance.empty())
		return std::nullopt;

	return FinanceDto{
		.original_price	 = optional_money_dto(finance.original_price),
		.real_sale_price = optional_money_dto(finance.real_sale_price),
		.expenses_total	 = optional_money_dto(finance.expenses_total)};
}

[[nodiscard]] ItemDto item_dto(const ItemEnvelope& envelope) {
	const auto& item = envelope.record;
	return ItemDto{
		.id				= item.id.value(),
		.schema_version = item.schema_version,
		.display_name	= item.display_name,
		.category		= item.category,
		.storage_id	 = item.storage_id ? std::optional{item.storage_id->value()}
									   : std::nullopt,
		.tags		 = tag_row_dtos(item.tags),
		.notes		 = optional_non_empty(item.notes),
		.status		 = std::string{domain::to_string(item.status)},
		.listing	 = listing_dto(item.listing),
		.acquisition = acquisition_dto(item.acquisition),
		.finance	 = finance_dto(item.finance),
		.created_at	 = item.timestamps.created_at.count(),
		.updated_at	 = item.timestamps.updated_at.count(),
		.unknown_fields =
			glaze_unknown_fields(envelope.unknown_fields, item_known_fields)};
}

[[nodiscard]] StorageDto storage_dto(const StorageEnvelope& envelope) {
	const auto& storage = envelope.record;
	return StorageDto{
		.id				= storage.id.value(),
		.schema_version = storage.schema_version,
		.display_name	= storage.display_name,
		.storage_type	= storage.storage_type,
		.parent_storage_id =
			storage.parent_storage_id
				? std::optional{storage.parent_storage_id->value()}
				: std::nullopt,
		.location = optional_non_empty(storage.location),
		.tags	  = tag_row_dtos(storage.tags),
		.notes	  = optional_non_empty(storage.notes),
		.lifecycle_status =
			std::string{domain::to_string(storage.lifecycle_status)},
		.created_at		= storage.timestamps.created_at.count(),
		.updated_at		= storage.timestamps.updated_at.count(),
		.unknown_fields = glaze_unknown_fields(envelope.unknown_fields,
											   storage_known_fields)};
}

[[nodiscard]] PhotoDto photo_dto(const PhotoEnvelope& envelope) {
	const auto& photo = envelope.record;
	return PhotoDto{
		.id				  = photo.id.value(),
		.schema_version	  = photo.schema_version,
		.owner_type		  = std::string{domain::to_string(photo.owner_type)},
		.owner_id		  = photo.owner_id.value(),
		.media_format	  = std::string{domain::to_string(photo.media_format)},
		.sort_order		  = photo.sort_order,
		.is_main		  = photo.is_main ? std::optional{true} : std::nullopt,
		.width			  = photo.width,
		.height			  = photo.height,
		.encoded_bytes	  = photo.encoded_bytes,
		.source_mime_type = optional_non_empty(photo.source_mime_type),
		.created_at		  = photo.timestamps.created_at.count(),
		.updated_at		  = photo.timestamps.updated_at.count(),
		.unknown_fields =
			glaze_unknown_fields(envelope.unknown_fields, photo_known_fields)};
}

[[nodiscard]] std::optional<std::int64_t> nullable_epoch_milliseconds(
	const glz::raw_json& raw, std::string_view field,
	SchemaDiagnostic& diagnostic) {
	if (raw.str.empty())
		return std::nullopt;

	if (raw.str == "null")
		return std::nullopt;

	auto value		 = std::int64_t{};
	const auto error = glz::read_json(value, raw.str);
	if (error) {
		diagnostic = make_diagnostic(
			SchemaIssue::MalformedKnownField, std::string{field},
			"Nullable epoch millisecond field is malformed.",
			glz::format_error(error, raw.str));
		return std::nullopt;
	}

	return value;
}

[[nodiscard]] std::optional<std::string> required_exact_path(
	const std::optional<std::string>& value, std::string_view field,
	std::string_view expected, SchemaDiagnostic& diagnostic) {
	if (!value) {
		diagnostic = make_diagnostic(SchemaIssue::MissingRequiredField,
									 std::string{field},
									 "Required layout path is missing.");
		return std::nullopt;
	}

	if (*value != expected) {
		diagnostic =
			make_diagnostic(SchemaIssue::InvalidKnownValue, std::string{field},
							"Catalog layout path differs from the "
							"first-version supported layout.",
							*value);
		return std::nullopt;
	}

	return *value;
}

[[nodiscard]] std::optional<CatalogDataFiles> catalog_data_files(
	const std::optional<DataFilesDto>& value, SchemaDiagnostic& diagnostic) {
	if (!value) {
		diagnostic =
			make_diagnostic(SchemaIssue::MissingRequiredField, "dataFiles",
							"Manifest data file layout is required.");
		return std::nullopt;
	}

	auto files = CatalogDataFiles{};
	auto items = required_exact_path(value->items, "dataFiles.items",
									 items_data_file_path, diagnostic);
	if (!items)
		return std::nullopt;
	files.items = *items;

	auto storages = required_exact_path(value->storages, "dataFiles.storages",
										storages_data_file_path, diagnostic);
	if (!storages)
		return std::nullopt;
	files.storages = *storages;

	auto photos = required_exact_path(value->photos, "dataFiles.photos",
									  photos_data_file_path, diagnostic);
	if (!photos)
		return std::nullopt;
	files.photos = *photos;

	return files;
}

[[nodiscard]] std::optional<CatalogMediaLayout> catalog_media(
	const std::optional<MediaDto>& value, SchemaDiagnostic& diagnostic) {
	if (!value) {
		diagnostic = make_diagnostic(SchemaIssue::MissingRequiredField, "media",
									 "Manifest media layout is required.");
		return std::nullopt;
	}

	auto media = CatalogMediaLayout{};
	auto directory =
		required_exact_path(value->photo_directory, "media.photoDirectory",
							photo_media_directory_path, diagnostic);
	if (!directory)
		return std::nullopt;
	media.photo_directory = *directory;

	auto format = required_exact_path(value->photo_format, "media.photoFormat",
									  photo_media_format_name, diagnostic);
	if (!format)
		return std::nullopt;
	media.photo_format = *format;

	auto extension =
		required_exact_path(value->photo_extension, "media.photoExtension",
							photo_media_extension, diagnostic);
	if (!extension)
		return std::nullopt;
	media.photo_extension = *extension;

	return media;
}

[[nodiscard]] std::optional<CatalogFeatures> catalog_features(
	const std::optional<FeaturesDto>& value, SchemaDiagnostic& diagnostic) {
	auto features = CatalogFeatures{};
	if (!value)
		return features;

	features.photo_owner_records = value->photo_owner_records.value_or(true);
	features.unknown_entity_field_preservation =
		value->unknown_entity_field_preservation.value_or(true);
	features.entity_jsonl_current_state_tables =
		value->entity_jsonl_current_state_tables.value_or(true);

	if (!features.photo_owner_records
		|| !features.unknown_entity_field_preservation
		|| !features.entity_jsonl_current_state_tables) {
		diagnostic = make_diagnostic(
			SchemaIssue::InvalidKnownValue, "features",
			"Manifest disables a first-version required feature.");
		return std::nullopt;
	}

	return features;
}

[[nodiscard]] ManifestDto manifest_dto(const ManifestRecord& manifest) {
	auto last_migration_at = glz::raw_json{};
	last_migration_at.str =
		manifest.last_migration_at
			? std::to_string(manifest.last_migration_at->count())
			: std::string{"null"};

	return ManifestDto{
		.schema_version	   = manifest.schema_version,
		.catalog_id		   = manifest.catalog_id.value(),
		.created_at		   = manifest.created_at.count(),
		.last_migration_at = std::move(last_migration_at),
		.data_files = DataFilesDto{.items	 = manifest.data_files.items,
								   .storages = manifest.data_files.storages,
								   .photos	 = manifest.data_files.photos},
		.media	  = MediaDto{.photo_directory = manifest.media.photo_directory,
							 .photo_format	  = manifest.media.photo_format,
							 .photo_extension = manifest.media.photo_extension},
		.features = FeaturesDto{
			.photo_owner_records = manifest.features.photo_owner_records,
			.unknown_entity_field_preservation =
				manifest.features.unknown_entity_field_preservation,
			.entity_jsonl_current_state_tables =
				manifest.features.entity_jsonl_current_state_tables}};
}

[[nodiscard]] SettingsDto settings_dto(const SettingsRecord& settings) {
	return SettingsDto{
		.schema_version	  = settings.schema_version,
		.default_currency = optional_non_empty(settings.default_currency),
		.jpeg_export = JpegExportDto{.quality = settings.jpeg_export.quality}};
}
}	 // namespace

std::string_view to_string(SchemaIssue issue) noexcept {
	switch (issue) {
		case SchemaIssue::None:
			return "none";
		case SchemaIssue::JsonParseError:
			return "JSON parse error";
		case SchemaIssue::UnsupportedSchemaVersion:
			return "unsupported schema version";
		case SchemaIssue::InvalidIdentifier:
			return "invalid identifier";
		case SchemaIssue::MissingRequiredField:
			return "missing required field";
		case SchemaIssue::MalformedKnownField:
			return "malformed known field";
		case SchemaIssue::InvalidEnumValue:
			return "invalid enum value";
		case SchemaIssue::InvalidKnownValue:
			return "invalid known value";
		case SchemaIssue::SerializationFailure:
			return "serialization failure";
	}

	return "unknown schema issue";
}

EmptyCatalogFixture make_empty_catalog_fixture(
	core::StableIdentifier catalog_id, core::EpochMilliseconds created_at) {
	return {.manifest	 = ManifestRecord{.catalog_id = std::move(catalog_id),
										  .created_at = created_at},
			.settings	 = SettingsRecord{},
			.items_jsonl = {},
			.storages_jsonl = {},
			.photos_jsonl	= {}};
}

SchemaReadResult<ManifestRecord> parse_manifest_json(std::string_view json) {
	auto dto = ManifestDto{};
	if (auto diagnostic = read_json_dto(dto, json, "manifest"))
		return read_failure<ManifestRecord>(std::move(*diagnostic));

	auto diagnostic		= SchemaDiagnostic{};
	auto schema_version = required_schema_version_or_diagnostic(
		dto.schema_version, "schemaVersion", diagnostic);
	if (!schema_version)
		return read_failure<ManifestRecord>(std::move(diagnostic));

	auto catalog_id =
		required_identifier(dto.catalog_id, "catalogId", diagnostic);
	if (!catalog_id)
		return read_failure<ManifestRecord>(std::move(diagnostic));

	if (!dto.created_at) {
		return read_failure<ManifestRecord>(
			make_diagnostic(SchemaIssue::MissingRequiredField, "createdAt",
							"Manifest created timestamp is required."));
	}

	if (dto.last_migration_at.str.empty()) {
		return read_failure<ManifestRecord>(make_diagnostic(
			SchemaIssue::MissingRequiredField, "lastMigrationAt",
			"Manifest last migration timestamp marker is required."));
	}

	auto last_migration_at = nullable_epoch_milliseconds(
		dto.last_migration_at, "lastMigrationAt", diagnostic);
	if (diagnostic.issue != SchemaIssue::None)
		return read_failure<ManifestRecord>(std::move(diagnostic));

	auto data_files = catalog_data_files(dto.data_files, diagnostic);
	if (!data_files)
		return read_failure<ManifestRecord>(std::move(diagnostic));

	auto media = catalog_media(dto.media, diagnostic);
	if (!media)
		return read_failure<ManifestRecord>(std::move(diagnostic));

	auto features = catalog_features(dto.features, diagnostic);
	if (!features)
		return read_failure<ManifestRecord>(std::move(diagnostic));

	return read_success(ManifestRecord{
		.schema_version = *schema_version,
		.catalog_id		= std::move(*catalog_id),
		.created_at		= core::EpochMilliseconds{*dto.created_at},
		.last_migration_at =
			last_migration_at
				? std::optional{core::EpochMilliseconds{*last_migration_at}}
				: std::nullopt,
		.data_files = std::move(*data_files),
		.media		= std::move(*media),
		.features	= std::move(*features)});
}

SchemaWriteResult serialize_manifest_json(const ManifestRecord& manifest) {
	return write_json_dto(manifest_dto(manifest), "manifest");
}

SchemaReadResult<SettingsRecord> parse_settings_json(std::string_view json) {
	auto dto = SettingsDto{};
	if (auto diagnostic = read_json_dto(dto, json, "settings"))
		return read_failure<SettingsRecord>(std::move(*diagnostic));

	auto diagnostic		= SchemaDiagnostic{};
	auto schema_version = schema_version_or_diagnostic(
		dto.schema_version, "schemaVersion", diagnostic);
	if (!schema_version)
		return read_failure<SettingsRecord>(std::move(diagnostic));

	auto settings = SettingsRecord{.schema_version = *schema_version};
	if (dto.default_currency
		&& domain::has_non_whitespace(*dto.default_currency))
		settings.default_currency = *dto.default_currency;

	if (dto.jpeg_export && dto.jpeg_export->quality) {
		if (*dto.jpeg_export->quality < 1 || *dto.jpeg_export->quality > 100) {
			return read_failure<SettingsRecord>(make_diagnostic(
				SchemaIssue::InvalidKnownValue, "jpegExport.quality",
				"JPEG export quality must be between 1 and 100."));
		}

		settings.jpeg_export.quality = *dto.jpeg_export->quality;
	}

	return read_success(std::move(settings));
}

SchemaWriteResult serialize_settings_json(const SettingsRecord& settings) {
	return write_json_dto(settings_dto(settings), "settings");
}

SchemaReadResult<ItemEnvelope> parse_item_record_json(std::string_view json) {
	auto dto = ItemDto{};
	if (auto diagnostic = read_json_dto(dto, json, "item"))
		return read_failure<ItemEnvelope>(std::move(*diagnostic));

	auto diagnostic = SchemaDiagnostic{};
	auto id			= required_identifier(dto.id, "id", diagnostic);
	if (!id)
		return read_failure<ItemEnvelope>(std::move(diagnostic));

	auto schema_version = schema_version_or_diagnostic(
		dto.schema_version, "schemaVersion", diagnostic);
	if (!schema_version)
		return read_failure<ItemEnvelope>(std::move(diagnostic));

	auto display_name =
		required_text(dto.display_name, "displayName", diagnostic);
	if (!display_name)
		return read_failure<ItemEnvelope>(std::move(diagnostic));

	auto category = required_text(dto.category, "category", diagnostic);
	if (!category)
		return read_failure<ItemEnvelope>(std::move(diagnostic));

	auto storage_id =
		optional_identifier(dto.storage_id, "storageId", diagnostic);
	if (diagnostic.issue != SchemaIssue::None)
		return read_failure<ItemEnvelope>(std::move(diagnostic));

	auto tags = tag_rows(dto.tags, diagnostic);
	if (!tags)
		return read_failure<ItemEnvelope>(std::move(diagnostic));

	if (!dto.status) {
		return read_failure<ItemEnvelope>(
			make_diagnostic(SchemaIssue::MissingRequiredField, "status",
							"Item status is required."));
	}

	auto status = domain::parse_item_status(*dto.status);
	if (!status) {
		return read_failure<ItemEnvelope>(make_diagnostic(
			SchemaIssue::InvalidEnumValue, "status",
			"Item status is outside the first-version status set.",
			*dto.status));
	}

	auto listing = listing_data(dto.listing, diagnostic);
	if (!listing)
		return read_failure<ItemEnvelope>(std::move(diagnostic));

	auto acquisition = acquisition_data(dto.acquisition, diagnostic);
	if (!acquisition)
		return read_failure<ItemEnvelope>(std::move(diagnostic));

	auto finance = finance_data(dto.finance, diagnostic);
	if (!finance)
		return read_failure<ItemEnvelope>(std::move(diagnostic));

	auto timestamps =
		required_timestamps(dto.created_at, dto.updated_at, diagnostic);
	if (!timestamps)
		return read_failure<ItemEnvelope>(std::move(diagnostic));

	return read_success(ItemEnvelope{
		.record = domain::ItemRecord{.id			 = std::move(*id),
									 .schema_version = *schema_version,
									 .display_name	 = std::move(*display_name),
									 .category		 = std::move(*category),
									 .storage_id	 = std::move(storage_id),
									 .tags			 = std::move(*tags),
									 .notes = dto.notes.value_or(std::string{}),
									 .status	  = *status,
									 .listing	  = std::move(*listing),
									 .acquisition = std::move(*acquisition),
									 .finance	  = std::move(*finance),
									 .timestamps  = *timestamps},
		.unknown_fields = public_unknown_fields(dto.unknown_fields)});
}

SchemaWriteResult serialize_item_record_json(const ItemEnvelope& envelope) {
	if (auto diagnostic =
			validate_unknown_fields(envelope.unknown_fields, item_known_fields))
		return write_failure(std::move(*diagnostic));

	return write_json_dto(item_dto(envelope), "item");
}

SchemaReadResult<StorageEnvelope> parse_storage_record_json(
	std::string_view json) {
	auto dto = StorageDto{};
	if (auto diagnostic = read_json_dto(dto, json, "storage"))
		return read_failure<StorageEnvelope>(std::move(*diagnostic));

	auto diagnostic = SchemaDiagnostic{};
	auto id			= required_identifier(dto.id, "id", diagnostic);
	if (!id)
		return read_failure<StorageEnvelope>(std::move(diagnostic));

	auto schema_version = schema_version_or_diagnostic(
		dto.schema_version, "schemaVersion", diagnostic);
	if (!schema_version)
		return read_failure<StorageEnvelope>(std::move(diagnostic));

	auto display_name =
		required_text(dto.display_name, "displayName", diagnostic);
	if (!display_name)
		return read_failure<StorageEnvelope>(std::move(diagnostic));

	auto storage_type =
		required_text(dto.storage_type, "storageType", diagnostic);
	if (!storage_type)
		return read_failure<StorageEnvelope>(std::move(diagnostic));

	auto parent_storage_id = optional_identifier(dto.parent_storage_id,
												 "parentStorageId", diagnostic);
	if (diagnostic.issue != SchemaIssue::None)
		return read_failure<StorageEnvelope>(std::move(diagnostic));

	auto tags = tag_rows(dto.tags, diagnostic);
	if (!tags)
		return read_failure<StorageEnvelope>(std::move(diagnostic));

	if (!dto.lifecycle_status) {
		return read_failure<StorageEnvelope>(make_diagnostic(
			SchemaIssue::MissingRequiredField, "lifecycleStatus",
			"Storage lifecycle status is required."));
	}

	auto lifecycle_status =
		domain::parse_storage_lifecycle_status(*dto.lifecycle_status);
	if (!lifecycle_status) {
		return read_failure<StorageEnvelope>(make_diagnostic(
			SchemaIssue::InvalidEnumValue, "lifecycleStatus",
			"Storage lifecycle status is outside the first-version status set.",
			*dto.lifecycle_status));
	}

	auto timestamps =
		required_timestamps(dto.created_at, dto.updated_at, diagnostic);
	if (!timestamps)
		return read_failure<StorageEnvelope>(std::move(diagnostic));

	return read_success(StorageEnvelope{
		.record =
			domain::StorageRecord{
				.id				   = std::move(*id),
				.schema_version	   = *schema_version,
				.display_name	   = std::move(*display_name),
				.storage_type	   = std::move(*storage_type),
				.parent_storage_id = std::move(parent_storage_id),
				.location		   = dto.location.value_or(std::string{}),
				.tags			   = std::move(*tags),
				.notes			   = dto.notes.value_or(std::string{}),
				.lifecycle_status  = *lifecycle_status,
				.timestamps		   = *timestamps},
		.unknown_fields = public_unknown_fields(dto.unknown_fields)});
}

SchemaWriteResult serialize_storage_record_json(
	const StorageEnvelope& envelope) {
	if (auto diagnostic = validate_unknown_fields(envelope.unknown_fields,
												  storage_known_fields))
		return write_failure(std::move(*diagnostic));

	return write_json_dto(storage_dto(envelope), "storage");
}

SchemaReadResult<PhotoEnvelope> parse_photo_record_json(std::string_view json) {
	auto dto = PhotoDto{};
	if (auto diagnostic = read_json_dto(dto, json, "photo"))
		return read_failure<PhotoEnvelope>(std::move(*diagnostic));

	auto diagnostic = SchemaDiagnostic{};
	auto id			= required_identifier(dto.id, "id", diagnostic);
	if (!id)
		return read_failure<PhotoEnvelope>(std::move(diagnostic));

	auto schema_version = schema_version_or_diagnostic(
		dto.schema_version, "schemaVersion", diagnostic);
	if (!schema_version)
		return read_failure<PhotoEnvelope>(std::move(diagnostic));

	if (!dto.owner_type) {
		return read_failure<PhotoEnvelope>(
			make_diagnostic(SchemaIssue::MissingRequiredField, "ownerType",
							"Photo owner type is required."));
	}

	auto owner_type = domain::parse_photo_owner_type(*dto.owner_type);
	if (!owner_type) {
		return read_failure<PhotoEnvelope>(make_diagnostic(
			SchemaIssue::InvalidEnumValue, "ownerType",
			"Photo owner type is outside the first-version owner set.",
			*dto.owner_type));
	}

	auto owner_id = required_identifier(dto.owner_id, "ownerId", diagnostic);
	if (!owner_id)
		return read_failure<PhotoEnvelope>(std::move(diagnostic));

	if (!dto.media_format) {
		return read_failure<PhotoEnvelope>(
			make_diagnostic(SchemaIssue::MissingRequiredField, "mediaFormat",
							"Photo media format is required."));
	}

	auto media_format = domain::parse_photo_media_format(*dto.media_format);
	if (!media_format) {
		return read_failure<PhotoEnvelope>(make_diagnostic(
			SchemaIssue::InvalidEnumValue, "mediaFormat",
			"Photo media format is outside the first-version media set.",
			*dto.media_format));
	}

	if (!dto.sort_order) {
		return read_failure<PhotoEnvelope>(
			make_diagnostic(SchemaIssue::MissingRequiredField, "sortOrder",
							"Photo sort order is required."));
	}

	auto timestamps =
		required_timestamps(dto.created_at, dto.updated_at, diagnostic);
	if (!timestamps)
		return read_failure<PhotoEnvelope>(std::move(diagnostic));

	return read_success(PhotoEnvelope{
		.record = domain::PhotoRecord{.id			  = std::move(*id),
									  .schema_version = *schema_version,
									  .owner_type	  = *owner_type,
									  .owner_id		  = std::move(*owner_id),
									  .media_format	  = *media_format,
									  .sort_order	  = *dto.sort_order,
									  .is_main = dto.is_main.value_or(false),
									  .width   = dto.width,
									  .height  = dto.height,
									  .encoded_bytes = dto.encoded_bytes,
									  .source_mime_type =
										  dto.source_mime_type.value_or(
											  std::string{}),
									  .timestamps = *timestamps},
		.unknown_fields = public_unknown_fields(dto.unknown_fields)});
}

SchemaWriteResult serialize_photo_record_json(const PhotoEnvelope& envelope) {
	if (auto diagnostic = validate_unknown_fields(envelope.unknown_fields,
												  photo_known_fields))
		return write_failure(std::move(*diagnostic));

	return write_json_dto(photo_dto(envelope), "photo");
}
}	 // namespace shuba::persistence
