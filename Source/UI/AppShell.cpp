#include "UI/AppShell.hpp"
#include "UI/View/AppShellContentComponent.hpp"
#include "UI/View/ScreenText.hpp"
#include "UI/View/UiPrimitives.hpp"

#include "UI/Session/BackupRecoverySession.hpp"
#include "UI/Session/CatalogStartupSession.hpp"
#include "UI/Session/EntityEditSession.hpp"
#include "UI/Session/PhotoSession.hpp"

#include "Domain/Domain.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace shuba::ui {
core::StableIdentifier ShellIdentifierSource::next_stable_identifier() {
	return random_identifiers.next_stable_identifier();
}

core::OperationIdentifier ShellIdentifierSource::next_operation_identifier() {
	return random_identifiers.next_operation_identifier();
}

core::EpochMilliseconds ShellClock::now() const {
	return core::SystemClock{}.now();
}

AppShellComponent::AppShellComponent(CatalogSessionState session_state,
									 PlatformServices platform_services)
	: session(std::move(session_state))
	, internal_photo_codec(platform_services.internal_photo_codec)
	, content(std::make_unique<AppShellContentComponent>()) {
	setOpaque(true);
	setSize(480, 720);

	chrome = std::make_unique<AppShellChromeComponent>(
		AppShellChromeComponent::Callbacks{.catalog_search_changed = [this] {
		schedule_content_refresh();
	}, .storage_search_changed = [this] {
		schedule_content_refresh();
	}, .catalog_clear = [this] {
		chrome->clear_catalog_query_without_notification();
		refresh_content();
	}, .catalog_filter = [this] {
		catalog_filter_state.draft = catalog_filter_state.applied;
		catalog_filter_state.panel_visible =
			!catalog_filter_state.panel_visible;
		refresh_all();
	}, .catalog_clear_filters = [this] {
		reset_catalog_filters();
		refresh_all();
	}, .storage_clear = [this] {
		chrome->clear_storage_query_without_notification();
		refresh_content();
	}, .back = [this] {
		if (route.destination == RootDestination::ItemDetail) {
			select_root(RootDestination::Catalog);
		} else if (route.destination == RootDestination::PhotoViewer
				   && route.selected_photo_owner
				   && route.selected_photo_owner->type
						  == domain::PhotoOwnerType::Item) {
			route.selected_item_id = route.selected_photo_owner->id;
			select_root(RootDestination::ItemDetail);
		} else if (route.destination == RootDestination::PhotoViewer
				   && route.selected_photo_owner
				   && route.selected_photo_owner->type
						  == domain::PhotoOwnerType::Storage) {
			route.selected_storage_id = route.selected_photo_owner->id;
			select_root(RootDestination::StorageDetail);
		} else if (route.destination == RootDestination::ItemForm
				   || route.destination == RootDestination::StorageForm) {
			select_root(route.form_return_destination.value_or(
				RootDestination::Catalog));
		} else if (route.destination == RootDestination::BackupRecovery) {
			select_root(RootDestination::More);
		} else {
			select_root(RootDestination::Storages);
		}
	}, .form_cancel = [this] {
		select_root(
			route.form_return_destination.value_or(RootDestination::Catalog));
	}, .form_save = [this] {
		if (route.destination == RootDestination::ItemForm)
			save_item_form();
		else if (route.destination == RootDestination::StorageForm)
			save_storage_form();
	}, .select_catalog = [this] {
		select_root(RootDestination::Catalog);
	}, .select_storages = [this] {
		select_root(RootDestination::Storages);
	}, .select_add = [this] {
		select_root(RootDestination::Add);
	}, .select_more = [this] { select_root(RootDestination::More); }});
	addAndMakeVisible(*chrome);

	photo_coordinator = std::make_unique<AppShellPhotoCoordinator>(
		AppShellPhotoCoordinator::Dependencies{
			.session				 = session,
			.route					 = route,
			.item_form				 = item_form,
			.feedback				 = feedback,
			.photo_display			 = photo_display,
			.identifiers			 = edit_identifiers,
			.clock					 = edit_clock,
			.operation_gate			 = ui_operation_gate,
			.photo_selection_service = photo_selection_service,
			.document_export_service = document_export_service,
			.content_staging_service = content_staging_service,
			.source_decode_service	 = source_decode_service,
			.jpeg_export_service	 = jpeg_export_service,
			.internal_photo_codec	 = internal_photo_codec,
			.progress_events		 = last_progress_events,
			.cancellation_token		 = never_cancelled,
			.refresh_all			 = [this] { refresh_all(); }});

	screen_renderer = std::make_unique<AppShellScreenRenderer>(
		AppShellScreenRenderer::Dependencies{
			.session = session,
			.route = route,
			.catalog_filter_state = catalog_filter_state,
			.item_form = item_form,
			.storage_form = storage_form,
			.feedback = feedback,
			.backup = backup,
			.photo_display = photo_display,
			.storage_detail = storage_detail,
			.edit_identifiers = edit_identifiers,
			.edit_clock = edit_clock,
			.ui_operation_gate = ui_operation_gate,
			.internal_photo_codec = internal_photo_codec,
			.jpeg_export_service = jpeg_export_service,
			.document_export_service = document_export_service,
			.last_progress_events = last_progress_events,
			.never_cancelled = never_cancelled,
			.content = *content,
			.editors = AppShellScreenRenderer::Editors{
				.item_name_editor = item_name_editor,
				.item_category_editor = item_category_editor,
				.item_notes_editor = item_notes_editor,
				.item_listing_marketplace_editor =
					item_listing_marketplace_editor,
				.item_listing_url_editor = item_listing_url_editor,
				.item_listing_note_editor = item_listing_note_editor,
				.item_acquisition_source_editor =
					item_acquisition_source_editor,
				.storage_name_editor = storage_name_editor,
				.storage_type_editor = storage_type_editor,
				.storage_location_editor = storage_location_editor,
				.storage_notes_editor = storage_notes_editor},
			.queries = AppShellScreenRenderer::Queries{
				.catalog_query = [this] {
					return chrome != nullptr ? chrome->catalog_query()
									 : std::string{};
				},
				.storage_query = [this] {
					return chrome != nullptr ? chrome->storage_query()
									 : std::string{};
				}},
			.actions = AppShellScreenRenderer::Actions{
				.select_root = [this](RootDestination destination_value) {
					select_root(destination_value);
				},
				.open_item_detail = [this](core::StableIdentifier item_id) {
					open_item_detail(std::move(item_id));
				},
				.open_storage_detail =
					[this](core::StableIdentifier storage_id) {
						open_storage_detail(std::move(storage_id));
					},
				.open_photo_viewer =
					[this](domain::PhotoOwner owner,
						   std::optional<core::StableIdentifier> photo_id) {
						open_photo_viewer(std::move(owner), std::move(photo_id));
					},
				.open_new_item_form =
					[this](std::optional<core::StableIdentifier> storage_id) {
						open_new_item_form(std::move(storage_id));
					},
				.open_existing_item_form =
					[this](core::StableIdentifier item_id) {
						open_existing_item_form(std::move(item_id));
					},
				.open_new_storage_form =
					[this](std::optional<core::StableIdentifier> parent_id) {
						open_new_storage_form(std::move(parent_id));
					},
				.open_existing_storage_form =
					[this](core::StableIdentifier storage_id) {
						open_existing_storage_form(std::move(storage_id));
					},
				.request_add_photos = [this](domain::PhotoOwner owner) {
					request_add_photos(std::move(owner));
				},
				.request_add_pending_item_photos = [this] {
					request_add_pending_item_photos();
				},
				.request_export_photo =
					[this](core::StableIdentifier photo_id) {
						request_export_photo(std::move(photo_id));
					},
				.request_export_backup = [this] { request_export_backup(); },
				.request_export_diagnostic_archive = [this] {
					request_export_diagnostic_archive();
				},
				.request_import_backup = [this] { request_import_backup(); },
				.confirm_staged_backup_import = [this] {
					confirm_staged_backup_import();
				},
				.cleanup_item_pending_photos = [this] {
					cleanup_item_pending_photos();
				},
				.remove_item_pending_photo = [this](std::size_t index) {
					remove_item_pending_photo(index);
				},
				.reset_catalog_filters = [this] { reset_catalog_filters(); },
				.apply_entity_edit_result = [this](EntityEditResult result) {
					apply_entity_edit_result(std::move(result));
				},
				.apply_photo_edit_result =
					[this](EntityEditResult result,
						   core::StableIdentifier selected_photo_id) {
						apply_photo_edit_result(std::move(result),
											std::move(selected_photo_id));
					},
				.refresh_all = [this] { refresh_all(); },
				.refresh_content = [this] { refresh_content(); }}});

	viewport.setViewedComponent(content.get(), false);
	viewport.setScrollBarsShown(true, false);
	viewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::nonHover);
	addAndMakeVisible(viewport);

	refresh_all();
}

