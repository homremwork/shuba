#include "UI/AppShell/PhotoCoordinator.hpp"

#include "Localization/Facade.hpp"
#include "UI/Session/PhotoSession.hpp"
#include "UI/View/ScreenText.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace shuba::ui {
namespace {
void copy_entity_diagnostics_to_photo_feedback(
	FeedbackState& feedback,
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

PhotoCoordinator::PhotoCoordinator(Dependencies dependencies)
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
	, shell_operation_runner(dependencies.shell_operation_runner)
	, shell_operation_state(dependencies.shell_operation_state)
	, localization(dependencies.localization)
	, invalidate_all_previews_handler(
		  std::move(dependencies.invalidate_all_previews))
	, invalidate_internal_photo_preview_handler(
		  std::move(dependencies.invalidate_internal_photo_preview))
	, invalidate_staged_photo_preview_handler(
		  std::move(dependencies.invalidate_staged_photo_preview))
	, refresh_all_handler(std::move(dependencies.refresh_all))
	, begin_shell_operation_handler(
		  std::move(dependencies.begin_shell_operation))
	, complete_shell_operation_handler(
		  std::move(dependencies.complete_shell_operation)) {}

PhotoCoordinator::~PhotoCoordinator() {
	const std::shared_ptr<CallbackLifetimeToken> token =
		std::move(lifetime_token);
	if (token != nullptr)
		token->invalidate_and_wait();
}

void PhotoCoordinator::request_add_photos(
	const domain::PhotoOwner& owner) {
	if (shell_operation_state.active()) {
		apply_busy_result();
		return;
	}
	feedback.photo_diagnostics.clear();
	feedback.photo_message = localization.photo_workflow_text(
		localization::PhotoWorkflowMessageId::SelectImport);
	const std::weak_ptr<CallbackLifetimeToken> lifetime = lifetime_token;
	core::OperationResult picker_started =
		photo_selection_service.request_photo_selection(
			platform::PhotoSelectionRequest{
				.allow_multiple = true,
				.accepted_mime_types =
					platform::supported_source_image_picker_mime_types()},
			[this, lifetime,
			 owner](platform::PlatformValueResult<
					std::vector<platform::ContentSourceDescriptor>>
						result) mutable {
		const std::optional<CallbackLifetimeLease> callback_lease =
			CallbackLifetimeLease::try_acquire(lifetime);
		if (!callback_lease.has_value())
			return;
		if (result.was_user_cancelled()) {
			feedback.photo_message = localization.photo_workflow_text(
				localization::PhotoWorkflowMessageId::SelectionCancelled);
			refresh_all();
			return;
		}
		if (result.failed()) {
			feedback.photo_message = localization.photo_workflow_text(
				localization::PhotoWorkflowMessageId::SelectionFailed);
			feedback.photo_diagnostics = std::move(result.diagnostics);
			refresh_all();
			return;
		}
		const OperationRunner::Submission submission =
			shell_operation_runner.submit_direct_import(
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
				[this](OperationRunner::CompletionResult completion) {
			if (shell_operation_state.generation != completion.generation
				|| shell_operation_state.job_type != completion.job_type) {
				return;
			}
			shell_operation_state.state = ShellOperationState::Applying;
			apply_photo_import_result(std::get<PhotoImportSessionResult>(
				std::move(completion.value)));
			complete_shell_operation();
		});
		if (!submission.accepted) {
			apply_busy_result();
			return;
		}
		begin_shell_operation(ShellOperationJobType::DirectImport,
							  submission.generation);
	});
	if (picker_started.failed()) {
		feedback.photo_message = localization.photo_workflow_text(
			localization::PhotoWorkflowMessageId::PickerUnavailable);
		feedback.photo_diagnostics = picker_started.diagnostics();
		refresh_all();
	}
}

void PhotoCoordinator::request_add_pending_item_photos() {
	request_add_pending_photos(PendingPhotoDraftTarget::Item);
}

void PhotoCoordinator::request_add_pending_storage_photos() {
	request_add_pending_photos(PendingPhotoDraftTarget::Storage);
}

void PhotoCoordinator::request_add_pending_photos(
	PendingPhotoDraftTarget target) {
	if (shell_operation_state.active()) {
		apply_busy_result();
		return;
	}
	feedback.photo_diagnostics.clear();
	feedback.photo_message =
		target == PendingPhotoDraftTarget::Item
			? localization.photo_workflow_text(
				  localization::PhotoWorkflowMessageId::SelectPendingItem)
			: localization.photo_workflow_text(
				  localization::PhotoWorkflowMessageId::SelectPendingStorage);
	const std::weak_ptr<CallbackLifetimeToken> lifetime = lifetime_token;
	core::OperationResult picker_started =
		photo_selection_service.request_photo_selection(
			platform::PhotoSelectionRequest{
				.allow_multiple = true,
				.accepted_mime_types =
					platform::supported_source_image_picker_mime_types()},
			[this, lifetime,
			 target](platform::PlatformValueResult<
					 std::vector<platform::ContentSourceDescriptor>>
						 result) mutable {
		const std::optional<CallbackLifetimeLease> callback_lease =
			CallbackLifetimeLease::try_acquire(lifetime);
		if (!callback_lease.has_value())
			return;
		if (result.was_user_cancelled()) {
			feedback.photo_message = localization.photo_workflow_text(
				localization::PhotoWorkflowMessageId::
					PendingSelectionCancelled);
			refresh_all();
			return;
		}
		if (result.failed()) {
			feedback.photo_message = localization.photo_workflow_text(
				localization::PhotoWorkflowMessageId::PendingSelectionFailed);
			feedback.photo_diagnostics = std::move(result.diagnostics);
			refresh_all();
			return;
		}
		if (result.value->empty()) {
			feedback.photo_message = localization.photo_workflow_text(
				localization::PhotoWorkflowMessageId::PendingNoneSelected);
			refresh_all();
			return;
		}

		const ShellOperationJobType job_type =
			target == PendingPhotoDraftTarget::Item
				? ShellOperationJobType::PendingItemStaging
				: ShellOperationJobType::PendingStorageStaging;
		const OperationRunner::Submission submission =
			shell_operation_runner.submit_pending_staging(
				job_type,
				PendingPhotoStagingRequest{
					.current_session		  = session,
					.identifiers			  = identifiers,
					.operation_gate			  = operation_gate,
					.staging_service		  = content_staging_service,
					.fingerprint_service	  = source_fingerprint_service,
					.sources				  = std::move(*result.value),
					.existing_pending_sources = pending_sources_for(target),
					.existing_owner = owner_for_pending_target(target)},
				[this,
				 target](OperationRunner::CompletionResult completion) {
			if (shell_operation_state.generation != completion.generation
				|| shell_operation_state.job_type != completion.job_type) {
				return;
			}
			shell_operation_state.state = ShellOperationState::Applying;
			apply_pending_photo_staging_result(
				std::get<PendingPhotoStagingResult>(
					std::move(completion.value)),
				target);
			complete_shell_operation();
		});
		if (!submission.accepted) {
			apply_busy_result();
			return;
		}
		begin_shell_operation(job_type, submission.generation);
	});
	if (picker_started.failed()) {
		feedback.photo_message = localization.photo_workflow_text(
			localization::PhotoWorkflowMessageId::PendingPickerUnavailable);
		feedback.photo_diagnostics = picker_started.diagnostics();
		refresh_all();
	}
}

std::vector<PendingPhotoSource> PhotoCoordinator::pending_sources_for(
	PendingPhotoDraftTarget target) const {
	if (target == PendingPhotoDraftTarget::Item)
		return item_form.pending_photos;
	return storage_form.pending_photos;
}

std::optional<domain::PhotoOwner>
PhotoCoordinator::owner_for_pending_target(
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

void PhotoCoordinator::request_export_photo(
	const core::StableIdentifier& photo_id) {
	feedback.photo_diagnostics.clear();
	const std::string suggested_name =
		catalog::suggested_jpeg_export_file_name(session.repository, photo_id);
	const std::weak_ptr<CallbackLifetimeToken> lifetime = lifetime_token;
	core::OperationResult destination_started =
		document_export_service.request_export_destination_selection(
			platform::DocumentExportRequest{
				.suggested_file_name = suggested_name,
				.mime_type			 = "image/jpeg",
				.purpose			 = "photo JPEG export"},
			[this, lifetime, photo_id](platform::PlatformValueResult<
									   platform::DocumentDestinationDescriptor>
										   result) mutable {
		const std::optional<CallbackLifetimeLease> callback_lease =
			CallbackLifetimeLease::try_acquire(lifetime);
		if (!callback_lease.has_value())
			return;
		if (result.was_user_cancelled()) {
			feedback.photo_message = localization.photo_workflow_text(
				localization::PhotoWorkflowMessageId::JpegDestinationCancelled);
			refresh_all();
			return;
		}
		if (result.failed()) {
			feedback.photo_message = localization.photo_workflow_text(
				localization::PhotoWorkflowMessageId::JpegDestinationFailed);
			feedback.photo_diagnostics = std::move(result.diagnostics);
			refresh_all();
			return;
		}
		if (!session.paths.has_value()) {
			feedback.photo_message = localization.photo_workflow_text(
				localization::PhotoWorkflowMessageId::JpegFailed);
			refresh_all();
			return;
		}

		const OperationRunner::Submission submission =
			shell_operation_runner.submit_jpeg_export(
				catalog::PhotoExportRequest{
					.current_state = session.repository,
					.paths		   = *session.paths,
					.photo_id	   = photo_id,
					.destination   = std::move(*result.value),
					.jpeg_quality  = 90},
				[this](OperationRunner::CompletionResult completion) {
			if (!shell_operation_state.active()
				|| shell_operation_state.generation != completion.generation
				|| shell_operation_state.job_type != completion.job_type) {
				return;
			}
			shell_operation_state.state = ShellOperationState::Applying;
			catalog::PhotoExportResult exported =
				std::get<catalog::PhotoExportResult>(
					std::move(completion.value));
			feedback.photo_diagnostics = std::move(exported.diagnostics);
			if (exported.succeeded())
				feedback.photo_message =
					localization.jpeg_export_completed(exported.bytes_written);
			else if (exported.was_user_cancelled())
				feedback.photo_message = localization.photo_workflow_text(
					localization::PhotoWorkflowMessageId::JpegCancelled);
			else
				feedback.photo_message = localization.photo_workflow_text(
					localization::PhotoWorkflowMessageId::JpegFailed);
			complete_shell_operation();
		});
		if (!submission.accepted) {
			apply_busy_result();
			return;
		}
		begin_shell_operation(ShellOperationJobType::JpegExport,
							  submission.generation);
	});
	if (destination_started.failed()) {
		feedback.photo_message = localization.photo_workflow_text(
			localization::PhotoWorkflowMessageId::JpegPickerUnavailable);
		feedback.photo_diagnostics = destination_started.diagnostics();
		refresh_all();
	}
}

void PhotoCoordinator::request_delete_photo_confirmation(
	const core::StableIdentifier& photo_id) {
	photo_display.pending_delete_photo_id = photo_id;
	feedback.photo_message				  = localization.photo_workflow_text(
		localization::PhotoWorkflowMessageId::DeleteInstruction);
	refresh_all();
}

void PhotoCoordinator::cancel_delete_photo_confirmation() {
	photo_display.pending_delete_photo_id.reset();
	feedback.photo_message = localization.photo_workflow_text(
		localization::PhotoWorkflowMessageId::DeleteCancelled);
	refresh_all();
}

void PhotoCoordinator::confirm_delete_photo(
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

void PhotoCoordinator::apply_pending_photo_staging_result(
	PendingPhotoStagingResult result, PendingPhotoDraftTarget target) {
	feedback.photo_diagnostics = std::move(result.diagnostics);
	std::vector<PendingPhotoSource>& pending_photos =
		target == PendingPhotoDraftTarget::Item ? item_form.pending_photos
												: storage_form.pending_photos;
	ManagedPhotoDeckState& photo_deck =
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
		feedback.photo_message = localization.pending_photo_staging_completed(
			localization::PendingPhotoStagingCompletion{
				.staged_count = result.staged_count,
				.failed_count = result.failure_count});
	} else if (result.was_user_cancelled()) {
		feedback.photo_message = localization.photo_workflow_text(
			localization::PhotoWorkflowMessageId::PendingStagingCancelled);
	} else {
		feedback.photo_message = localization.photo_workflow_text(
			localization::PhotoWorkflowMessageId::PendingStagingFailed);
	}
	refresh_all();
}

void PhotoCoordinator::apply_busy_result() {
	feedback.photo_message =
		localization.text(localization::MessageId::ShellOperationBusy);
	refresh_all();
}

void PhotoCoordinator::begin_shell_operation(
	ShellOperationJobType job_type, std::uint64_t generation) {
	if (begin_shell_operation_handler)
		begin_shell_operation_handler(job_type, generation);
}

void PhotoCoordinator::complete_shell_operation() {
	if (complete_shell_operation_handler)
		complete_shell_operation_handler();
}

void PhotoCoordinator::apply_photo_import_result(
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
		feedback.photo_message = localization.photo_import_completed(
			localization::PhotoImportCompletion{
				.imported_count = result.summary.success_count,
				.failed_count	= result.summary.failure_count});
		if (!result.imported_photo_ids.empty())
			route.selected_photo_id = result.imported_photo_ids.front();
		photo_display.displayed_photo_id.reset();
	} else if (result.was_user_cancelled()) {
		feedback.photo_message = localization.photo_workflow_text(
			localization::PhotoWorkflowMessageId::ImportCancelled);
	} else {
		feedback.photo_message = localization.photo_workflow_text(
			localization::PhotoWorkflowMessageId::ImportFailed);
	}
	refresh_all();
}

void PhotoCoordinator::apply_photo_edit_result(
	EntityEditResult result,
	const core::StableIdentifier& selected_photo_id_value) {
	copy_entity_diagnostics_to_photo_feedback(feedback, result.diagnostics);
	if (result.failed()) {
		feedback.photo_message = localization.photo_workflow_text(
			localization::PhotoWorkflowMessageId::MetadataUpdateFailed);
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
	feedback.photo_message = localization.photo_workflow_text(
		result.metadata_changed
			? localization::PhotoWorkflowMessageId::MainUpdated
			: localization::PhotoWorkflowMessageId::MainUnchanged);
	refresh_all();
}

void PhotoCoordinator::apply_photo_delete_result(
	EntityEditResult result,
	std::optional<core::StableIdentifier> next_photo_id) {
	copy_entity_diagnostics_to_photo_feedback(feedback, result.diagnostics);
	if (result.failed()) {
		feedback.photo_message = localization.photo_workflow_text(
			localization::PhotoWorkflowMessageId::DeleteFailed);
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
	feedback.photo_message =
		localization.photo_deletion_outcome(localization::PhotoDeletionOutcome{
			.state = result.metadata_changed
						 ? localization::PhotoDeletionState::Deleted
						 : localization::PhotoDeletionState::NoChanges,
			.media_cleanup_needs_attention =
				!feedback.photo_diagnostics.empty()});
	refresh_all();
}

void PhotoCoordinator::cleanup_item_pending_photos() {
	cleanup_pending_photos(item_form.pending_photos, item_form.photo_deck);
}

void PhotoCoordinator::cleanup_storage_pending_photos() {
	cleanup_pending_photos(storage_form.pending_photos,
						   storage_form.photo_deck);
}

void PhotoCoordinator::cleanup_pending_photos(
	std::vector<PendingPhotoSource>& pending_photos,
	ManagedPhotoDeckState& photo_deck) {
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
	feedback.photo_message = localization.photo_workflow_text(
		cleanup.failed()
			? localization::PhotoWorkflowMessageId::PendingCleanupPartial
			: localization::PhotoWorkflowMessageId::PendingCleared);
}

void PhotoCoordinator::remove_item_pending_photo(
	std::size_t pending_photo_index) {
	remove_pending_photo(item_form.pending_photos, item_form.photo_deck,
						 pending_photo_index);
}

void PhotoCoordinator::remove_storage_pending_photo(
	std::size_t pending_photo_index) {
	remove_pending_photo(storage_form.pending_photos, storage_form.photo_deck,
						 pending_photo_index);
}

void PhotoCoordinator::remove_pending_photo(
	std::vector<PendingPhotoSource>& pending_photos,
	ManagedPhotoDeckState& photo_deck,
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
		feedback.photo_message = localization.photo_workflow_text(
			localization::PhotoWorkflowMessageId::PendingCleanupRetry);
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
	feedback.photo_message = localization.photo_workflow_text(
		localization::PhotoWorkflowMessageId::PendingRemoved);
	refresh_all();
}

void PhotoCoordinator::refresh_all() {
	if (refresh_all_handler)
		refresh_all_handler();
}
}	 // namespace shuba::ui
