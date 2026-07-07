#include "UI/AppShellRouteCoordinator.hpp"

#include "UI/Session/PhotoSession.hpp"
#include "UI/View/ScreenText.hpp"

#include <utility>

namespace shuba::ui {
AppShellRouteCoordinator::AppShellRouteCoordinator(Dependencies dependencies)
	: session(dependencies.session)
	, route(dependencies.route)
	, backup(dependencies.backup)
	, feedback(dependencies.feedback)
	, photo_display(dependencies.photo_display)
	, storage_detail(dependencies.storage_detail)
	, cleanup_item_pending_photos_handler(
		  std::move(dependencies.cleanup_item_pending_photos))
	, cleanup_storage_pending_photos_handler(
		  std::move(dependencies.cleanup_storage_pending_photos))
	, refresh_all_handler(std::move(dependencies.refresh_all)) {}

void AppShellRouteCoordinator::select_root(RootDestination destination_value) {
	const RootDestination previous_destination = route.destination;
	if (previous_destination == RootDestination::ItemForm
		&& destination_value != RootDestination::ItemForm) {
		if (cleanup_item_pending_photos_handler)
			cleanup_item_pending_photos_handler();
	}
	if (previous_destination == RootDestination::StorageForm
		&& destination_value != RootDestination::StorageForm) {
		if (cleanup_storage_pending_photos_handler)
			cleanup_storage_pending_photos_handler();
	}
	route.destination = destination_value;
	if (route.destination != RootDestination::ItemDetail
		&& route.destination != RootDestination::ItemForm
		&& !(route.destination == RootDestination::PhotoViewer
			 && route.selected_photo_owner
			 && route.selected_photo_owner->type
					== domain::PhotoOwnerType::Item)) {
		route.selected_item_id.reset();
	}
	if (route.destination != RootDestination::StorageDetail
		&& route.destination != RootDestination::StorageForm
		&& route.destination != RootDestination::ItemForm
		&& !(route.destination == RootDestination::PhotoViewer
			 && route.selected_photo_owner
			 && route.selected_photo_owner->type
					== domain::PhotoOwnerType::Storage)) {
		route.selected_storage_id.reset();
	}
	if (route.destination != RootDestination::PhotoViewer) {
		route.selected_photo_owner.reset();
		route.selected_photo_id.reset();
		photo_display.displayed_photo_id.reset();
		photo_display.requested_display_photo_id.reset();
		++photo_display.display_request_generation;
		photo_display.viewer_transform_photo_id.reset();
		photo_display.viewer_rotation_quarter_turns = 0;
		photo_display.result = catalog::PhotoDisplayResult{};
	}
	if (destination_value != previous_destination)
		photo_display.pending_delete_photo_id.reset();
	if (route.destination != RootDestination::ItemForm
		&& route.destination != RootDestination::StorageForm) {
		route.form_return_destination.reset();
	}
	if (route.destination != RootDestination::BackupRecovery) {
		backup.pending_import_staging.reset();
		backup.pending_import_degraded_acknowledged = false;
	}
	if (refresh_all_handler)
		refresh_all_handler();
}

void AppShellRouteCoordinator::open_item_detail(
	core::StableIdentifier item_id) {
	route.selected_item_id = std::move(item_id);
	route.destination	   = RootDestination::ItemDetail;
	if (refresh_all_handler)
		refresh_all_handler();
}

void AppShellRouteCoordinator::open_storage_detail(
	core::StableIdentifier storage_id) {
	route.selected_storage_id	  = std::move(storage_id);
	route.destination			  = RootDestination::StorageDetail;
	storage_detail.include_nested = true;
	if (refresh_all_handler)
		refresh_all_handler();
}

void AppShellRouteCoordinator::open_photo_viewer(
	domain::PhotoOwner owner,
	std::optional<core::StableIdentifier> requested_photo_id) {
	route.selected_photo_owner = owner;
	route.selected_photo_id =
		requested_photo_id.has_value()
			? requested_photo_id
			: first_viewable_photo_id(session.repository, owner);
	if (owner.type == domain::PhotoOwnerType::Item)
		route.selected_item_id = owner.id;
	else
		route.selected_storage_id = owner.id;
	feedback.photo_message.clear();
	feedback.photo_diagnostics.clear();
	photo_display.pending_delete_photo_id.reset();
	photo_display.displayed_photo_id.reset();
	photo_display.requested_display_photo_id.reset();
	++photo_display.display_request_generation;
	photo_display.viewer_transform_photo_id.reset();
	photo_display.viewer_rotation_quarter_turns = 0;
	photo_display.result						= catalog::PhotoDisplayResult{};
	route.destination							= RootDestination::PhotoViewer;
	if (refresh_all_handler)
		refresh_all_handler();
}
}	 // namespace shuba::ui
