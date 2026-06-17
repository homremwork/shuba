#include "Platform/LinuxFakes.hpp"
#include "UI/CatalogSession.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

namespace {
class TemporaryDirectory final {
public:
	explicit TemporaryDirectory(std::string leaf_name)
		: root(std::filesystem::temp_directory_path() / std::move(leaf_name)) {
		reset();
	}

	TemporaryDirectory(const TemporaryDirectory&)			 = delete;
	TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

	~TemporaryDirectory() {
		std::error_code error;
		std::filesystem::remove_all(root, error);
	}

	void reset() {
		std::error_code error;
		std::filesystem::remove_all(root, error);
		std::filesystem::create_directories(root, error);
		REQUIRE_FALSE(error);
	}

	[[nodiscard]] const std::filesystem::path& path() const noexcept {
		return root;
	}

private:
	std::filesystem::path root;
};

[[nodiscard]] shuba::ui::CatalogSessionState load_session(
	shuba::platform::LinuxFakePathProvider& path_provider,
	shuba::platform::ScriptedIdentifierSource& identifiers,
	shuba::core::ManualClock& clock, bool debug_demo_seed_enabled) {
	return shuba::ui::load_catalog_session(shuba::ui::CatalogSessionLoadRequest{
		.path_provider			 = path_provider,
		.identifiers			 = identifiers,
		.clock					 = clock,
		.debug_demo_seed_enabled = debug_demo_seed_enabled});
}
}	 // namespace

TEST_CASE("B17 debug startup seeds canonical demo catalog once") {
	TemporaryDirectory temporary{"shuba-b17-debug-demo"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	shuba::platform::ScriptedIdentifierSource identifiers;
	identifiers.script_stable_identifier("catalog-debug-demo");
	identifiers.script_operation_identifier("operation-init-demo");
	identifiers.script_operation_identifier("operation-demo-storages");
	identifiers.script_operation_identifier("operation-demo-items");
	identifiers.script_operation_identifier("operation-demo-photos");
	shuba::core::ManualClock clock{
		shuba::core::EpochMilliseconds{1760000000000}};

	shuba::ui::CatalogSessionState first =
		load_session(path_provider, identifiers, clock, true);

	REQUIRE(first.source
			== shuba::ui::CatalogSessionStartupSource::SeededDemoCatalog);
	REQUIRE(first.ready_for_browsing());
	REQUIRE(first.demo_catalog_seeded);
	REQUIRE(first.demo_catalog_active);
	REQUIRE(first.repository.items.size() >= 5U);
	REQUIRE(first.repository.storages.size() >= 4U);
	REQUIRE(first.repository.photos.size() >= 5U);
	REQUIRE(first.degraded());
	REQUIRE(first.search_index.items.size() == first.repository.items.size());
	REQUIRE(
		std::filesystem::exists(first.paths->active_catalog_root
								/ std::filesystem::path{std::string{
									shuba::ui::debug_demo_marker_file_name}}));

	shuba::ui::CatalogSessionState second =
		load_session(path_provider, identifiers, clock, true);

	REQUIRE(second.source
			== shuba::ui::CatalogSessionStartupSource::ExistingCatalog);
	REQUIRE(second.ready_for_browsing());
	REQUIRE_FALSE(second.demo_catalog_seeded);
	REQUIRE(second.demo_catalog_active);
	REQUIRE(second.repository.items.size() == first.repository.items.size());
}

TEST_CASE("B17 release startup initializes empty catalog without demo seed") {
	TemporaryDirectory temporary{"shuba-b17-release-empty"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	shuba::platform::ScriptedIdentifierSource identifiers;
	identifiers.script_stable_identifier("catalog-release-empty");
	identifiers.script_operation_identifier("operation-release-init");
	shuba::core::ManualClock clock{
		shuba::core::EpochMilliseconds{1760000000000}};

	shuba::ui::CatalogSessionState state =
		load_session(path_provider, identifiers, clock, false);

	REQUIRE(state.source
			== shuba::ui::CatalogSessionStartupSource::InitializedEmptyCatalog);
	REQUIRE(state.ready_for_browsing());
	REQUIRE(state.initialized_empty_catalog);
	REQUIRE_FALSE(state.demo_catalog_seeded);
	REQUIRE_FALSE(state.demo_catalog_active);
	REQUIRE(state.repository.items.empty());
	REQUIRE(state.repository.storages.empty());
	REQUIRE(state.repository.photos.empty());
	REQUIRE(state.load_status == shuba::persistence::CatalogLoadStatus::Normal);
}

TEST_CASE("B17 debug startup preserves existing canonical empty catalog") {
	TemporaryDirectory temporary{"shuba-b17-debug-existing"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	shuba::platform::ScriptedIdentifierSource initializer_identifiers;
	initializer_identifiers.script_stable_identifier("catalog-existing-empty");
	initializer_identifiers.script_operation_identifier(
		"operation-existing-init");
	shuba::core::ManualClock clock{
		shuba::core::EpochMilliseconds{1760000000000}};

	shuba::ui::CatalogSessionState initialized =
		load_session(path_provider, initializer_identifiers, clock, false);
	REQUIRE(initialized.ready_for_browsing());
	REQUIRE(initialized.repository.items.empty());

	shuba::platform::ScriptedIdentifierSource debug_identifiers;
	shuba::ui::CatalogSessionState debug_load =
		load_session(path_provider, debug_identifiers, clock, true);

	REQUIRE(debug_load.source
			== shuba::ui::CatalogSessionStartupSource::ExistingCatalog);
	REQUIRE(debug_load.ready_for_browsing());
	REQUIRE_FALSE(debug_load.demo_catalog_seeded);
	REQUIRE_FALSE(debug_load.demo_catalog_active);
	REQUIRE(debug_load.repository.items.empty());
	REQUIRE(debug_load.repository.storages.empty());
}
