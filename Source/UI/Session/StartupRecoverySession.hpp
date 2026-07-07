#pragma once

#include "Core/Clock.hpp"
#include "Core/Result.hpp"
#include "Platform/PlatformServices.hpp"
#include "UI/Session/CatalogSessionState.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace shuba::ui {
inline constexpr int startup_attempt_marker_schema_version = 1;
inline constexpr std::string_view startup_recovery_directory_relative_path{
	"recovery/startup"};
inline constexpr std::string_view active_startup_attempt_marker_relative_path{
	"recovery/startup/active-startup-attempt.json"};
inline constexpr int startup_exception_report_schema_version = 1;
inline constexpr std::string_view last_startup_exception_report_relative_path{
	"recovery/startup/last-startup-exception.json"};
inline constexpr int startup_safe_mode_report_schema_version = 1;
inline constexpr std::string_view last_startup_safe_mode_report_relative_path{
	"recovery/startup/last-safe-mode-report.json"};

struct StartupAttemptMarker final {
	int schema_version{startup_attempt_marker_schema_version};
	std::string attempt_id;
	core::EpochMilliseconds started_at{};
	std::string app_version;
	std::string platform;
	std::string stage;
	bool retry_requested_by_user{};
	std::uint64_t previous_attempt_count{};
	std::vector<std::string> notes;

	friend bool operator==(const StartupAttemptMarker&,
						   const StartupAttemptMarker&) = default;
};

struct StartupExceptionReport final {
	int schema_version{startup_exception_report_schema_version};
	std::string attempt_id;
	core::EpochMilliseconds captured_at{};
	std::string app_version;
	std::string platform;
	std::string stage;
	std::string exception_kind;
	std::string message;
	std::string technical_details;

	friend bool operator==(const StartupExceptionReport&,
						   const StartupExceptionReport&) = default;
};

struct StartupSafeModeReport final {
	int schema_version{startup_safe_mode_report_schema_version};
	core::EpochMilliseconds detected_at{};
	bool marker_file_present{};
	bool marker_malformed{};
	std::string attempt_id;
	core::EpochMilliseconds started_at{};
	std::string app_version;
	std::string platform;
	std::string stage;
	bool retry_requested_by_user{};
	std::uint64_t previous_attempt_count{};
	std::vector<std::string> marker_notes;
	std::vector<std::string> diagnostic_codes;

	friend bool operator==(const StartupSafeModeReport&,
						   const StartupSafeModeReport&) = default;
};

struct StartupRecoveryFileResult final {
	core::OperationResultCategory category{
		core::OperationResultCategory::Success};
	std::vector<core::Diagnostic> diagnostics;

	[[nodiscard]] bool succeeded() const noexcept;
	[[nodiscard]] bool failed() const noexcept;
	[[nodiscard]] explicit operator bool() const noexcept;
};

struct StartupAttemptMarkerReadResult final {
	core::OperationResultCategory category{
		core::OperationResultCategory::Success};
	std::vector<core::Diagnostic> diagnostics;
	std::optional<StartupAttemptMarker> marker;
	bool marker_file_present{};
	bool marker_malformed{};

	[[nodiscard]] bool succeeded() const noexcept;
	[[nodiscard]] bool failed() const noexcept;
	[[nodiscard]] bool safe_mode_required() const noexcept;
	[[nodiscard]] explicit operator bool() const noexcept;
};

struct StartupSyntheticFatalSessionRequest final {
	platform::AppPrivatePaths paths;
	CatalogSessionStartupSource source{
		CatalogSessionStartupSource::StartupCrashSafeMode};
	std::vector<core::Diagnostic> diagnostics;
};

struct StartupExceptionSessionRequest final {
	platform::AppPrivatePaths paths;
	core::EpochMilliseconds captured_at{};
	std::string app_version;
	std::string platform;
	std::string fallback_stage{"startup"};
	std::string exception_kind;
	std::string message;
	std::string technical_details;
	std::vector<core::Diagnostic> diagnostics;
};

[[nodiscard]] std::filesystem::path startup_recovery_directory_path(
	const platform::AppPrivatePaths& paths);
[[nodiscard]] std::filesystem::path active_startup_attempt_marker_path(
	const platform::AppPrivatePaths& paths);
[[nodiscard]] std::filesystem::path last_startup_exception_report_path(
	const platform::AppPrivatePaths& paths);
[[nodiscard]] std::filesystem::path last_startup_safe_mode_report_path(
	const platform::AppPrivatePaths& paths);

[[nodiscard]] StartupRecoveryFileResult write_startup_attempt_marker(
	const platform::AppPrivatePaths& paths, const StartupAttemptMarker& marker);
[[nodiscard]] StartupAttemptMarkerReadResult read_startup_attempt_marker(
	const platform::AppPrivatePaths& paths);
[[nodiscard]] StartupRecoveryFileResult update_startup_attempt_stage(
	const platform::AppPrivatePaths& paths, std::string stage);
[[nodiscard]] StartupRecoveryFileResult clear_startup_attempt_marker(
	const platform::AppPrivatePaths& paths);
[[nodiscard]] StartupRecoveryFileResult write_startup_exception_report(
	const platform::AppPrivatePaths& paths,
	const StartupExceptionReport& report);
[[nodiscard]] StartupRecoveryFileResult write_startup_safe_mode_report(
	const platform::AppPrivatePaths& paths,
	const StartupSafeModeReport& report);

[[nodiscard]] CatalogSessionState make_synthetic_fatal_startup_session(
	StartupSyntheticFatalSessionRequest request);
[[nodiscard]] CatalogSessionState make_startup_crash_safe_mode_session(
	platform::AppPrivatePaths paths,
	StartupAttemptMarkerReadResult marker_result,
	core::EpochMilliseconds detected_at);
[[nodiscard]] CatalogSessionState make_startup_exception_session(
	StartupExceptionSessionRequest request);
}	 // namespace shuba::ui
