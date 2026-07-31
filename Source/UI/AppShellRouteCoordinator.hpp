#pragma once

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

private:
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
