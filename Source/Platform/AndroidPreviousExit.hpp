#pragma once

#include "Core/Clock.hpp"
#include "Platform/PlatformServices.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace shuba::platform {
inline constexpr int android_application_exit_reason_unknown				= 0;
inline constexpr int android_application_exit_reason_exit_self				= 1;
inline constexpr int android_application_exit_reason_signaled				= 2;
inline constexpr int android_application_exit_reason_low_memory				= 3;
inline constexpr int android_application_exit_reason_crash					= 4;
inline constexpr int android_application_exit_reason_crash_native			= 5;
inline constexpr int android_application_exit_reason_anr					= 6;
inline constexpr int android_application_exit_reason_initialization_failure = 7;
inline constexpr int android_application_exit_reason_permission_change		= 8;
inline constexpr int android_application_exit_reason_excessive_resource_usage =
	9;
inline constexpr int android_application_exit_reason_user_requested	 = 10;
inline constexpr int android_application_exit_reason_user_stopped	 = 11;
inline constexpr int android_application_exit_reason_dependency_died = 12;
inline constexpr int android_application_exit_reason_other			 = 13;
inline constexpr int android_application_exit_reason_freezer		 = 14;

struct AndroidPreviousExitQueryRequest final {
	std::filesystem::path trace_output_path;
	core::EpochMilliseconds captured_at{};
	bool capture_trace{true};

	friend bool operator==(const AndroidPreviousExitQueryRequest&,
						   const AndroidPreviousExitQueryRequest&) = default;
};

struct AndroidPreviousExitInfo final {
	bool record_available{};
	core::EpochMilliseconds captured_at{};
	core::EpochMilliseconds record_timestamp{};
	int process_id{};
	std::string process_name;
	int reason{android_application_exit_reason_unknown};
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
	std::filesystem::path trace_path;
	std::string trace_error_text;

	friend bool operator==(const AndroidPreviousExitInfo&,
						   const AndroidPreviousExitInfo&) = default;
};

[[nodiscard]] std::string_view android_exit_reason_name(int reason) noexcept;

class AndroidPreviousExitService {
public:
	AndroidPreviousExitService()								  = default;
	AndroidPreviousExitService(const AndroidPreviousExitService&) = default;
	AndroidPreviousExitService& operator=(const AndroidPreviousExitService&) =
		default;
	AndroidPreviousExitService(AndroidPreviousExitService&&) noexcept = default;
	AndroidPreviousExitService& operator=(
		AndroidPreviousExitService&&) noexcept = default;
	virtual ~AndroidPreviousExitService()	   = default;

	[[nodiscard]] virtual PlatformValueResult<AndroidPreviousExitInfo>
	query_previous_exit(const AndroidPreviousExitQueryRequest& request) = 0;
};

class NoOpAndroidPreviousExitService final : public AndroidPreviousExitService {
public:
	[[nodiscard]] PlatformValueResult<AndroidPreviousExitInfo>
	query_previous_exit(
		const AndroidPreviousExitQueryRequest& request) override;
};
}	 // namespace shuba::platform
