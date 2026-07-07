#include "Platform/AndroidPreviousExit.hpp"
#include "Platform/JuceZipArchive.hpp"
#include "Platform/LinuxFakes.hpp"
#include "UI/Session/AndroidPreviousExitArtifacts.hpp"
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

class FakeAndroidPreviousExitService final
	: public shuba::platform::AndroidPreviousExitService {
public:
	shuba::platform::AndroidPreviousExitInfo response;
	std::vector<shuba::core::Diagnostic> response_diagnostics;
	std::string trace_bytes;
	int calls{};
	std::optional<shuba::platform::AndroidPreviousExitQueryRequest>
		last_request;

	[[nodiscard]] shuba::platform::PlatformValueResult<
		shuba::platform::AndroidPreviousExitInfo>
	query_previous_exit(const shuba::platform::AndroidPreviousExitQueryRequest&
							request) override {
		++calls;
		last_request								  = request;
		shuba::platform::AndroidPreviousExitInfo info = response;
		info.captured_at							  = request.captured_at;
		info.trace_requested						  = request.capture_trace;
		info.trace_path = request.trace_output_path;
		if (info.reason_name.empty()) {
			info.reason_name = std::string{
				shuba::platform::android_exit_reason_name(info.reason)};
		}

		if (!trace_bytes.empty() && request.capture_trace) {
			std::filesystem::create_directories(
				request.trace_output_path.parent_path());
			std::ofstream output{request.trace_output_path,
								 std::ios::binary | std::ios::trunc};
			output.write(trace_bytes.data(),
						 static_cast<std::streamsize>(trace_bytes.size()));
			output.close();
			info.trace_available = true;
			info.trace_captured	 = true;
			info.trace_byte_count =
				static_cast<std::uint64_t>(trace_bytes.size());
		}

		return shuba::platform::platform_value_success(info,
													   response_diagnostics);
	}
};

[[nodiscard]] shuba::platform::AppPrivatePaths resolve_paths(
	shuba::platform::LinuxFakePathProvider& path_provider) {
	shuba::platform::PlatformValueResult<shuba::platform::AppPrivatePaths>
		paths = path_provider.resolve_app_private_paths();
	REQUIRE(paths.succeeded());
	return std::move(*paths.value);
}

[[nodiscard]] shuba::ui::StartupAttemptMarker make_marker(
	std::string attempt_id = "operation-b25-startup-attempt") {
	return shuba::ui::StartupAttemptMarker{
		.attempt_id	 = std::move(attempt_id),
		.started_at	 = shuba::core::EpochMilliseconds{1760000000000},
		.app_version = "0.1.0",
		.platform	 = "host-test",
		.stage		 = "loading-catalog",
		.retry_requested_by_user = false,
		.previous_attempt_count	 = 1U,
		.notes					 = {"b25 marker"}};
}

[[nodiscard]] shuba::platform::AndroidPreviousExitInfo native_crash_info() {
	return shuba::platform::AndroidPreviousExitInfo{
		.record_available = true,
		.record_timestamp = shuba::core::EpochMilliseconds{1760000000123},
		.process_id		  = 4242,
		.process_name	  = "com.example.shuba",
		.reason = shuba::platform::android_application_exit_reason_crash_native,
		.reason_name  = std::string{shuba::platform::android_exit_reason_name(
			shuba::platform::android_application_exit_reason_crash_native)},
		.status		  = 11,
		.importance	  = 100,
		.description  = "native crash in startup",
		.native_crash = true,
		.actionable_for_startup = true};
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
			zip_context("operation-b25-extract-zip"), progress, cancellation);
	REQUIRE(extracted.succeeded());
	return target_directory;
}
}	 // namespace

TEST_CASE(
	"B25 no-op Android previous-exit service returns unavailable record") {
	shuba::platform::NoOpAndroidPreviousExitService service;
	shuba::platform::PlatformValueResult<
		shuba::platform::AndroidPreviousExitInfo>
		result = service.query_previous_exit(
			shuba::platform::AndroidPreviousExitQueryRequest{
				.trace_output_path = "trace.bin",
				.captured_at   = shuba::core::EpochMilliseconds{1760000000000},
				.capture_trace = true});

	REQUIRE(result.succeeded());
	REQUIRE(result.value.has_value());
	REQUIRE_FALSE(result.value->record_available);
	REQUIRE(result.value->trace_requested);
	REQUIRE(diagnostics_contain_code(result.diagnostics,
									 "android-previous-exit-unavailable"));
}

