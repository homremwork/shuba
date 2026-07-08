#include "UI/AppShellPhotoCoordinator.hpp"

#include "UI/Session/PhotoSession.hpp"
#include "UI/View/ScreenText.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace shuba::ui {
namespace {
void copy_entity_diagnostics_to_photo_feedback(
	AppShellFeedbackState& feedback,
	const std::vector<EntityEditDiagnostic>& diagnostics) {
	feedback.photo_diagnostics.clear();
	for (const EntityEditDiagnostic& diagnostic : diagnostics) {
		feedback.photo_diagnostics.push_back(core::Diagnostic{
			.severity		   = diagnostic.severity,
			.code			   = diagnostic.code,
			.message		   = diagnostic.message,
			.technical_details = diagnostic.technical_details});
	}
}

[[nodiscard]] std::optional<core::StableIdentifier>
next_photo_selection_after_delete(
	const catalog::CatalogRepositoryState& repository,
	const domain::PhotoOwner& owner,
	const core::StableIdentifier& deleted_photo_id) {
	const catalog::OwnerPhotoProjection* projection =
		owner_photo_projection(repository, owner);
	if (projection == nullptr || projection->ordered_photo_ids.size() <= 1U)
		return std::nullopt;

	const std::optional<std::size_t> deleted_index = find_photo_index_in_order(
		projection->ordered_photo_ids, deleted_photo_id);
	if (!deleted_index.has_value())
		return first_viewable_photo_id(repository, owner);

	const std::size_t next_index =
		(*deleted_index + 1U) % projection->ordered_photo_ids.size();
	core::StableIdentifier next_photo_id =
		projection->ordered_photo_ids[next_index];
	if (next_photo_id == deleted_photo_id
		&& projection->ordered_photo_ids.size() > 1U) {
		next_photo_id =
			projection
				->ordered_photo_ids[*deleted_index == 0U ? 1U
														 : *deleted_index - 1U];
	}
	return next_photo_id;
}
}	 // namespace

AppShellPhotoCoordinator::AppShellPhotoCoordinator(Dependencies dependencies)
	: session(dependencies.session)
	, route(dependencies.route)
	, item_form(dependencies.item_form)
	, storage_form(dependencies.storage_form)
	, feedback(dependencies.feedback)
	, photo_display(dependencies.photo_display)
	, preview_cache(dependencies.preview_cache)
	, identifiers(dependencies.identifiers)
	, clock(dependencies.clock)
	, operation_gate(dependencies.operation_gate)
	, photo_selection_service(dependencies.photo_selection_service)
	, document_export_service(dependencies.document_export_service)
	, content_staging_service(dependencies.content_staging_service)
	, source_fingerprint_service(dependencies.source_fingerprint_service)
	, source_decode_service(dependencies.source_decode_service)
	, jpeg_export_service(dependencies.jpeg_export_service)
	, internal_photo_codec(dependencies.internal_photo_codec)
	, progress_events(dependencies.progress_events)
	, cancellation_token(dependencies.cancellation_token)
	, invalidate_all_previews_handler(
		  std::move(dependencies.invalidate_all_previews))
	, invalidate_internal_photo_preview_handler(
		  std::move(dependencies.invalidate_internal_photo_preview))
	, invalidate_staged_photo_preview_handler(
		  std::move(dependencies.invalidate_staged_photo_preview))
	, refresh_all_handler(std::move(dependencies.refresh_all)) {}

