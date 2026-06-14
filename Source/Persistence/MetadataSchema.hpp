#pragma once

#include "Core/Clock.hpp"
#include "Core/Identifier.hpp"
#include "Domain/Domain.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace shuba::persistence {
inline constexpr auto first_catalog_schema_version = 1;

inline constexpr auto active_catalog_directory_name =
	std::string_view{"active-catalog"};
inline constexpr auto manifest_file_path = std::string_view{"manifest.json"};
inline constexpr auto settings_file_path = std::string_view{"settings.json"};
inline constexpr auto items_data_file_path =
	std::string_view{"data/items.jsonl"};
inline constexpr auto storages_data_file_path =
	std::string_view{"data/storages.jsonl"};
inline constexpr auto photos_data_file_path =
	std::string_view{"data/photos.jsonl"};
inline constexpr auto photo_media_directory_path =
	std::string_view{"media/photos"};
inline constexpr auto photo_media_format_name = std::string_view{"jxl"};
inline constexpr auto photo_media_extension	  = std::string_view{".jxl"};

enum class SchemaIssue : std::uint8_t {
	None,
	JsonParseError,
	UnsupportedSchemaVersion,
	InvalidIdentifier,
	MissingRequiredField,
	MalformedKnownField,
	InvalidEnumValue,
	InvalidKnownValue,
	SerializationFailure,
};

[[nodiscard]] std::string_view to_string(SchemaIssue issue) noexcept;

struct SchemaDiagnostic final {
	SchemaIssue issue{SchemaIssue::None};
	std::string field;
	std::string message;
	std::string technical_details;

	friend bool operator==(const SchemaDiagnostic&,
						   const SchemaDiagnostic&) = default;
};

template<class Value>
struct SchemaReadResult final {
	std::optional<Value> value;
	std::optional<SchemaDiagnostic> diagnostic;

	[[nodiscard]] bool succeeded() const noexcept {
		return value.has_value() && !diagnostic.has_value();
	}

	[[nodiscard]] explicit operator bool() const noexcept {
		return succeeded();
	}
};

struct SchemaWriteResult final {
	std::string json;
	std::optional<SchemaDiagnostic> diagnostic;

	[[nodiscard]] bool succeeded() const noexcept {
		return !json.empty() && !diagnostic.has_value();
	}

	[[nodiscard]] explicit operator bool() const noexcept {
		return succeeded();
	}
};

using UnknownFields = std::map<std::string, std::string>;

template<class Record>
struct EntityEnvelope final {
	Record record;
	UnknownFields unknown_fields;

	friend bool operator==(const EntityEnvelope&,
						   const EntityEnvelope&) = default;
};

using ItemEnvelope	  = EntityEnvelope<domain::ItemRecord>;
using StorageEnvelope = EntityEnvelope<domain::StorageRecord>;
using PhotoEnvelope	  = EntityEnvelope<domain::PhotoRecord>;

struct CatalogDataFiles final {
	std::string items{std::string{items_data_file_path}};
	std::string storages{std::string{storages_data_file_path}};
	std::string photos{std::string{photos_data_file_path}};

	friend bool operator==(const CatalogDataFiles&,
						   const CatalogDataFiles&) = default;
};

struct CatalogMediaLayout final {
	std::string photo_directory{std::string{photo_media_directory_path}};
	std::string photo_format{std::string{photo_media_format_name}};
	std::string photo_extension{std::string{photo_media_extension}};

	friend bool operator==(const CatalogMediaLayout&,
						   const CatalogMediaLayout&) = default;
};

struct CatalogFeatures final {
	bool photo_owner_records{true};
	bool unknown_entity_field_preservation{true};
	bool entity_jsonl_current_state_tables{true};

	friend bool operator==(const CatalogFeatures&,
						   const CatalogFeatures&) = default;
};

struct ManifestRecord final {
	int schema_version{first_catalog_schema_version};
	core::StableIdentifier catalog_id;
	core::EpochMilliseconds created_at{};
	std::optional<core::EpochMilliseconds> last_migration_at;
	CatalogDataFiles data_files;
	CatalogMediaLayout media;
	CatalogFeatures features;

	friend bool operator==(const ManifestRecord&,
						   const ManifestRecord&) = default;
};

struct JpegExportSettings final {
	int quality{90};

	friend bool operator==(const JpegExportSettings&,
						   const JpegExportSettings&) = default;
};

struct SettingsRecord final {
	int schema_version{first_catalog_schema_version};
	std::string default_currency{"BYN"};
	JpegExportSettings jpeg_export;

	friend bool operator==(const SettingsRecord&,
						   const SettingsRecord&) = default;
};

struct EmptyCatalogFixture final {
	ManifestRecord manifest;
	SettingsRecord settings;
	std::string items_jsonl;
	std::string storages_jsonl;
	std::string photos_jsonl;

	friend bool operator==(const EmptyCatalogFixture&,
						   const EmptyCatalogFixture&) = default;
};

[[nodiscard]] EmptyCatalogFixture make_empty_catalog_fixture(
	core::StableIdentifier catalog_id, core::EpochMilliseconds created_at);

[[nodiscard]] SchemaReadResult<ManifestRecord> parse_manifest_json(
	std::string_view json);
[[nodiscard]] SchemaWriteResult serialize_manifest_json(
	const ManifestRecord& manifest);

[[nodiscard]] SchemaReadResult<SettingsRecord> parse_settings_json(
	std::string_view json);
[[nodiscard]] SchemaWriteResult serialize_settings_json(
	const SettingsRecord& settings);

[[nodiscard]] SchemaReadResult<ItemEnvelope> parse_item_record_json(
	std::string_view json);
[[nodiscard]] SchemaWriteResult serialize_item_record_json(
	const ItemEnvelope& envelope);

[[nodiscard]] SchemaReadResult<StorageEnvelope> parse_storage_record_json(
	std::string_view json);
[[nodiscard]] SchemaWriteResult serialize_storage_record_json(
	const StorageEnvelope& envelope);

[[nodiscard]] SchemaReadResult<PhotoEnvelope> parse_photo_record_json(
	std::string_view json);
[[nodiscard]] SchemaWriteResult serialize_photo_record_json(
	const PhotoEnvelope& envelope);
}	 // namespace shuba::persistence