TEST_CASE("B25 previous-exit report serializes native crash metadata") {
	TemporaryDirectory temporary{"shuba-b25-report"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	const shuba::platform::AppPrivatePaths paths = resolve_paths(path_provider);
	shuba::core::ManualClock clock{
		shuba::core::EpochMilliseconds{1760000100000}};
	FakeAndroidPreviousExitService service;
	service.response = native_crash_info();

	shuba::ui::StartupRecoveryFileResult result =
		shuba::ui::capture_android_previous_exit_artifacts(
			shuba::ui::AndroidPreviousExitArtifactCaptureRequest{
				.paths = paths, .service = service, .clock = clock});

	REQUIRE(result.succeeded());
	REQUIRE(service.calls == 1);
	const std::string report =
		read_text(shuba::ui::android_previous_exit_report_path(paths));
	REQUIRE(report.find(R"("schemaVersion":1)") != std::string::npos);
	REQUIRE(report.find(R"("recordAvailable":true)") != std::string::npos);
	REQUIRE(report.find("REASON_CRASH_NATIVE") != std::string::npos);
	REQUIRE(report.find(R"("nativeCrash":true)") != std::string::npos);
	REQUIRE(report.find("com.example.shuba") != std::string::npos);
	REQUIRE(report.find(R"("traceCaptured":false)") != std::string::npos);
}

TEST_CASE("B25 previous-exit trace artifact is copied and reported") {
	TemporaryDirectory temporary{"shuba-b25-trace"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	const shuba::platform::AppPrivatePaths paths = resolve_paths(path_provider);
	shuba::core::ManualClock clock{
		shuba::core::EpochMilliseconds{1760000200000}};
	FakeAndroidPreviousExitService service;
	service.response	= native_crash_info();
	service.trace_bytes = "trace-bytes-for-b25";

	shuba::ui::StartupRecoveryFileResult result =
		shuba::ui::capture_android_previous_exit_artifacts(
			shuba::ui::AndroidPreviousExitArtifactCaptureRequest{
				.paths = paths, .service = service, .clock = clock});

	REQUIRE(result.succeeded());
	REQUIRE(read_text(shuba::ui::android_previous_exit_trace_path(paths))
			== service.trace_bytes);
	const std::string report =
		read_text(shuba::ui::android_previous_exit_report_path(paths));
	REQUIRE(report.find(R"("traceCaptured":true)") != std::string::npos);
	REQUIRE(report.find(R"("traceByteCount":19)") != std::string::npos);
	REQUIRE(report.find("recovery/startup/android-previous-exit-trace.bin")
			!= std::string::npos);
}

TEST_CASE("B25 missing previous-exit trace still writes diagnostic report") {
	TemporaryDirectory temporary{"shuba-b25-missing-trace"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	const shuba::platform::AppPrivatePaths paths = resolve_paths(path_provider);
	shuba::core::ManualClock clock{
		shuba::core::EpochMilliseconds{1760000300000}};
	FakeAndroidPreviousExitService service;
	service.response				 = native_crash_info();
	service.response.trace_available = false;
	service.response_diagnostics.push_back(shuba::core::Diagnostic{
		.severity = shuba::core::DiagnosticSeverity::RecoverableWarning,
		.code	  = "android-previous-exit-trace-unavailable",
		.message  = "Android did not return a trace stream."});

	shuba::ui::StartupRecoveryFileResult result =
		shuba::ui::capture_android_previous_exit_artifacts(
			shuba::ui::AndroidPreviousExitArtifactCaptureRequest{
				.paths = paths, .service = service, .clock = clock});

	REQUIRE(result.succeeded());
	REQUIRE_FALSE(std::filesystem::exists(
		shuba::ui::android_previous_exit_trace_path(paths)));
	const std::string report =
		read_text(shuba::ui::android_previous_exit_report_path(paths));
	REQUIRE(report.find(R"("traceAvailable":false)") != std::string::npos);
	REQUIRE(report.find("android-previous-exit-trace-unavailable")
			!= std::string::npos);
}

TEST_CASE("B25 diagnostic archive includes Android previous-exit artifacts") {
	TemporaryDirectory temporary{"shuba-b25-archive"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	const shuba::platform::AppPrivatePaths paths = resolve_paths(path_provider);
	shuba::ui::StartupRecoveryFileResult marker_written =
		shuba::ui::write_startup_attempt_marker(paths, make_marker());
	REQUIRE(marker_written.succeeded());
	FakeAndroidPreviousExitService service;
	service.response	= native_crash_info();
	service.trace_bytes = "archive-trace-bytes";
	shuba::platform::ScriptedIdentifierSource identifiers;
	identifiers.script_operation_identifier("operation-b25-archive");
	shuba::core::ManualClock clock{
		shuba::core::EpochMilliseconds{1760000400000}};

	shuba::ui::CatalogSessionState state =
		shuba::ui::load_guarded_catalog_session(
			shuba::ui::GuardedCatalogSessionLoadRequest{
				.path_provider				   = path_provider,
				.identifiers				   = identifiers,
				.clock						   = clock,
				.app_version				   = "0.1.0",
				.platform					   = "host-test",
				.debug_demo_seed_enabled	   = false,
				.android_previous_exit_service = &service});
	REQUIRE(state.fatal());
	REQUIRE(state.source
			== shuba::ui::CatalogSessionStartupSource::StartupCrashSafeMode);

	shuba::platform::JuceZipArchiveService zip;
	shuba::platform::LinuxFakeDocumentExportService document_export;
	shuba::platform::LinuxFakeContentStagingService staging;
	shuba::core::OperationGate gate;
	shuba::platform::ProgressCollector progress;
	shuba::platform::ManualCancellationToken cancellation;
	const std::filesystem::path destination =
		temporary.path() / "android-previous-exit-diagnostic.zip";
	shuba::ui::BackupExportSessionResult archive =
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

	REQUIRE(archive.succeeded());
	REQUIRE(contains_entry(archive.export_result.included_entries,
						   "recovery/startup/android-previous-exit.json"));
	REQUIRE(contains_entry(archive.export_result.included_entries,
						   "recovery/startup/android-previous-exit-trace.bin"));
	const std::filesystem::path extracted =
		extract_zip_archive(destination, temporary.path() / "archive-extracted",
							zip, progress, cancellation);
	REQUIRE(read_text(extracted
					  / "recovery/startup/android-previous-exit-trace.bin")
			== service.trace_bytes);
	const std::string report =
		read_text(extracted / "recovery/startup/android-previous-exit.json");
	REQUIRE(report.find("REASON_CRASH_NATIVE") != std::string::npos);
}

TEST_CASE("B25 guarded startup captures previous exit only when warranted") {
	TemporaryDirectory stale_temporary{"shuba-b25-guarded-stale"};
	shuba::platform::LinuxFakePathProvider stale_path_provider{
		stale_temporary.path()};
	const shuba::platform::AppPrivatePaths stale_paths =
		resolve_paths(stale_path_provider);
	shuba::ui::StartupRecoveryFileResult marker_written =
		shuba::ui::write_startup_attempt_marker(stale_paths, make_marker());
	REQUIRE(marker_written.succeeded());
	FakeAndroidPreviousExitService stale_service;
	stale_service.response = native_crash_info();
	shuba::platform::ScriptedIdentifierSource stale_identifiers;
	shuba::core::ManualClock stale_clock{
		shuba::core::EpochMilliseconds{1760000500000}};

	shuba::ui::CatalogSessionState stale_state =
		shuba::ui::load_guarded_catalog_session(
			shuba::ui::GuardedCatalogSessionLoadRequest{
				.path_provider				   = stale_path_provider,
				.identifiers				   = stale_identifiers,
				.clock						   = stale_clock,
				.app_version				   = "0.1.0",
				.platform					   = "host-test",
				.debug_demo_seed_enabled	   = false,
				.android_previous_exit_service = &stale_service});
	REQUIRE(stale_state.fatal());
	REQUIRE(stale_service.calls == 1);
	REQUIRE(std::filesystem::exists(
		shuba::ui::android_previous_exit_report_path(stale_paths)));

	TemporaryDirectory clean_temporary{"shuba-b25-guarded-clean"};
	shuba::platform::LinuxFakePathProvider clean_path_provider{
		clean_temporary.path()};
	const shuba::platform::AppPrivatePaths clean_paths =
		resolve_paths(clean_path_provider);
	FakeAndroidPreviousExitService clean_service;
	clean_service.response = native_crash_info();
	shuba::platform::ScriptedIdentifierSource clean_identifiers;
	clean_identifiers.script_operation_identifier("operation-b25-clean-marker");
	clean_identifiers.script_stable_identifier("catalog-b25-clean");
	clean_identifiers.script_operation_identifier("operation-b25-clean-init");
	shuba::core::ManualClock clean_clock{
		shuba::core::EpochMilliseconds{1760000600000}};

	shuba::ui::CatalogSessionState clean_state =
		shuba::ui::load_guarded_catalog_session(
			shuba::ui::GuardedCatalogSessionLoadRequest{
				.path_provider				   = clean_path_provider,
				.identifiers				   = clean_identifiers,
				.clock						   = clean_clock,
				.app_version				   = "0.1.0",
				.platform					   = "host-test",
				.debug_demo_seed_enabled	   = false,
				.android_previous_exit_service = &clean_service});
	REQUIRE(clean_state.ready_for_browsing());
	REQUIRE(clean_service.calls == 0);
	REQUIRE_FALSE(std::filesystem::exists(
		shuba::ui::android_previous_exit_report_path(clean_paths)));
}
