#include "Persistence/MetadataSchema.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

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

[[nodiscard]] bool contains(std::string_view text, std::string_view fragment) {
	return text.find(fragment) != std::string_view::npos;
}
}	 // namespace

TEST_CASE("B05 item schema preserves top-level unknown fields after input dies",
		  "[b05][schema][unknown-fields]") {
	using namespace shuba::persistence;

	SchemaReadResult<ItemEnvelope> parsed;
	{
		const auto input = std::string{
			R"({"id":"item-unknown","schemaVersion":1,"displayName":"Black leather boots","category":"footwear","storageId":"storage-main","tags":[{"key":"brand","value":"Ecco"},{"key":"season","value":"winter"}],"notes":"small scratch","status":"listed","listing":{"marketplace":"Kufar","listedOn":"2026-05-20","price":{"amount":"120.00","currency":"BYN"}},"acquisition":{"source":"thrift","acquiredOn":"2026-04-10","cost":{"amount":"45.00","currency":"BYN"}},"finance":{"originalPrice":{"amount":"180.00","currency":"BYN"},"realSalePrice":{"amount":"110.00","currency":"BYN"},"expensesTotal":{"amount":"0.00","currency":"BYN"}},"createdAt":1760000000000,"updatedAt":1760000100000,"futureField":{"nested":[1,2,{"k":"v"}]},"futureArray":[true,{"x":"y"}]})"};
		parsed = parse_item_record_json(input);
	}

	REQUIRE(parsed.succeeded());
	REQUIRE(parsed.value.has_value());

	auto envelope = *parsed.value;
	REQUIRE(envelope.record.display_name == "Black leather boots");
	REQUIRE(envelope.record.status == shuba::domain::ItemStatus::Listed);
	REQUIRE(envelope.record.storage_id.has_value());
	REQUIRE(envelope.record.storage_id->value() == "storage-main");
	REQUIRE(envelope.record.tags.size() == 2);
	REQUIRE(envelope.unknown_fields.at("futureField")
			== R"({"nested":[1,2,{"k":"v"}]})");
	REQUIRE(envelope.unknown_fields.at("futureArray") == R"([true,{"x":"y"}])");

	envelope.record.display_name = "Edited boots";
	const auto written			 = serialize_item_record_json(envelope);
	REQUIRE(written.succeeded());
	REQUIRE(
		contains(written.json, R"("futureField":{"nested":[1,2,{"k":"v"}]})"));
	REQUIRE(contains(written.json, R"("futureArray":[true,{"x":"y"}])"));

	const auto reparsed = parse_item_record_json(written.json);
	REQUIRE(reparsed.succeeded());
	REQUIRE(reparsed.value->unknown_fields == envelope.unknown_fields);
	REQUIRE(reparsed.value->record.display_name == "Edited boots");
}

TEST_CASE("B05 storage and photo rows round-trip unknown top-level values",
		  "[b05][schema][storage][photo]") {
	using namespace shuba::persistence;

	const auto storage_input = std::string{
		R"({"id":"storage-unknown","schemaVersion":1,"displayName":"Shoes bag","storageType":"bag","parentStorageId":"storage-root","location":"hall","tags":[{"key":"","value":"legacy blank key"}],"notes":"winter items","lifecycleStatus":"active","createdAt":10,"updatedAt":20,"futureStorage":{"levels":["a","b"]}})"};
	const auto parsed_storage = parse_storage_record_json(storage_input);
	REQUIRE(parsed_storage.succeeded());
	REQUIRE(parsed_storage.value->record.tags.size() == 1);
	REQUIRE(parsed_storage.value->record.tags.front().key.empty());
	REQUIRE(parsed_storage.value->unknown_fields.at("futureStorage")
			== R"({"levels":["a","b"]})");

	const auto written_storage =
		serialize_storage_record_json(*parsed_storage.value);
	REQUIRE(written_storage.succeeded());
	REQUIRE(contains(written_storage.json, R"("key":"")"));
	REQUIRE(contains(written_storage.json,
					 R"("futureStorage":{"levels":["a","b"]})"));

	const auto photo_input = std::string{
		R"({"id":"photo-unknown","schemaVersion":1,"ownerType":"storage","ownerId":"storage-unknown","mediaFormat":"jxl","sortOrder":1000,"isMain":true,"width":4032,"height":3024,"encodedBytes":823451,"sourceMimeType":"image/jpeg","sourceMd5":"be897b804568f7c80a0d999d836657bb","createdAt":30,"updatedAt":40,"futurePhoto":[{"codec":"next"}]})"};
	const auto parsed_photo = parse_photo_record_json(photo_input);
	REQUIRE(parsed_photo.succeeded());
	REQUIRE(parsed_photo.value->record.owner_type
			== shuba::domain::PhotoOwnerType::Storage);
	REQUIRE(parsed_photo.value->record.is_main);
	REQUIRE(parsed_photo.value->record.source_md5
			== "be897b804568f7c80a0d999d836657bb");
	REQUIRE(parsed_photo.value->unknown_fields.at("futurePhoto")
			== R"([{"codec":"next"}])");

	auto photo_envelope			  = *parsed_photo.value;
	photo_envelope.record.is_main = false;
	const auto written_photo	  = serialize_photo_record_json(photo_envelope);
	REQUIRE(written_photo.succeeded());
	REQUIRE_FALSE(contains(written_photo.json, "isMain"));
	REQUIRE(contains(written_photo.json,
					 R"("sourceMd5":"be897b804568f7c80a0d999d836657bb")"));
	REQUIRE(
		contains(written_photo.json, R"("futurePhoto":[{"codec":"next"}])"));
}