AppShellComponent::~AppShellComponent() {
	stopTimer();
}

void AppShellComponent::paint(juce::Graphics& graphics) {
	graphics.fillAll(background_colour());
}

void AppShellComponent::resized() {
	juce::Rectangle<int> bounds = getLocalBounds().reduced(10);
	if (chrome != nullptr) {
		chrome->setBounds(getLocalBounds());
		bounds = chrome->layout_shell(bounds);
	}
	viewport.setBounds(bounds);
	if (content)
		content->setSize(viewport.getWidth(), content->getHeight());
}

void AppShellComponent::build_catalog_content() {
	if (screen_renderer != nullptr)
		screen_renderer->build_catalog_content();
}

void AppShellComponent::build_filter_panel() {
	if (screen_renderer != nullptr)
		screen_renderer->build_filter_panel();
}

void AppShellComponent::build_storages_content() {
	if (screen_renderer != nullptr)
		screen_renderer->build_storages_content();
}

void AppShellComponent::build_item_detail_content() {
	if (screen_renderer != nullptr)
		screen_renderer->build_item_detail_content();
}

void AppShellComponent::build_storage_detail_content() {
	if (screen_renderer != nullptr)
		screen_renderer->build_storage_detail_content();
}

void AppShellComponent::build_item_form_content() {
	if (screen_renderer != nullptr)
		screen_renderer->build_item_form_content();
}