void AppShellPhotoCoordinator::request_add_photos(
	const domain::PhotoOwner& owner) {
	progress_events.clear();
	feedback.photo_diagnostics.clear();
	feedback.photo_message = "Select photos to import.";
	core::OperationResult picker_started =
		photo_selection_service.request_photo_selection(
			platform::PhotoSelectionRequest{
				.allow_multiple		 = true,
				.accepted_mime_types = {"image/jpeg", "image/png", "image/webp",
										"image/heic", "image/heif"}},
			[this, owner](platform::PlatformValueResult<
						  std::vector<platform::ContentSourceDescriptor>>
							  result) mutable {
		if (result.was_user_cancelled()) {
			feedback.photo_message = "Photo selection cancelled.";
			refresh_all();
			return;
		}
		if (result.failed()) {
			feedback.photo_message	   = "Photo selection failed.";
			feedback.photo_diagnostics = std::move(result.diagnostics);
			refresh_all();
			return;
		}
		PhotoImportSessionResult import_result = import_photos_into_session(
			PhotoImportSessionRequest{
				.current_session	 = session,
				.identifiers		 = identifiers,
				.clock				 = clock,
				.operation_gate		 = operation_gate,
				.staging_service	 = content_staging_service,
				.fingerprint_service = source_fingerprint_service,
				.decode_service		 = source_decode_service,
				.photo_codec		 = internal_photo_codec,
				.owner				 = owner,
				.sources			 = std::move(*result.value)},
			progress_events, cancellation_token);
		apply_photo_import_result(std::move(import_result));
	});
	if (picker_started.failed()) {
		feedback.photo_message	   = "Photo picker could not be opened.";
		feedback.photo_diagnostics = picker_started.diagnostics();
		refresh_all();
	}
}

void AppShellPhotoCoordinator::request_add_pending_item_photos() {
	request_add_pending_photos(PendingPhotoDraftTarget::Item);
}

void AppShellPhotoCoordinator::request_add_pending_storage_photos() {
	request_add_pending_photos(PendingPhotoDraftTarget::Storage);
}

void AppShellPhotoCoordinator::request_add_pending_photos(
	PendingPhotoDraftTarget target) {
	progress_events.clear();
	feedback.photo_diagnostics.clear();
	feedback.photo_message =
		target == PendingPhotoDraftTarget::Item
			? "Select photos to stage before saving the item."
			: "Select photos to stage before saving the storage.";
	core::OperationResult picker_started =
		photo_selection_service.request_photo_selection(
			platform::PhotoSelectionRequest{
				.allow_multiple		 = true,
				.accepted_mime_types = {"image/jpeg", "image/png", "image/webp",
										"image/heic", "image/heif"}},
			[this, target](platform::PlatformValueResult<
						   std::vector<platform::ContentSourceDescriptor>>
							   result) mutable {
		if (result.was_user_cancelled()) {
			feedback.photo_message = "Pending photo selection cancelled.";
			refresh_all();
			return;
		}
		if (result.failed()) {
			feedback.photo_message	   = "Pending photo selection failed.";
			feedback.photo_diagnostics = std::move(result.diagnostics);
			refresh_all();
			return;
		}
		if (result.value->empty()) {
			feedback.photo_message = "No photos selected for pending staging.";
			refresh_all();
			return;
		}

		PendingPhotoStagingResult staging_result =
			stage_pending_photos_for_session(
				PendingPhotoStagingRequest{
					.current_session		  = session,
					.identifiers			  = identifiers,
					.operation_gate			  = operation_gate,
					.staging_service		  = content_staging_service,
					.fingerprint_service	  = source_fingerprint_service,
					.sources				  = std::move(*result.value),
					.existing_pending_sources = pending_sources_for(target),
					.existing_owner = owner_for_pending_target(target)},
				progress_events, cancellation_token);
		apply_pending_photo_staging_result(std::move(staging_result), target);
	});
	if (picker_started.failed()) {
		feedback.photo_message = "Pending photo picker could not be opened.";
		feedback.photo_diagnostics = picker_started.diagnostics();
		refresh_all();
	}
}

std::vector<PendingPhotoSource> AppShellPhotoCoordinator::pending_sources_for(
	PendingPhotoDraftTarget target) const {
	if (target == PendingPhotoDraftTarget::Item)
		return item_form.pending_photos;
	return storage_form.pending_photos;
}

std::optional<domain::PhotoOwner>
AppShellPhotoCoordinator::owner_for_pending_target(
	PendingPhotoDraftTarget target) const {
	if (target == PendingPhotoDraftTarget::Item) {
		if (item_form.mode == FormMode::Edit
			&& item_form.draft.existing_id.has_value()) {
			return domain::PhotoOwner{.type = domain::PhotoOwnerType::Item,
									  .id	= *item_form.draft.existing_id};
		}
		return std::nullopt;
	}

	if (storage_form.mode == FormMode::Edit
		&& storage_form.draft.existing_id.has_value()) {
		return domain::PhotoOwner{.type = domain::PhotoOwnerType::Storage,
								  .id	= *storage_form.draft.existing_id};
	}
	return std::nullopt;
}

