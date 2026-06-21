#include "UI/AppShellPhotoCoordinator.hpp"

#include "UI/Session/PhotoSession.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace shuba::ui {
AppShellPhotoCoordinator::AppShellPhotoCoordinator(Dependencies dependencies)
	: session(dependencies.session)
	, route(dependencies.route)
	, item_form(dependencies.item_form)
	, feedback(dependencies.feedback)
	, photo_display(dependencies.photo_display)
	, identifiers(dependencies.identifiers)
	, clock(dependencies.clock)
	, operation_gate(dependencies.operation_gate)
	, photo_selection_service(dependencies.photo_selection_service)
	, document_export_service(dependencies.document_export_service)
	, content_staging_service(dependencies.content_staging_service)
	, source_decode_service(dependencies.source_decode_service)
	, jpeg_export_service(dependencies.jpeg_export_service)
	, internal_photo_codec(dependencies.internal_photo_codec)
	, progress_events(dependencies.progress_events)
	, cancellation_token(dependencies.cancellation_token)
	, refresh_all_handler(std::move(dependencies.refresh_all)) {}

void AppShellPhotoCoordinator::request_add_photos(domain::PhotoOwner owner) {
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
				.current_session = session,
				.identifiers	 = identifiers,
				.clock			 = clock,
				.operation_gate	 = operation_gate,
				.staging_service = content_staging_service,
				.decode_service	 = source_decode_service,
				.photo_codec	 = internal_photo_codec,
				.owner			 = owner,
				.sources		 = std::move(*result.value)},
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
	progress_events.clear();
	feedback.photo_diagnostics.clear();
	feedback.photo_message = "Select photos to stage before saving the item.";
	core::OperationResult picker_started =
		photo_selection_service.request_photo_selection(
			platform::PhotoSelectionRequest{
				.allow_multiple		 = true,
				.accepted_mime_types = {"image/jpeg", "image/png", "image/webp",
										"image/heic", "image/heif"}},
			[this](platform::PlatformValueResult<
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
					.current_session = session,
					.identifiers	 = identifiers,
					.operation_gate	 = operation_gate,
					.staging_service = content_staging_service,
					.sources		 = std::move(*result.value)},
				progress_events, cancellation_token);
		apply_pending_photo_staging_result(std::move(staging_result));
	});
	if (picker_started.failed()) {
		feedback.photo_message = "Pending photo picker could not be opened.";
		feedback.photo_diagnostics = picker_started.diagnostics();
		refresh_all();
	}
}

void AppShellPhotoCoordinator::request_export_photo(
	core::StableIdentifier photo_id) {
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

void AppShellPhotoCoordinator::apply_pending_photo_staging_result(
	PendingPhotoStagingResult result) {
	feedback.photo_diagnostics = std::move(result.diagnostics);
	for (PendingPhotoSource& source : result.sources)
		item_form.pending_photos.push_back(std::move(source));

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
	EntityEditResult result, core::StableIdentifier selected_photo_id_value) {
	feedback.photo_diagnostics.clear();
	for (const EntityEditDiagnostic& diagnostic : result.diagnostics) {
		feedback.photo_diagnostics.push_back(core::Diagnostic{
			.severity		   = diagnostic.severity,
			.code			   = diagnostic.code,
			.message		   = diagnostic.message,
			.technical_details = diagnostic.technical_details});
	}
	if (result.failed()) {
		feedback.photo_message = "Photo metadata update failed.";
		refresh_all();
		return;
	}
	session					= std::move(result.session);
	route.selected_photo_id = selected_photo_id_value;
	photo_display.displayed_photo_id.reset();
	feedback.photo_message = result.metadata_changed ? "Main photo updated."
													 : "Main photo unchanged.";
	refresh_all();
}

void AppShellPhotoCoordinator::cleanup_item_pending_photos() {
	if (item_form.pending_photos.empty())
		return;

	PendingPhotoCleanupResult cleanup =
		cleanup_pending_photo_sources(item_form.pending_photos);
	feedback.photo_diagnostics = cleanup.diagnostics;
	std::erase_if(item_form.pending_photos,
				  [](const PendingPhotoSource& source) {
		return !source.staged_path.has_value();
	});
	feedback.photo_message =
		cleanup.failed() ? "Some pending staged photos could not be cleaned."
						 : "Pending photos cleared.";
}

void AppShellPhotoCoordinator::remove_item_pending_photo(
	std::size_t pending_photo_index) {
	if (pending_photo_index >= item_form.pending_photos.size())
		return;

	std::vector<PendingPhotoSource> cleanup_sources;
	cleanup_sources.push_back(item_form.pending_photos[pending_photo_index]);
	PendingPhotoCleanupResult cleanup =
		cleanup_pending_photo_sources(cleanup_sources);
	feedback.photo_diagnostics = cleanup.diagnostics;
	if (cleanup.failed()) {
		PendingPhotoSource& source =
			item_form.pending_photos[pending_photo_index];
		source.diagnostics.insert(source.diagnostics.end(),
								  cleanup.diagnostics.begin(),
								  cleanup.diagnostics.end());
		feedback.photo_message =
			"Pending photo cleanup needs attention; source kept for retry.";
		refresh_all();
		return;
	}

	item_form.pending_photos.erase(
		item_form.pending_photos.begin()
		+ static_cast<std::ptrdiff_t>(pending_photo_index));
	feedback.photo_message = "Pending photo removed.";
	refresh_all();
}

void AppShellPhotoCoordinator::refresh_all() {
	if (refresh_all_handler)
		refresh_all_handler();
}
}	 // namespace shuba::ui