void AppShellComponent::build_storage_form_content() {
	if (screen_renderer != nullptr)
		screen_renderer->build_storage_form_content();
}

void AppShellComponent::build_photo_viewer_content() {
	if (screen_renderer != nullptr)
		screen_renderer->build_photo_viewer_content();
}

void AppShellComponent::build_backup_recovery_content() {
	if (screen_renderer != nullptr)
		screen_renderer->build_backup_recovery_content();
}

void AppShellComponent::build_add_content() {
	if (screen_renderer != nullptr)
		screen_renderer->build_add_content();
}

void AppShellComponent::build_more_content() {
	if (screen_renderer != nullptr)
		screen_renderer->build_more_content();
}

void AppShellComponent::select_root(RootDestination destination_value) {
	const RootDestination previous_destination = route.destination;
	if (previous_destination == RootDestination::ItemForm
		&& destination_value != RootDestination::ItemForm) {
		cleanup_item_pending_photos();
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
		photo_display.result = catalog::PhotoDisplayResult{};
	}
	if (route.destination != RootDestination::ItemForm
		&& route.destination != RootDestination::StorageForm) {
		route.form_return_destination.reset();
	}
	if (route.destination != RootDestination::BackupRecovery) {
		backup.pending_import_staging.reset();
		backup.pending_import_degraded_acknowledged = false;
	}
	refresh_all();
}

void AppShellComponent::open_item_detail(core::StableIdentifier item_id) {
	route.selected_item_id = std::move(item_id);
	route.destination	   = RootDestination::ItemDetail;
	refresh_all();
}

void AppShellComponent::open_storage_detail(core::StableIdentifier storage_id) {
	route.selected_storage_id	  = std::move(storage_id);
	route.destination			  = RootDestination::StorageDetail;
	storage_detail.include_nested = true;
	refresh_all();
}

void AppShellComponent::open_photo_viewer(
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
	photo_display.displayed_photo_id.reset();
	photo_display.result = catalog::PhotoDisplayResult{};
	route.destination	 = RootDestination::PhotoViewer;
	refresh_all();
}

void AppShellComponent::open_new_item_form(
	std::optional<core::StableIdentifier> storage_id) {
	reset_item_form();
	item_form.mode				  = FormMode::Create;
	item_form.draft.storage_id	  = std::move(storage_id);
	route.form_return_destination = route.selected_storage_id
										? RootDestination::StorageDetail
										: RootDestination::Add;
	route.destination			  = RootDestination::ItemForm;
	refresh_all();
}

void AppShellComponent::open_existing_item_form(
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
	refresh_all();
}

