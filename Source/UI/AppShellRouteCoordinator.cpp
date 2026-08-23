#include "UI/AppShellRouteCoordinator.hpp"

#include "UI/Session/PhotoSession.hpp"
#include "UI/View/ScreenText.hpp"

#include <utility>

namespace shuba::ui {
bool AppShellBackDecision::consumed() const noexcept {
	return action != AppShellBackAction::Unhandled;
}

AppShellBackDecision decide_app_shell_back_navigation(
	const AppShellBackNavigationState& state) noexcept {
	if (state.shell_operation_active || state.session_fatal)
		return {};

	if (state.destination == RootDestination::Catalog
		&& state.catalog_filter_panel_visible) {
		return AppShellBackDecision{
			.action = AppShellBackAction::CloseCatalogFilterPanel};
	}

	if (state.destination == RootDestination::PhotoViewer
		&& state.photo_deletion_confirmation_pending) {
		return AppShellBackDecision{
			.action = AppShellBackAction::CancelPhotoDeletion};
	}

	switch (state.destination) {
		case RootDestination::ItemDetail:
		case RootDestination::StorageDetail:
			return state.contextual_return_available
					   ? AppShellBackDecision{.action = AppShellBackAction::
												  RestoreContextualLocation}
					   : AppShellBackDecision{};
		case RootDestination::ItemForm:
		case RootDestination::StorageForm:
			return AppShellBackDecision{
				.action = AppShellBackAction::ReturnToFormDestination};
		case RootDestination::PhotoViewer:
			if (state.selected_viewer_owner_is_item
				|| state.selected_viewer_owner_is_storage) {
				return AppShellBackDecision{
					.action = AppShellBackAction::ReturnPhotoViewerToOwner};
			}
			return {};
		case RootDestination::BackupRecovery:
			return state.staged_import_confirmation_pending
					   ? AppShellBackDecision{}
					   : AppShellBackDecision{.action = AppShellBackAction::
												  ReturnBackupRecoveryToMore};
		case RootDestination::Catalog:
			return {};
		case RootDestination::Storages:
		case RootDestination::Add:
		case RootDestination::More:
			return AppShellBackDecision{.action =
											AppShellBackAction::SelectCatalog};
	}

	return {};
}

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
	leave_current_form_if_needed(destination_value);
	route.contextual_return_locations.clear();
	route.destination = destination_value;
	reset_transient_state_for_destination(
		destination_value, destination_value != previous_destination);
	refresh_all();
}

void AppShellRouteCoordinator::open_item_detail(
	core::StableIdentifier item_id) {
	if (const std::optional<AppShellRouteLocation> location =
			current_location())
		push_contextual_return_location(*location);
	route.selected_item_id = std::move(item_id);
	route.destination	   = RootDestination::ItemDetail;
	refresh_all();
}

void AppShellRouteCoordinator::open_storage_detail(
	core::StableIdentifier storage_id) {
	if (const std::optional<AppShellRouteLocation> location =
			current_location())
		push_contextual_return_location(*location);
	route.selected_storage_id	  = std::move(storage_id);
	route.destination			  = RootDestination::StorageDetail;
	storage_detail.include_nested = true;
	refresh_all();
}

void AppShellRouteCoordinator::open_photo_viewer(
	const domain::PhotoOwner& owner,
	const std::optional<core::StableIdentifier>& requested_photo_id) {
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
	refresh_all();
}

void AppShellRouteCoordinator::return_from_form(
	RootDestination destination_value) {
	const RootDestination previous_destination = route.destination;
	leave_current_form_if_needed(destination_value);
	route.destination = destination_value;
	reset_transient_state_for_destination(
		destination_value, destination_value != previous_destination);
	refresh_all();
}

bool AppShellRouteCoordinator::handle_system_back(
	const AppShellBackDecision& decision) {
	switch (decision.action) {
		case AppShellBackAction::RestoreContextualLocation:
			if (route.contextual_return_locations.empty())
				return false;
			restore_contextual_return_location();
			return true;
		case AppShellBackAction::SelectCatalog:
			select_root(RootDestination::Catalog);
			return true;
		case AppShellBackAction::ReturnToFormDestination:
			return_from_form(route.form_return_destination.value_or(
				RootDestination::Catalog));
			return true;
		case AppShellBackAction::ReturnPhotoViewerToOwner:
			if (!route.selected_photo_owner.has_value())
				return false;
			if (route.selected_photo_owner->type
				== domain::PhotoOwnerType::Item) {
				route.selected_item_id = route.selected_photo_owner->id;
				route.destination	   = RootDestination::ItemDetail;
			} else {
				route.selected_storage_id = route.selected_photo_owner->id;
				route.destination		  = RootDestination::StorageDetail;
			}
			clear_photo_viewer_state();
			refresh_all();
			return true;
		case AppShellBackAction::ReturnBackupRecoveryToMore:
			select_root(RootDestination::More);
			return true;
		case AppShellBackAction::Unhandled:
		case AppShellBackAction::CloseCatalogFilterPanel:
		case AppShellBackAction::CancelPhotoDeletion:
			return false;
	}

	return false;
}