void AppShellPhotoCoordinator::request_export_photo(
	const core::StableIdentifier& photo_id) {
	progress_events.clear();
	feedback.photo_diagnostics.clear();
	const std::string suggested_name =
		catalog::suggested_jpeg_export_file_name(session.repository, photo_id);
	core::OperationResult destination_started =
		document_export_service.request_export_destination_selection(
			platform::DocumentExportRequest{
				.suggested_file_name = suggested_name,
				.mime_type			 = "image/jpeg",
				.purpose			 = "photo JPEG export"},
			[this, photo_id](platform::PlatformValueResult<
							 platform::DocumentDestinationDescriptor>
								 result) mutable {
		if (result.was_user_cancelled()) {
			feedback.photo_message = "JPEG export destination cancelled.";
			refresh_all();
			return;
		}
		if (result.failed()) {
			feedback.photo_message	   = "JPEG export destination failed.";
			feedback.photo_diagnostics = std::move(result.diagnostics);
			refresh_all();
			return;
		}
		catalog::PhotoExportUseCase export_use_case{
			identifiers, operation_gate, internal_photo_codec,
			jpeg_export_service, document_export_service};
		catalog::PhotoExportResult exported =
			export_use_case.export_photo_as_jpeg(
				catalog::PhotoExportRequest{
					.current_state = session.repository,
					.paths		   = *session.paths,
					.photo_id	   = photo_id,
					.destination   = std::move(*result.value),
					.jpeg_quality  = 90},
				progress_events, cancellation_token);
		feedback.photo_diagnostics = std::move(exported.diagnostics);
		if (exported.succeeded())
			feedback.photo_message = "JPEG export completed: "
									 + std::to_string(exported.bytes_written)
									 + " bytes.";
		else if (exported.was_user_cancelled())
			feedback.photo_message = "JPEG export cancelled.";
		else
			feedback.photo_message = "JPEG export failed.";
		refresh_all();
	});
	if (destination_started.failed()) {
		feedback.photo_message =
			"JPEG export destination picker could not be opened.";
		feedback.photo_diagnostics = destination_started.diagnostics();
		refresh_all();
	}
}

void AppShellPhotoCoordinator::request_delete_photo_confirmation(
	const core::StableIdentifier& photo_id) {
	photo_display.pending_delete_photo_id = photo_id;
	feedback.photo_message =
		"Tap Confirm delete to remove the selected photo metadata first.";
	refresh_all();
}

void AppShellPhotoCoordinator::cancel_delete_photo_confirmation() {
	photo_display.pending_delete_photo_id.reset();
	feedback.photo_message = "Photo deletion cancelled.";
	refresh_all();
}

void AppShellPhotoCoordinator::confirm_delete_photo(
	const core::StableIdentifier& photo_id) {
	if (!photo_display.pending_delete_photo_id.has_value()
		|| *photo_display.pending_delete_photo_id != photo_id) {
		request_delete_photo_confirmation(photo_id);
		return;
	}

	const persistence::PhotoEnvelope* selected =
		catalog::find_photo_envelope(session.repository, photo_id);
	std::optional<core::StableIdentifier> next_photo_id;
	if (selected != nullptr) {
		next_photo_id = next_photo_selection_after_delete(
			session.repository, domain::owner_of(selected->record), photo_id);
	}

	EntityEditResult result =
		delete_photo_in_session(EntityEditRequest{.current_session = session,
												  .identifiers = identifiers,
												  .clock	   = clock},
								photo_id);
	apply_photo_delete_result(std::move(result), std::move(next_photo_id));
}