void AppShellComponent::open_new_storage_form(
	std::optional<core::StableIdentifier> parent_id) {
	reset_storage_form();
	storage_form.mode					 = FormMode::Create;
	storage_form.draft.parent_storage_id = std::move(parent_id);
	route.form_return_destination		 = route.selected_storage_id
											   ? RootDestination::StorageDetail
											   : RootDestination::Add;
	route.destination					 = RootDestination::StorageForm;
	refresh_all();
}

void AppShellComponent::open_existing_storage_form(
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
	refresh_all();
}

void AppShellComponent::request_add_photos(domain::PhotoOwner owner) {
	if (photo_coordinator != nullptr)
		photo_coordinator->request_add_photos(std::move(owner));
}

void AppShellComponent::request_add_pending_item_photos() {
	if (photo_coordinator != nullptr)
		photo_coordinator->request_add_pending_item_photos();
}

void AppShellComponent::request_export_photo(core::StableIdentifier photo_id) {
	if (photo_coordinator != nullptr)
		photo_coordinator->request_export_photo(std::move(photo_id));
}

void AppShellComponent::apply_photo_edit_result(
	EntityEditResult result, core::StableIdentifier selected_photo_id_value) {
	if (photo_coordinator != nullptr) {
		photo_coordinator->apply_photo_edit_result(
			std::move(result), std::move(selected_photo_id_value));
	}
}

void AppShellComponent::cleanup_item_pending_photos() {
	if (photo_coordinator != nullptr)
		photo_coordinator->cleanup_item_pending_photos();
}

void AppShellComponent::remove_item_pending_photo(
	std::size_t pending_photo_index) {
	if (photo_coordinator != nullptr)
		photo_coordinator->remove_item_pending_photo(pending_photo_index);
}

void AppShellComponent::reset_catalog_filters() {
	catalog_filter_state.applied	   = catalog::CatalogSearchFilters{};
	catalog_filter_state.draft		   = catalog_filter_state.applied;
	catalog_filter_state.panel_visible = false;
}

void AppShellComponent::reset_item_form() {
	cleanup_item_pending_photos();
	item_form.draft						  = ItemDraft{};
	item_form.mode						  = FormMode::Create;
	item_form.storage_candidates_expanded = false;
	item_form.tag_candidates_expanded	  = false;
	item_form.listing_expanded			  = false;
	item_form.finance_expanded			  = false;
	for (juce::TextEditor* editor :
		 {&item_name_editor, &item_category_editor, &item_notes_editor,
		  &item_listing_marketplace_editor, &item_listing_url_editor,
		  &item_listing_note_editor, &item_acquisition_source_editor}) {
		editor->setText(juce::String{}, juce::dontSendNotification);
	}
}

void AppShellComponent::reset_storage_form() {
	storage_form.draft						  = StorageDraft{};
	storage_form.mode						  = FormMode::Create;
	storage_form.parent_candidates_expanded	  = false;
	storage_form.tag_candidates_expanded	  = false;
	storage_form.archive_warning_acknowledged = false;
	for (juce::TextEditor* editor :
		 {&storage_name_editor, &storage_type_editor, &storage_location_editor,
		  &storage_notes_editor}) {
		editor->setText(juce::String{}, juce::dontSendNotification);
	}
}

void AppShellComponent::load_item_form_from_record(
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
}

void AppShellComponent::load_storage_form_from_record(
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
}

void AppShellComponent::save_item_form() {
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

	ItemSaveWithPendingPhotosResult result =
		save_item_draft_and_import_pending_photos(
			ItemSaveWithPendingPhotosRequest{
				.current_session = session,
				.identifiers	 = edit_identifiers,
				.clock			 = edit_clock,
				.operation_gate	 = ui_operation_gate,
				.staging_service = content_staging_service,
				.decode_service	 = source_decode_service,
				.photo_codec	 = internal_photo_codec,
				.draft			 = item_form.draft,
				.pending_sources = item_form.pending_photos},
			last_progress_events, never_cancelled);
	if (result.warning_acknowledgement_required())
		item_form.draft.warning_acknowledged = true;
	if (result.warning_acknowledgement_required()
		&& result.save_result.saved_record_id) {
		item_form.draft.reserved_new_id = result.save_result.saved_record_id;
	}
	if (result.item_saved())
		item_form.draft.existing_id = result.save_result.saved_record_id;
	apply_item_save_with_pending_photos_result(std::move(result));
}

