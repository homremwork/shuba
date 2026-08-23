#pragma once

#include "UI/AppShell/State.hpp"

#include <cstdint>

namespace shuba::ui {
enum class BackAction : std::uint8_t {
	Unhandled,
	CloseCatalogFilterPanel,
	CancelPhotoDeletion,
	RestoreContextualLocation,
	SelectCatalog,
	ReturnToFormDestination,
	ReturnPhotoViewerToOwner,
	ReturnBackupRecoveryToMore,
};

struct BackDecision final {
	BackAction action{BackAction::Unhandled};

	[[nodiscard]] bool consumed() const noexcept;
};

struct BackNavigationState final {
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

[[nodiscard]] BackDecision decide_back_navigation(
	const BackNavigationState& state) noexcept;
}	 // namespace shuba::ui