std::optional<AppShellRouteLocation>
AppShellRouteCoordinator::current_location() const {
	switch (route.destination) {
		case RootDestination::Catalog:
		case RootDestination::Storages:
		case RootDestination::Add:
		case RootDestination::More:
			return AppShellRouteLocation{.destination = route.destination};
		case RootDestination::ItemDetail:
			if (!route.selected_item_id.has_value())
				return std::nullopt;
			return AppShellRouteLocation{
				.destination	  = RootDestination::ItemDetail,
				.selected_item_id = route.selected_item_id};
		case RootDestination::StorageDetail:
			if (!route.selected_storage_id.has_value())
				return std::nullopt;
			return AppShellRouteLocation{
				.destination		 = RootDestination::StorageDetail,
				.selected_storage_id = route.selected_storage_id};
		case RootDestination::ItemForm:
		case RootDestination::StorageForm:
		case RootDestination::PhotoViewer:
		case RootDestination::BackupRecovery:
			return std::nullopt;
	}

	return std::nullopt;
}

void AppShellRouteCoordinator::push_contextual_return_location(
	const AppShellRouteLocation& location) {
	if (route.contextual_return_locations.size()
		>= AppShellRouteState::maximum_contextual_return_locations) {
		if (route.contextual_return_locations.size() > 1U) {
			route.contextual_return_locations.erase(
				route.contextual_return_locations.begin() + 1);
		} else {
			return;
		}
	}
	route.contextual_return_locations.push_back(location);
}

void AppShellRouteCoordinator::restore_contextual_return_location() {
	const AppShellRouteLocation location =
		route.contextual_return_locations.back();
	route.contextual_return_locations.pop_back();
	apply_location(location);
	reset_transient_state_for_destination(location.destination, true);
	refresh_all();
}

void AppShellRouteCoordinator::apply_location(
	const AppShellRouteLocation& location) {
	route.destination		  = location.destination;
	route.selected_item_id	  = location.selected_item_id;
	route.selected_storage_id = location.selected_storage_id;
}

void AppShellRouteCoordinator::clear_photo_viewer_state() {
	route.selected_photo_owner.reset();
	route.selected_photo_id.reset();
	photo_display.displayed_photo_id.reset();
	photo_display.requested_display_photo_id.reset();
	++photo_display.display_request_generation;
	photo_display.viewer_transform_photo_id.reset();
	photo_display.viewer_rotation_quarter_turns = 0;
	photo_display.result						= catalog::PhotoDisplayResult{};
	photo_display.pending_delete_photo_id.reset();
}

void AppShellRouteCoordinator::reset_transient_state_for_destination(
	RootDestination destination_value, bool destination_changed) {
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
	if (route.destination != RootDestination::PhotoViewer)
		clear_photo_viewer_state();
	if (destination_changed)
		photo_display.pending_delete_photo_id.reset();
	if (route.destination != RootDestination::ItemForm
		&& route.destination != RootDestination::StorageForm) {
		route.form_return_destination.reset();
	}
	if (destination_value != RootDestination::BackupRecovery) {
		backup.pending_import_staging.reset();
		backup.pending_import_degraded_acknowledged = false;
	}
}

void AppShellRouteCoordinator::leave_current_form_if_needed(
	RootDestination destination_value) {
	if (route.destination == RootDestination::ItemForm
		&& destination_value != RootDestination::ItemForm
		&& cleanup_item_pending_photos_handler) {
		cleanup_item_pending_photos_handler();
	}
	if (route.destination == RootDestination::StorageForm
		&& destination_value != RootDestination::StorageForm
		&& cleanup_storage_pending_photos_handler) {
		cleanup_storage_pending_photos_handler();
	}
}

void AppShellRouteCoordinator::refresh_all() {
	if (refresh_all_handler)
		refresh_all_handler();
}
}	 // namespace shuba::ui
