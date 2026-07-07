#include "UI/Session/StartupRecoverySession.hpp"

#include <glaze/json.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace shuba::ui::startup_recovery_detail {
struct StartupAttemptMarkerDto final {
	int schema_version{};
	std::string attempt_id;
	std::int64_t started_at{};
	std::string app_version;
	std::string platform;
	std::string stage;
	bool retry_requested_by_user{};
	std::uint64_t previous_attempt_count{};
	std::vector<std::string> notes;
};

struct StartupExceptionReportDto final {
	int schema_version{};
	std::string attempt_id;
	std::int64_t captured_at{};
	std::string app_version;
	std::string platform;
	std::string stage;
	std::string exception_kind;
	std::string message;
	std::string technical_details;
};

struct StartupSafeModeReportDto final {
	int schema_version{};
	std::int64_t detected_at{};
	bool marker_file_present{};
	bool marker_malformed{};
	std::string attempt_id;
	std::int64_t started_at{};
	std::string app_version;
	std::string platform;
	std::string stage;
	bool retry_requested_by_user{};
	std::uint64_t previous_attempt_count{};
	std::vector<std::string> marker_notes;
	std::vector<std::string> diagnostic_codes;
};
}	 // namespace shuba::ui::startup_recovery_detail

template<>
struct glz::meta<shuba::ui::startup_recovery_detail::StartupAttemptMarkerDto> {
	using T = shuba::ui::startup_recovery_detail::StartupAttemptMarkerDto;
	static constexpr auto value = object(
		"schemaVersion", &T::schema_version, "attemptId", &T::attempt_id,
		"startedAt", &T::started_at, "appVersion", &T::app_version, "platform",
		&T::platform, "stage", &T::stage, "retryRequestedByUser",
		&T::retry_requested_by_user, "previousAttemptCount",
		&T::previous_attempt_count, "notes", &T::notes);
};

template<>
struct glz::meta<
	shuba::ui::startup_recovery_detail::StartupExceptionReportDto> {
	using T = shuba::ui::startup_recovery_detail::StartupExceptionReportDto;
	static constexpr auto value =
		object("schemaVersion", &T::schema_version, "attemptId", &T::attempt_id,
			   "capturedAt", &T::captured_at, "appVersion", &T::app_version,
			   "platform", &T::platform, "stage", &T::stage, "exceptionKind",
			   &T::exception_kind, "message", &T::message, "technicalDetails",
			   &T::technical_details);
};

template<>
struct glz::meta<shuba::ui::startup_recovery_detail::StartupSafeModeReportDto> {
	using T = shuba::ui::startup_recovery_detail::StartupSafeModeReportDto;
	static constexpr auto value = object(
		"schemaVersion", &T::schema_version, "detectedAt", &T::detected_at,
		"markerFilePresent", &T::marker_file_present, "markerMalformed",
		&T::marker_malformed, "attemptId", &T::attempt_id, "startedAt",
		&T::started_at, "appVersion", &T::app_version, "platform", &T::platform,
		"stage", &T::stage, "retryRequestedByUser", &T::retry_requested_by_user,
		"previousAttemptCount", &T::previous_attempt_count, "markerNotes",
		&T::marker_notes, "diagnosticCodes", &T::diagnostic_codes);
};

