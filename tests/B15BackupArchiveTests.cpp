#include "Catalog/BackupArchive.hpp"
#include "Platform/JuceZipArchive.hpp"
#include "Platform/LinuxFakes.hpp"

#include <juce_core/juce_core.h>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {
class TemporaryDirectory final {
public:
	explicit TemporaryDirectory(std::string leaf_prefix)
		: path_value(std::filesystem::temp_directory_path()
					 / (std::move(leaf_prefix) + "-"
						+ std::to_string(std::chrono::steady_clock::now()
											 .time_since_epoch()
											 .count()))) {
		std::error_code ignored;
		std::filesystem::remove_all(path_value, ignored);
		std::filesystem::create_directories(path_value);
	}

	TemporaryDirectory(const TemporaryDirectory&)				 = delete;
	TemporaryDirectory& operator=(const TemporaryDirectory&)	 = delete;
	TemporaryDirectory(TemporaryDirectory&&) noexcept			 = delete;
	TemporaryDirectory& operator=(TemporaryDirectory&&) noexcept = delete;

	~TemporaryDirectory() {
		std::error_code ignored;
		std::filesystem::remove_all(path_value, ignored);
	}

	[[nodiscard]] const std::filesystem::path& path() const noexcept {
		return path_value;
	}

private:
	std::filesystem::path path_value;
};

[[nodiscard]] shuba::core::StableIdentifier make_id(std::string text) {
	std::optional<shuba::core::StableIdentifier> identifier =
		shuba::core::StableIdentifier::try_create_file_safe(std::move(text));
	REQUIRE(identifier.has_value());
	return *identifier;
}

[[nodiscard]] shuba::core::OperationIdentifier make_operation_id(
	std::string text) {
	std::optional<shuba::core::OperationIdentifier> identifier =
		shuba::core::OperationIdentifier::try_create_file_safe(std::move(text));
	REQUIRE(identifier.has_value());
	return *identifier;
}

[[nodiscard]] shuba::domain::RecordTimestamps make_timestamps(
	std::int64_t created_at, std::int64_t updated_at) {
	return shuba::domain::RecordTimestamps{
		.created_at = shuba::core::EpochMilliseconds{created_at},
		.updated_at = shuba::core::EpochMilliseconds{updated_at}};
}

[[nodiscard]] shuba::persistence::ItemEnvelope make_item(std::string id) {
	return shuba::persistence::ItemEnvelope{
		.record =
			shuba::domain::ItemRecord{.id			= make_id(std::move(id)),
									  .display_name = "Backup item",
									  .category		= "other",
									  .timestamps	= make_timestamps(1, 2)}};
}

[[nodiscard]] shuba::persistence::PhotoEnvelope make_photo(
	std::string id, const shuba::core::StableIdentifier& owner_id) {
	return shuba::persistence::PhotoEnvelope{
		.record = shuba::domain::PhotoRecord{
			.id				  = make_id(std::move(id)),
			.owner_type		  = shuba::domain::PhotoOwnerType::Item,
			.owner_id		  = owner_id,
			.media_format	  = shuba::domain::PhotoMediaFormat::JpegXl,
			.sort_order		  = 1000,
			.is_main		  = true,
			.width			  = 2,
			.height			  = 2,
			.encoded_bytes	  = std::uint64_t{12},
			.source_mime_type = "image/jpeg",
			.timestamps		  = make_timestamps(10, 11)}};
}

void write_text(const std::filesystem::path& path, std::string_view text) {
	std::filesystem::create_directories(path.parent_path());
	std::ofstream output{path, std::ios::binary | std::ios::trunc};
	REQUIRE(output.good());
	output << text;
}

[[nodiscard]] std::string read_text(const std::filesystem::path& path) {
	std::ifstream input{path, std::ios::binary};
	REQUIRE(input.good());
	return std::string{std::istreambuf_iterator<char>{input},
					   std::istreambuf_iterator<char>{}};
}

[[nodiscard]] bool contains(const std::vector<std::string>& values,
							std::string_view expected) {
	return std::ranges::find(values, std::string{expected}) != values.end();
}

