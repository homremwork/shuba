#pragma once

#include "Core/Clock.hpp"
#include "Platform/AndroidPreviousExit.hpp"
#include "Platform/PlatformServices.hpp"
#include "UI/Session/StartupRecoverySession.hpp"

#include <filesystem>
#include <string_view>

namespace shuba::ui {
inline constexpr int android_previous_exit_report_schema_version = 1;
inline constexpr std::string_view android_previous_exit_report_relative_path{
	"recovery/startup/android-previous-exit.json"};
inline constexpr std::string_view android_previous_exit_trace_relative_path{
	"recovery/startup/android-previous-exit-trace.bin"};

struct AndroidPreviousExitArtifactCaptureRequest final {
	const platform::AppPrivatePaths& paths;
	platform::AndroidPreviousExitService& service;
	core::Clock& clock;
	bool capture_trace{true};
};

[[nodiscard]] std::filesystem::path android_previous_exit_report_path(
	const platform::AppPrivatePaths& paths);
[[nodiscard]] std::filesystem::path android_previous_exit_trace_path(
	const platform::AppPrivatePaths& paths);
[[nodiscard]] StartupRecoveryFileResult capture_android_previous_exit_artifacts(
	const AndroidPreviousExitArtifactCaptureRequest& request);
}	 // namespace shuba::ui
