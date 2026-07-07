#include "Platform/AndroidPreviousExit.hpp"

#include <string>
#include <utility>
#include <vector>

namespace shuba::platform {
namespace {
[[nodiscard]] core::Diagnostic make_diagnostic(
	core::DiagnosticSeverity severity, std::string code, std::string message,
	std::string technical_details = {}) {
	return core::Diagnostic{.severity		   = severity,
							.code			   = std::move(code),
							.message		   = std::move(message),
							.technical_details = std::move(technical_details)};
}
}	 // namespace

std::string_view android_exit_reason_name(int reason) noexcept {
	switch (reason) {
		case android_application_exit_reason_unknown:
			return "REASON_UNKNOWN";
		case android_application_exit_reason_exit_self:
			return "REASON_EXIT_SELF";
		case android_application_exit_reason_signaled:
			return "REASON_SIGNALED";
		case android_application_exit_reason_low_memory:
			return "REASON_LOW_MEMORY";
		case android_application_exit_reason_crash:
			return "REASON_CRASH";
		case android_application_exit_reason_crash_native:
			return "REASON_CRASH_NATIVE";
		case android_application_exit_reason_anr:
			return "REASON_ANR";
		case android_application_exit_reason_initialization_failure:
			return "REASON_INITIALIZATION_FAILURE";
		case android_application_exit_reason_permission_change:
			return "REASON_PERMISSION_CHANGE";
		case android_application_exit_reason_excessive_resource_usage:
			return "REASON_EXCESSIVE_RESOURCE_USAGE";
		case android_application_exit_reason_user_requested:
			return "REASON_USER_REQUESTED";
		case android_application_exit_reason_user_stopped:
			return "REASON_USER_STOPPED";
		case android_application_exit_reason_dependency_died:
			return "REASON_DEPENDENCY_DIED";
		case android_application_exit_reason_other:
			return "REASON_OTHER";
		case android_application_exit_reason_freezer:
			return "REASON_FREEZER";
	}

	return "REASON_UNRECOGNIZED";
}

PlatformValueResult<AndroidPreviousExitInfo>
NoOpAndroidPreviousExitService::query_previous_exit(
	const AndroidPreviousExitQueryRequest& request) {
	AndroidPreviousExitInfo info{
		.captured_at	 = request.captured_at,
		.reason_name	 = std::string{android_exit_reason_name(
			android_application_exit_reason_unknown)},
		.trace_requested = request.capture_trace,
		.trace_path		 = request.trace_output_path};
	std::vector<core::Diagnostic> diagnostics{make_diagnostic(
		core::DiagnosticSeverity::RecoverableWarning,
		"android-previous-exit-unavailable",
		"Android previous-exit diagnostics are unavailable on this "
		"platform.")};
	return platform_value_success(info, diagnostics);
}
}	 // namespace shuba::platform