void AppShellComponent::save_storage_form() {
	storage_form.draft.display_name =
		storage_name_editor.getText().toStdString();
	storage_form.draft.storage_type =
		storage_type_editor.getText().toStdString();
	storage_form.draft.location =
		storage_location_editor.getText().toStdString();
	storage_form.draft.notes = storage_notes_editor.getText().toStdString();
	storage_form.draft.archive_warning_acknowledged =
		storage_form.archive_warning_acknowledged;
	EntityEditResult result =
		save_storage_draft(EntityEditRequest{.current_session = session,
											 .identifiers = edit_identifiers,
											 .clock		  = edit_clock},
						   storage_form.draft);
	if (result.warning_acknowledgement_required)
		storage_form.archive_warning_acknowledged = true;
	if (result.warning_acknowledgement_required && result.saved_record_id)
		storage_form.draft.reserved_new_id = result.saved_record_id;
	if (result.succeeded() && result.saved_record_id)
		storage_form.draft.existing_id = result.saved_record_id;
	apply_entity_edit_result(std::move(result));
}

void AppShellComponent::apply_item_save_with_pending_photos_result(
	ItemSaveWithPendingPhotosResult result) {
	feedback.edit_diagnostics = result.save_result.diagnostics;
	item_form.pending_photos  = std::move(result.pending_sources);
	std::erase_if(item_form.pending_photos,
				  [](const PendingPhotoSource& source) {
		return !source.staged_path.has_value();
	});
	if (result.warning_acknowledgement_required()) {
		feedback.edit_message = "Confirm warning and save again.";
		refresh_all();
		return;
	}
	if (result.save_result.failed()) {
		feedback.edit_message = "Save failed.";
		refresh_all();
		return;
	}

	feedback.photo_diagnostics.clear();
	if (result.import_attempted) {
		for (const EntityEditDiagnostic& diagnostic :
			 result.import_result.diagnostics) {
			feedback.photo_diagnostics.push_back(core::Diagnostic{
				.severity		   = diagnostic.severity,
				.code			   = diagnostic.code,
				.message		   = diagnostic.message,
				.technical_details = diagnostic.technical_details});
		}
		feedback.photo_diagnostics.insert(
			feedback.photo_diagnostics.end(),
			result.cleanup_result.diagnostics.begin(),
			result.cleanup_result.diagnostics.end());
		if (result.import_result.succeeded()) {
			feedback.photo_message =
				"Pending photo import completed: "
				+ std::to_string(result.import_result.summary.success_count)
				+ " imported, "
				+ std::to_string(result.import_result.summary.failure_count)
				+ " failed.";
		} else if (result.import_result.was_user_cancelled()) {
			feedback.photo_message =
				"Item saved, pending photo import cancelled.";
		} else {
			feedback.photo_message =
				"Item saved, but pending photo import failed.";
		}
		if (result.cleanup_attempted && result.cleanup_result.failed())
			feedback.photo_message +=
				" Pending source cleanup needs attention.";
		if (!result.import_result.imported_photo_ids.empty())
			route.selected_photo_id =
				result.import_result.imported_photo_ids.front();
	} else if (!item_form.pending_photos.empty()) {
		feedback.photo_message =
			"Item saved, but no staged pending photos were ready.";
	}

	session = std::move(result.session);
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
	refresh_all();
}

void AppShellComponent::apply_entity_edit_result(EntityEditResult result) {
	feedback.edit_diagnostics = std::move(result.diagnostics);
	if (result.warning_acknowledgement_required) {
		feedback.edit_message = "Confirm warning and save again.";
		refresh_all();
		return;
	}
	if (result.failed()) {
		feedback.edit_message = "Save failed.";
		refresh_all();
		return;
	}
	const RootDestination completed_destination = route.destination;
	const std::optional<core::StableIdentifier> saved_item_id =
		item_form.draft.existing_id;
	const std::optional<core::StableIdentifier> saved_storage_id =
		storage_form.draft.existing_id;
	session				  = std::move(result.session);
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
	refresh_all();
}

void AppShellComponent::schedule_content_refresh() {
	startTimer(85);
}

void AppShellComponent::timerCallback() {
	stopTimer();
	refresh_content();
	resized();
	repaint();
}

void AppShellComponent::refresh_all() {
	stopTimer();
	refresh_controls();
	refresh_content();
	resized();
	repaint();
}

