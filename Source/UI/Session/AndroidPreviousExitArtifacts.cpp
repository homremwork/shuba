#include "UI/Session/AndroidPreviousExitArtifacts.hpp"

#include <glaze/json.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace shuba::ui::android_previous_exit_artifacts_detail {
struct AndroidPreviousExitReportDto final {
	int schema_version{};
	std::int64_t captured_at{};
	bool record_available{};
	std::int64_t record_timestamp{};
	int process_id{};
	std::string process_name;
	int reason{};
	std::string reason_name;
	int status{};
	int importance{};
	std::string description;
	bool native_crash{};
	bool java_crash{};
	bool anr{};
	bool actionable_for_startup{};
	bool trace_requested{};
	bool trace_available{};
	bool trace_captured{};
	std::uint64_t trace_byte_count{};
	std::string trace_relative_path;
	std::string trace_error_text;
	std::vector<std::string> diagnostic_codes;
};
}	 // namespace shuba::ui::android_previous_exit_artifacts_detail

template<>
struct glz::meta<shuba::ui::android_previous_exit_artifacts_detail::
					 AndroidPreviousExitReportDto> {
	using T = shuba::ui::android_previous_exit_artifacts_detail::
		AndroidPreviousExitReportDto;
	static constexpr auto value = object(
		"schemaVersion", &T::schema_version, "capturedAt", &T::captured_at,
		"recordAvailable", &T::record_available, "recordTimestamp",
		&T::record_timestamp, "processId", &T::process_id, "processName",
		&T::process_name, "reason", &T::reason, "reasonName", &T::reason_name,
		"status", &T::status, "importance", &T::importance, "description",
		&T::description, "nativeCrash", &T::native_crash, "javaCrash",
		&T::java_crash, "anr", &T::anr, "actionableForStartup",
		&T::actionable_for_startup, "traceRequested", &T::trace_requested,
		"traceAvailable", &T::trace_available, "traceCaptured",
		&T::trace_captured, "traceByteCount", &T::trace_byte_count,
		"traceRelativePath", &T::trace_relative_path, "traceErrorText",
		&T::trace_error_text, "diagnosticCodes", &T::diagnostic_codes);
};

