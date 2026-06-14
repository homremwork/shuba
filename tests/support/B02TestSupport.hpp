#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace shuba::test_support::b02 {
using EpochMilliseconds = std::chrono::milliseconds;

class FakeClock final {
public:
	explicit FakeClock(EpochMilliseconds FallbackNow = EpochMilliseconds{0})
		: fallback_time(FallbackNow) {}

	[[nodiscard]] EpochMilliseconds fallback_now() const noexcept {
		return fallback_time;
	}

	void set_fallback(EpochMilliseconds Value) noexcept {
		fallback_time = Value;
	}

	void script(std::initializer_list<EpochMilliseconds> Values) {
		scripted_times.assign(Values.begin(), Values.end());
	}

	void append(EpochMilliseconds Value) { scripted_times.push_back(Value); }

	[[nodiscard]] bool has_scripted_values() const noexcept {
		return !scripted_times.empty();
	}

	[[nodiscard]] EpochMilliseconds now() {
		if (scripted_times.empty())
			return fallback_time;

		auto value = scripted_times.front();
		scripted_times.pop_front();
		return value;
	}

private:
	EpochMilliseconds fallback_time{};
	std::deque<EpochMilliseconds> scripted_times;
};

class FakeIdentifierSource final {
public:
	void script_entity_identifiers(std::initializer_list<std::string> Values) {
		entity_identifiers.assign(Values.begin(), Values.end());
	}

	void script_operation_identifiers(
		std::initializer_list<std::string> Values) {
		operation_identifiers.assign(Values.begin(), Values.end());
	}

	[[nodiscard]] bool has_entity_identifiers() const noexcept {
		return !entity_identifiers.empty();
	}

	[[nodiscard]] bool has_operation_identifiers() const noexcept {
		return !operation_identifiers.empty();
	}

	[[nodiscard]] std::string next_entity_identifier() {
		return pop_next(entity_identifiers, "entity identifier");
	}

	[[nodiscard]] std::string next_operation_identifier() {
		return pop_next(operation_identifiers, "operation identifier");
	}

private:
	[[nodiscard]] static std::string pop_next(std::deque<std::string>& Values,
											  std::string_view Label) {
		if (Values.empty())
			throw std::logic_error("FakeIdentifierSource exhausted scripted "
								   + std::string(Label) + " values");

		auto value = std::move(Values.front());
		Values.pop_front();
		return value;
	}

	std::deque<std::string> entity_identifiers;
	std::deque<std::string> operation_identifiers;
};

struct OperationProgressEvent final {
	std::string operation_id;
	std::string operation_type;
	std::string phase;
	std::optional<std::uint64_t> completed_units;
	std::optional<std::uint64_t> total_units;
	std::string message;
	bool cancellable{};
};

class ProgressCollector final {
public:
	void record(OperationProgressEvent Event) {
		recorded_events.push_back(std::move(Event));
	}

	void clear() noexcept { recorded_events.clear(); }

	[[nodiscard]] const std::vector<OperationProgressEvent>& events()
		const noexcept {
		return recorded_events;
	}

private:
	std::vector<OperationProgressEvent> recorded_events;
};

enum class CleanupPolicy : std::uint8_t {
	Keep,
	RemoveOnDestruction,
};

class FakePathProvider final {
public:
	[[nodiscard]] static FakePathProvider create_under_system_temp(
		std::string LeafName) {
		return FakePathProvider(
			std::filesystem::temp_directory_path() / std::move(LeafName),
			CleanupPolicy::RemoveOnDestruction);
	}

	explicit FakePathProvider(
		std::filesystem::path RootPath,
		CleanupPolicy CleanupPolicyValue = CleanupPolicy::Keep)
		: root_path(std::move(RootPath)), cleanup_policy(CleanupPolicyValue) {
		reset();
	}

	FakePathProvider(const FakePathProvider&)			 = delete;
	FakePathProvider& operator=(const FakePathProvider&) = delete;

	FakePathProvider(FakePathProvider&& Other) noexcept
		: root_path(std::move(Other.root_path))
		, cleanup_policy(Other.cleanup_policy) {
		Other.cleanup_policy = CleanupPolicy::Keep;
	}

	FakePathProvider& operator=(FakePathProvider&& Other) noexcept {
		if (this != &Other) {
			cleanup();
			root_path			 = std::move(Other.root_path);
			cleanup_policy		 = Other.cleanup_policy;
			Other.cleanup_policy = CleanupPolicy::Keep;
		}

		return *this;
	}

	~FakePathProvider() { cleanup(); }

	void reset() {
		assert_safe_root();
		std::filesystem::remove_all(root_path);
		std::filesystem::create_directories(temporary_root());
		std::filesystem::create_directories(staged_content_root());
		std::filesystem::create_directories(media_root());
	}

	[[nodiscard]] const std::filesystem::path& root() const noexcept {
		return root_path;
	}

	[[nodiscard]] std::filesystem::path app_private_root() const {
		return root_path / "app-private";
	}

	[[nodiscard]] std::filesystem::path active_catalog_root() const {
		return app_private_root() / "active-catalog";
	}

	[[nodiscard]] std::filesystem::path temporary_root() const {
		return app_private_root() / "tmp";
	}

	[[nodiscard]] std::filesystem::path staged_content_root() const {
		return temporary_root() / "staged-content";
	}

	[[nodiscard]] std::filesystem::path media_root() const {
		return active_catalog_root() / "media" / "photos";
	}

private:
	void cleanup() noexcept {
		if (cleanup_policy == CleanupPolicy::Keep || root_path.empty())
			return;

		std::error_code ignored;
		std::filesystem::remove_all(root_path, ignored);
	}

	void assert_safe_root() const {
		const auto leaf_name = root_path.filename().string();
		if (root_path.empty() || root_path == root_path.root_path()
			|| !leaf_name.starts_with("shuba-")) {
			throw std::invalid_argument(
				"FakePathProvider roots must be non-root paths named with the "
				"shuba- prefix");
		}
	}

	std::filesystem::path root_path;
	CleanupPolicy cleanup_policy{CleanupPolicy::Keep};
};

struct FakePlatformResponse final {
	bool success{};
	std::filesystem::path local_path;
	std::string message;
};

class FakePlatformShell final {
public:
	void script(std::string SeamName, FakePlatformResponse Response) {
		scripted_responses[std::move(SeamName)].push_back(std::move(Response));
	}

	[[nodiscard]] bool has_scripted_responses(std::string_view SeamName) const {
		const auto iterator = scripted_responses.find(std::string(SeamName));
		return iterator != scripted_responses.end()
			   && !iterator->second.empty();
	}

	[[nodiscard]] std::optional<FakePlatformResponse> take(
		std::string_view SeamName) {
		auto iterator = scripted_responses.find(std::string(SeamName));
		if (iterator == scripted_responses.end() || iterator->second.empty())
			return std::nullopt;

		auto response = std::move(iterator->second.front());
		iterator->second.pop_front();

		if (iterator->second.empty())
			scripted_responses.erase(iterator);

		return response;
	}

private:
	std::unordered_map<std::string, std::deque<FakePlatformResponse>>
		scripted_responses;
};
}	 // namespace shuba::test_support::b02