void AppShellPhotoCoordinator::apply_pending_photo_staging_result(
	PendingPhotoStagingResult result, PendingPhotoDraftTarget target) {
	feedback.photo_diagnostics = std::move(result.diagnostics);
	std::vector<PendingPhotoSource>& pending_photos =
		target == PendingPhotoDraftTarget::Item ? item_form.pending_photos
												: storage_form.pending_photos;
	AppShellManagedPhotoDeckState& photo_deck =
		target == PendingPhotoDraftTarget::Item ? item_form.photo_deck
												: storage_form.photo_deck;
	const std::size_t first_new_index = pending_photos.size();
	const bool should_select_staged	  = !result.sources.empty();
	for (PendingPhotoSource& source : result.sources)
		pending_photos.push_back(std::move(source));
	if (should_select_staged) {
		photo_deck.staged_selected = true;
		photo_deck.selected_index  = first_new_index;
	}

	if (result.succeeded()) {
		feedback.photo_message =
			"Pending photo staging completed: "
			+ std::to_string(result.staged_count) + " staged, "
			+ std::to_string(result.failure_count) + " failed.";
	} else if (result.was_user_cancelled()) {
		feedback.photo_message = "Pending photo staging cancelled.";
	} else {
		feedback.photo_message = "Pending photo staging failed.";
	}
	refresh_all();
}

void AppShellPhotoCoordinator::apply_photo_import_result(
	PhotoImportSessionResult result) {
	feedback.photo_diagnostics.clear();
	for (const EntityEditDiagnostic& diagnostic : result.diagnostics) {
		feedback.photo_diagnostics.push_back(core::Diagnostic{
			.severity		   = diagnostic.severity,
			.code			   = diagnostic.code,
			.message		   = diagnostic.message,
			.technical_details = diagnostic.technical_details});
	}
	if (result.succeeded()) {
		session = std::move(result.session);
		if (invalidate_all_previews_handler)
			invalidate_all_previews_handler();
		else
			preview_cache.clear();
		feedback.photo_message =
			"Photo import completed: "
			+ std::to_string(result.summary.success_count) + " imported, "
			+ std::to_string(result.summary.failure_count) + " failed.";
		if (!result.imported_photo_ids.empty())
			route.selected_photo_id = result.imported_photo_ids.front();
		photo_display.displayed_photo_id.reset();
	} else if (result.was_user_cancelled()) {
		feedback.photo_message = "Photo import cancelled.";
	} else {
		feedback.photo_message = "Photo import failed.";
	}
	refresh_all();
}

void AppShellPhotoCoordinator::apply_photo_edit_result(
	EntityEditResult result,
	const core::StableIdentifier& selected_photo_id_value) {
	copy_entity_diagnostics_to_photo_feedback(feedback, result.diagnostics);
	if (result.failed()) {
		feedback.photo_message = "Photo metadata update failed.";
		refresh_all();
		return;
	}
	session = std::move(result.session);
	if (invalidate_internal_photo_preview_handler)
		invalidate_internal_photo_preview_handler(selected_photo_id_value);
	else
		preview_cache.remove_internal_photo(selected_photo_id_value);
	route.selected_photo_id = selected_photo_id_value;
	photo_display.displayed_photo_id.reset();
	feedback.photo_message = result.metadata_changed ? "Main photo updated."
													 : "Main photo unchanged.";
	refresh_all();
}

void AppShellPhotoCoordinator::apply_photo_delete_result(
	EntityEditResult result,
	std::optional<core::StableIdentifier> next_photo_id) {
	copy_entity_diagnostics_to_photo_feedback(feedback, result.diagnostics);
	if (result.failed()) {
		feedback.photo_message = "Photo deletion failed.";
		refresh_all();
		return;
	}

	session = std::move(result.session);
	if (invalidate_all_previews_handler)
		invalidate_all_previews_handler();
	else
		preview_cache.clear();
	photo_display.pending_delete_photo_id.reset();
	photo_display.displayed_photo_id.reset();
	photo_display.result	= catalog::PhotoDisplayResult{};
	route.selected_photo_id = std::move(next_photo_id);
	feedback.photo_message	= result.metadata_changed
								  ? "Photo deleted."
								  : "Photo deletion made no changes.";
	if (!feedback.photo_diagnostics.empty())
		feedback.photo_message += " Media cleanup needs attention.";
	refresh_all();
}