TEST_CASE("B05 malformed known optional data rejects the whole entity row",
		  "[b05][schema][malformed-known]") {
	using namespace shuba::persistence;

	const auto bad_money = parse_item_record_json(
		R"({"id":"item-bad-money","schemaVersion":1,"displayName":"Bad money","category":"other","tags":[],"status":"draft","finance":{"originalPrice":{"amount":"12,34","currency":"BYN"}},"createdAt":1,"updatedAt":2})");
	REQUIRE_FALSE(bad_money.succeeded());
	REQUIRE(bad_money.diagnostic.has_value());
	REQUIRE(bad_money.diagnostic->issue == SchemaIssue::MalformedKnownField);
	REQUIRE(bad_money.diagnostic->field == "finance.originalPrice");

	const auto bad_date = parse_item_record_json(
		R"({"id":"item-bad-date","schemaVersion":1,"displayName":"Bad date","category":"other","tags":[],"status":"draft","listing":{"listedOn":"20 May 2026"},"createdAt":1,"updatedAt":2})");
	REQUIRE_FALSE(bad_date.succeeded());
	REQUIRE(bad_date.diagnostic.has_value());
	REQUIRE(bad_date.diagnostic->issue == SchemaIssue::MalformedKnownField);
	REQUIRE(bad_date.diagnostic->field == "listing.listedOn");

	const auto missing_tag_key = parse_storage_record_json(
		R"({"id":"storage-bad-tag","schemaVersion":1,"displayName":"Bad tag","storageType":"box","tags":[{"value":"missing key"}],"lifecycleStatus":"active","createdAt":1,"updatedAt":2})");
	REQUIRE_FALSE(missing_tag_key.succeeded());
	REQUIRE(missing_tag_key.diagnostic.has_value());
	REQUIRE(missing_tag_key.diagnostic->issue
			== SchemaIssue::MalformedKnownField);
	REQUIRE(missing_tag_key.diagnostic->field == "tags[0].key");
}

TEST_CASE("B05 singleton metadata writes first-schema defaults only",
		  "[b05][schema][singletons]") {
	using namespace shuba::persistence;

	const auto fixture = make_empty_catalog_fixture(
		make_id("catalog-001"), shuba::core::EpochMilliseconds{1760000000000});

	REQUIRE(fixture.items_jsonl.empty());
	REQUIRE(fixture.storages_jsonl.empty());
	REQUIRE(fixture.photos_jsonl.empty());
	REQUIRE(active_catalog_directory_name == "active-catalog");

	const auto manifest_json = serialize_manifest_json(fixture.manifest);
	REQUIRE(manifest_json.succeeded());
	REQUIRE(contains(manifest_json.json, R"("dataFiles")"));
	REQUIRE(contains(manifest_json.json, R"("items":"data/items.jsonl")"));
	REQUIRE(contains(manifest_json.json, R"("photoDirectory":"media/photos")"));
	REQUIRE(contains(manifest_json.json,
					 R"("unknownEntityFieldPreservation":true)"));
	REQUIRE(contains(manifest_json.json, R"("lastMigrationAt":null)"));

	const auto parsed_manifest = parse_manifest_json(manifest_json.json);
	REQUIRE(parsed_manifest.succeeded());
	REQUIRE(parsed_manifest.value->catalog_id == fixture.manifest.catalog_id);
	REQUIRE(parsed_manifest.value->created_at == fixture.manifest.created_at);
	REQUIRE_FALSE(parsed_manifest.value->last_migration_at.has_value());

	const auto settings_json = serialize_settings_json(fixture.settings);
	REQUIRE(settings_json.succeeded());
	REQUIRE(contains(settings_json.json, R"("defaultCurrency":"BYN")"));
	REQUIRE(contains(settings_json.json, R"("jpegExport":{"quality":90})"));
	REQUIRE_FALSE(contains(settings_json.json, "includeArchivedByDefault"));
	REQUIRE_FALSE(contains(settings_json.json, "preferredResultLayout"));

	const auto parsed_settings = parse_settings_json(settings_json.json);
	REQUIRE(parsed_settings.succeeded());
	REQUIRE(parsed_settings.value->default_currency == "BYN");
	REQUIRE(parsed_settings.value->jpeg_export.quality == 90);

	const auto missing_manifest_version = parse_manifest_json(
		R"({"catalogId":"catalog-001","createdAt":1760000000000,"lastMigrationAt":null,"dataFiles":{"items":"data/items.jsonl","storages":"data/storages.jsonl","photos":"data/photos.jsonl"},"media":{"photoDirectory":"media/photos","photoFormat":"jxl","photoExtension":".jxl"},"features":{"photoOwnerRecords":true,"unknownEntityFieldPreservation":true,"entityJsonlCurrentStateTables":true}})");
	REQUIRE_FALSE(missing_manifest_version.succeeded());
	REQUIRE(missing_manifest_version.diagnostic.has_value());
	REQUIRE(missing_manifest_version.diagnostic->issue
			== SchemaIssue::MissingRequiredField);
	REQUIRE(missing_manifest_version.diagnostic->field == "schemaVersion");
}