[[nodiscard]] shuba::platform::PlatformOperationContext zip_context(
	std::string operation_id) {
	return shuba::platform::PlatformOperationContext{
		.operation_id	= make_operation_id(std::move(operation_id)),
		.operation_type = shuba::platform::ProgressOperationType::BackupImport};
}

void write_catalog_files(const std::filesystem::path& catalog_root,
						 const shuba::persistence::ItemEnvelope& item,
						 const shuba::persistence::PhotoEnvelope& photo,
						 bool write_photo_media,
						 std::string_view raw_item_suffix = {}) {
	const shuba::persistence::EmptyCatalogFixture fixture =
		shuba::persistence::make_empty_catalog_fixture(
			make_id("catalog-b15"), shuba::core::EpochMilliseconds{1000});
	const shuba::persistence::SchemaWriteResult manifest =
		shuba::persistence::serialize_manifest_json(fixture.manifest);
	const shuba::persistence::SchemaWriteResult settings =
		shuba::persistence::serialize_settings_json(fixture.settings);
	const shuba::persistence::SchemaWriteResult item_json =
		shuba::persistence::serialize_item_record_json(item);
	const shuba::persistence::SchemaWriteResult photo_json =
		shuba::persistence::serialize_photo_record_json(photo);
	REQUIRE(manifest.succeeded());
	REQUIRE(settings.succeeded());
	REQUIRE(item_json.succeeded());
	REQUIRE(photo_json.succeeded());

	write_text(catalog_root / "manifest.json", manifest.json);
	write_text(catalog_root / "settings.json", settings.json);
	write_text(catalog_root / "data/items.jsonl",
			   item_json.json + "\n" + std::string{raw_item_suffix});
	write_text(catalog_root / "data/storages.jsonl", "");
	write_text(catalog_root / "data/photos.jsonl", photo_json.json + "\n");
	if (write_photo_media)
		write_text(catalog_root / "media/photos/photo-001.jxl", "photo-bytes");
}

[[nodiscard]] shuba::catalog::CatalogRepositoryState state_for_backup(
	const shuba::persistence::ItemEnvelope& item,
	const shuba::persistence::PhotoEnvelope& photo,
	std::vector<std::string> readable_media) {
	return shuba::catalog::build_catalog_repository(
		shuba::catalog::CatalogRepositoryInput{
			.items	= {item},
			.photos = {photo},
			.media	= shuba::catalog::CatalogMediaSnapshot{
				.complete_scan_available	= true,
				.readable_photo_media_files = std::move(readable_media)}});
}

struct BackupHarness final {
	TemporaryDirectory temporary{"shuba-b15-backup"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	shuba::platform::AppPrivatePaths paths{
		*path_provider.resolve_app_private_paths().value};
	shuba::platform::JuceZipArchiveService zip;
	shuba::platform::LinuxFakeDocumentExportService document_export;
	shuba::platform::LinuxFakeContentStagingService staging;
	shuba::core::ManualClock clock{shuba::core::EpochMilliseconds{2000}};
	shuba::core::OperationGate gate;
	shuba::platform::ScriptedIdentifierSource identifiers;
	shuba::platform::ProgressCollector progress;
	shuba::platform::ManualCancellationToken cancellation;

	[[nodiscard]] shuba::catalog::BackupArchiveUseCase use_case() {
		return shuba::catalog::BackupArchiveUseCase{
			identifiers, clock, gate, zip, document_export, staging};
	}
};

void write_malicious_zip(const std::filesystem::path& archive_path) {
	std::filesystem::create_directories(archive_path.parent_path());
	juce::ZipFile::Builder builder;
	const std::string payload = "escape";
	builder.addEntry(std::make_unique<juce::MemoryInputStream>(
						 payload.data(), payload.size(), true),
					 0, "../evil.txt", juce::Time{});
	juce::FileOutputStream output{
		juce::File{juce::String{archive_path.string()}}};
	REQUIRE(output.openedOk());
	REQUIRE(builder.writeToStream(output, nullptr));
	output.flush();
}
}	 // namespace