namespace shuba::ui {
namespace {
using shuba::core::Diagnostic;
using shuba::core::DiagnosticSeverity;
using shuba::core::OperationResultCategory;
using shuba::ui::android_previous_exit_artifacts_detail::
	AndroidPreviousExitReportDto;

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

void append_diagnostics(std::vector<Diagnostic>& target,
						const std::vector<Diagnostic>& source) {
	for (const Diagnostic& diagnostic : source)
		target.push_back(diagnostic);
}

[[nodiscard]] StartupRecoveryFileResult make_file_failure(
	OperationResultCategory category, Diagnostic diagnostic) {
	return StartupRecoveryFileResult{.category	  = category,
									 .diagnostics = {std::move(diagnostic)}};
}

[[nodiscard]] std::vector<std::string> diagnostic_codes(
	const std::vector<Diagnostic>& diagnostics) {
	std::vector<std::string> codes;
	codes.reserve(diagnostics.size());
	for (const Diagnostic& diagnostic : diagnostics)
		codes.push_back(diagnostic.code);
	return codes;
}

[[nodiscard]] std::string trace_relative_path(
	const platform::AndroidPreviousExitInfo& info) {
	return info.trace_captured
			   ? std::string{android_previous_exit_trace_relative_path}
			   : std::string{};
}

[[nodiscard]] AndroidPreviousExitReportDto report_dto(
	const platform::AndroidPreviousExitInfo& info,
	const std::vector<Diagnostic>& diagnostics) {
	return AndroidPreviousExitReportDto{
		.schema_version			= android_previous_exit_report_schema_version,
		.captured_at			= info.captured_at.count(),
		.record_available		= info.record_available,
		.record_timestamp		= info.record_timestamp.count(),
		.process_id				= info.process_id,
		.process_name			= info.process_name,
		.reason					= info.reason,
		.reason_name			= info.reason_name,
		.status					= info.status,
		.importance				= info.importance,
		.description			= info.description,
		.native_crash			= info.native_crash,
		.java_crash				= info.java_crash,
		.anr					= info.anr,
		.actionable_for_startup = info.actionable_for_startup,
		.trace_requested		= info.trace_requested,
		.trace_available		= info.trace_available,
		.trace_captured			= info.trace_captured,
		.trace_byte_count		= info.trace_byte_count,
		.trace_relative_path	= trace_relative_path(info),
		.trace_error_text		= info.trace_error_text,
		.diagnostic_codes		= diagnostic_codes(diagnostics)};
}

[[nodiscard]] std::optional<std::string> report_json(
	const platform::AndroidPreviousExitInfo& info,
	std::vector<Diagnostic>& diagnostics) {
	const AndroidPreviousExitReportDto dto = report_dto(info, diagnostics);
	std::string output;
	const auto error = glz::write_json(dto, output);
	if (!error)
		return output;

	diagnostics.push_back(
		make_diagnostic(DiagnosticSeverity::WriteBlockingError,
						"android_previous_exit_report_serialization_failed",
						"Android previous-exit report could not be serialized.",
						glz::format_error(error, output)));
	return std::nullopt;
}

[[nodiscard]] StartupRecoveryFileResult write_report_json(
	const platform::AppPrivatePaths& paths, std::string_view json) {
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
		android_previous_exit_report_path(paths);
	std::ofstream output{report_path, std::ios::binary | std::ios::trunc};
	if (!output) {
		return make_file_failure(
			OperationResultCategory::TemporaryStorageFailure,
			make_diagnostic(
				DiagnosticSeverity::WriteBlockingError,
				"android_previous_exit_report_write_failed",
				"Android previous-exit report could not be opened for writing.",
				path_text(report_path)));
	}

	output.write(json.data(), static_cast<std::streamsize>(json.size()));
	if (!output) {
		return make_file_failure(
			OperationResultCategory::TemporaryStorageFailure,
			make_diagnostic(
				DiagnosticSeverity::WriteBlockingError,
				"android_previous_exit_report_write_failed",
				"Android previous-exit report could not be written.",
				path_text(report_path)));
	}

	output.flush();
	if (!output) {
		return make_file_failure(
			OperationResultCategory::TemporaryStorageFailure,
			make_diagnostic(
				DiagnosticSeverity::WriteBlockingError,
				"android_previous_exit_report_flush_failed",
				"Android previous-exit report could not be flushed.",
				path_text(report_path)));
	}

	output.close();
	if (!output) {
		return make_file_failure(
			OperationResultCategory::TemporaryStorageFailure,
			make_diagnostic(DiagnosticSeverity::WriteBlockingError,
							"android_previous_exit_report_close_failed",
							"Android previous-exit report could not be closed "
							"after writing.",
							path_text(report_path)));
	}

	return {};
}
}	 // namespace

std::filesystem::path android_previous_exit_report_path(
	const platform::AppPrivatePaths& paths) {
	return paths.active_catalog_root
		   / std::filesystem::path{
			   std::string{android_previous_exit_report_relative_path}};
}

std::filesystem::path android_previous_exit_trace_path(
	const platform::AppPrivatePaths& paths) {
	return paths.active_catalog_root
		   / std::filesystem::path{
			   std::string{android_previous_exit_trace_relative_path}};
}

StartupRecoveryFileResult capture_android_previous_exit_artifacts(
	const AndroidPreviousExitArtifactCaptureRequest& request) {
	platform::AndroidPreviousExitQueryRequest query{
		.trace_output_path = android_previous_exit_trace_path(request.paths),
		.captured_at	   = request.clock.now(),
		.capture_trace	   = request.capture_trace};
	platform::PlatformValueResult<platform::AndroidPreviousExitInfo>
		query_result = request.service.query_previous_exit(query);

	std::vector<Diagnostic> diagnostics;
	append_diagnostics(diagnostics, query_result.diagnostics);
	platform::AndroidPreviousExitInfo info;
	if (query_result.value.has_value()) {
		info = std::move(*query_result.value);
	} else {
		info.captured_at	 = query.captured_at;
		info.reason_name	 = std::string{platform::android_exit_reason_name(
			platform::android_application_exit_reason_unknown)};
		info.trace_requested = query.capture_trace;
		info.trace_path		 = query.trace_output_path;
		diagnostics.push_back(make_diagnostic(
			DiagnosticSeverity::RecoverableWarning,
			"android_previous_exit_query_failed",
			"Android previous-exit query did not return a diagnostic value.",
			std::string{core::to_string(query_result.category)}));
	}

	std::optional<std::string> json = report_json(info, diagnostics);
	if (!json.has_value()) {
		return StartupRecoveryFileResult{
			.category	 = OperationResultCategory::InternalError,
			.diagnostics = std::move(diagnostics)};
	}

	StartupRecoveryFileResult written = write_report_json(request.paths, *json);
	append_diagnostics(diagnostics, written.diagnostics);
	return StartupRecoveryFileResult{.category	  = written.category,
									 .diagnostics = std::move(diagnostics)};
}
}	 // namespace shuba::ui
