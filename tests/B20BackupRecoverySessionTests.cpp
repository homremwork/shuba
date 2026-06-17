#include "Platform/JuceZipArchive.hpp"
#include "Platform/LinuxFakes.hpp"
#include "UI/CatalogSession.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

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

[[nodiscard]] shuba::ui::CatalogSessionState load_empty_session(
	shuba::platform::LinuxFakePathProvider& path_provider,
	shuba::platform::ScriptedIdentifierSource& identifiers,
	shuba::core::ManualClock& clock) {
	return shuba::ui::load_catalog_session(
		shuba::ui::CatalogSessionLoadRequest{.path_provider = path_provider,
											 .identifiers	= identifiers,
											 .clock			= clock,
											 .debug_demo_seed_enabled = false});
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
	std::ostringstream buffer;
	buffer << input.rdbuf();
	return buffer.str();
}

[[nodiscard]] std::string item_row(std::string_view id,
								   std::string_view display_name) {
	std::string text;
	text += R"({"id":")";
	text += id;
	text += R"(","schemaVersion":1,"displayName":")";
	text += display_name;
	text +=
		R"(","category":"other","tags":[],"status":"draft","createdAt":1,"updatedAt":2})";
	return text;
}

void write_catalog_files(const std::filesystem::path& catalog_root,
						 const shuba::core::StableIdentifier& catalog_id,
						 std::string_view item_id, std::string_view item_name,
						 std::string_view photos_jsonl = {}) {
	const shuba::persistence::EmptyCatalogFixture fixture =
		shuba::persistence::make_empty_catalog_fixture(
			catalog_id, shuba::core::EpochMilliseconds{1000});
	const shuba::persistence::SchemaWriteResult manifest =
		shuba::persistence::serialize_manifest_json(fixture.manifest);
	const shuba::persistence::SchemaWriteResult settings =
		shuba::persistence::serialize_settings_json(fixture.settings);
	REQUIRE(manifest.succeeded());
	REQUIRE(settings.succeeded());

	write_text(catalog_root / "manifest.json", manifest.json);
	write_text(catalog_root / "settings.json", settings.json);
	write_text(catalog_root / "data/items.jsonl",
			   item_row(item_id, item_name) + "\n");
	write_text(catalog_root / "data/storages.jsonl", "");
	write_text(catalog_root / "data/photos.jsonl", photos_jsonl);
	std::filesystem::create_directories(catalog_root / "media/photos");
}

struct B20Harness final {
	TemporaryDirectory temporary{"shuba-b20-recovery"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	shuba::platform::ScriptedIdentifierSource identifiers;
	shuba::core::ManualClock clock{shuba::core::EpochMilliseconds{4000}};
	shuba::core::OperationGate gate;
	shuba::platform::JuceZipArchiveService zip;
	shuba::platform::LinuxFakeDocumentExportService document_export;
	shuba::platform::LinuxFakeContentStagingService staging;
	shuba::platform::ProgressCollector progress;
	shuba::platform::ManualCancellationToken cancellation;

	[[nodiscard]] shuba::ui::CatalogSessionState initialize_empty() {
		identifiers.script_stable_identifier("catalog-b20-active");
		identifiers.script_operation_identifier("operation-b20-init");
		return load_empty_session(path_provider, identifiers, clock);
	}
};
}	 // namespace

