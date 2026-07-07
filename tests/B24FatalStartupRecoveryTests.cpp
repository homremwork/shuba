#include "Persistence/MetadataSchema.hpp"
#include "Platform/JuceZipArchive.hpp"
#include "Platform/LinuxFakes.hpp"
#include "UI/Session/BackupRecoverySession.hpp"
#include "UI/Session/CatalogStartupSession.hpp"
#include "UI/Session/StartupRecoverySession.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {
class ThrowingPathProvider final
	: public shuba::platform::AppPrivatePathProvider {
public:
	[[nodiscard]] shuba::platform::PlatformValueResult<
		shuba::platform::AppPrivatePaths>
	resolve_app_private_paths() const override {
		throw std::runtime_error{"path provider exploded"};
	}
};

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

[[nodiscard]] shuba::platform::AppPrivatePaths resolve_paths(
	shuba::platform::LinuxFakePathProvider& path_provider) {
	shuba::platform::PlatformValueResult<shuba::platform::AppPrivatePaths>
		paths = path_provider.resolve_app_private_paths();
	REQUIRE(paths.succeeded());
	return std::move(*paths.value);
}

[[nodiscard]] shuba::ui::StartupAttemptMarker make_marker(
	std::string attempt_id = "operation-startup-attempt") {
	return shuba::ui::StartupAttemptMarker{
		.attempt_id	 = std::move(attempt_id),
		.started_at	 = shuba::core::EpochMilliseconds{1760000000000},
		.app_version = "0.1.0",
		.platform	 = "host-test",
		.stage		 = "loading-catalog",
		.retry_requested_by_user = false,
		.previous_attempt_count	 = 2U,
		.notes					 = {"test marker"}};
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

[[nodiscard]] bool diagnostics_contain_code(
	const std::vector<shuba::core::Diagnostic>& diagnostics,
	std::string_view code) {
	return std::ranges::any_of(
		diagnostics, [code](const shuba::core::Diagnostic& diagnostic) {
		return diagnostic.code == code;
	});
}

[[nodiscard]] bool contains_text(std::span<const std::string> values,
								 std::string_view text) {
	return std::ranges::any_of(
		values, [text](const std::string& value) { return value == text; });
}

[[nodiscard]] bool contains_entry(const std::vector<std::string>& values,
								  std::string_view expected) {
	return std::ranges::find(values, std::string{expected}) != values.end();
}

[[nodiscard]] shuba::platform::PlatformOperationContext zip_context(
	std::string operation_id) {
	return shuba::platform::PlatformOperationContext{
		.operation_id = *shuba::core::OperationIdentifier::try_create_file_safe(
			std::move(operation_id)),
		.operation_type =
			shuba::platform::ProgressOperationType::DiagnosticExport};
}

[[nodiscard]] std::filesystem::path extract_zip_archive(
	const std::filesystem::path& archive_path,
	const std::filesystem::path& target_directory,
	shuba::platform::JuceZipArchiveService& zip,
	shuba::platform::ProgressCollector& progress,
	shuba::platform::ManualCancellationToken& cancellation) {
	shuba::platform::PlatformValueResult<shuba::platform::ZipArchiveInspection>
		extracted = zip.extract_zip_archive(
			shuba::platform::ZipArchiveExtractRequest{
				.archive_path	  = archive_path,
				.target_directory = target_directory},
			zip_context("operation-b24-extract-zip"), progress, cancellation);
	REQUIRE(extracted.succeeded());
	return target_directory;
}

[[nodiscard]] shuba::ui::CatalogSessionState load_session(
	shuba::platform::LinuxFakePathProvider& path_provider,
	shuba::platform::ScriptedIdentifierSource& identifiers,
	shuba::core::ManualClock& clock, bool honor_startup_safe_mode) {
	return shuba::ui::load_catalog_session(shuba::ui::CatalogSessionLoadRequest{
		.path_provider			 = path_provider,
		.identifiers			 = identifiers,
		.clock					 = clock,
		.debug_demo_seed_enabled = false,
		.honor_startup_safe_mode = honor_startup_safe_mode});
}

[[nodiscard]] shuba::ui::CatalogSessionState load_guarded_session(
	shuba::platform::LinuxFakePathProvider& path_provider,
	shuba::platform::ScriptedIdentifierSource& identifiers,
	shuba::core::ManualClock& clock,
	shuba::ui::CatalogSessionLoader loader = {},
	bool retry_requested_by_user		   = false) {
	return shuba::ui::load_guarded_catalog_session(
		shuba::ui::GuardedCatalogSessionLoadRequest{
			.path_provider			 = path_provider,
			.identifiers			 = identifiers,
			.clock					 = clock,
			.app_version			 = "0.1.0",
			.platform				 = "host-test",
			.debug_demo_seed_enabled = false,
			.retry_requested_by_user = retry_requested_by_user,
			.loader					 = std::move(loader)});
}
}	 // namespace