TEST_CASE(
	"B15 normal backup preserves raw degraded metadata and excludes "
	"diagnostics",
	"[b15][backup][normal]") {
	BackupHarness harness;
	harness.identifiers.script_operation_identifier("operation-b15-normal");
	const shuba::persistence::ItemEnvelope item = make_item("item-001");
	const shuba::persistence::PhotoEnvelope photo =
		make_photo("photo-001", item.record.id);
	write_catalog_files(harness.paths.active_catalog_root, item, photo, true,
						"{not-json-but-raw}\n");
	write_text(
		harness.paths.active_catalog_root / "media/photos/orphan-media.jxl",
		"orphan-bytes");
	write_text(
		harness.paths.active_catalog_root / "recovery/last-load-report.json",
		"recovery-report");
	write_text(harness.paths.active_catalog_root
				   / "backup/previous-data-copies/old/items.jsonl",
			   "old-copy");

	const shuba::catalog::CatalogRepositoryState state = state_for_backup(
		item, photo,
		{"media/photos/photo-001.jxl", "media/photos/orphan-media.jxl"});
	const std::filesystem::path destination =
		harness.temporary.path() / "normal-backup.zip";
	shuba::catalog::BackupArchiveUseCase use_case = harness.use_case();
	shuba::catalog::BackupExportResult result = use_case.export_normal_backup(
		shuba::catalog::BackupExportRequest{
			.current_state = state,
			.current_load_status =
				shuba::persistence::CatalogLoadStatus::Degraded,
			.paths = harness.paths,
			.destination =
				shuba::platform::make_local_file_destination(destination),
			.keep_temp_zip = true},
		harness.progress, harness.cancellation);

	REQUIRE(result.succeeded());
	REQUIRE(result.temp_zip_built);
	REQUIRE(result.temp_zip_validated);
	REQUIRE(result.destination_copied);
	REQUIRE(result.degraded_warning_required);
	REQUIRE(result.diagnostic_companion_recommended);
	REQUIRE(result.archive_byte_count > 0U);
	REQUIRE(result.largest_entry_byte_count > 0U);
	REQUIRE_FALSE(result.classic_zip64_risk_observed);
	REQUIRE(contains(result.included_entries, "manifest.json"));
	REQUIRE(contains(result.included_entries, "settings.json"));
	REQUIRE(contains(result.included_entries, "data/items.jsonl"));
	REQUIRE(contains(result.included_entries, "data/storages.jsonl"));
	REQUIRE(contains(result.included_entries, "data/photos.jsonl"));
	REQUIRE(contains(result.included_entries, "media/photos/photo-001.jxl"));
	REQUIRE_FALSE(
		contains(result.included_entries, "media/photos/orphan-media.jxl"));
	REQUIRE_FALSE(
		contains(result.included_entries, "recovery/last-load-report.json"));

	const std::filesystem::path extracted =
		harness.temporary.path() / "extracted";
	shuba::platform::PlatformValueResult<shuba::platform::ZipArchiveInspection>
		extracted_zip = harness.zip.extract_zip_archive(
			shuba::platform::ZipArchiveExtractRequest{
				.archive_path = destination, .target_directory = extracted},
			zip_context("operation-b15-normal-read"), harness.progress,
			harness.cancellation);
	REQUIRE(extracted_zip.succeeded());
	REQUIRE(read_text(extracted / "data/items.jsonl").find("{not-json-but-raw}")
			!= std::string::npos);
	REQUIRE_FALSE(
		std::filesystem::exists(extracted / "media/photos/orphan-media.jxl"));
	REQUIRE_FALSE(
		std::filesystem::exists(extracted / "recovery/last-load-report.json"));
	REQUIRE_FALSE(std::filesystem::exists(
		extracted / "backup/previous-data-copies/old/items.jsonl"));
}

