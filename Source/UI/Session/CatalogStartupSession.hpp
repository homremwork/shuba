#pragma once

#include "Core/Clock.hpp"
#include "Core/Identifier.hpp"
#include "Platform/AndroidPreviousExit.hpp"
#include "Platform/PlatformServices.hpp"
#include "UI/Session/CatalogSessionState.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace shuba::ui {
struct CatalogSessionLoadRequest final {
	platform::AppPrivatePathProvider& path_provider;
	core::IdentifierSource& identifiers;
	core::Clock& clock;
	bool debug_demo_seed_enabled{};
	bool honor_startup_safe_mode{};
	std::optional<platform::AppPrivatePaths> resolved_paths;
};

using CatalogSessionLoader =
	std::function<CatalogSessionState(const CatalogSessionLoadRequest&)>;

struct GuardedCatalogSessionLoadRequest final {
	platform::AppPrivatePathProvider& path_provider;
	core::IdentifierSource& identifiers;
	core::Clock& clock;
	std::string app_version;
	std::string platform;
	bool debug_demo_seed_enabled{};
	bool retry_requested_by_user{};
	platform::AndroidPreviousExitService* android_previous_exit_service{};
	CatalogSessionLoader loader;
};

struct CatalogRecoveryUiSummary final {
	persistence::CatalogLoadStatus load_status{
		persistence::CatalogLoadStatus::Fatal};
	CatalogSessionStartupSource startup_source{
		CatalogSessionStartupSource::PathResolutionFailed};
	std::uint64_t accepted_item_count{};
	std::uint64_t accepted_storage_count{};
	std::uint64_t accepted_photo_count{};
	std::uint64_t skipped_item_count{};
	std::uint64_t skipped_storage_count{};
	std::uint64_t skipped_photo_count{};
	std::uint64_t broken_reference_count{};
	std::uint64_t orphan_media_count{};
	std::vector<RecoveryAction> safe_actions;
	std::vector<std::string> technical_details;

	[[nodiscard]] bool fatal() const noexcept;
	[[nodiscard]] bool degraded() const noexcept;
	[[nodiscard]] bool startup_crash_safe_mode() const noexcept;
};

[[nodiscard]] CatalogSessionState load_catalog_session(
	const CatalogSessionLoadRequest& request);
[[nodiscard]] CatalogSessionState load_guarded_catalog_session(
	GuardedCatalogSessionLoadRequest request);
[[nodiscard]] CatalogSessionState reload_catalog_session(
	CatalogSessionState session);
[[nodiscard]] CatalogRecoveryUiSummary make_recovery_ui_summary(
	const CatalogSessionState& session);
}	 // namespace shuba::ui