TEST_CASE("B20 recovery summary exposes degraded load safe actions") {
	B20Harness harness;
	shuba::ui::CatalogSessionState session = harness.initialize_empty();
	REQUIRE(session.ready_for_browsing());

	write_text(session.paths->active_catalog_root / "data/items.jsonl",
			   item_row("item-valid", "Valid item") + "\n{broken-json\n");
	shuba::platform::ScriptedIdentifierSource reload_identifiers;
	shuba::ui::CatalogSessionState degraded = load_empty_session(
		harness.path_provider, reload_identifiers, harness.clock);
	REQUIRE(degraded.degraded());

	shuba::ui::CatalogRecoveryUiSummary summary =
		shuba::ui::make_recovery_ui_summary(degraded);
	REQUIRE(summary.degraded());
	REQUIRE_FALSE(summary.fatal());
	REQUIRE(summary.accepted_item_count == 1U);
	REQUIRE(summary.skipped_item_count == 1U);
	REQUIRE_FALSE(summary.safe_actions.empty());
	REQUIRE(summary.plain_summary_message.find("Accepted records")
			!= std::string::npos);
	REQUIRE_FALSE(summary.technical_details.empty());
}

TEST_CASE("B20 normal backup session warns for degraded raw backup") {
	B20Harness harness;
	shuba::ui::CatalogSessionState session = harness.initialize_empty();
	write_text(session.paths->active_catalog_root / "data/items.jsonl",
			   item_row("item-valid", "Valid item") + "\n{broken-json\n");
	shuba::platform::ScriptedIdentifierSource reload_identifiers;
	shuba::ui::CatalogSessionState degraded = load_empty_session(
		harness.path_provider, reload_identifiers, harness.clock);
	REQUIRE(degraded.degraded());

	harness.identifiers.script_operation_identifier("operation-b20-backup");
	const std::filesystem::path destination =
		harness.temporary.path() / "degraded-backup.zip";
	shuba::ui::BackupExportSessionResult result =
		shuba::ui::export_backup_from_session(
			shuba::ui::BackupExportSessionRequest{
				.current_session		 = degraded,
				.identifiers			 = harness.identifiers,
				.clock					 = harness.clock,
				.operation_gate			 = harness.gate,
				.zip_archive_service	 = harness.zip,
				.document_export_service = harness.document_export,
				.content_staging_service = harness.staging,
				.destination =
					shuba::platform::make_local_file_destination(destination),
				.keep_temp_zip = true},
			harness.progress, harness.cancellation);

	REQUIRE(result.succeeded());
	REQUIRE(result.unencrypted_zip_warning_required);
	REQUIRE(result.degraded_backup_warning_required);
	REQUIRE(result.diagnostic_companion_recommended);
	REQUIRE(std::filesystem::exists(destination));
	REQUIRE(read_text(degraded.paths->active_catalog_root / "data/items.jsonl")
				.find("{broken-json")
			!= std::string::npos);
}

