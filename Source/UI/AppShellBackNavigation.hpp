#pragma once

#include "UI/AppShellState.hpp"

#include <cstdint>

namespace shuba::ui {
enum class AppShellBackAction : std::uint8_t {
	Unhandled,
	CloseCatalogFilterPanel,
	CancelPhotoDeletion,
	RestoreContextualLocation,
	SelectCatalog,
	ReturnToFormDestination,
	ReturnPhotoViewerToOwner,
	ReturnBackupRecoveryToMore,
};

struct AppShellBackDecision final {
	AppShellBackAction action{AppShellBackAction::Unhandled};

	[[nodiscard]] bool consumed() const noexcept;
};

struct AppShellBackNavigationState final {
	RootDestination destination{RootDestination::Catalog};
	bool shell_operation_active{};
	bool session_fatal{};
	bool catalog_filter_panel_visible{};
	bool photo_deletion_confirmation_pending{};
	bool selected_viewer_owner_is_item{};
	bool selected_viewer_owner_is_storage{};
	bool staged_import_confirmation_pending{};
	bool contextual_return_available{};
	RootDestination form_return_destination{RootDestination::Catalog};
};

[[nodiscard]] AppShellBackDecision decide_app_shell_back_navigation(
	const AppShellBackNavigationState& state) noexcept;
}	 // namespace shuba::ui