TEST_CASE("B05 representative schema rows reload deterministically",
		  "[b05][schema][deterministic]") {
	using namespace shuba::persistence;
	using namespace shuba::domain;

	const auto sale = parse_money_amount("100.00", "BYN");
	const auto cost = parse_money_amount("60.25", "BYN");
	REQUIRE(sale);
	REQUIRE(cost);

	const auto item = ItemEnvelope{
		.record =
			ItemRecord{.id			 = make_id("item-deterministic"),
					   .display_name = "Deterministic item",
					   .category	 = "other",
					   .tags		= {TagRow{.key = "brand", .value = "Acme"}},
					   .status		= ItemStatus::Sold,
					   .listing		= ListingData{.marketplace = "Kufar",
												  .price	   = sale.value()},
					   .acquisition = AcquisitionData{.source = "donation",
													  .cost	  = cost.value()},
					   .timestamps	= make_timestamps(100, 200)},
		.unknown_fields = {{"zFuture", R"({"stable":[3,2,1]})"}}};

	const auto first_write = serialize_item_record_json(item);
	REQUIRE(first_write.succeeded());
	const auto reload = parse_item_record_json(first_write.json);
	REQUIRE(reload.succeeded());
	const auto second_write = serialize_item_record_json(*reload.value);
	REQUIRE(second_write.succeeded());
	REQUIRE(second_write.json == first_write.json);

	auto conflicting_unknown = item;
	conflicting_unknown.unknown_fields.emplace("displayName", R"("Raw name")");
	const auto conflict_write = serialize_item_record_json(conflicting_unknown);
	REQUIRE_FALSE(conflict_write.succeeded());
	REQUIRE(conflict_write.diagnostic.has_value());
	REQUIRE(conflict_write.diagnostic->issue == SchemaIssue::InvalidKnownValue);
	REQUIRE(conflict_write.diagnostic->field == "displayName");
}

TEST_CASE(
	"B05 entity serializers reject unknown fields colliding with known fields",
	"[b05][schema][unknown-fields]") {
	using namespace shuba::persistence;
	using namespace shuba::domain;

	const StorageEnvelope storage{
		.record			= StorageRecord{.id			  = make_id("storage-conflict"),
										.display_name = "Conflict storage",
										.storage_type = "box",
										.timestamps	  = make_timestamps(1, 2)},
		.unknown_fields = {{"displayName", R"("raw display")"}}};
	const SchemaWriteResult storage_write =
		serialize_storage_record_json(storage);
	REQUIRE_FALSE(storage_write.succeeded());
	REQUIRE(storage_write.diagnostic.has_value());
	REQUIRE(storage_write.diagnostic->issue == SchemaIssue::InvalidKnownValue);
	REQUIRE(storage_write.diagnostic->field == "displayName");

	const PhotoEnvelope photo{
		.record = PhotoRecord{.id		  = make_id("photo-conflict"),
							  .owner_id	  = make_id("item-conflict-owner"),
							  .sort_order = 1000,
							  .timestamps = make_timestamps(3, 4)},
		.unknown_fields = {{"ownerId", R"("raw-owner")"}}};
	const SchemaWriteResult photo_write = serialize_photo_record_json(photo);
	REQUIRE_FALSE(photo_write.succeeded());
	REQUIRE(photo_write.diagnostic.has_value());
	REQUIRE(photo_write.diagnostic->issue == SchemaIssue::InvalidKnownValue);
	REQUIRE(photo_write.diagnostic->field == "ownerId");

	PhotoEnvelope photo_source_md5_conflict = photo;
	photo_source_md5_conflict.unknown_fields.clear();
	photo_source_md5_conflict.unknown_fields.emplace("sourceMd5",
													 R"("raw-md5")");
	const SchemaWriteResult photo_source_md5_write =
		serialize_photo_record_json(photo_source_md5_conflict);
	REQUIRE_FALSE(photo_source_md5_write.succeeded());
	REQUIRE(photo_source_md5_write.diagnostic.has_value());
	REQUIRE(photo_source_md5_write.diagnostic->issue
			== SchemaIssue::InvalidKnownValue);
	REQUIRE(photo_source_md5_write.diagnostic->field == "sourceMd5");
}