void AppShellPhotoCoordinator::cleanup_item_pending_photos() {
	cleanup_pending_photos(item_form.pending_photos, item_form.photo_deck);
}

void AppShellPhotoCoordinator::cleanup_storage_pending_photos() {
	cleanup_pending_photos(storage_form.pending_photos,
						   storage_form.photo_deck);
}

void AppShellPhotoCoordinator::cleanup_pending_photos(
	std::vector<PendingPhotoSource>& pending_photos,
	AppShellManagedPhotoDeckState& photo_deck) {
	if (pending_photos.empty())
		return;

	PendingPhotoCleanupResult cleanup =
		cleanup_pending_photo_sources(pending_photos);
	feedback.photo_diagnostics = cleanup.diagnostics;
	if (invalidate_all_previews_handler)
		invalidate_all_previews_handler();
	else
		preview_cache.clear();
	std::erase_if(pending_photos, [](const PendingPhotoSource& source) {
		return !source.staged_path.has_value();
	});
	photo_deck.staged_main_index.reset();
	if (photo_deck.staged_selected
		&& photo_deck.selected_index >= pending_photos.size()) {
		photo_deck.selected_index =
			pending_photos.empty() ? 0U : pending_photos.size() - 1U;
	}
	feedback.photo_message =
		cleanup.failed() ? "Some pending staged photos could not be cleaned."
						 : "Pending photos cleared.";
}

void AppShellPhotoCoordinator::remove_item_pending_photo(
	std::size_t pending_photo_index) {
	remove_pending_photo(item_form.pending_photos, item_form.photo_deck,
						 pending_photo_index);
}

void AppShellPhotoCoordinator::remove_storage_pending_photo(
	std::size_t pending_photo_index) {
	remove_pending_photo(storage_form.pending_photos, storage_form.photo_deck,
						 pending_photo_index);
}

void AppShellPhotoCoordinator::remove_pending_photo(
	std::vector<PendingPhotoSource>& pending_photos,
	AppShellManagedPhotoDeckState& photo_deck,
	std::size_t pending_photo_index) {
	if (pending_photo_index >= pending_photos.size())
		return;

	if (pending_photos[pending_photo_index].staged_path.has_value()) {
		if (invalidate_staged_photo_preview_handler) {
			invalidate_staged_photo_preview_handler(
				*pending_photos[pending_photo_index].staged_path);
		} else {
			preview_cache.remove_staged_photo(
				*pending_photos[pending_photo_index].staged_path);
		}
	}
	std::vector<PendingPhotoSource> cleanup_sources;
	cleanup_sources.push_back(pending_photos[pending_photo_index]);
	PendingPhotoCleanupResult cleanup =
		cleanup_pending_photo_sources(cleanup_sources);
	feedback.photo_diagnostics = cleanup.diagnostics;
	if (cleanup.failed()) {
		PendingPhotoSource& source = pending_photos[pending_photo_index];
		source.diagnostics.insert(source.diagnostics.end(),
								  cleanup.diagnostics.begin(),
								  cleanup.diagnostics.end());
		feedback.photo_message =
			"Pending photo cleanup needs attention; source kept for retry.";
		refresh_all();
		return;
	}

	pending_photos.erase(pending_photos.begin()
						 + static_cast<std::ptrdiff_t>(pending_photo_index));
	if (photo_deck.staged_main_index.has_value()) {
		if (*photo_deck.staged_main_index == pending_photo_index)
			photo_deck.staged_main_index.reset();
		else if (*photo_deck.staged_main_index > pending_photo_index)
			--(*photo_deck.staged_main_index);
	}
	if (photo_deck.staged_selected
		&& photo_deck.selected_index >= pending_photos.size()) {
		photo_deck.selected_index =
			pending_photos.empty() ? 0U : pending_photos.size() - 1U;
	}
	feedback.photo_message = "Pending photo removed.";
	refresh_all();
}

void AppShellPhotoCoordinator::refresh_all() {
	if (refresh_all_handler)
		refresh_all_handler();
}
}	 // namespace shuba::ui
