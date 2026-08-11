#include "UI/AppShellEditCoordinator.hpp"

#include "Localization/Facade.hpp"

#include "Catalog/CatalogRepository.hpp"
#include "UI/Session/EntityEditSession.hpp"
#include "UI/Session/PhotoSession.hpp"
#include "UI/View/ScreenText.hpp"

#include <algorithm>
#include <utility>

namespace shuba::ui {
namespace {
void append_entity_diagnostics_to_core_feedback(
	std::vector<core::Diagnostic>& target,
	const std::vector<EntityEditDiagnostic>& diagnostics) {
	for (const EntityEditDiagnostic& diagnostic : diagnostics) {
		target.push_back(core::Diagnostic{
			.severity		   = diagnostic.severity,
			.code			   = diagnostic.code,
			.message		   = diagnostic.message,
			.technical_details = diagnostic.technical_details});
	}
}
}	 // namespace

AppShellEditCoordinator::AppShellEditCoordinator(Dependencies dependencies)
	: session(dependencies.session)
	, route(dependencies.route)
	, item_form(dependencies.item_form)
	, storage_form(dependencies.storage_form)
	, feedback(dependencies.feedback)
	, identifiers(dependencies.identifiers)
	, clock(dependencies.clock)
	, operation_gate(dependencies.operation_gate)
	, content_staging_service(dependencies.content_staging_service)
	, source_fingerprint_service(dependencies.source_fingerprint_service)
	, source_decode_service(dependencies.source_decode_service)
	, internal_photo_codec(dependencies.internal_photo_codec)
	, photo_operation_runner(dependencies.photo_operation_runner)
	, photo_operation_state(dependencies.photo_operation_state)
	, localization(dependencies.localization)
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
	, cleanup_item_pending_photos_handler(
		  std::move(dependencies.cleanup_item_pending_photos))
	, cleanup_storage_pending_photos_handler(
		  std::move(dependencies.cleanup_storage_pending_photos))
	, invalidate_all_previews_handler(
		  std::move(dependencies.invalidate_all_previews))
	, refresh_all_handler(std::move(dependencies.refresh_all))
	, refresh_content_handler(std::move(dependencies.refresh_content))
	, begin_photo_operation_handler(
		  std::move(dependencies.begin_photo_operation))
	, complete_photo_operation_handler(
		  std::move(dependencies.complete_photo_operation)) {}

void AppShellEditCoordinator::open_new_item_form(
	std::optional<core::StableIdentifier> storage_id) {
	reset_item_form();
	item_form.mode				  = FormMode::Create;
	item_form.draft.storage_id	  = std::move(storage_id);
	route.form_return_destination = route.selected_storage_id
										? RootDestination::StorageDetail
										: RootDestination::Add;
	route.destination			  = RootDestination::ItemForm;
	if (refresh_all_handler)
		refresh_all_handler();
}

void AppShellEditCoordinator::open_existing_item_form(
	core::StableIdentifier item_id) {
	const persistence::ItemEnvelope* item =
		catalog::find_item_envelope(session.repository, item_id);
	if (item == nullptr)
		return;
	load_item_form_from_record(*item);
	route.selected_item_id		  = std::move(item_id);
	item_form.mode				  = FormMode::Edit;
	route.form_return_destination = RootDestination::ItemDetail;
	route.destination			  = RootDestination::ItemForm;
	if (refresh_all_handler)
		refresh_all_handler();
}

void AppShellEditCoordinator::open_new_storage_form(
	std::optional<core::StableIdentifier> parent_id) {
	reset_storage_form();
	storage_form.mode					 = FormMode::Create;
	storage_form.draft.parent_storage_id = std::move(parent_id);
	route.form_return_destination		 = route.selected_storage_id
											   ? RootDestination::StorageDetail
											   : RootDestination::Add;
	route.destination					 = RootDestination::StorageForm;
	if (refresh_all_handler)
		refresh_all_handler();
}

void AppShellEditCoordinator::open_existing_storage_form(
	core::StableIdentifier storage_id) {
	const persistence::StorageEnvelope* storage =
		catalog::find_storage_envelope(session.repository, storage_id);
	if (storage == nullptr)
		return;
	load_storage_form_from_record(*storage);
	route.selected_storage_id	  = std::move(storage_id);
	storage_form.mode			  = FormMode::Edit;
	route.form_return_destination = RootDestination::StorageDetail;
	route.destination			  = RootDestination::StorageForm;
	if (refresh_all_handler)
		refresh_all_handler();
}

void AppShellEditCoordinator::set_item_pending_photo_as_main(
	std::size_t pending_photo_index) {
	set_pending_photo_as_main(item_form.photo_deck, item_form.pending_photos,
							  pending_photo_index);
}

void AppShellEditCoordinator::set_storage_pending_photo_as_main(
	std::size_t pending_photo_index) {
	set_pending_photo_as_main(storage_form.photo_deck,
							  storage_form.pending_photos, pending_photo_index);
}

void AppShellEditCoordinator::save_item_form() {
	if (photo_operation_state.active()) {
		feedback.photo_message =
			localization.text(localization::MessageId::PhotoOperationBusy);
		if (refresh_all_handler)
			refresh_all_handler();
		return;
	}
	item_form.draft.display_name = item_name_editor.getText().toStdString();
	item_form.draft.category	 = item_category_editor.getText().toStdString();
	item_form.draft.notes		 = item_notes_editor.getText().toStdString();
	item_form.draft.listing.marketplace =
		item_listing_marketplace_editor.getText().toStdString();
	item_form.draft.listing.url =
		item_listing_url_editor.getText().toStdString();
	item_form.draft.listing.note =
		item_listing_note_editor.getText().toStdString();
	item_form.draft.acquisition.source =
		item_acquisition_source_editor.getText().toStdString();
	item_form.draft.pending_photo_import_planned =
		has_ready_pending_photo(item_form.pending_photos);

	if (!has_ready_pending_photo(item_form.pending_photos)) {
		EntityEditResult result = save_item_draft(
			EntityEditRequest{.current_session = session,
							  .identifiers = identifiers,
							  .clock = clock},
			item_form.draft);
		if (result.warning_acknowledgement_required)
			item_form.draft.warning_acknowledged = true;
		if (result.warning_acknowledgement_required && result.saved_record_id)
			item_form.draft.reserved_new_id = result.saved_record_id;
		if (result.succeeded())
			item_form.draft.existing_id = result.saved_record_id;
		apply_entity_edit_result(std::move(result));
		return;
	}

	const AppShellPhotoOperationRunner::Submission submission =
		photo_operation_runner.submit_item_save(
		ItemSaveWithPendingPhotosRequest{
			.current_session = session,
			.identifiers = identifiers,
			.clock = clock,
			.operation_gate = operation_gate,
			.staging_service = content_staging_service,
			.fingerprint_service = source_fingerprint_service,
			.decode_service = source_decode_service,
			.photo_codec = internal_photo_codec,
			.draft = item_form.draft,
			.pending_sources = item_form.pending_photos,
			.main_pending_source_index = item_form.photo_deck.staged_main_index},
		[this](AppShellPhotoOperationRunner::Result operation_result) {
			photo_operation_state.state = PhotoOperationState::Applying;
			ItemSaveWithPendingPhotosResult result =
				std::get<ItemSaveWithPendingPhotosResult>(std::move(operation_result));
			if (result.warning_acknowledgement_required())
				item_form.draft.warning_acknowledged = true;
			if (result.warning_acknowledgement_required()
				&& result.save_result.saved_record_id) {
				item_form.draft.reserved_new_id = result.save_result.saved_record_id;
			}
			if (result.item_saved())
				item_form.draft.existing_id = result.save_result.saved_record_id;
			apply_item_save_with_pending_photos_result(std::move(result));
			if (complete_photo_operation_handler)
				complete_photo_operation_handler();
		});
	if (!submission.accepted) {
		feedback.photo_message =
			localization.text(localization::MessageId::PhotoOperationBusy);
		if (refresh_all_handler)
			refresh_all_handler();
		return;
	}
	if (begin_photo_operation_handler) {
		begin_photo_operation_handler(
			PhotoOperationJobType::ItemSaveWithPendingPhotos,
			submission.generation);
	}
}

void AppShellEditCoordinator::save_storage_form() {
	if (photo_operation_state.active()) {
		feedback.photo_message =
			localization.text(localization::MessageId::PhotoOperationBusy);
		if (refresh_all_handler)
			refresh_all_handler();
		return;
	}
	storage_form.draft.display_name =
		storage_name_editor.getText().toStdString();
	storage_form.draft.storage_type =
		storage_type_editor.getText().toStdString();
	storage_form.draft.location =
		storage_location_editor.getText().toStdString();
	storage_form.draft.notes = storage_notes_editor.getText().toStdString();
	storage_form.draft.archive_warning_acknowledged =
		storage_form.archive_warning_acknowledged;
	if (!has_ready_pending_photo(storage_form.pending_photos)) {
		EntityEditResult result = save_storage_draft(
			EntityEditRequest{.current_session = session,
							  .identifiers = identifiers,
							  .clock = clock},
			storage_form.draft);
		if (result.warning_acknowledgement_required)
			storage_form.archive_warning_acknowledged = true;
		if (result.warning_acknowledgement_required && result.saved_record_id)
			storage_form.draft.reserved_new_id = result.saved_record_id;
		if (result.succeeded())
			storage_form.draft.existing_id = result.saved_record_id;
		apply_entity_edit_result(std::move(result));
		return;
	}

	const AppShellPhotoOperationRunner::Submission submission =
		photo_operation_runner.submit_storage_save(
		StorageSaveWithPendingPhotosRequest{
			.current_session = session,
			.identifiers = identifiers,
			.clock = clock,
			.operation_gate = operation_gate,
			.staging_service = content_staging_service,
			.fingerprint_service = source_fingerprint_service,
			.decode_service = source_decode_service,
			.photo_codec = internal_photo_codec,
			.draft = storage_form.draft,
			.pending_sources = storage_form.pending_photos,
			.main_pending_source_index = storage_form.photo_deck.staged_main_index},
		[this](AppShellPhotoOperationRunner::Result operation_result) {
			photo_operation_state.state = PhotoOperationState::Applying;
			StorageSaveWithPendingPhotosResult result =
				std::get<StorageSaveWithPendingPhotosResult>(
					std::move(operation_result));
			if (result.warning_acknowledgement_required())
				storage_form.archive_warning_acknowledged = true;
			if (result.warning_acknowledgement_required()
				&& result.save_result.saved_record_id) {
				storage_form.draft.reserved_new_id =
					result.save_result.saved_record_id;
			}
			if (result.storage_saved())
				storage_form.draft.existing_id = result.save_result.saved_record_id;
			apply_storage_save_with_pending_photos_result(std::move(result));
			if (complete_photo_operation_handler)
				complete_photo_operation_handler();
		});
	if (!submission.accepted) {
		feedback.photo_message =
			localization.text(localization::MessageId::PhotoOperationBusy);
		if (refresh_all_handler)
			refresh_all_handler();
		return;
	}
	if (begin_photo_operation_handler) {
		begin_photo_operation_handler(
			PhotoOperationJobType::StorageSaveWithPendingPhotos,
			submission.generation);
	}
}

void AppShellEditCoordinator::apply_entity_edit_result(
	EntityEditResult result) {
	feedback.edit_diagnostics = std::move(result.diagnostics);
	if (result.warning_acknowledgement_required) {
		feedback.edit_message = "Confirm warning and save again.";
		if (refresh_all_handler)
			refresh_all_handler();
		return;
	}
	if (result.failed()) {
		feedback.edit_message = "Save failed.";
		if (refresh_all_handler)
			refresh_all_handler();
		return;
	}
	const RootDestination completed_destination = route.destination;
	const std::optional<core::StableIdentifier> saved_item_id =
		item_form.draft.existing_id;
	const std::optional<core::StableIdentifier> saved_storage_id =
		storage_form.draft.existing_id;
	session = std::move(result.session);
	if (invalidate_all_previews_handler)
		invalidate_all_previews_handler();
	feedback.edit_message = result.metadata_changed ? "Saved." : "No changes.";
	feedback.edit_diagnostics.clear();
	if (completed_destination == RootDestination::ItemForm && saved_item_id) {
		route.selected_item_id = *saved_item_id;
		route.destination	   = RootDestination::ItemDetail;
	} else if (completed_destination == RootDestination::StorageForm
			   && saved_storage_id) {
		route.selected_storage_id = *saved_storage_id;
		route.destination		  = RootDestination::StorageDetail;
	} else {
		route.destination =
			route.form_return_destination.value_or(RootDestination::Catalog);
	}
	if (refresh_all_handler)
		refresh_all_handler();
}

void AppShellEditCoordinator::set_pending_photo_as_main(
	AppShellManagedPhotoDeckState& photo_deck,
	std::vector<PendingPhotoSource>& pending_photos,
	std::size_t pending_photo_index) {
	if (pending_photo_index >= pending_photos.size()
		|| !pending_photos[pending_photo_index].ready_for_import()) {
		photo_deck.staged_main_index.reset();
		feedback.photo_message =
			"Selected staged photo is not ready to become main.";
		if (refresh_content_handler)
			refresh_content_handler();
		return;
	}

	photo_deck.staged_main_index = pending_photo_index;
	photo_deck.staged_selected	 = true;
	photo_deck.selected_index	 = pending_photo_index;
	feedback.photo_message =
		"Staged photo will become main after the edit is saved.";
	if (refresh_content_handler)
		refresh_content_handler();
}

void AppShellEditCoordinator::reset_item_form() {
	if (cleanup_item_pending_photos_handler)
		cleanup_item_pending_photos_handler();
	item_form.draft						  = ItemDraft{};
	item_form.mode						  = FormMode::Create;
	item_form.storage_candidates_expanded = false;
	item_form.tag_candidates_expanded	  = false;
	item_form.listing_expanded			  = false;
	item_form.finance_expanded			  = false;
	item_form.photo_deck				  = AppShellManagedPhotoDeckState{};
	for (juce::TextEditor* editor :
		 {&item_name_editor, &item_category_editor, &item_notes_editor,
		  &item_listing_marketplace_editor, &item_listing_url_editor,
		  &item_listing_note_editor, &item_acquisition_source_editor}) {
		editor->setText(juce::String{}, juce::dontSendNotification);
	}
}

void AppShellEditCoordinator::reset_storage_form() {
	if (cleanup_storage_pending_photos_handler)
		cleanup_storage_pending_photos_handler();
	storage_form.draft						  = StorageDraft{};
	storage_form.mode						  = FormMode::Create;
	storage_form.parent_candidates_expanded	  = false;
	storage_form.tag_candidates_expanded	  = false;
	storage_form.archive_warning_acknowledged = false;
	storage_form.photo_deck					  = AppShellManagedPhotoDeckState{};
	for (juce::TextEditor* editor :
		 {&storage_name_editor, &storage_type_editor, &storage_location_editor,
		  &storage_notes_editor}) {
		editor->setText(juce::String{}, juce::dontSendNotification);
	}
}

void AppShellEditCoordinator::load_item_form_from_record(
	const persistence::ItemEnvelope& item) {
	item_form.draft = ItemDraft{.existing_id  = item.record.id,
								.display_name = item.record.display_name,
								.category	  = item.record.category,
								.storage_id	  = item.record.storage_id,
								.tags		  = item.record.tags,
								.notes		  = item.record.notes,
								.status		  = item.record.status,
								.listing	  = item.record.listing,
								.acquisition  = item.record.acquisition,
								.finance	  = item.record.finance,
								.warning_acknowledged = true};
	item_name_editor.setText(juce_text(item.record.display_name),
							 juce::dontSendNotification);
	item_category_editor.setText(juce_text(item.record.category),
								 juce::dontSendNotification);
	item_notes_editor.setText(juce_text(item.record.notes),
							  juce::dontSendNotification);
	item_listing_marketplace_editor.setText(
		juce_text(item.record.listing.marketplace), juce::dontSendNotification);
	item_listing_url_editor.setText(juce_text(item.record.listing.url),
									juce::dontSendNotification);
	item_listing_note_editor.setText(juce_text(item.record.listing.note),
									 juce::dontSendNotification);
	item_acquisition_source_editor.setText(
		juce_text(item.record.acquisition.source), juce::dontSendNotification);
	item_form.listing_expanded = !item.record.listing.empty();
	item_form.finance_expanded =
		!item.record.acquisition.empty() || !item.record.finance.empty();
	item_form.storage_candidates_expanded = false;
	item_form.tag_candidates_expanded	  = false;
	item_form.photo_deck				  = AppShellManagedPhotoDeckState{};
}

void AppShellEditCoordinator::load_storage_form_from_record(
	const persistence::StorageEnvelope& storage) {
	storage_form.draft =
		StorageDraft{.existing_id		= storage.record.id,
					 .display_name		= storage.record.display_name,
					 .storage_type		= storage.record.storage_type,
					 .parent_storage_id = storage.record.parent_storage_id,
					 .location			= storage.record.location,
					 .tags				= storage.record.tags,
					 .notes				= storage.record.notes,
					 .lifecycle_status	= storage.record.lifecycle_status,
					 .archive_warning_acknowledged = true};
	storage_name_editor.setText(juce_text(storage.record.display_name),
								juce::dontSendNotification);
	storage_type_editor.setText(juce_text(storage.record.storage_type),
								juce::dontSendNotification);
	storage_location_editor.setText(juce_text(storage.record.location),
									juce::dontSendNotification);
	storage_notes_editor.setText(juce_text(storage.record.notes),
								 juce::dontSendNotification);
	storage_form.parent_candidates_expanded	  = false;
	storage_form.tag_candidates_expanded	  = false;
	storage_form.archive_warning_acknowledged = false;
	storage_form.photo_deck					  = AppShellManagedPhotoDeckState{};
}

void AppShellEditCoordinator::apply_item_save_with_pending_photos_result(
	ItemSaveWithPendingPhotosResult result) {
	feedback.edit_diagnostics = result.save_result.diagnostics;
	item_form.pending_photos  = std::move(result.pending_sources);
	std::erase_if(item_form.pending_photos,
				  [](const PendingPhotoSource& source) {
		return !source.staged_path.has_value();
	});
	if (result.warning_acknowledgement_required()) {
		feedback.edit_message = "Confirm warning and save again.";
		if (refresh_all_handler)
			refresh_all_handler();
		return;
	}
	if (result.save_result.failed()) {
		feedback.edit_message = "Save failed.";
		if (refresh_all_handler)
			refresh_all_handler();
		return;
	}

	feedback.photo_diagnostics.clear();
	if (result.import_attempted) {
		append_entity_diagnostics_to_core_feedback(
			feedback.photo_diagnostics, result.import_result.diagnostics);
		feedback.photo_diagnostics.insert(
			feedback.photo_diagnostics.end(),
			result.cleanup_result.diagnostics.begin(),
			result.cleanup_result.diagnostics.end());
		if (result.main_selection_attempted) {
			append_entity_diagnostics_to_core_feedback(
				feedback.photo_diagnostics,
				result.main_selection_result.diagnostics);
		}

		const localization::PendingSavePhotoImportState import_state =
			result.import_result.succeeded()
				? localization::PendingSavePhotoImportState::Completed
			: result.import_result.was_user_cancelled()
				? localization::PendingSavePhotoImportState::Cancelled
				: localization::PendingSavePhotoImportState::Failed;
		const localization::PendingSaveCleanupState cleanup_state =
			result.cleanup_attempted && result.cleanup_result.failed()
				? localization::PendingSaveCleanupState::NeedsAttention
				: localization::PendingSaveCleanupState::Clear;
		const localization::PendingSaveMainPhotoState main_photo_state =
			result.main_selection_attempted
				? result.main_selected_photo_id.has_value()
					  ? localization::PendingSaveMainPhotoState::Applied
					  : localization::PendingSaveMainPhotoState::NotApplied
				: localization::PendingSaveMainPhotoState::NotSelected;
		feedback.photo_message = localization.pending_save_photo_outcome(
			localization::PendingSavePhotoOutcome{
				.owner			  = localization::PendingSavePhotoOwner::Item,
				.import_state	  = import_state,
				.imported_count	  = result.import_result.summary.success_count,
				.failed_count	  = result.import_result.summary.failure_count,
				.cleanup_state	  = cleanup_state,
				.main_photo_state = main_photo_state});
		if (result.main_selected_photo_id.has_value())
			route.selected_photo_id = result.main_selected_photo_id;
		else if (!result.import_result.imported_photo_ids.empty())
			route.selected_photo_id =
				result.import_result.imported_photo_ids.front();
	} else if (!item_form.pending_photos.empty()) {
		feedback.photo_message = localization.pending_save_photo_outcome(
			localization::PendingSavePhotoOutcome{
				.owner = localization::PendingSavePhotoOwner::Item});
	}

	session = std::move(result.session);
	if (invalidate_all_previews_handler)
		invalidate_all_previews_handler();
	item_form.photo_deck.staged_main_index.reset();
	feedback.edit_message =
		result.save_result.metadata_changed ? "Saved." : "No changes.";
	feedback.edit_diagnostics.clear();
	if (item_form.draft.existing_id) {
		route.selected_item_id = *item_form.draft.existing_id;
		route.destination	   = RootDestination::ItemDetail;
	} else {
		route.destination =
			route.form_return_destination.value_or(RootDestination::Catalog);
	}
	if (refresh_all_handler)
		refresh_all_handler();
}

void AppShellEditCoordinator::apply_storage_save_with_pending_photos_result(
	StorageSaveWithPendingPhotosResult result) {
	feedback.edit_diagnostics	= result.save_result.diagnostics;
	storage_form.pending_photos = std::move(result.pending_sources);
	std::erase_if(storage_form.pending_photos,
				  [](const PendingPhotoSource& source) {
		return !source.staged_path.has_value();
	});
	if (result.warning_acknowledgement_required()) {
		feedback.edit_message = "Confirm warning and save again.";
		if (refresh_all_handler)
			refresh_all_handler();
		return;
	}
	if (result.save_result.failed()) {
		feedback.edit_message = "Save failed.";
		if (refresh_all_handler)
			refresh_all_handler();
		return;
	}

	feedback.photo_diagnostics.clear();
	if (result.import_attempted) {
		append_entity_diagnostics_to_core_feedback(
			feedback.photo_diagnostics, result.import_result.diagnostics);
		feedback.photo_diagnostics.insert(
			feedback.photo_diagnostics.end(),
			result.cleanup_result.diagnostics.begin(),
			result.cleanup_result.diagnostics.end());
		if (result.main_selection_attempted) {
			append_entity_diagnostics_to_core_feedback(
				feedback.photo_diagnostics,
				result.main_selection_result.diagnostics);
		}

		const localization::PendingSavePhotoImportState import_state =
			result.import_result.succeeded()
				? localization::PendingSavePhotoImportState::Completed
			: result.import_result.was_user_cancelled()
				? localization::PendingSavePhotoImportState::Cancelled
				: localization::PendingSavePhotoImportState::Failed;
		const localization::PendingSaveCleanupState cleanup_state =
			result.cleanup_attempted && result.cleanup_result.failed()
				? localization::PendingSaveCleanupState::NeedsAttention
				: localization::PendingSaveCleanupState::Clear;
		const localization::PendingSaveMainPhotoState main_photo_state =
			result.main_selection_attempted
				? result.main_selected_photo_id.has_value()
					  ? localization::PendingSaveMainPhotoState::Applied
					  : localization::PendingSaveMainPhotoState::NotApplied
				: localization::PendingSaveMainPhotoState::NotSelected;
		feedback.photo_message = localization.pending_save_photo_outcome(
			localization::PendingSavePhotoOutcome{
				.owner			= localization::PendingSavePhotoOwner::Storage,
				.import_state	= import_state,
				.imported_count = result.import_result.summary.success_count,
				.failed_count	= result.import_result.summary.failure_count,
				.cleanup_state	= cleanup_state,
				.main_photo_state = main_photo_state});
		if (result.main_selected_photo_id.has_value())
			route.selected_photo_id = result.main_selected_photo_id;
		else if (!result.import_result.imported_photo_ids.empty())
			route.selected_photo_id =
				result.import_result.imported_photo_ids.front();
	} else if (!storage_form.pending_photos.empty()) {
		feedback.photo_message = localization.pending_save_photo_outcome(
			localization::PendingSavePhotoOutcome{
				.owner = localization::PendingSavePhotoOwner::Storage});
	}

	session = std::move(result.session);
	if (invalidate_all_previews_handler)
		invalidate_all_previews_handler();
	storage_form.photo_deck.staged_main_index.reset();
	feedback.edit_message =
		result.save_result.metadata_changed ? "Saved." : "No changes.";
	feedback.edit_diagnostics.clear();
	if (storage_form.draft.existing_id) {
		route.selected_storage_id = *storage_form.draft.existing_id;
		route.destination		  = RootDestination::StorageDetail;
	} else {
		route.destination =
			route.form_return_destination.value_or(RootDestination::Catalog);
	}
	if (refresh_all_handler)
		refresh_all_handler();
}
}	 // namespace shuba::ui
