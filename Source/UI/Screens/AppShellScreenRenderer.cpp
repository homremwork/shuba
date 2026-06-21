#include "UI/Screens/AppShellScreenRenderer.hpp"

#include "UI/View/AppShellContentComponent.hpp"

#include <utility>

namespace shuba::ui {
AppShellScreenRenderer::AppShellScreenRenderer(Dependencies dependencies)
	: session(dependencies.session)
	, route(dependencies.route)
	, catalog_filter_state(dependencies.catalog_filter_state)
	, item_form(dependencies.item_form)
	, storage_form(dependencies.storage_form)
	, feedback(dependencies.feedback)
	, backup(dependencies.backup)
	, photo_display(dependencies.photo_display)
	, storage_detail(dependencies.storage_detail)
	, edit_identifiers(dependencies.edit_identifiers)
	, edit_clock(dependencies.edit_clock)
	, ui_operation_gate(dependencies.ui_operation_gate)
	, internal_photo_codec(dependencies.internal_photo_codec)
	, jpeg_export_service(dependencies.jpeg_export_service)
	, document_export_service(dependencies.document_export_service)
	, last_progress_events(dependencies.last_progress_events)
	, never_cancelled(dependencies.never_cancelled)
	, content(&dependencies.content)
	, item_name_editor(dependencies.editors.item_name_editor)
	, item_category_editor(dependencies.editors.item_category_editor)
	, item_notes_editor(dependencies.editors.item_notes_editor)
	, item_listing_marketplace_editor(
		  dependencies.editors.item_listing_marketplace_editor)
	, item_listing_url_editor(dependencies.editors.item_listing_url_editor)
	, item_listing_note_editor(dependencies.editors.item_listing_note_editor)
	, item_acquisition_source_editor(
		  dependencies.editors.item_acquisition_source_editor)
	, storage_name_editor(dependencies.editors.storage_name_editor)
	, storage_type_editor(dependencies.editors.storage_type_editor)
	, storage_location_editor(dependencies.editors.storage_location_editor)
	, storage_notes_editor(dependencies.editors.storage_notes_editor)
	, catalog_query_provider(std::move(dependencies.queries.catalog_query))
	, storage_query_provider(std::move(dependencies.queries.storage_query))
	, select_root_handler(std::move(dependencies.actions.select_root))
	, open_item_detail_handler(std::move(dependencies.actions.open_item_detail))
	, open_storage_detail_handler(
		  std::move(dependencies.actions.open_storage_detail))
	, open_photo_viewer_handler(
		  std::move(dependencies.actions.open_photo_viewer))
	, open_new_item_form_handler(
		  std::move(dependencies.actions.open_new_item_form))
	, open_existing_item_form_handler(
		  std::move(dependencies.actions.open_existing_item_form))
	, open_new_storage_form_handler(
		  std::move(dependencies.actions.open_new_storage_form))
	, open_existing_storage_form_handler(
		  std::move(dependencies.actions.open_existing_storage_form))
	, request_add_photos_handler(
		  std::move(dependencies.actions.request_add_photos))
	, request_add_pending_item_photos_handler(
		  std::move(dependencies.actions.request_add_pending_item_photos))
	, request_export_photo_handler(
		  std::move(dependencies.actions.request_export_photo))
	, request_export_backup_handler(
		  std::move(dependencies.actions.request_export_backup))
	, request_export_diagnostic_archive_handler(
		  std::move(dependencies.actions.request_export_diagnostic_archive))
	, request_import_backup_handler(
		  std::move(dependencies.actions.request_import_backup))
	, confirm_staged_backup_import_handler(
		  std::move(dependencies.actions.confirm_staged_backup_import))
	, cleanup_item_pending_photos_handler(
		  std::move(dependencies.actions.cleanup_item_pending_photos))
	, remove_item_pending_photo_handler(
		  std::move(dependencies.actions.remove_item_pending_photo))
	, reset_catalog_filters_handler(
		  std::move(dependencies.actions.reset_catalog_filters))
	, apply_entity_edit_result_handler(
		  std::move(dependencies.actions.apply_entity_edit_result))
	, apply_photo_edit_result_handler(
		  std::move(dependencies.actions.apply_photo_edit_result))
	, refresh_all_handler(std::move(dependencies.actions.refresh_all))
	, refresh_content_handler(std::move(dependencies.actions.refresh_content)) {
}

std::string AppShellScreenRenderer::catalog_query() const {
	return catalog_query_provider ? catalog_query_provider() : std::string{};
}

std::string AppShellScreenRenderer::storage_query() const {
	return storage_query_provider ? storage_query_provider() : std::string{};
}

void AppShellScreenRenderer::select_root(RootDestination destination) {
	if (select_root_handler)
		select_root_handler(destination);
}

void AppShellScreenRenderer::open_item_detail(core::StableIdentifier item_id) {
	if (open_item_detail_handler)
		open_item_detail_handler(std::move(item_id));
}

void AppShellScreenRenderer::open_storage_detail(
	core::StableIdentifier storage_id) {
	if (open_storage_detail_handler)
		open_storage_detail_handler(std::move(storage_id));
}

void AppShellScreenRenderer::open_photo_viewer(
	domain::PhotoOwner owner,
	std::optional<core::StableIdentifier> requested_photo_id) {
	if (open_photo_viewer_handler)
		open_photo_viewer_handler(std::move(owner),
								  std::move(requested_photo_id));
}

void AppShellScreenRenderer::open_new_item_form(
	std::optional<core::StableIdentifier> storage_id) {
	if (open_new_item_form_handler)
		open_new_item_form_handler(std::move(storage_id));
}

void AppShellScreenRenderer::open_existing_item_form(
	core::StableIdentifier item_id) {
	if (open_existing_item_form_handler)
		open_existing_item_form_handler(std::move(item_id));
}

void AppShellScreenRenderer::open_new_storage_form(
	std::optional<core::StableIdentifier> parent_id) {
	if (open_new_storage_form_handler)
		open_new_storage_form_handler(std::move(parent_id));
}

void AppShellScreenRenderer::open_existing_storage_form(
	core::StableIdentifier storage_id) {
	if (open_existing_storage_form_handler)
		open_existing_storage_form_handler(std::move(storage_id));
}

void AppShellScreenRenderer::request_add_photos(domain::PhotoOwner owner) {
	if (request_add_photos_handler)
		request_add_photos_handler(std::move(owner));
}

void AppShellScreenRenderer::request_add_pending_item_photos() {
	if (request_add_pending_item_photos_handler)
		request_add_pending_item_photos_handler();
}

void AppShellScreenRenderer::request_export_photo(
	core::StableIdentifier photo_id) {
	if (request_export_photo_handler)
		request_export_photo_handler(std::move(photo_id));
}

void AppShellScreenRenderer::request_export_backup() {
	if (request_export_backup_handler)
		request_export_backup_handler();
}

void AppShellScreenRenderer::request_export_diagnostic_archive() {
	if (request_export_diagnostic_archive_handler)
		request_export_diagnostic_archive_handler();
}

void AppShellScreenRenderer::request_import_backup() {
	if (request_import_backup_handler)
		request_import_backup_handler();
}

void AppShellScreenRenderer::confirm_staged_backup_import() {
	if (confirm_staged_backup_import_handler)
		confirm_staged_backup_import_handler();
}

void AppShellScreenRenderer::cleanup_item_pending_photos() {
	if (cleanup_item_pending_photos_handler)
		cleanup_item_pending_photos_handler();
}

void AppShellScreenRenderer::remove_item_pending_photo(
	std::size_t pending_photo_index) {
	if (remove_item_pending_photo_handler)
		remove_item_pending_photo_handler(pending_photo_index);
}

void AppShellScreenRenderer::reset_catalog_filters() {
	if (reset_catalog_filters_handler)
		reset_catalog_filters_handler();
}

void AppShellScreenRenderer::apply_entity_edit_result(EntityEditResult result) {
	if (apply_entity_edit_result_handler)
		apply_entity_edit_result_handler(std::move(result));
}

void AppShellScreenRenderer::apply_photo_edit_result(
	EntityEditResult result, core::StableIdentifier selected_photo_id) {
	if (apply_photo_edit_result_handler) {
		apply_photo_edit_result_handler(std::move(result),
										std::move(selected_photo_id));
	}
}

void AppShellScreenRenderer::refresh_all() {
	if (refresh_all_handler)
		refresh_all_handler();
}

void AppShellScreenRenderer::refresh_content() {
	if (refresh_content_handler)
		refresh_content_handler();
}
}	 // namespace shuba::ui