namespace shuba::ui {
namespace {
using shuba::core::Diagnostic;
using shuba::core::DiagnosticSeverity;
using shuba::core::OperationResultCategory;
using shuba::ui::startup_recovery_detail::StartupAttemptMarkerDto;
using shuba::ui::startup_recovery_detail::StartupExceptionReportDto;
using shuba::ui::startup_recovery_detail::StartupSafeModeReportDto;

[[nodiscard]] std::string path_text(const std::filesystem::path& path) {
	return path.generic_string();
}

[[nodiscard]] Diagnostic make_diagnostic(DiagnosticSeverity severity,
										 std::string code, std::string message,
										 std::string technical_details = {}) {
	return Diagnostic{.severity			 = severity,
					  .code				 = std::move(code),
					  .message			 = std::move(message),
					  .technical_details = std::move(technical_details)};
}

[[nodiscard]] Diagnostic filesystem_diagnostic(
	DiagnosticSeverity severity, std::string code, std::string message,
	const std::filesystem::path& path, const std::error_code& error) {
	std::string details = path_text(path);
	if (error)
		details += ": " + error.message();
	return make_diagnostic(severity, std::move(code), std::move(message),
						   std::move(details));
}

[[nodiscard]] StartupRecoveryFileResult make_file_failure(
	OperationResultCategory category, Diagnostic diagnostic) {
	return StartupRecoveryFileResult{.category	  = category,
									 .diagnostics = {std::move(diagnostic)}};
}

void append_diagnostics(std::vector<Diagnostic>& target,
						const std::vector<Diagnostic>& source) {
	for (const Diagnostic& diagnostic : source)
		target.push_back(diagnostic);
}

[[nodiscard]] StartupAttemptMarkerDto marker_dto(
	const StartupAttemptMarker& marker) {
	return StartupAttemptMarkerDto{
		.schema_version			 = marker.schema_version,
		.attempt_id				 = marker.attempt_id,
		.started_at				 = marker.started_at.count(),
		.app_version			 = marker.app_version,
		.platform				 = marker.platform,
		.stage					 = marker.stage,
		.retry_requested_by_user = marker.retry_requested_by_user,
		.previous_attempt_count	 = marker.previous_attempt_count,
		.notes					 = marker.notes};
}

[[nodiscard]] StartupAttemptMarker marker_from_dto(
	StartupAttemptMarkerDto dto) {
	return StartupAttemptMarker{
		.schema_version			 = dto.schema_version,
		.attempt_id				 = std::move(dto.attempt_id),
		.started_at				 = core::EpochMilliseconds{dto.started_at},
		.app_version			 = std::move(dto.app_version),
		.platform				 = std::move(dto.platform),
		.stage					 = std::move(dto.stage),
		.retry_requested_by_user = dto.retry_requested_by_user,
		.previous_attempt_count	 = dto.previous_attempt_count,
		.notes					 = std::move(dto.notes)};
}

[[nodiscard]] StartupExceptionReportDto exception_report_dto(
	const StartupExceptionReport& report) {
	return StartupExceptionReportDto{
		.schema_version	   = report.schema_version,
		.attempt_id		   = report.attempt_id,
		.captured_at	   = report.captured_at.count(),
		.app_version	   = report.app_version,
		.platform		   = report.platform,
		.stage			   = report.stage,
		.exception_kind	   = report.exception_kind,
		.message		   = report.message,
		.technical_details = report.technical_details};
}

[[nodiscard]] StartupSafeModeReportDto safe_mode_report_dto(
	const StartupSafeModeReport& report) {
	return StartupSafeModeReportDto{
		.schema_version			 = report.schema_version,
		.detected_at			 = report.detected_at.count(),
		.marker_file_present	 = report.marker_file_present,
		.marker_malformed		 = report.marker_malformed,
		.attempt_id				 = report.attempt_id,
		.started_at				 = report.started_at.count(),
		.app_version			 = report.app_version,
		.platform				 = report.platform,
		.stage					 = report.stage,
		.retry_requested_by_user = report.retry_requested_by_user,
		.previous_attempt_count	 = report.previous_attempt_count,
		.marker_notes			 = report.marker_notes,
		.diagnostic_codes		 = report.diagnostic_codes};
}

[[nodiscard]] std::optional<std::string> marker_json(
	const StartupAttemptMarker& marker, std::vector<Diagnostic>& diagnostics) {
	const StartupAttemptMarkerDto dto = marker_dto(marker);
	std::string output;
	const auto error = glz::write_json(dto, output);
	if (!error)
		return output;

	diagnostics.push_back(
		make_diagnostic(DiagnosticSeverity::WriteBlockingError,
						"startup_marker_serialization_failed",
						"Startup attempt marker could not be serialized.",
						glz::format_error(error, output)));
	return std::nullopt;
}

[[nodiscard]] std::optional<std::string> exception_report_json(
	const StartupExceptionReport& report,
	std::vector<Diagnostic>& diagnostics) {
	const StartupExceptionReportDto dto = exception_report_dto(report);
	std::string output;
	const auto error = glz::write_json(dto, output);
	if (!error)
		return output;

	diagnostics.push_back(
		make_diagnostic(DiagnosticSeverity::WriteBlockingError,
						"startup_exception_report_serialization_failed",
						"Startup exception report could not be serialized.",
						glz::format_error(error, output)));
	return std::nullopt;
}

[[nodiscard]] std::optional<std::string> safe_mode_report_json(
	const StartupSafeModeReport& report, std::vector<Diagnostic>& diagnostics) {
	const StartupSafeModeReportDto dto = safe_mode_report_dto(report);
	std::string output;
	const auto error = glz::write_json(dto, output);
	if (!error)
		return output;

	diagnostics.push_back(
		make_diagnostic(DiagnosticSeverity::WriteBlockingError,
						"startup_safe_mode_report_serialization_failed",
						"Startup safe-mode report could not be serialized.",
						glz::format_error(error, output)));
	return std::nullopt;
}

[[nodiscard]] std::vector<std::string> diagnostic_codes(
	const std::vector<Diagnostic>& diagnostics) {
	std::vector<std::string> codes;
	codes.reserve(diagnostics.size());
	for (const Diagnostic& diagnostic : diagnostics)
		codes.push_back(diagnostic.code);
	return codes;
}

[[nodiscard]] StartupSafeModeReport safe_mode_report_from_marker_result(
	const StartupAttemptMarkerReadResult& marker_result,
	core::EpochMilliseconds detected_at,
	const std::vector<Diagnostic>& diagnostics) {
	StartupSafeModeReport report{
		.detected_at		 = detected_at,
		.marker_file_present = marker_result.marker_file_present,
		.marker_malformed	 = marker_result.marker_malformed,
		.diagnostic_codes	 = diagnostic_codes(diagnostics)};
	if (marker_result.marker.has_value()) {
		report.attempt_id  = marker_result.marker->attempt_id;
		report.started_at  = marker_result.marker->started_at;
		report.app_version = marker_result.marker->app_version;
		report.platform	   = marker_result.marker->platform;
		report.stage	   = marker_result.marker->stage;
		report.retry_requested_by_user =
			marker_result.marker->retry_requested_by_user;
		report.previous_attempt_count =
			marker_result.marker->previous_attempt_count;
		report.marker_notes = marker_result.marker->notes;
	}
	return report;
}

[[nodiscard]] std::string marker_technical_details(
	const StartupAttemptMarker& marker) {
	std::ostringstream output;
	output << "attemptId=" << marker.attempt_id
		   << "; startedAt=" << marker.started_at.count()
		   << "; appVersion=" << marker.app_version
		   << "; platform=" << marker.platform << "; stage=" << marker.stage
		   << "; previousAttemptCount=" << marker.previous_attempt_count;
	return output.str();
}

[[nodiscard]] std::string safe_mode_technical_details(
	const StartupAttemptMarkerReadResult& marker_result) {
	if (marker_result.marker.has_value())
		return marker_technical_details(*marker_result.marker);
	if (marker_result.marker_malformed)
		return "Startup attempt marker exists but could not be parsed.";
	if (marker_result.marker_file_present)
		return "Startup attempt marker exists but could not be read.";
	return "Startup crash safe mode was requested without a marker file.";
}

[[nodiscard]] std::string exception_technical_details(
	const StartupExceptionReport& report) {
	std::ostringstream output;
	output << "attemptId=" << report.attempt_id
		   << "; capturedAt=" << report.captured_at.count()
		   << "; appVersion=" << report.app_version
		   << "; platform=" << report.platform << "; stage=" << report.stage
		   << "; exceptionKind=" << report.exception_kind
		   << "; message=" << report.message;
	if (!report.technical_details.empty())
		output << "; details=" << report.technical_details;
	return output.str();
}
}	 // namespace

bool StartupRecoveryFileResult::succeeded() const noexcept {
	return category == core::OperationResultCategory::Success;
}

bool StartupRecoveryFileResult::failed() const noexcept {
	return !succeeded();
}

StartupRecoveryFileResult::operator bool() const noexcept {
	return succeeded();
}

bool StartupAttemptMarkerReadResult::succeeded() const noexcept {
	return category == core::OperationResultCategory::Success;
}

bool StartupAttemptMarkerReadResult::failed() const noexcept {
	return !succeeded();
}

bool StartupAttemptMarkerReadResult::safe_mode_required() const noexcept {
	return marker_file_present;
}

StartupAttemptMarkerReadResult::operator bool() const noexcept {
	return succeeded();
}

std::filesystem::path startup_recovery_directory_path(
	const platform::AppPrivatePaths& paths) {
	return paths.active_catalog_root
		   / std::filesystem::path{
			   std::string{startup_recovery_directory_relative_path}};
}

std::filesystem::path active_startup_attempt_marker_path(
	const platform::AppPrivatePaths& paths) {
	return paths.active_catalog_root
		   / std::filesystem::path{
			   std::string{active_startup_attempt_marker_relative_path}};
}

std::filesystem::path last_startup_exception_report_path(
	const platform::AppPrivatePaths& paths) {
	return paths.active_catalog_root
		   / std::filesystem::path{
			   std::string{last_startup_exception_report_relative_path}};
}

std::filesystem::path last_startup_safe_mode_report_path(
	const platform::AppPrivatePaths& paths) {
	return paths.active_catalog_root
		   / std::filesystem::path{
			   std::string{last_startup_safe_mode_report_relative_path}};
}

StartupRecoveryFileResult write_startup_attempt_marker(
	const platform::AppPrivatePaths& paths,
	const StartupAttemptMarker& marker) {
	std::vector<Diagnostic> diagnostics;
	std::optional<std::string> json = marker_json(marker, diagnostics);
	if (!json.has_value()) {
		return StartupRecoveryFileResult{
			.category	 = OperationResultCategory::InternalError,
			.diagnostics = std::move(diagnostics)};
	}

	const std::filesystem::path directory =
		startup_recovery_directory_path(paths);
	std::error_code error;
	std::filesystem::create_directories(directory, error);
	if (error) {
		return make_file_failure(
			OperationResultCategory::DestinationUnavailable,
			filesystem_diagnostic(
				DiagnosticSeverity::WriteBlockingError,
				"startup_recovery_directory_unavailable",
				"Startup recovery directory could not be created.", directory,
				error));
	}

	const std::filesystem::path marker_path =
		active_startup_attempt_marker_path(paths);
	std::ofstream output{marker_path, std::ios::binary | std::ios::trunc};
	if (!output) {
		return make_file_failure(
			OperationResultCategory::TemporaryStorageFailure,
			make_diagnostic(
				DiagnosticSeverity::WriteBlockingError,
				"startup_marker_write_failed",
				"Startup attempt marker could not be opened for writing.",
				path_text(marker_path)));
	}

	output.write(json->data(), static_cast<std::streamsize>(json->size()));
	if (!output) {
		return make_file_failure(
			OperationResultCategory::TemporaryStorageFailure,
			make_diagnostic(DiagnosticSeverity::WriteBlockingError,
							"startup_marker_write_failed",
							"Startup attempt marker could not be written.",
							path_text(marker_path)));
	}

	output.flush();
	if (!output) {
		return make_file_failure(
			OperationResultCategory::TemporaryStorageFailure,
			make_diagnostic(DiagnosticSeverity::WriteBlockingError,
							"startup_marker_flush_failed",
							"Startup attempt marker could not be flushed.",
							path_text(marker_path)));
	}

	output.close();
	if (!output) {
		return make_file_failure(
			OperationResultCategory::TemporaryStorageFailure,
			make_diagnostic(
				DiagnosticSeverity::WriteBlockingError,
				"startup_marker_close_failed",
				"Startup attempt marker could not be closed after writing.",
				path_text(marker_path)));
	}

	return {};
}

StartupAttemptMarkerReadResult read_startup_attempt_marker(
	const platform::AppPrivatePaths& paths) {
	StartupAttemptMarkerReadResult result;
	const std::filesystem::path marker_path =
		active_startup_attempt_marker_path(paths);
	std::error_code error;
	const bool exists = std::filesystem::exists(marker_path, error);
	if (error) {
		result.marker_file_present = true;
		result.diagnostics.push_back(filesystem_diagnostic(
			DiagnosticSeverity::RecoverableWarning,
			"startup_marker_status_failed",
			"Startup attempt marker status could not be checked.", marker_path,
			error));
		return result;
	}
	if (!exists)
		return result;

	result.marker_file_present = true;
	std::ifstream input{marker_path, std::ios::binary};
	if (!input) {
		result.diagnostics.push_back(make_diagnostic(
			DiagnosticSeverity::RecoverableWarning,
			"startup_marker_read_failed",
			"Startup attempt marker could not be opened for reading.",
			path_text(marker_path)));
		return result;
	}

	std::ostringstream buffer;
	buffer << input.rdbuf();
	if (input.bad()) {
		result.diagnostics.push_back(make_diagnostic(
			DiagnosticSeverity::RecoverableWarning,
			"startup_marker_read_failed",
			"Startup attempt marker could not be read completely.",
			path_text(marker_path)));
		return result;
	}

	std::string input_text = buffer.str();
	StartupAttemptMarkerDto dto;
	const auto parse_error =
		glz::read<glz::opts{.error_on_unknown_keys = false}>(dto, input_text);
	if (parse_error) {
		result.marker_malformed = true;
		result.diagnostics.push_back(
			make_diagnostic(DiagnosticSeverity::RecoverableWarning,
							"startup_marker_parse_failed",
							"Startup attempt marker could not be parsed.",
							glz::format_error(parse_error, input_text)));
		return result;
	}

	if (dto.schema_version != startup_attempt_marker_schema_version) {
		result.marker_malformed = true;
		result.diagnostics.push_back(make_diagnostic(
			DiagnosticSeverity::RecoverableWarning,
			"startup_marker_schema_unsupported",
			"Startup attempt marker schema version is unsupported.",
			std::to_string(dto.schema_version)));
		return result;
	}

	result.marker = marker_from_dto(std::move(dto));
	return result;
}

StartupRecoveryFileResult update_startup_attempt_stage(
	const platform::AppPrivatePaths& paths, std::string stage) {
	StartupAttemptMarkerReadResult read_result =
		read_startup_attempt_marker(paths);
	StartupRecoveryFileResult result;
	append_diagnostics(result.diagnostics, read_result.diagnostics);
	if (!read_result.marker.has_value()) {
		if (read_result.marker_file_present) {
			result.category = OperationResultCategory::ValidationFailure;
			result.diagnostics.push_back(make_diagnostic(
				DiagnosticSeverity::ActionValidationError,
				"startup_marker_update_unavailable",
				"Startup attempt marker exists but cannot be updated.",
				path_text(active_startup_attempt_marker_path(paths))));
		}
		return result;
	}

	StartupAttemptMarker marker = std::move(*read_result.marker);
	marker.stage				= std::move(stage);
	StartupRecoveryFileResult written =
		write_startup_attempt_marker(paths, marker);
	result.category = written.category;
	append_diagnostics(result.diagnostics, written.diagnostics);
	return result;
}

StartupRecoveryFileResult clear_startup_attempt_marker(
	const platform::AppPrivatePaths& paths) {
	const std::filesystem::path marker_path =
		active_startup_attempt_marker_path(paths);
	std::error_code error;
	std::filesystem::remove(marker_path, error);
	if (error) {
		return make_file_failure(
			OperationResultCategory::TemporaryStorageFailure,
			filesystem_diagnostic(
				DiagnosticSeverity::WriteBlockingError,
				"startup_marker_clear_failed",
				"Startup attempt marker could not be removed.", marker_path,
				error));
	}
	return {};
}

StartupRecoveryFileResult write_startup_exception_report(
	const platform::AppPrivatePaths& paths,
	const StartupExceptionReport& report) {
	std::vector<Diagnostic> diagnostics;
	std::optional<std::string> json =
		exception_report_json(report, diagnostics);
	if (!json.has_value()) {
		return StartupRecoveryFileResult{
			.category	 = OperationResultCategory::InternalError,
			.diagnostics = std::move(diagnostics)};
	}

	const std::filesystem::path directory =
		startup_recovery_directory_path(paths);
	std::error_code error;
	std::filesystem::create_directories(directory, error);
	if (error) {
		return make_file_failure(
			OperationResultCategory::DestinationUnavailable,
			filesystem_diagnostic(
				DiagnosticSeverity::WriteBlockingError,
				"startup_recovery_directory_unavailable",
				"Startup recovery directory could not be created.", directory,
				error));
	}

	const std::filesystem::path report_path =
		last_startup_exception_report_path(paths);
	std::ofstream output{report_path, std::ios::binary | std::ios::trunc};
	if (!output) {
		return make_file_failure(
			OperationResultCategory::TemporaryStorageFailure,
			make_diagnostic(
				DiagnosticSeverity::WriteBlockingError,
				"startup_exception_report_write_failed",
				"Startup exception report could not be opened for writing.",
				path_text(report_path)));
	}

	output.write(json->data(), static_cast<std::streamsize>(json->size()));
	if (!output) {
		return make_file_failure(
			OperationResultCategory::TemporaryStorageFailure,
			make_diagnostic(DiagnosticSeverity::WriteBlockingError,
							"startup_exception_report_write_failed",
							"Startup exception report could not be written.",
							path_text(report_path)));
	}

	output.flush();
	if (!output) {
		return make_file_failure(
			OperationResultCategory::TemporaryStorageFailure,
			make_diagnostic(DiagnosticSeverity::WriteBlockingError,
							"startup_exception_report_flush_failed",
							"Startup exception report could not be flushed.",
							path_text(report_path)));
	}

	output.close();
	if (!output) {
		return make_file_failure(
			OperationResultCategory::TemporaryStorageFailure,
			make_diagnostic(
				DiagnosticSeverity::WriteBlockingError,
				"startup_exception_report_close_failed",
				"Startup exception report could not be closed after writing.",
				path_text(report_path)));
	}

	return {};
}

StartupRecoveryFileResult write_startup_safe_mode_report(
	const platform::AppPrivatePaths& paths,
	const StartupSafeModeReport& report) {
	std::vector<Diagnostic> diagnostics;
	std::optional<std::string> json =
		safe_mode_report_json(report, diagnostics);
	if (!json.has_value()) {
		return StartupRecoveryFileResult{
			.category	 = OperationResultCategory::InternalError,
			.diagnostics = std::move(diagnostics)};
	}

	const std::filesystem::path directory =
		startup_recovery_directory_path(paths);
	std::error_code error;
	std::filesystem::create_directories(directory, error);
	if (error) {
		return make_file_failure(
			OperationResultCategory::DestinationUnavailable,
			filesystem_diagnostic(
				DiagnosticSeverity::WriteBlockingError,
				"startup_recovery_directory_unavailable",
				"Startup recovery directory could not be created.", directory,
				error));
	}

	const std::filesystem::path report_path =
		last_startup_safe_mode_report_path(paths);
	std::ofstream output{report_path, std::ios::binary | std::ios::trunc};
	if (!output) {
		return make_file_failure(
			OperationResultCategory::TemporaryStorageFailure,
			make_diagnostic(
				DiagnosticSeverity::WriteBlockingError,
				"startup_safe_mode_report_write_failed",
				"Startup safe-mode report could not be opened for writing.",
				path_text(report_path)));
	}

	output.write(json->data(), static_cast<std::streamsize>(json->size()));
	if (!output) {
		return make_file_failure(
			OperationResultCategory::TemporaryStorageFailure,
			make_diagnostic(DiagnosticSeverity::WriteBlockingError,
							"startup_safe_mode_report_write_failed",
							"Startup safe-mode report could not be written.",
							path_text(report_path)));
	}

	output.flush();
	if (!output) {
		return make_file_failure(
			OperationResultCategory::TemporaryStorageFailure,
			make_diagnostic(DiagnosticSeverity::WriteBlockingError,
							"startup_safe_mode_report_flush_failed",
							"Startup safe-mode report could not be flushed.",
							path_text(report_path)));
	}

	output.close();
	if (!output) {
		return make_file_failure(
			OperationResultCategory::TemporaryStorageFailure,
			make_diagnostic(
				DiagnosticSeverity::WriteBlockingError,
				"startup_safe_mode_report_close_failed",
				"Startup safe-mode report could not be closed after writing.",
				path_text(report_path)));
	}

	return {};
}

CatalogSessionState make_synthetic_fatal_startup_session(
	StartupSyntheticFatalSessionRequest request) {
	CatalogSessionState state;
	state.paths					  = std::move(request.paths);
	state.source				  = request.source;
	state.load_status			  = persistence::CatalogLoadStatus::Fatal;
	state.load_result.load_status = persistence::CatalogLoadStatus::Fatal;
	state.startup_diagnostics	  = std::move(request.diagnostics);
	return state;
}

CatalogSessionState make_startup_crash_safe_mode_session(
	platform::AppPrivatePaths paths,
	StartupAttemptMarkerReadResult marker_result,
	core::EpochMilliseconds detected_at) {
	std::vector<Diagnostic> diagnostics = std::move(marker_result.diagnostics);
	diagnostics.push_back(make_diagnostic(
		DiagnosticSeverity::FatalCatalogError, "startup_attempt_unresolved",
		"Previous launch stopped before startup completed. Normal catalog load "
		"was skipped for safe-mode recovery.",
		safe_mode_technical_details(marker_result)));
	StartupSafeModeReport report = safe_mode_report_from_marker_result(
		marker_result, detected_at, diagnostics);
	StartupRecoveryFileResult written =
		write_startup_safe_mode_report(paths, report);
	append_diagnostics(diagnostics, written.diagnostics);
	return make_synthetic_fatal_startup_session(
		StartupSyntheticFatalSessionRequest{
			.paths		 = std::move(paths),
			.source		 = CatalogSessionStartupSource::StartupCrashSafeMode,
			.diagnostics = std::move(diagnostics)});
}

CatalogSessionState make_startup_exception_session(
	StartupExceptionSessionRequest request) {
	std::vector<Diagnostic> diagnostics = std::move(request.diagnostics);
	StartupAttemptMarkerReadResult marker_result =
		read_startup_attempt_marker(request.paths);
	append_diagnostics(diagnostics, marker_result.diagnostics);

	StartupExceptionReport report{
		.attempt_id		   = {},
		.captured_at	   = request.captured_at,
		.app_version	   = std::move(request.app_version),
		.platform		   = std::move(request.platform),
		.stage			   = std::move(request.fallback_stage),
		.exception_kind	   = std::move(request.exception_kind),
		.message		   = std::move(request.message),
		.technical_details = std::move(request.technical_details)};
	if (marker_result.marker.has_value()) {
		report.attempt_id = marker_result.marker->attempt_id;
		report.stage	  = marker_result.marker->stage;
	}

	StartupRecoveryFileResult written =
		write_startup_exception_report(request.paths, report);
	append_diagnostics(diagnostics, written.diagnostics);
	diagnostics.push_back(make_diagnostic(
		DiagnosticSeverity::FatalCatalogError, "startup_exception_captured",
		"Startup failed before catalog browsing could open. A local startup "
		"exception report was written for diagnostic export.",
		exception_technical_details(report)));

	return make_synthetic_fatal_startup_session(
		StartupSyntheticFatalSessionRequest{
			.paths		 = std::move(request.paths),
			.source		 = CatalogSessionStartupSource::StartupException,
			.diagnostics = std::move(diagnostics)});
}
}	 // namespace shuba::ui