TEST_CASE("B20 import staging requires validation before replacement") {
	B20Harness harness;
	shuba::ui::CatalogSessionState session = harness.initialize_empty();
	REQUIRE(session.repository.items.empty());

	const std::filesystem::path imported_root =
		harness.temporary.path() / "imported-catalog";
	write_catalog_files(imported_root, make_id("catalog-b20-imported"),
						"item-imported", "Imported item");
	const std::filesystem::path imported_zip =
		harness.temporary.path() / "imported.zip";
	shuba::platform::PlatformOperationContext zip_context{
		.operation_id = *shuba::core::OperationIdentifier::try_create_file_safe(
			"operation-b20-build-zip"),
		.operation_type = shuba::platform::ProgressOperationType::BackupImport};
	shuba::platform::PlatformValueResult<shuba::platform::ZipArchiveInspection>
		built = harness.zip.build_zip_archive(
			shuba::platform::ZipArchiveBuildRequest{
				.output_path = imported_zip,
				.entries =
					{shuba::platform::ZipArchiveEntrySource{
						 .source_path  = imported_root / "manifest.json",
						 .archive_path = "manifest.json"},
					 shuba::platform::ZipArchiveEntrySource{
						 .source_path  = imported_root / "settings.json",
						 .archive_path = "settings.json"},
					 shuba::platform::ZipArchiveEntrySource{
						 .source_path  = imported_root / "data/items.jsonl",
						 .archive_path = "data/items.jsonl"},
					 shuba::platform::ZipArchiveEntrySource{
						 .source_path  = imported_root / "data/storages.jsonl",
						 .archive_path = "data/storages.jsonl"},
					 shuba::platform::ZipArchiveEntrySource{
						 .source_path  = imported_root / "data/photos.jsonl",
						 .archive_path = "data/photos.jsonl"}}},
			zip_context, harness.progress, harness.cancellation);
	REQUIRE(built.succeeded());

	harness.identifiers.script_operation_identifier("operation-b20-stage");
	shuba::ui::BackupImportStagingSessionResult staged =
		shuba::ui::stage_backup_import_for_session(
			shuba::ui::BackupImportStagingSessionRequest{
				.current_session		 = session,
				.identifiers			 = harness.identifiers,
				.clock					 = harness.clock,
				.operation_gate			 = harness.gate,
				.zip_archive_service	 = harness.zip,
				.document_export_service = harness.document_export,
				.content_staging_service = harness.staging,
				.source =
					shuba::platform::make_local_file_source(imported_zip)},
			harness.progress, harness.cancellation);

	REQUIRE(staged.succeeded());
	REQUIRE(staged.import_validation_ready);
	REQUIRE(staged.staging_result.staging_catalog_root.has_value());
	REQUIRE(staged.staging_result.validation.load_status
			== shuba::persistence::CatalogLoadStatus::Normal);
	REQUIRE(session.repository.items.empty());

	harness.identifiers.script_operation_identifier("operation-b20-replace");
	shuba::ui::BackupImportReplacementSessionResult replaced =
		shuba::ui::replace_session_with_staged_import(
			shuba::ui::BackupImportReplacementSessionRequest{
				.current_session = session,
				.identifiers	 = harness.identifiers,
				.clock			 = harness.clock,
				.operation_gate	 = harness.gate,
				.staged_catalog_root =
					*staged.staging_result.staging_catalog_root,
				.replacement_confirmed = true},
			harness.progress, harness.cancellation);

	REQUIRE(replaced.succeeded());
	REQUIRE(replaced.session.ready_for_browsing());
	REQUIRE(replaced.session.repository.items.size() == 1U);
	REQUIRE(replaced.session.repository.items.front().record.display_name
			== "Imported item");
}

TEST_CASE(
	"B20 fatal replacement result enters fatal recovery without auto repair") {
	B20Harness harness;
	shuba::ui::CatalogSessionState session = harness.initialize_empty();
	const std::filesystem::path staged_root =
		session.paths->operation_tmp_root / "fatal-replacement-stage";
	write_catalog_files(staged_root, make_id("catalog-b20-fatal"), "item-fatal",
						"Fatal replacement item");

	harness.identifiers.script_operation_identifier("operation-b20-fatal");
	shuba::ui::BackupImportReplacementSessionResult result =
		shuba::ui::replace_session_with_staged_import(
			shuba::ui::BackupImportReplacementSessionRequest{
				.current_session	   = session,
				.identifiers		   = harness.identifiers,
				.clock				   = harness.clock,
				.operation_gate		   = harness.gate,
				.staged_catalog_root   = staged_root,
				.replacement_confirmed = true,
				.fault_mode = shuba::catalog::CatalogReplacementFaultMode::
					ForceImportedLoadFatalAndFailRollbackRestore},
			harness.progress, harness.cancellation);

	REQUIRE(result.failed());
	REQUIRE(result.fatal_recovery_required);
	REQUIRE(result.session.fatal());
	shuba::ui::CatalogRecoveryUiSummary summary =
		shuba::ui::make_recovery_ui_summary(result.session);
	REQUIRE(summary.fatal());
	REQUIRE(summary.plain_summary_message.find("cannot safely open")
			!= std::string::npos);
	REQUIRE(read_text(session.paths->active_catalog_root / "data/items.jsonl")
				.find("Fatal replacement item")
			!= std::string::npos);
}