void AppShellComponent::refresh_controls() {
	juce::String title;
	switch (route.destination) {
		case RootDestination::Catalog:
			title = "Catalog";
			break;
		case RootDestination::Storages:
			title = "Storages";
			break;
		case RootDestination::Add:
			title = "Add";
			break;
		case RootDestination::More:
			title = "More";
			break;
		case RootDestination::ItemDetail:
			title = "Item detail";
			break;
		case RootDestination::StorageDetail:
			title = "Storage detail";
			break;
		case RootDestination::ItemForm:
			title =
				item_form.mode == FormMode::Create ? "Add item" : "Edit item";
			break;
		case RootDestination::StorageForm:
			title = storage_form.mode == FormMode::Create ? "Add storage"
														  : "Edit storage";
			break;
		case RootDestination::PhotoViewer:
			title = "Photo viewer";
			break;
		case RootDestination::BackupRecovery:
			title = session.fatal() ? "Fatal recovery" : "Backup and recovery";
			break;
	}
	if (session.fatal()) {
		route.destination = RootDestination::BackupRecovery;
		title			  = "Fatal recovery";
	}

	std::string status =
		"Load: " + std::string{persistence::to_string(session.load_status)};
	status += " · " + std::string{to_string(session.source)};
	status += " · items=" + std::to_string(session.repository.items.size());
	status +=
		" · storages=" + std::to_string(session.repository.storages.size());
	if (session.demo_catalog_active)
		status += " · demo catalog";
	if (chrome != nullptr) {
		chrome->update_model(AppShellChromeComponent::Model{
			.destination	   = route.destination,
			.item_form_mode	   = item_form.mode,
			.storage_form_mode = storage_form.mode,
			.title			   = title,
			.status			   = juce_text(status),
			.session_fatal	   = session.fatal(),
			.catalog_filters_active =
				has_catalog_filters(catalog_filter_state.applied)});
	}
}

void AppShellComponent::refresh_content() {
	content->begin_rebuild();
	content->clear_rows();
	if (!session.ready_for_browsing() && session.fatal()) {
		build_backup_recovery_content();
	} else if (!session.ready_for_browsing()) {
		content->add_label(
			"Catalog could not be loaded and app-private paths are "
			"unavailable. "
			"Review technical diagnostics before retrying.",
			86, warning_panel_colour(), true);
	} else {
		if (!feedback.edit_message.empty()) {
			content->add_label(juce_text(feedback.edit_message), 42,
							   accent_colour().withAlpha(0.34f), true);
		}
		if (!feedback.edit_diagnostics.empty()) {
			content->add_label(
				juce_text(diagnostic_summary(feedback.edit_diagnostics)), 76,
				warning_panel_colour(), true);
		}
		if (route.destination != RootDestination::PhotoViewer
			&& !feedback.photo_message.empty()) {
			content->add_label(juce_text(feedback.photo_message), 54,
							   accent_colour().withAlpha(0.34f), true);
		}
		if (route.destination != RootDestination::PhotoViewer
			&& !feedback.photo_diagnostics.empty()) {
			content->add_label(
				juce_text(core_diagnostic_summary(feedback.photo_diagnostics)),
				76, warning_panel_colour(), true);
		}
		if (route.destination != RootDestination::BackupRecovery
			&& !feedback.backup_message.empty()) {
			content->add_label(juce_text(feedback.backup_message), 62,
							   accent_colour().withAlpha(0.34f), true);
		}
		if (route.destination != RootDestination::BackupRecovery
			&& !feedback.backup_diagnostics.empty()) {
			content->add_label(
				juce_text(core_diagnostic_summary(feedback.backup_diagnostics)),
				76, warning_panel_colour(), true);
		}
		switch (route.destination) {
			case RootDestination::Catalog:
				build_catalog_content();
				break;
			case RootDestination::Storages:
				build_storages_content();
				break;
			case RootDestination::ItemDetail:
				build_item_detail_content();
				break;
			case RootDestination::StorageDetail:
				build_storage_detail_content();
				break;
			case RootDestination::ItemForm:
				build_item_form_content();
				break;
			case RootDestination::StorageForm:
				build_storage_form_content();
				break;
			case RootDestination::Add:
				build_add_content();
				break;
			case RootDestination::More:
				build_more_content();
				break;
			case RootDestination::PhotoViewer:
				build_photo_viewer_content();
				break;
			case RootDestination::BackupRecovery:
				build_backup_recovery_content();
				break;
		}
	}
	content->end_rebuild();
}

}	 // namespace shuba::ui
