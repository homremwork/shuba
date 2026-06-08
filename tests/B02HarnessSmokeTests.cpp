#include "B02TestSupport.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
consteval bool is_cpp23_or_newer() {
	return __cplusplus > 202002L;
}
}	 // namespace

TEST_CASE("B02 Catch2 runner and C++23 mode are active", "[b02][harness]") {
	static_assert(is_cpp23_or_newer());
	constexpr auto compiled_as_cpp23_or_newer = is_cpp23_or_newer();
	constexpr auto values					  = std::array{1, 2, 3};
	constexpr auto sum = values[0] + values[1] + values[2];

	REQUIRE(compiled_as_cpp23_or_newer);
	REQUIRE(sum == 6);
}

TEST_CASE("FakeClock returns scripted UTC epoch millisecond values",
		  "[b02][clock]") {
	using namespace std::chrono_literals;
	using shuba::test_support::b02::FakeClock;

	FakeClock clock{123ms};
	REQUIRE(clock.now() == 123ms);

	clock.script({10ms, 20ms});
	REQUIRE(clock.has_scripted_values());
	REQUIRE(clock.now() == 10ms);
	REQUIRE(clock.now() == 20ms);
	REQUIRE_FALSE(clock.has_scripted_values());
	REQUIRE(clock.now() == 123ms);

	clock.set_fallback(456ms);
	clock.append(30ms);
	REQUIRE(clock.now() == 30ms);
	REQUIRE(clock.now() == 456ms);
}

TEST_CASE(
	"FakeIdentifierSource returns scripted identifiers and fails explicitly",
	"[b02][identifiers]") {
	using shuba::test_support::b02::FakeIdentifierSource;

	FakeIdentifierSource identifiers;
	identifiers.script_entity_identifiers({"item-001", "storage-001"});
	identifiers.script_operation_identifiers({"operation-001"});

	REQUIRE(identifiers.next_entity_identifier() == "item-001");
	REQUIRE(identifiers.next_entity_identifier() == "storage-001");
	REQUIRE_FALSE(identifiers.has_entity_identifiers());
	REQUIRE_THROWS_AS(identifiers.next_entity_identifier(), std::logic_error);

	REQUIRE(identifiers.next_operation_identifier() == "operation-001");
	REQUIRE_FALSE(identifiers.has_operation_identifiers());
	REQUIRE_THROWS_AS(identifiers.next_operation_identifier(),
					  std::logic_error);
}

TEST_CASE("ProgressCollector records ordered operation events",
		  "[b02][progress]") {
	using shuba::test_support::b02::OperationProgressEvent;
	using shuba::test_support::b02::ProgressCollector;

	ProgressCollector progress;
	progress.record(OperationProgressEvent{.operation_id	= "operation-001",
										   .operation_type	= "photo-import",
										   .phase			= "staging",
										   .completed_units = std::uint64_t{0},
										   .total_units		= std::uint64_t{2},
										   .message			= "starting",
										   .cancellable		= true});
	progress.record(OperationProgressEvent{.operation_id	= "operation-001",
										   .operation_type	= "photo-import",
										   .phase			= "done",
										   .completed_units = std::uint64_t{2},
										   .total_units		= std::uint64_t{2},
										   .message			= "finished",
										   .cancellable		= false});

	const auto& events = progress.events();
	REQUIRE(events.size() == 2);
	REQUIRE(events[0].operation_id == "operation-001");
	REQUIRE(events[0].phase == "staging");
	REQUIRE(events[0].completed_units == std::uint64_t{0});
	REQUIRE(events[0].cancellable);
	REQUIRE(events[1].phase == "done");
	REQUIRE(events[1].completed_units == std::uint64_t{2});
	REQUIRE_FALSE(events[1].cancellable);

	progress.clear();
	REQUIRE(progress.events().empty());
}

TEST_CASE("FakePathProvider creates isolated app-private style test roots",
		  "[b02][paths]") {
	namespace fs = std::filesystem;
	using shuba::test_support::b02::FakePathProvider;

	auto paths = FakePathProvider::create_under_system_temp(
		"shuba-b02-path-provider-smoke");

	REQUIRE(fs::exists(paths.root()));
	REQUIRE(fs::exists(paths.app_private_root()));
	REQUIRE(fs::exists(paths.active_catalog_root()));
	REQUIRE(fs::exists(paths.temporary_root()));
	REQUIRE(fs::exists(paths.staged_content_root()));
	REQUIRE(fs::exists(paths.media_root()));
	REQUIRE(paths.active_catalog_root().parent_path()
			== paths.app_private_root());

	const auto marker_path = paths.temporary_root() / "marker.txt";
	{
		std::ofstream marker{marker_path};
		marker << "b02";
	}

	REQUIRE(fs::exists(marker_path));

	paths.reset();
	REQUIRE_FALSE(fs::exists(marker_path));
	REQUIRE(fs::exists(paths.temporary_root()));
	REQUIRE(fs::exists(paths.media_root()));
}

TEST_CASE(
	"FakePlatformShell provides narrow scripted platform seam placeholders",
	"[b02][platform-shell]") {
	using shuba::test_support::b02::FakePlatformResponse;
	using shuba::test_support::b02::FakePlatformShell;

	FakePlatformShell shell;
	REQUIRE_FALSE(shell.take("photo-picker").has_value());

	shell.script("photo-picker",
				 FakePlatformResponse{.success	  = true,
									  .local_path = "/tmp/selected-image.jpg",
									  .message	  = "selected"});

	REQUIRE(shell.has_scripted_responses("photo-picker"));
	const auto response = shell.take("photo-picker");
	REQUIRE(response.has_value());
	REQUIRE(response->success);
	REQUIRE(response->local_path
			== std::filesystem::path{"/tmp/selected-image.jpg"});
	REQUIRE(response->message == "selected");
	REQUIRE_FALSE(shell.has_scripted_responses("photo-picker"));
}
