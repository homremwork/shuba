#include "UI/AppShellPhotoCoordinator.hpp"

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

class AppShellPhotoCoordinator::CallbackLifetimeLease final {
public:
	[[nodiscard]] static std::optional<CallbackLifetimeLease> try_acquire(
		const std::weak_ptr<LifetimeToken>& lifetime) {
		const std::shared_ptr<LifetimeToken> token = lifetime.lock();
		if (token == nullptr || !token->alive.load(std::memory_order_acquire))
			return std::nullopt;

		token->callback_count.fetch_add(1U, std::memory_order_acq_rel);
		if (!token->alive.load(std::memory_order_acquire)) {
			token->callback_count.fetch_sub(1U, std::memory_order_acq_rel);
			token->callbacks_finished.notify_all();
			return std::nullopt;
		}
		return CallbackLifetimeLease{token};
	}

	CallbackLifetimeLease(const CallbackLifetimeLease&)			   = delete;
	CallbackLifetimeLease& operator=(const CallbackLifetimeLease&) = delete;
	CallbackLifetimeLease(CallbackLifetimeLease&&) noexcept		   = default;
	CallbackLifetimeLease& operator=(CallbackLifetimeLease&&) noexcept = delete;

	~CallbackLifetimeLease() {
		if (token == nullptr)
			return;
		if (token->callback_count.fetch_sub(1U, std::memory_order_acq_rel)
			== 1U) {
			token->callbacks_finished.notify_all();
		}
	}

private:
	explicit CallbackLifetimeLease(std::shared_ptr<LifetimeToken> token_value)
		: token(std::move(token_value)) {}

	std::shared_ptr<LifetimeToken> token;
};

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
	, localization(dependencies.localization)
	, invalidate_all_previews_handler(
		  std::move(dependencies.invalidate_all_previews))
	, invalidate_internal_photo_preview_handler(
		  std::move(dependencies.invalidate_internal_photo_preview))
	, invalidate_staged_photo_preview_handler(
		  std::move(dependencies.invalidate_staged_photo_preview))
	, refresh_all_handler(std::move(dependencies.refresh_all)) {}

AppShellPhotoCoordinator::~AppShellPhotoCoordinator() {
	const std::shared_ptr<LifetimeToken> token = std::move(lifetime_token);
	if (token == nullptr)
		return;

	token->alive.store(false, std::memory_order_release);
	std::unique_lock<std::mutex> lock{token->mutex};
	token->callbacks_finished.wait(lock, [&token] {
		return token->callback_count.load(std::memory_order_acquire) == 0U;
	});
}

void AppShellPhotoCoordinator::request_add_photos(
	const domain::PhotoOwner& owner) {
	progress_events.clear();
	feedback.photo_diagnostics.clear();
	feedback.photo_message = localization.photo_workflow_text(
		localization::PhotoWorkflowMessageId::SelectImport);
	const std::weak_ptr<LifetimeToken> lifetime = lifetime_token;
	core::OperationResult picker_started =
		photo_selection_service.request_photo_selection(
			platform::PhotoSelectionRequest{
				.allow_multiple		 = true,
				.accepted_mime_types = {"image/jpeg", "image/png", "image/webp",
										"image/heic", "image/heif"}},
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
		feedback.photo_message = localization.photo_workflow_text(
			localization::PhotoWorkflowMessageId::PickerUnavailable);
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
			? localization.photo_workflow_text(
				  localization::PhotoWorkflowMessageId::SelectPendingItem)
			: localization.photo_workflow_text(
				  localization::PhotoWorkflowMessageId::SelectPendingStorage);
	const std::weak_ptr<LifetimeToken> lifetime = lifetime_token;
	core::OperationResult picker_started =
		photo_selection_service.request_photo_selection(
			platform::PhotoSelectionRequest{
				.allow_multiple		 = true,
				.accepted_mime_types = {"image/jpeg", "image/png", "image/webp",
										"image/heic", "image/heif"}},
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
		feedback.photo_message = localization.photo_workflow_text(
			localization::PhotoWorkflowMessageId::PendingPickerUnavailable);
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
	const std::weak_ptr<LifetimeToken> lifetime = lifetime_token;
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
			feedback.photo_message =
				localization.jpeg_export_completed(exported.bytes_written);
		else if (exported.was_user_cancelled())
			feedback.photo_message = localization.photo_workflow_text(
				localization::PhotoWorkflowMessageId::JpegCancelled);
		else
			feedback.photo_message = localization.photo_workflow_text(
				localization::PhotoWorkflowMessageId::JpegFailed);
		refresh_all();
	});
	if (destination_started.failed()) {
		feedback.photo_message = localization.photo_workflow_text(
			localization::PhotoWorkflowMessageId::JpegPickerUnavailable);
		feedback.photo_diagnostics = destination_started.diagnostics();
		refresh_all();
	}
}

void AppShellPhotoCoordinator::request_delete_photo_confirmation(
	const core::StableIdentifier& photo_id) {
	photo_display.pending_delete_photo_id = photo_id;
	feedback.photo_message				  = localization.photo_workflow_text(
		localization::PhotoWorkflowMessageId::DeleteInstruction);
	refresh_all();
}

void AppShellPhotoCoordinator::cancel_delete_photo_confirmation() {
	photo_display.pending_delete_photo_id.reset();
	feedback.photo_message = localization.photo_workflow_text(
		localization::PhotoWorkflowMessageId::DeleteCancelled);
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

void AppShellPhotoCoordinator::apply_photo_edit_result(
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

void AppShellPhotoCoordinator::apply_photo_delete_result(
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
	feedback.photo_message = localization.photo_workflow_text(
		cleanup.failed()
			? localization::PhotoWorkflowMessageId::PendingCleanupPartial
			: localization::PhotoWorkflowMessageId::PendingCleared);
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

void AppShellPhotoCoordinator::refresh_all() {
	if (refresh_all_handler)
		refresh_all_handler();
}
}	 // namespace shuba::ui