TEST_CASE("B24 startup marker write, read, update, and clear round trip") {
	TemporaryDirectory temporary{"shuba-b24-marker-round-trip"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	const shuba::platform::AppPrivatePaths paths = resolve_paths(path_provider);
	const shuba::ui::StartupAttemptMarker marker = make_marker();

	shuba::ui::StartupRecoveryFileResult written =
		shuba::ui::write_startup_attempt_marker(paths, marker);

	REQUIRE(written.succeeded());
	REQUIRE(std::filesystem::exists(
		shuba::ui::active_startup_attempt_marker_path(paths)));

	shuba::ui::StartupAttemptMarkerReadResult read =
		shuba::ui::read_startup_attempt_marker(paths);

	REQUIRE(read.succeeded());
	REQUIRE(read.marker_file_present);
	REQUIRE(read.safe_mode_required());
	REQUIRE(read.marker.has_value());
	REQUIRE(*read.marker == marker);

	shuba::ui::StartupRecoveryFileResult updated =
		shuba::ui::update_startup_attempt_stage(paths, "ui-construction");
	REQUIRE(updated.succeeded());

	shuba::ui::StartupAttemptMarkerReadResult updated_read =
		shuba::ui::read_startup_attempt_marker(paths);
	REQUIRE(updated_read.marker.has_value());
	REQUIRE(updated_read.marker->stage == "ui-construction");
	REQUIRE(updated_read.marker->attempt_id == marker.attempt_id);

	shuba::ui::StartupRecoveryFileResult cleared =
		shuba::ui::clear_startup_attempt_marker(paths);
	REQUIRE(cleared.succeeded());

	shuba::ui::StartupAttemptMarkerReadResult after_clear =
		shuba::ui::read_startup_attempt_marker(paths);
	REQUIRE(after_clear.succeeded());
	REQUIRE_FALSE(after_clear.marker_file_present);
	REQUIRE_FALSE(after_clear.safe_mode_required());
	REQUIRE_FALSE(after_clear.marker.has_value());
}

TEST_CASE(
	"B24 stale startup marker produces synthetic fatal safe mode session") {
	TemporaryDirectory temporary{"shuba-b24-safe-mode"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	const shuba::platform::AppPrivatePaths paths = resolve_paths(path_provider);
	shuba::ui::StartupRecoveryFileResult written =
		shuba::ui::write_startup_attempt_marker(paths, make_marker());
	REQUIRE(written.succeeded());
	write_text(paths.operation_tmp_root / "startup-evidence.tmp", "keep me");
	write_text(paths.active_catalog_root / "tmp/startup-active-evidence.tmp",
			   "keep me too");
	write_text(paths.active_catalog_root / ".manifest.json.crash.tmp",
			   "metadata evidence");

	shuba::platform::ScriptedIdentifierSource identifiers;
	shuba::core::ManualClock clock{
		shuba::core::EpochMilliseconds{1760000000000}};

	shuba::ui::CatalogSessionState state =
		load_session(path_provider, identifiers, clock, true);

	REQUIRE(state.fatal());
	REQUIRE(state.source
			== shuba::ui::CatalogSessionStartupSource::StartupCrashSafeMode);
	REQUIRE(state.paths.has_value());
	REQUIRE(state.paths->active_catalog_root == paths.active_catalog_root);
	REQUIRE(state.repository.items.empty());
	REQUIRE(state.repository.storages.empty());
	REQUIRE(state.repository.photos.empty());
	REQUIRE(state.search_index.items.empty());
	REQUIRE(diagnostics_contain_code(state.startup_diagnostics,
									 "startup_attempt_unresolved"));
	REQUIRE(std::filesystem::exists(paths.operation_tmp_root
									/ "startup-evidence.tmp"));
	REQUIRE(std::filesystem::exists(paths.active_catalog_root
									/ "tmp/startup-active-evidence.tmp"));
	REQUIRE(std::filesystem::exists(paths.active_catalog_root
									/ ".manifest.json.crash.tmp"));

	shuba::ui::CatalogRecoveryUiSummary summary =
		shuba::ui::make_recovery_ui_summary(state);
	REQUIRE(summary.fatal());
	REQUIRE(summary.plain_summary_message.find("Previous launch stopped")
			!= std::string::npos);
	REQUIRE(summary.startup_crash_safe_mode());
	REQUIRE(contains_text(summary.safe_actions, "Export diagnostic archive"));
	REQUIRE(contains_text(summary.safe_actions, "Import backup ZIP"));
	REQUIRE(contains_text(summary.safe_actions, "Show technical report"));
	REQUIRE(contains_text(summary.safe_actions, "Retry normal launch"));
	REQUIRE(contains_text(summary.safe_actions, "Exit"));
}

TEST_CASE("B24 direct safe mode checks stale marker before startup cleanup") {
	TemporaryDirectory temporary{"shuba-b24-direct-safe-mode-before-cleanup"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	const shuba::platform::AppPrivatePaths paths = resolve_paths(path_provider);
	shuba::ui::StartupRecoveryFileResult written =
		shuba::ui::write_startup_attempt_marker(paths, make_marker());
	REQUIRE(written.succeeded());
	write_text(paths.operation_tmp_root / "preserve-operation.tmp",
			   "operation temp evidence");
	write_text(paths.active_catalog_root / "tmp/preserve-active.tmp",
			   "active temp evidence");
	write_text(paths.active_catalog_root / ".settings.json.preserve.tmp",
			   "metadata temp evidence");

	shuba::platform::ScriptedIdentifierSource identifiers;
	shuba::core::ManualClock clock{
		shuba::core::EpochMilliseconds{1760000000000}};

	shuba::ui::CatalogSessionState state =
		load_session(path_provider, identifiers, clock, true);

	REQUIRE(state.fatal());
	REQUIRE(state.source
			== shuba::ui::CatalogSessionStartupSource::StartupCrashSafeMode);
	REQUIRE(std::filesystem::exists(paths.operation_tmp_root
									/ "preserve-operation.tmp"));
	REQUIRE(std::filesystem::exists(paths.active_catalog_root
									/ "tmp/preserve-active.tmp"));
	REQUIRE(std::filesystem::exists(paths.active_catalog_root
									/ ".settings.json.preserve.tmp"));
}

TEST_CASE("B24 startup retry bypasses stale marker for one guarded load") {
	TemporaryDirectory temporary{"shuba-b24-retry-bypass"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	const shuba::platform::AppPrivatePaths paths = resolve_paths(path_provider);
	shuba::ui::StartupRecoveryFileResult written =
		shuba::ui::write_startup_attempt_marker(paths, make_marker());
	REQUIRE(written.succeeded());

	shuba::platform::ScriptedIdentifierSource identifiers;
	identifiers.script_operation_identifier("operation-retry-normal");
	shuba::core::ManualClock clock{
		shuba::core::EpochMilliseconds{1760000000000}};
	int loader_calls = 0;

	shuba::ui::CatalogSessionState state = load_guarded_session(
		path_provider, identifiers, clock,
		[&loader_calls](const shuba::ui::CatalogSessionLoadRequest& request)
			-> shuba::ui::CatalogSessionState {
		++loader_calls;
		REQUIRE(request.resolved_paths.has_value());
		REQUIRE_FALSE(request.honor_startup_safe_mode);

		shuba::ui::CatalogSessionState loaded;
		loaded.paths  = request.resolved_paths;
		loaded.source = shuba::ui::CatalogSessionStartupSource::ExistingCatalog;
		loaded.load_status = shuba::persistence::CatalogLoadStatus::Normal;
		loaded.load_result.load_status =
			shuba::persistence::CatalogLoadStatus::Normal;
		return loaded;
	},
		true);

	REQUIRE(loader_calls == 1);
	REQUIRE(state.ready_for_browsing());
	REQUIRE(state.source
			== shuba::ui::CatalogSessionStartupSource::ExistingCatalog);
	REQUIRE_FALSE(diagnostics_contain_code(state.startup_diagnostics,
										   "startup_attempt_unresolved"));

	shuba::ui::StartupAttemptMarkerReadResult marker_after_retry =
		shuba::ui::read_startup_attempt_marker(paths);
	REQUIRE(marker_after_retry.marker.has_value());
	REQUIRE(marker_after_retry.marker->retry_requested_by_user);
	REQUIRE(marker_after_retry.marker->previous_attempt_count == 3U);
}

TEST_CASE("B24 startup retry runs cleanup through normal load path") {
	TemporaryDirectory temporary{"shuba-b24-retry-cleanup"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	const shuba::platform::AppPrivatePaths paths = resolve_paths(path_provider);
	shuba::ui::StartupRecoveryFileResult written =
		shuba::ui::write_startup_attempt_marker(paths, make_marker());
	REQUIRE(written.succeeded());
	write_text(paths.operation_tmp_root / "retry-operation.tmp",
			   "retry cleanup should remove operation temp evidence");
	write_text(paths.active_catalog_root / "tmp/retry-active.tmp",
			   "retry cleanup should remove active temp evidence");
	write_text(paths.active_catalog_root / ".manifest.json.retry.tmp",
			   "retry cleanup should remove metadata temp evidence");

	shuba::platform::ScriptedIdentifierSource identifiers;
	identifiers.script_operation_identifier("operation-retry-cleanup");
	identifiers.script_stable_identifier("catalog-retry-cleanup");
	identifiers.script_operation_identifier("operation-retry-cleanup-init");
	shuba::core::ManualClock clock{
		shuba::core::EpochMilliseconds{1760000000000}};

	shuba::ui::CatalogSessionState state =
		load_guarded_session(path_provider, identifiers, clock, {}, true);

	REQUIRE(state.ready_for_browsing());
	REQUIRE_FALSE(std::filesystem::exists(paths.operation_tmp_root
										  / "retry-operation.tmp"));
	REQUIRE_FALSE(std::filesystem::exists(paths.active_catalog_root
										  / "tmp/retry-active.tmp"));
	REQUIRE_FALSE(std::filesystem::exists(paths.active_catalog_root
										  / ".manifest.json.retry.tmp"));

	shuba::ui::StartupAttemptMarkerReadResult marker_after_retry =
		shuba::ui::read_startup_attempt_marker(paths);
	REQUIRE(marker_after_retry.marker.has_value());
	REQUIRE(marker_after_retry.marker->retry_requested_by_user);
}

TEST_CASE(
	"B24 safe mode import replacement performs deferred startup cleanup") {
	TemporaryDirectory temporary{"shuba-b24-import-deferred-cleanup"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	const shuba::platform::AppPrivatePaths paths = resolve_paths(path_provider);
	shuba::ui::StartupRecoveryFileResult written =
		shuba::ui::write_startup_attempt_marker(paths, make_marker());
	REQUIRE(written.succeeded());
	write_text(paths.operation_tmp_root / "import-operation.tmp",
			   "import cleanup should remove operation temp evidence");
	write_text(paths.active_catalog_root / "tmp/import-active.tmp",
			   "import cleanup should remove active temp evidence");
	write_text(paths.active_catalog_root / ".settings.json.import.tmp",
			   "import cleanup should remove metadata temp evidence");

	shuba::platform::ScriptedIdentifierSource identifiers;
	shuba::core::ManualClock clock{
		shuba::core::EpochMilliseconds{1760000000000}};
	shuba::ui::CatalogSessionState safe_mode =
		load_session(path_provider, identifiers, clock, true);
	REQUIRE(safe_mode.fatal());
	REQUIRE(safe_mode.source
			== shuba::ui::CatalogSessionStartupSource::StartupCrashSafeMode);
	REQUIRE(std::filesystem::exists(paths.operation_tmp_root
									/ "import-operation.tmp"));

	const std::filesystem::path staged_root =
		paths.operation_tmp_root / "import-staging"
		/ "operation-import-deferred-cleanup";
	const shuba::persistence::EmptyCatalogFixture fixture =
		shuba::persistence::make_empty_catalog_fixture(
			*shuba::core::StableIdentifier::try_create_file_safe(
				"catalog-import-deferred-cleanup"),
			clock.now());
	const shuba::persistence::SchemaWriteResult manifest =
		shuba::persistence::serialize_manifest_json(fixture.manifest);
	const shuba::persistence::SchemaWriteResult settings =
		shuba::persistence::serialize_settings_json(fixture.settings);
	REQUIRE(manifest.succeeded());
	REQUIRE(settings.succeeded());
	write_text(staged_root / "manifest.json", manifest.json);
	write_text(staged_root / "settings.json", settings.json);
	write_text(staged_root / "data/items.jsonl", "");
	write_text(staged_root / "data/storages.jsonl", "");
	write_text(staged_root / "data/photos.jsonl", "");

	identifiers.script_operation_identifier(
		"operation-import-deferred-cleanup");
	shuba::core::OperationGate gate;
	shuba::platform::ProgressCollector progress;
	shuba::platform::ManualCancellationToken cancellation;
	shuba::ui::BackupImportReplacementSessionResult result =
		shuba::ui::replace_session_with_staged_import(
			shuba::ui::BackupImportReplacementSessionRequest{
				.current_session	   = safe_mode,
				.identifiers		   = identifiers,
				.clock				   = clock,
				.operation_gate		   = gate,
				.staged_catalog_root   = staged_root,
				.replacement_confirmed = true},
			progress, cancellation);

	REQUIRE(result.succeeded());
	REQUIRE(result.session.ready_for_browsing());
	REQUIRE(
		std::filesystem::exists(paths.operation_tmp_root / "import-staging"));
	REQUIRE_FALSE(std::filesystem::exists(paths.operation_tmp_root
										  / "import-operation.tmp"));
	REQUIRE_FALSE(std::filesystem::exists(paths.active_catalog_root
										  / "tmp/import-active.tmp"));
	REQUIRE_FALSE(std::filesystem::exists(paths.active_catalog_root
										  / ".settings.json.import.tmp"));
}

TEST_CASE("B24 startup marker opt-in preserves existing normal load behavior") {
	TemporaryDirectory temporary{"shuba-b24-opt-in"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	const shuba::platform::AppPrivatePaths paths = resolve_paths(path_provider);
	shuba::ui::StartupRecoveryFileResult written =
		shuba::ui::write_startup_attempt_marker(paths, make_marker());
	REQUIRE(written.succeeded());

	shuba::platform::ScriptedIdentifierSource identifiers;
	identifiers.script_stable_identifier("catalog-opt-in-normal");
	identifiers.script_operation_identifier("operation-opt-in-normal");
	shuba::core::ManualClock clock{
		shuba::core::EpochMilliseconds{1760000000000}};

	shuba::ui::CatalogSessionState state =
		load_session(path_provider, identifiers, clock, false);

	REQUIRE(state.source
			== shuba::ui::CatalogSessionStartupSource::InitializedEmptyCatalog);
	REQUIRE(state.ready_for_browsing());
	REQUIRE(state.repository.items.empty());
}

TEST_CASE("B24 malformed startup marker is recoverable safe mode evidence") {
	TemporaryDirectory temporary{"shuba-b24-malformed"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	const shuba::platform::AppPrivatePaths paths = resolve_paths(path_provider);
	write_text(shuba::ui::active_startup_attempt_marker_path(paths),
			   "{ this is not json");

	shuba::ui::StartupAttemptMarkerReadResult read =
		shuba::ui::read_startup_attempt_marker(paths);
	REQUIRE(read.succeeded());
	REQUIRE(read.marker_file_present);
	REQUIRE(read.marker_malformed);
	REQUIRE_FALSE(read.marker.has_value());
	REQUIRE(read.safe_mode_required());
	REQUIRE(diagnostics_contain_code(read.diagnostics,
									 "startup_marker_parse_failed"));

	shuba::platform::ScriptedIdentifierSource identifiers;
	shuba::core::ManualClock clock{
		shuba::core::EpochMilliseconds{1760000000000}};
	shuba::ui::CatalogSessionState state =
		load_session(path_provider, identifiers, clock, true);

	REQUIRE(state.fatal());
	REQUIRE(state.source
			== shuba::ui::CatalogSessionStartupSource::StartupCrashSafeMode);
	REQUIRE(diagnostics_contain_code(state.startup_diagnostics,
									 "startup_marker_parse_failed"));
	REQUIRE(diagnostics_contain_code(state.startup_diagnostics,
									 "startup_attempt_unresolved"));
}

TEST_CASE("B24 synthetic fatal startup session carries paths and diagnostics") {
	TemporaryDirectory temporary{"shuba-b24-synthetic"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	shuba::platform::AppPrivatePaths paths = resolve_paths(path_provider);
	const std::filesystem::path active_catalog_root = paths.active_catalog_root;

	shuba::ui::CatalogSessionState state =
		shuba::ui::make_synthetic_fatal_startup_session(
			shuba::ui::StartupSyntheticFatalSessionRequest{
				.paths = std::move(paths),
				.source =
					shuba::ui::CatalogSessionStartupSource::StartupException,
				.diagnostics = {shuba::core::Diagnostic{
					.severity =
						shuba::core::DiagnosticSeverity::FatalCatalogError,
					.code	 = "startup_exception",
					.message = "Startup exception was captured."}}});

	REQUIRE(state.fatal());
	REQUIRE_FALSE(state.ready_for_browsing());
	REQUIRE(state.paths.has_value());
	REQUIRE(state.paths->active_catalog_root == active_catalog_root);
	REQUIRE(state.source
			== shuba::ui::CatalogSessionStartupSource::StartupException);
	REQUIRE(state.repository.items.empty());
	REQUIRE(state.startup_diagnostics.size() == 1U);
}

TEST_CASE("B24 guarded startup converts loader exception to fatal session") {
	TemporaryDirectory temporary{"shuba-b24-guarded-exception"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	const shuba::platform::AppPrivatePaths paths = resolve_paths(path_provider);
	shuba::platform::ScriptedIdentifierSource identifiers;
	identifiers.script_operation_identifier("operation-guarded-exception");
	shuba::core::ManualClock clock{
		shuba::core::EpochMilliseconds{1760000000000}};

	shuba::ui::CatalogSessionState state =
		load_guarded_session(path_provider, identifiers, clock,
							 [](const shuba::ui::CatalogSessionLoadRequest&)
								 -> shuba::ui::CatalogSessionState {
		throw std::runtime_error{"catalog loader exploded"};
	});

	REQUIRE(state.fatal());
	REQUIRE(state.source
			== shuba::ui::CatalogSessionStartupSource::StartupException);
	REQUIRE(state.paths.has_value());
	REQUIRE(state.paths->active_catalog_root == paths.active_catalog_root);
	REQUIRE(diagnostics_contain_code(state.startup_diagnostics,
									 "startup_exception_captured"));
	REQUIRE(std::filesystem::exists(
		shuba::ui::active_startup_attempt_marker_path(paths)));
	REQUIRE(std::filesystem::exists(
		shuba::ui::last_startup_exception_report_path(paths)));
	const std::string report_text =
		read_text(shuba::ui::last_startup_exception_report_path(paths));
	REQUIRE(report_text.find("catalog loader exploded") != std::string::npos);
}

TEST_CASE("B24 guarded startup catches path provider exception") {
	ThrowingPathProvider path_provider;
	shuba::platform::ScriptedIdentifierSource identifiers;
	shuba::core::ManualClock clock{
		shuba::core::EpochMilliseconds{1760000000000}};

	shuba::ui::CatalogSessionState state =
		shuba::ui::load_guarded_catalog_session(
			shuba::ui::GuardedCatalogSessionLoadRequest{
				.path_provider			 = path_provider,
				.identifiers			 = identifiers,
				.clock					 = clock,
				.app_version			 = "0.1.0",
				.platform				 = "host-test",
				.debug_demo_seed_enabled = false});

	REQUIRE(state.fatal());
	REQUIRE_FALSE(state.paths.has_value());
	REQUIRE(state.source
			== shuba::ui::CatalogSessionStartupSource::StartupException);
	REQUIRE(diagnostics_contain_code(state.startup_diagnostics,
									 "startup_path_resolution_exception"));
}

TEST_CASE("B24 guarded expected fatal load keeps catalog fatal wording") {
	TemporaryDirectory temporary{"shuba-b24-guarded-expected-fatal"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	shuba::platform::ScriptedIdentifierSource initializer_identifiers;
	initializer_identifiers.script_stable_identifier("catalog-guarded-fatal");
	initializer_identifiers.script_operation_identifier("operation-init-fatal");
	shuba::core::ManualClock clock{
		shuba::core::EpochMilliseconds{1760000000000}};
	shuba::ui::CatalogSessionState initialized =
		load_session(path_provider, initializer_identifiers, clock, false);
	REQUIRE(initialized.ready_for_browsing());
	write_text(initialized.paths->active_catalog_root / "manifest.json",
			   "{ not valid json");

	shuba::platform::ScriptedIdentifierSource guarded_identifiers;
	guarded_identifiers.script_operation_identifier("operation-expected-fatal");
	shuba::ui::CatalogSessionState state =
		load_guarded_session(path_provider, guarded_identifiers, clock);

	REQUIRE(state.fatal());
	REQUIRE(state.source == shuba::ui::CatalogSessionStartupSource::LoadFailed);
	REQUIRE_FALSE(diagnostics_contain_code(state.startup_diagnostics,
										   "startup_exception_captured"));
	REQUIRE_FALSE(std::filesystem::exists(
		shuba::ui::last_startup_exception_report_path(*state.paths)));
	REQUIRE(std::filesystem::exists(
		shuba::ui::active_startup_attempt_marker_path(*state.paths)));

	shuba::ui::CatalogRecoveryUiSummary summary =
		shuba::ui::make_recovery_ui_summary(state);
	REQUIRE(summary.plain_summary_message.find("Catalog cannot safely open")
			!= std::string::npos);
}

TEST_CASE(
	"B24 diagnostic archive from safe mode includes startup crash artifacts") {
	TemporaryDirectory temporary{"shuba-b24-safe-mode-diagnostic"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	const shuba::platform::AppPrivatePaths paths = resolve_paths(path_provider);
	shuba::ui::StartupRecoveryFileResult written =
		shuba::ui::write_startup_attempt_marker(paths, make_marker());
	REQUIRE(written.succeeded());
	write_text(paths.active_catalog_root / "recovery/startup/manual-note.txt",
			   "manual safe-mode note");

	shuba::platform::ScriptedIdentifierSource identifiers;
	identifiers.script_operation_identifier("operation-safe-mode-diagnostic");
	shuba::core::ManualClock clock{
		shuba::core::EpochMilliseconds{1760000000000}};
	shuba::ui::CatalogSessionState state =
		load_session(path_provider, identifiers, clock, true);
	REQUIRE(state.fatal());
	REQUIRE(state.source
			== shuba::ui::CatalogSessionStartupSource::StartupCrashSafeMode);
	REQUIRE(std::filesystem::exists(
		shuba::ui::last_startup_safe_mode_report_path(paths)));

	shuba::platform::JuceZipArchiveService zip;
	shuba::platform::LinuxFakeDocumentExportService document_export;
	shuba::platform::LinuxFakeContentStagingService staging;
	shuba::core::OperationGate gate;
	shuba::platform::ProgressCollector progress;
	shuba::platform::ManualCancellationToken cancellation;
	const std::filesystem::path destination =
		temporary.path() / "safe-mode-diagnostic.zip";
	shuba::ui::BackupExportSessionResult result =
		shuba::ui::export_diagnostic_archive_from_session(
			shuba::ui::BackupExportSessionRequest{
				.current_session		 = state,
				.identifiers			 = identifiers,
				.clock					 = clock,
				.operation_gate			 = gate,
				.zip_archive_service	 = zip,
				.document_export_service = document_export,
				.content_staging_service = staging,
				.destination =
					shuba::platform::make_local_file_destination(destination),
				.keep_temp_zip = true},
			progress, cancellation);

	REQUIRE(result.succeeded());
	REQUIRE(contains_entry(result.export_result.included_entries,
						   "recovery/startup/active-startup-attempt.json"));
	REQUIRE(contains_entry(result.export_result.included_entries,
						   "recovery/startup/last-safe-mode-report.json"));
	REQUIRE(contains_entry(result.export_result.included_entries,
						   "recovery/startup/manual-note.txt"));
	REQUIRE(contains_entry(result.export_result.included_entries,
						   "diagnostic/archive-report.json"));

	const std::filesystem::path extracted = extract_zip_archive(
		destination, temporary.path() / "safe-mode-diagnostic-extracted", zip,
		progress, cancellation);
	const std::string safe_mode_report =
		read_text(extracted / "recovery/startup/last-safe-mode-report.json");
	REQUIRE(safe_mode_report.find("operation-startup-attempt")
			!= std::string::npos);
	REQUIRE(safe_mode_report.find("startup_attempt_unresolved")
			!= std::string::npos);
	const std::string archive_report =
		read_text(extracted / "diagnostic/archive-report.json");
	REQUIRE(archive_report.find("previous startup crash artifacts")
			!= std::string::npos);
	REQUIRE(archive_report.find("active-startup-attempt.json")
			!= std::string::npos);
	REQUIRE(archive_report.find("last-safe-mode-report.json")
			!= std::string::npos);
}

TEST_CASE(
	"B24 diagnostic archive from startup exception includes exception report") {
	TemporaryDirectory temporary{"shuba-b24-exception-diagnostic"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	shuba::platform::ScriptedIdentifierSource identifiers;
	identifiers.script_operation_identifier("operation-exception-diagnostic");
	identifiers.script_operation_identifier("operation-exception-archive");
	shuba::core::ManualClock clock{
		shuba::core::EpochMilliseconds{1760000000000}};

	shuba::ui::CatalogSessionState state =
		load_guarded_session(path_provider, identifiers, clock,
							 [](const shuba::ui::CatalogSessionLoadRequest&)
								 -> shuba::ui::CatalogSessionState {
		throw std::runtime_error{"diagnostic loader exploded"};
	});
	REQUIRE(state.fatal());
	REQUIRE(state.source
			== shuba::ui::CatalogSessionStartupSource::StartupException);
	REQUIRE(state.paths.has_value());
	REQUIRE(std::filesystem::exists(
		shuba::ui::last_startup_exception_report_path(*state.paths)));

	shuba::platform::JuceZipArchiveService zip;
	shuba::platform::LinuxFakeDocumentExportService document_export;
	shuba::platform::LinuxFakeContentStagingService staging;
	shuba::core::OperationGate gate;
	shuba::platform::ProgressCollector progress;
	shuba::platform::ManualCancellationToken cancellation;
	const std::filesystem::path destination =
		temporary.path() / "exception-diagnostic.zip";
	shuba::ui::BackupExportSessionResult result =
		shuba::ui::export_diagnostic_archive_from_session(
			shuba::ui::BackupExportSessionRequest{
				.current_session		 = state,
				.identifiers			 = identifiers,
				.clock					 = clock,
				.operation_gate			 = gate,
				.zip_archive_service	 = zip,
				.document_export_service = document_export,
				.content_staging_service = staging,
				.destination =
					shuba::platform::make_local_file_destination(destination),
				.keep_temp_zip = true},
			progress, cancellation);

	REQUIRE(result.succeeded());
	REQUIRE(contains_entry(result.export_result.included_entries,
						   "recovery/startup/active-startup-attempt.json"));
	REQUIRE(contains_entry(result.export_result.included_entries,
						   "recovery/startup/last-startup-exception.json"));
	REQUIRE_FALSE(
		contains_entry(result.export_result.included_entries,
					   "recovery/startup/last-safe-mode-report.json"));

	const std::filesystem::path extracted = extract_zip_archive(
		destination, temporary.path() / "exception-diagnostic-extracted", zip,
		progress, cancellation);
	const std::string exception_report =
		read_text(extracted / "recovery/startup/last-startup-exception.json");
	REQUIRE(exception_report.find("diagnostic loader exploded")
			!= std::string::npos);
	const std::string archive_report =
		read_text(extracted / "diagnostic/archive-report.json");
	REQUIRE(archive_report.find("last-startup-exception.json")
			!= std::string::npos);
	REQUIRE(archive_report.find("startup markers") != std::string::npos);
}

TEST_CASE("B24 normal backup excludes startup recovery artifacts") {
	TemporaryDirectory temporary{"shuba-b24-normal-excludes-startup"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	shuba::platform::ScriptedIdentifierSource identifiers;
	identifiers.script_stable_identifier("catalog-normal-excludes-startup");
	identifiers.script_operation_identifier("operation-normal-init");
	identifiers.script_operation_identifier("operation-normal-backup");
	shuba::core::ManualClock clock{
		shuba::core::EpochMilliseconds{1760000000000}};
	shuba::ui::CatalogSessionState state =
		load_session(path_provider, identifiers, clock, false);
	REQUIRE(state.ready_for_browsing());
	shuba::ui::StartupRecoveryFileResult marker_written =
		shuba::ui::write_startup_attempt_marker(*state.paths, make_marker());
	REQUIRE(marker_written.succeeded());
	shuba::ui::StartupRecoveryFileResult exception_written =
		shuba::ui::write_startup_exception_report(
			*state.paths, shuba::ui::StartupExceptionReport{
							  .attempt_id	  = "operation-normal-exception",
							  .captured_at	  = clock.now(),
							  .app_version	  = "0.1.0",
							  .platform		  = "host-test",
							  .stage		  = "catalog-load",
							  .exception_kind = "std::exception",
							  .message = "normal backup must exclude this",
							  .technical_details = "test"});
	REQUIRE(exception_written.succeeded());
	shuba::ui::StartupRecoveryFileResult safe_mode_written =
		shuba::ui::write_startup_safe_mode_report(
			*state.paths,
			shuba::ui::StartupSafeModeReport{
				.detected_at			= clock.now(),
				.marker_file_present	= true,
				.attempt_id				= "operation-normal-safe-mode",
				.started_at				= clock.now(),
				.app_version			= "0.1.0",
				.platform				= "host-test",
				.stage					= "catalog-load",
				.previous_attempt_count = 1U,
				.diagnostic_codes		= {"startup_attempt_unresolved"}});
	REQUIRE(safe_mode_written.succeeded());

	shuba::platform::JuceZipArchiveService zip;
	shuba::platform::LinuxFakeDocumentExportService document_export;
	shuba::platform::LinuxFakeContentStagingService staging;
	shuba::core::OperationGate gate;
	shuba::platform::ProgressCollector progress;
	shuba::platform::ManualCancellationToken cancellation;
	const std::filesystem::path destination =
		temporary.path() / "normal-backup.zip";
	shuba::ui::BackupExportSessionResult result =
		shuba::ui::export_backup_from_session(
			shuba::ui::BackupExportSessionRequest{
				.current_session		 = state,
				.identifiers			 = identifiers,
				.clock					 = clock,
				.operation_gate			 = gate,
				.zip_archive_service	 = zip,
				.document_export_service = document_export,
				.content_staging_service = staging,
				.destination =
					shuba::platform::make_local_file_destination(destination),
				.keep_temp_zip = true},
			progress, cancellation);

	REQUIRE(result.succeeded());
	REQUIRE_FALSE(
		contains_entry(result.export_result.included_entries,
					   "recovery/startup/active-startup-attempt.json"));
	REQUIRE_FALSE(
		contains_entry(result.export_result.included_entries,
					   "recovery/startup/last-startup-exception.json"));
	REQUIRE_FALSE(
		contains_entry(result.export_result.included_entries,
					   "recovery/startup/last-safe-mode-report.json"));

	const std::filesystem::path extracted = extract_zip_archive(
		destination, temporary.path() / "normal-backup-extracted", zip,
		progress, cancellation);
	REQUIRE_FALSE(std::filesystem::exists(
		extracted / "recovery/startup/active-startup-attempt.json"));
	REQUIRE_FALSE(std::filesystem::exists(
		extracted / "recovery/startup/last-startup-exception.json"));
	REQUIRE_FALSE(std::filesystem::exists(
		extracted / "recovery/startup/last-safe-mode-report.json"));
}