TEST_CASE(
	"B15 diagnostic archive includes orphan media recovery files and report",
	"[b15][backup][diagnostic]") {
	BackupHarness harness;
	harness.identifiers.script_operation_identifier("operation-b15-diagnostic");
	const shuba::persistence::ItemEnvelope item = make_item("item-001");
	const shuba::persistence::PhotoEnvelope photo =
		make_photo("photo-001", item.record.id);
	write_catalog_files(harness.paths.active_catalog_root, item, photo, true);
	write_text(
		harness.paths.active_catalog_root / "media/photos/orphan-media.jxl",
		"orphan-bytes");
	write_text(
		harness.paths.active_catalog_root / "recovery/last-load-report.json",
		"recovery-report");
	write_text(harness.paths.active_catalog_root
				   / "recovery/quarantine/items.invalid.jsonl",
			   "bad-row");
	write_text(harness.paths.active_catalog_root
				   / "backup/previous-data-copies/old/items.jsonl",
			   "old-copy");

	const shuba::catalog::CatalogRepositoryState state = state_for_backup(
		item, photo,
		{"media/photos/photo-001.jxl", "media/photos/orphan-media.jxl"});
	const std::filesystem::path destination =
		harness.temporary.path() / "diagnostic.zip";
	shuba::catalog::BackupArchiveUseCase use_case = harness.use_case();
	shuba::catalog::BackupExportResult result =
		use_case.export_diagnostic_archive(
			shuba::catalog::BackupExportRequest{
				.current_state = state,
				.current_load_status =
					shuba::persistence::CatalogLoadStatus::Fatal,
				.paths = harness.paths,
				.destination =
					shuba::platform::make_local_file_destination(destination),
				.keep_temp_zip = true},
			harness.progress, harness.cancellation);

	REQUIRE(result.succeeded());
	REQUIRE(contains(result.included_entries, "media/photos/photo-001.jxl"));
	REQUIRE(contains(result.included_entries, "media/photos/orphan-media.jxl"));
	REQUIRE(
		contains(result.included_entries, "recovery/last-load-report.json"));
	REQUIRE(contains(result.included_entries,
					 "recovery/quarantine/items.invalid.jsonl"));
	REQUIRE(
		contains(result.included_entries, "diagnostic/archive-report.json"));
	REQUIRE_FALSE(contains(result.included_entries,
						   "backup/previous-data-copies/old/items.jsonl"));

	const std::filesystem::path extracted =
		harness.temporary.path() / "diagnostic-extracted";
	shuba::platform::PlatformValueResult<shuba::platform::ZipArchiveInspection>
		extracted_zip = harness.zip.extract_zip_archive(
			shuba::platform::ZipArchiveExtractRequest{
				.archive_path = destination, .target_directory = extracted},
			zip_context("operation-b15-diagnostic-read"), harness.progress,
			harness.cancellation);
	REQUIRE(extracted_zip.succeeded());
	REQUIRE(read_text(extracted / "media/photos/orphan-media.jxl")
			== "orphan-bytes");
	const std::string report =
		read_text(extracted / "diagnostic/archive-report.json");
	REQUIRE(report.find("diagnostic") != std::string::npos);
	REQUIRE(report.find("orphan-media.jxl") != std::string::npos);
}

TEST_CASE("B15 import staging rejects unsafe ZIP paths before extraction",
		  "[b15][backup][unsafe]") {
	BackupHarness harness;
	harness.identifiers.script_operation_identifier("operation-b15-unsafe");
	const std::filesystem::path malicious_zip =
		harness.temporary.path() / "malicious.zip";
	write_malicious_zip(malicious_zip);

	shuba::catalog::BackupArchiveUseCase use_case = harness.use_case();
	shuba::catalog::BackupImportStagingResult result =
		use_case.stage_and_validate_import(
			shuba::catalog::BackupImportStagingRequest{
				.source =
					shuba::platform::make_local_file_source(malicious_zip),
				.paths					= harness.paths,
				.keep_extracted_catalog = true},
			harness.progress, harness.cancellation);

	REQUIRE(result.failed());
	REQUIRE(result.category
			== shuba::core::OperationResultCategory::ValidationFailure);
	REQUIRE(result.staged_zip_copied);
	REQUIRE_FALSE(result.zip_extracted);
	REQUIRE_FALSE(std::filesystem::exists(harness.paths.operation_tmp_root
										  / "import-staging" / "evil.txt"));
	REQUIRE_FALSE(
		std::filesystem::exists(harness.paths.operation_tmp_root / "evil.txt"));
}

