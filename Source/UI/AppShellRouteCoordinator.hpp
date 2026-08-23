#pragma once

#include "UI/AppShellBackNavigation.hpp"
#include "UI/AppShellState.hpp"
#include "UI/Session/CatalogSessionState.hpp"

#include <functional>
#include <optional>

namespace shuba::ui {
class AppShellRouteCoordinator final {
public:
	struct Dependencies final {
		CatalogSessionState& session;
		AppShellRouteState& route;
		AppShellBackupState& backup;
		AppShellFeedbackState& feedback;
		AppShellPhotoDisplayState& photo_display;
		AppShellStorageDetailState& storage_detail;
		std::function<void()> cleanup_item_pending_photos;
		std::function<void()> cleanup_storage_pending_photos;
		std::function<void()> refresh_all;
	};

	explicit AppShellRouteCoordinator(Dependencies dependencies);

	void select_root(RootDestination destination_value);
	void open_item_detail(core::StableIdentifier item_id);
	void open_storage_detail(core::StableIdentifier storage_id);
	void open_photo_viewer(
		const domain::PhotoOwner& owner,
		const std::optional<core::StableIdentifier>& requested_photo_id);
	void return_from_form(RootDestination destination_value);
	[[nodiscard]] bool handle_system_back(const AppShellBackDecision& decision);

private:
	[[nodiscard]] std::optional<AppShellRouteLocation> current_location() const;
	void push_contextual_return_location(const AppShellRouteLocation& location);
	void restore_contextual_return_location();
	void apply_location(const AppShellRouteLocation& location);
	void clear_photo_viewer_state();
	void reset_transient_state_for_destination(
		RootDestination destination_value, bool destination_changed);
	void leave_current_form_if_needed(RootDestination destination_value);
	void refresh_all();

	CatalogSessionState& session;
	AppShellRouteState& route;
	AppShellBackupState& backup;
	AppShellFeedbackState& feedback;
	AppShellPhotoDisplayState& photo_display;
	AppShellStorageDetailState& storage_detail;
	std::function<void()> cleanup_item_pending_photos_handler;
	std::function<void()> cleanup_storage_pending_photos_handler;
	std::function<void()> refresh_all_handler;
};
}	 // namespace shuba::ui