TEST_CASE(
	"B15 staged catalog validation classifies normal degraded and fatal "
	"imports",
	"[b15][backup][validation]") {
	TemporaryDirectory temporary{"shuba-b15-validation"};
	const shuba::persistence::ItemEnvelope item = make_item("item-001");
	const shuba::persistence::PhotoEnvelope photo =
		make_photo("photo-001", item.record.id);

	const std::filesystem::path normal_root = temporary.path() / "normal";
	write_catalog_files(normal_root, item, photo, true);
	shuba::catalog::StagedCatalogValidationResult normal =
		shuba::catalog::validate_staged_catalog(
			normal_root, shuba::core::EpochMilliseconds{3000});
	REQUIRE(normal.load_status
			== shuba::persistence::CatalogLoadStatus::Normal);
	REQUIRE(normal.import_allowed());
	REQUIRE_FALSE(normal.explicit_warning_required());
	REQUIRE(normal.items_accepted == 1U);
	REQUIRE(normal.photos_accepted == 1U);

	const std::filesystem::path degraded_root = temporary.path() / "degraded";
	write_catalog_files(degraded_root, item, photo, false);
	shuba::catalog::StagedCatalogValidationResult degraded =
		shuba::catalog::validate_staged_catalog(
			degraded_root, shuba::core::EpochMilliseconds{3000});
	REQUIRE(degraded.load_status
			== shuba::persistence::CatalogLoadStatus::Degraded);
	REQUIRE(degraded.import_allowed());
	REQUIRE(degraded.explicit_warning_required());
	REQUIRE(degraded.photos_accepted == 1U);

	const std::filesystem::path fatal_root = temporary.path() / "fatal";
	write_catalog_files(fatal_root, item, photo, true);
	std::string manifest = read_text(fatal_root / "manifest.json");
	const std::size_t version_position = manifest.find(R"("schemaVersion":1)");
	REQUIRE(version_position != std::string::npos);
	manifest.replace(version_position,
					 std::string_view{R"("schemaVersion":1)"}.size(),
					 R"("schemaVersion":99)");
	write_text(fatal_root / "manifest.json", manifest);
	shuba::catalog::StagedCatalogValidationResult fatal =
		shuba::catalog::validate_staged_catalog(
			fatal_root, shuba::core::EpochMilliseconds{3000});
	REQUIRE(fatal.load_status == shuba::persistence::CatalogLoadStatus::Fatal);
	REQUIRE_FALSE(fatal.import_allowed());
}

TEST_CASE(
	"B15 staged catalog validation degrades missing settings and entity files",
	"[b15][backup][validation]") {
	TemporaryDirectory temporary{"shuba-b15-validation-missing-files"};
	const shuba::persistence::ItemEnvelope item = make_item("item-001");
	const shuba::persistence::PhotoEnvelope photo =
		make_photo("photo-001", item.record.id);
	const std::filesystem::path staged_root =
		temporary.path() / "missing-files";
	write_catalog_files(staged_root, item, photo, true);

	std::filesystem::remove(staged_root / "settings.json");
	std::filesystem::remove(staged_root / "data/storages.jsonl");
	std::filesystem::remove(staged_root / "data/photos.jsonl");

	shuba::catalog::StagedCatalogValidationResult validation =
		shuba::catalog::validate_staged_catalog(
			staged_root, shuba::core::EpochMilliseconds{3000});

	REQUIRE(validation.load_status
			== shuba::persistence::CatalogLoadStatus::Degraded);
	REQUIRE(validation.import_allowed());
	REQUIRE(validation.explicit_warning_required());
	REQUIRE(validation.items_accepted == 1U);
	REQUIRE(validation.storages_accepted == 0U);
	REQUIRE(validation.photos_accepted == 0U);
	REQUIRE(validation.derived_recovery_summary.orphan_media_count == 1U);
}
