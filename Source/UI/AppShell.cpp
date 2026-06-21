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

	title_label.setJustificationType(juce::Justification::centredLeft);
	title_label.setColour(juce::Label::textColourId, text_colour());
	title_label.setFont(juce::FontOptions(22.0f, juce::Font::bold));
	addAndMakeVisible(title_label);

	status_label.setJustificationType(juce::Justification::centredLeft);
	status_label.setColour(juce::Label::textColourId, muted_text_colour());
	status_label.setMinimumHorizontalScale(0.70f);
	status_label.setFont(juce::FontOptions(14.5f, juce::Font::plain));
	addAndMakeVisible(status_label);

	catalog_search_editor.setTextToShowWhenEmpty("Search catalog",
												 muted_text_colour());
	style_text_editor(catalog_search_editor);
	catalog_search_editor.onTextChange = [this] { schedule_content_refresh(); };
	addAndMakeVisible(catalog_search_editor);

	storage_search_editor.setTextToShowWhenEmpty("Search storages",
												 muted_text_colour());
	style_text_editor(storage_search_editor);
	storage_search_editor.onTextChange = [this] { schedule_content_refresh(); };
	addAndMakeVisible(storage_search_editor);

	catalog_clear_button.onClick = [this] {
		catalog_search_editor.setText(juce::String{},
									  juce::dontSendNotification);
		refresh_content();
	};
	catalog_filter_button.onClick = [this] {
		catalog_filter_draft		 = catalog_filters;
		catalog_filter_panel_visible = !catalog_filter_panel_visible;
		refresh_all();
	};
	catalog_clear_filters_button.onClick = [this] {
		reset_catalog_filters();
		refresh_all();
	};
	storage_clear_button.onClick = [this] {
		storage_search_editor.setText(juce::String{},
									  juce::dontSendNotification);
		refresh_content();
	};
	form_cancel_button.onClick = [this] {
		select_root(form_return_destination.value_or(RootDestination::Catalog));
	};
	form_save_button.onClick = [this] {
		if (destination == RootDestination::ItemForm)
			save_item_form();
		else if (destination == RootDestination::StorageForm)
			save_storage_form();
	};
	back_button.onClick = [this] {
		if (destination == RootDestination::ItemDetail) {
			select_root(RootDestination::Catalog);
		} else if (destination == RootDestination::PhotoViewer
				   && selected_photo_owner
				   && selected_photo_owner->type
						  == domain::PhotoOwnerType::Item) {
			selected_item_id = selected_photo_owner->id;
			select_root(RootDestination::ItemDetail);
		} else if (destination == RootDestination::PhotoViewer
				   && selected_photo_owner
				   && selected_photo_owner->type
						  == domain::PhotoOwnerType::Storage) {
			selected_storage_id = selected_photo_owner->id;
			select_root(RootDestination::StorageDetail);
		} else if (destination == RootDestination::ItemForm
				   || destination == RootDestination::StorageForm) {
			select_root(
				form_return_destination.value_or(RootDestination::Catalog));
		} else if (destination == RootDestination::BackupRecovery) {
			select_root(RootDestination::More);
		} else {
			select_root(RootDestination::Storages);
		}
	};
	for (juce::TextButton* button :
		 {&catalog_clear_button, &catalog_filter_button,
		  &catalog_clear_filters_button, &storage_clear_button, &back_button}) {
		style_text_button(*button);
		addAndMakeVisible(*button);
	}
	for (juce::TextButton* button : {&form_cancel_button, &form_save_button}) {
		style_text_button(*button);
		addAndMakeVisible(*button);
	}

	viewport.setViewedComponent(content.get(), false);
	viewport.setScrollBarsShown(true, false);
	viewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::nonHover);
	addAndMakeVisible(viewport);

	catalog_nav_button.onClick = [this] {
		select_root(RootDestination::Catalog);
	};
	storages_nav_button.onClick = [this] {
		select_root(RootDestination::Storages);
	};
	add_nav_button.onClick	= [this] { select_root(RootDestination::Add); };
	more_nav_button.onClick = [this] { select_root(RootDestination::More); };
	for (juce::TextButton* button : {&catalog_nav_button, &storages_nav_button,
									 &add_nav_button, &more_nav_button}) {
		style_text_button(*button);
		addAndMakeVisible(*button);
	}

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
	const bool form_visible = destination == RootDestination::ItemForm
							  || destination == RootDestination::StorageForm;
	catalog_nav_button.setVisible(!form_visible);
	storages_nav_button.setVisible(!form_visible);
	add_nav_button.setVisible(!form_visible);
	more_nav_button.setVisible(!form_visible);
	form_cancel_button.setVisible(form_visible);
	form_save_button.setVisible(form_visible);

	if (form_visible) {
		juce::Rectangle<int> form_actions = bounds.removeFromBottom(58);
		const int action_width = std::max(1, form_actions.getWidth() / 2);
		juce::Rectangle<int> cancel_area =
			form_actions.removeFromLeft(action_width);
		form_cancel_button.setBounds(cancel_area.reduced(3));
		form_save_button.setBounds(form_actions.reduced(3));
	} else {
		juce::Rectangle<int> nav = bounds.removeFromBottom(54);
		const int nav_width		 = nav.getWidth() / 4;
		catalog_nav_button.setBounds(nav.removeFromLeft(nav_width).reduced(3));
		storages_nav_button.setBounds(nav.removeFromLeft(nav_width).reduced(3));
		add_nav_button.setBounds(nav.removeFromLeft(nav_width).reduced(3));
		more_nav_button.setBounds(nav.reduced(3));
	}

	title_label.setBounds(bounds.removeFromTop(32));
	status_label.setBounds(bounds.removeFromTop(26));

	juce::Rectangle<int> controls = bounds.removeFromTop(44);
	const bool catalog_visible	  = destination == RootDestination::Catalog;
	const bool storages_visible	  = destination == RootDestination::Storages;
	const bool detail_visible =
		destination == RootDestination::ItemDetail
		|| destination == RootDestination::StorageDetail
		|| destination == RootDestination::ItemForm
		|| destination == RootDestination::StorageForm
		|| destination == RootDestination::PhotoViewer
		|| destination == RootDestination::BackupRecovery;
	catalog_search_editor.setVisible(catalog_visible);
	catalog_clear_button.setVisible(catalog_visible);
	catalog_filter_button.setVisible(catalog_visible);
	catalog_clear_filters_button.setVisible(
		catalog_visible && has_catalog_filters(catalog_filters));
	storage_search_editor.setVisible(storages_visible);
	storage_clear_button.setVisible(storages_visible);
	back_button.setVisible(detail_visible);

	if (catalog_visible) {
		catalog_search_editor.setBounds(
			controls.removeFromLeft(std::max(120, controls.getWidth() - 244))
				.reduced(2));
		catalog_clear_button.setBounds(controls.removeFromLeft(64).reduced(2));
		catalog_filter_button.setBounds(controls.removeFromLeft(78).reduced(2));
		catalog_clear_filters_button.setBounds(controls.reduced(2));
	} else if (storages_visible) {
		storage_search_editor.setBounds(
			controls.removeFromLeft(std::max(140, controls.getWidth() - 72))
				.reduced(2));
		storage_clear_button.setBounds(controls.reduced(2));
	} else if (detail_visible) {
		back_button.setBounds(controls.removeFromLeft(92).reduced(2));
	}

	bounds.removeFromTop(4);
	viewport.setBounds(bounds);
	if (content)
		content->setSize(viewport.getWidth(), content->getHeight());
}

void AppShellComponent::select_root(RootDestination destination_value) {
	const RootDestination previous_destination = destination;
	if (previous_destination == RootDestination::ItemForm
		&& destination_value != RootDestination::ItemForm) {
		cleanup_item_pending_photos();
	}
	destination = destination_value;
	if (destination != RootDestination::ItemDetail
		&& destination != RootDestination::ItemForm
		&& !(destination == RootDestination::PhotoViewer && selected_photo_owner
			 && selected_photo_owner->type == domain::PhotoOwnerType::Item)) {
		selected_item_id.reset();
	}
	if (destination != RootDestination::StorageDetail
		&& destination != RootDestination::StorageForm
		&& destination != RootDestination::ItemForm
		&& !(destination == RootDestination::PhotoViewer && selected_photo_owner
			 && selected_photo_owner->type
					== domain::PhotoOwnerType::Storage)) {
		selected_storage_id.reset();
	}
	if (destination != RootDestination::PhotoViewer) {
		selected_photo_owner.reset();
		selected_photo_id.reset();
		last_display_photo_id.reset();
		last_photo_display_result = catalog::PhotoDisplayResult{};
	}
	if (destination != RootDestination::ItemForm
		&& destination != RootDestination::StorageForm) {
		form_return_destination.reset();
	}
	if (destination != RootDestination::BackupRecovery) {
		pending_import_staging.reset();
		pending_import_degraded_acknowledged = false;
	}
	refresh_all();
}

void AppShellComponent::open_item_detail(core::StableIdentifier item_id) {
	selected_item_id = std::move(item_id);
	destination		 = RootDestination::ItemDetail;
	refresh_all();
}

void AppShellComponent::open_storage_detail(core::StableIdentifier storage_id) {
	selected_storage_id			  = std::move(storage_id);
	destination					  = RootDestination::StorageDetail;
	storage_detail_include_nested = true;
	refresh_all();
}

void AppShellComponent::open_photo_viewer(
	domain::PhotoOwner owner,
	std::optional<core::StableIdentifier> requested_photo_id) {
	selected_photo_owner = owner;
	selected_photo_id =
		requested_photo_id.has_value()
			? requested_photo_id
			: first_viewable_photo_id(session.repository, owner);
	if (owner.type == domain::PhotoOwnerType::Item)
		selected_item_id = owner.id;
	else
		selected_storage_id = owner.id;
	last_photo_message.clear();
	last_photo_diagnostics.clear();
	last_display_photo_id.reset();
	last_photo_display_result = catalog::PhotoDisplayResult{};
	destination				  = RootDestination::PhotoViewer;
	refresh_all();
}

void AppShellComponent::open_new_item_form(
	std::optional<core::StableIdentifier> storage_id) {
	reset_item_form();
	item_form_mode			   = FormMode::Create;
	item_form_draft.storage_id = std::move(storage_id);
	form_return_destination	   = selected_storage_id
									 ? RootDestination::StorageDetail
									 : RootDestination::Add;
	destination				   = RootDestination::ItemForm;
	refresh_all();
}

void AppShellComponent::open_existing_item_form(
	core::StableIdentifier item_id) {
	const persistence::ItemEnvelope* item =
		catalog::find_item_envelope(session.repository, item_id);
	if (item == nullptr)
		return;
	load_item_form_from_record(*item);
	selected_item_id		= std::move(item_id);
	item_form_mode			= FormMode::Edit;
	form_return_destination = RootDestination::ItemDetail;
	destination				= RootDestination::ItemForm;
	refresh_all();
}

void AppShellComponent::open_new_storage_form(
	std::optional<core::StableIdentifier> parent_id) {
	reset_storage_form();
	storage_form_mode					 = FormMode::Create;
	storage_form_draft.parent_storage_id = std::move(parent_id);
	form_return_destination				 = selected_storage_id
											   ? RootDestination::StorageDetail
											   : RootDestination::Add;
	destination							 = RootDestination::StorageForm;
	refresh_all();
}

void AppShellComponent::open_existing_storage_form(
	core::StableIdentifier storage_id) {
	const persistence::StorageEnvelope* storage =
		catalog::find_storage_envelope(session.repository, storage_id);
	if (storage == nullptr)
		return;
	load_storage_form_from_record(*storage);
	selected_storage_id		= std::move(storage_id);
	storage_form_mode		= FormMode::Edit;
	form_return_destination = RootDestination::StorageDetail;
	destination				= RootDestination::StorageForm;
	refresh_all();
}

void AppShellComponent::request_add_photos(domain::PhotoOwner owner) {
	last_progress_events.clear();
	last_photo_diagnostics.clear();
	last_photo_message = "Select photos to import.";
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
			last_photo_message = "Photo selection cancelled.";
			refresh_all();
			return;
		}
		if (result.failed()) {
			last_photo_message	   = "Photo selection failed.";
			last_photo_diagnostics = std::move(result.diagnostics);
			refresh_all();
			return;
		}
		PhotoImportSessionResult import_result = import_photos_into_session(
			PhotoImportSessionRequest{
				.current_session = session,
				.identifiers	 = edit_identifiers,
				.clock			 = edit_clock,
				.operation_gate	 = ui_operation_gate,
				.staging_service = content_staging_service,
				.decode_service	 = source_decode_service,
				.photo_codec	 = internal_photo_codec,
				.owner			 = owner,
				.sources		 = std::move(*result.value)},
			last_progress_events, never_cancelled);
		apply_photo_import_result(std::move(import_result));
	});
	if (picker_started.failed()) {
		last_photo_message	   = "Photo picker could not be opened.";
		last_photo_diagnostics = picker_started.diagnostics();
		refresh_all();
	}
}

void AppShellComponent::request_add_pending_item_photos() {
	last_progress_events.clear();
	last_photo_diagnostics.clear();
	last_photo_message = "Select photos to stage before saving the item.";
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
			last_photo_message = "Pending photo selection cancelled.";
			refresh_all();
			return;
		}
		if (result.failed()) {
			last_photo_message	   = "Pending photo selection failed.";
			last_photo_diagnostics = std::move(result.diagnostics);
			refresh_all();
			return;
		}
		if (result.value->empty()) {
			last_photo_message = "No photos selected for pending staging.";
			refresh_all();
			return;
		}

		PendingPhotoStagingResult staging_result =
			stage_pending_photos_for_session(
				PendingPhotoStagingRequest{
					.current_session = session,
					.identifiers	 = edit_identifiers,
					.operation_gate	 = ui_operation_gate,
					.staging_service = content_staging_service,
					.sources		 = std::move(*result.value)},
				last_progress_events, never_cancelled);
		apply_pending_photo_staging_result(std::move(staging_result));
	});
	if (picker_started.failed()) {
		last_photo_message	   = "Pending photo picker could not be opened.";
		last_photo_diagnostics = picker_started.diagnostics();
		refresh_all();
	}
}

void AppShellComponent::request_export_photo(core::StableIdentifier photo_id) {
	last_progress_events.clear();
	last_photo_diagnostics.clear();
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
			last_photo_message = "JPEG export destination cancelled.";
			refresh_all();
			return;
		}
		if (result.failed()) {
			last_photo_message	   = "JPEG export destination failed.";
			last_photo_diagnostics = std::move(result.diagnostics);
			refresh_all();
			return;
		}
		catalog::PhotoExportUseCase export_use_case{
			edit_identifiers, ui_operation_gate, internal_photo_codec,
			jpeg_export_service, document_export_service};
		catalog::PhotoExportResult exported =
			export_use_case.export_photo_as_jpeg(
				catalog::PhotoExportRequest{
					.current_state = session.repository,
					.paths		   = *session.paths,
					.photo_id	   = photo_id,
					.destination   = std::move(*result.value),
					.jpeg_quality  = 90},
				last_progress_events, never_cancelled);
		last_photo_diagnostics = std::move(exported.diagnostics);
		if (exported.succeeded())
			last_photo_message = "JPEG export completed: "
								 + std::to_string(exported.bytes_written)
								 + " bytes.";
		else if (exported.was_user_cancelled())
			last_photo_message = "JPEG export cancelled.";
		else
			last_photo_message = "JPEG export failed.";
		refresh_all();
	});
	if (destination_started.failed()) {
		last_photo_message =
			"JPEG export destination picker could not be opened.";
		last_photo_diagnostics = destination_started.diagnostics();
		refresh_all();
	}
}

void AppShellComponent::apply_pending_photo_staging_result(
	PendingPhotoStagingResult result) {
	last_photo_diagnostics = std::move(result.diagnostics);
	for (PendingPhotoSource& source : result.sources)
		item_form_pending_photos.push_back(std::move(source));

	if (result.succeeded()) {
		last_photo_message = "Pending photo staging completed: "
							 + std::to_string(result.staged_count) + " staged, "
							 + std::to_string(result.failure_count)
							 + " failed.";
	} else if (result.was_user_cancelled()) {
		last_photo_message = "Pending photo staging cancelled.";
	} else {
		last_photo_message = "Pending photo staging failed.";
	}
	refresh_all();
}

void AppShellComponent::apply_photo_import_result(
	PhotoImportSessionResult result) {
	last_photo_diagnostics.clear();
	for (const EntityEditDiagnostic& diagnostic : result.diagnostics) {
		last_photo_diagnostics.push_back(core::Diagnostic{
			.severity		   = diagnostic.severity,
			.code			   = diagnostic.code,
			.message		   = diagnostic.message,
			.technical_details = diagnostic.technical_details});
	}
	if (result.succeeded()) {
		session = std::move(result.session);
		last_photo_message =
			"Photo import completed: "
			+ std::to_string(result.summary.success_count) + " imported, "
			+ std::to_string(result.summary.failure_count) + " failed.";
		if (!result.imported_photo_ids.empty())
			selected_photo_id = result.imported_photo_ids.front();
		last_display_photo_id.reset();
	} else if (result.was_user_cancelled()) {
		last_photo_message = "Photo import cancelled.";
	} else {
		last_photo_message = "Photo import failed.";
	}
	refresh_all();
}

void AppShellComponent::apply_photo_edit_result(
	EntityEditResult result, core::StableIdentifier selected_photo_id_value) {
	last_photo_diagnostics.clear();
	for (const EntityEditDiagnostic& diagnostic : result.diagnostics) {
		last_photo_diagnostics.push_back(core::Diagnostic{
			.severity		   = diagnostic.severity,
			.code			   = diagnostic.code,
			.message		   = diagnostic.message,
			.technical_details = diagnostic.technical_details});
	}
	if (result.failed()) {
		last_photo_message = "Photo metadata update failed.";
		refresh_all();
		return;
	}
	session			  = std::move(result.session);
	selected_photo_id = selected_photo_id_value;
	last_display_photo_id.reset();
	last_photo_message = result.metadata_changed ? "Main photo updated."
												 : "Main photo unchanged.";
	refresh_all();
}

void AppShellComponent::cleanup_item_pending_photos() {
	if (item_form_pending_photos.empty())
		return;

	PendingPhotoCleanupResult cleanup =
		cleanup_pending_photo_sources(item_form_pending_photos);
	last_photo_diagnostics = cleanup.diagnostics;
	std::erase_if(item_form_pending_photos,
				  [](const PendingPhotoSource& source) {
		return !source.staged_path.has_value();
	});
	last_photo_message =
		cleanup.failed() ? "Some pending staged photos could not be cleaned."
						 : "Pending photos cleared.";
}

void AppShellComponent::remove_item_pending_photo(
	std::size_t pending_photo_index) {
	if (pending_photo_index >= item_form_pending_photos.size())
		return;

	std::vector<PendingPhotoSource> cleanup_sources;
	cleanup_sources.push_back(item_form_pending_photos[pending_photo_index]);
	PendingPhotoCleanupResult cleanup =
		cleanup_pending_photo_sources(cleanup_sources);
	last_photo_diagnostics = cleanup.diagnostics;
	if (cleanup.failed()) {
		PendingPhotoSource& source =
			item_form_pending_photos[pending_photo_index];
		source.diagnostics.insert(source.diagnostics.end(),
								  cleanup.diagnostics.begin(),
								  cleanup.diagnostics.end());
		last_photo_message =
			"Pending photo cleanup needs attention; source kept for retry.";
		refresh_all();
		return;
	}

	item_form_pending_photos.erase(
		item_form_pending_photos.begin()
		+ static_cast<std::ptrdiff_t>(pending_photo_index));
	last_photo_message = "Pending photo removed.";
	refresh_all();
}

void AppShellComponent::reset_catalog_filters() {
	catalog_filters				 = catalog::CatalogSearchFilters{};
	catalog_filter_draft		 = catalog_filters;
	catalog_filter_panel_visible = false;
}

void AppShellComponent::reset_item_form() {
	cleanup_item_pending_photos();
	item_form_draft					 = ItemDraft{};
	item_form_mode					 = FormMode::Create;
	item_storage_candidates_expanded = false;
	item_tag_candidates_expanded	 = false;
	item_listing_expanded			 = false;
	item_finance_expanded			 = false;
	for (juce::TextEditor* editor :
		 {&item_name_editor, &item_category_editor, &item_notes_editor,
		  &item_listing_marketplace_editor, &item_listing_url_editor,
		  &item_listing_note_editor, &item_acquisition_source_editor}) {
		editor->setText(juce::String{}, juce::dontSendNotification);
	}
}

void AppShellComponent::reset_storage_form() {
	storage_form_draft					 = StorageDraft{};
	storage_form_mode					 = FormMode::Create;
	storage_parent_candidates_expanded	 = false;
	storage_tag_candidates_expanded		 = false;
	storage_archive_warning_acknowledged = false;
	for (juce::TextEditor* editor :
		 {&storage_name_editor, &storage_type_editor, &storage_location_editor,
		  &storage_notes_editor}) {
		editor->setText(juce::String{}, juce::dontSendNotification);
	}
}

void AppShellComponent::load_item_form_from_record(
	const persistence::ItemEnvelope& item) {
	item_form_draft = ItemDraft{.existing_id  = item.record.id,
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
	item_listing_expanded = !item.record.listing.empty();
	item_finance_expanded =
		!item.record.acquisition.empty() || !item.record.finance.empty();
	item_storage_candidates_expanded = false;
	item_tag_candidates_expanded	 = false;
}

void AppShellComponent::load_storage_form_from_record(
	const persistence::StorageEnvelope& storage) {
	storage_form_draft =
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
	storage_parent_candidates_expanded	 = false;
	storage_tag_candidates_expanded		 = false;
	storage_archive_warning_acknowledged = false;
}

void AppShellComponent::save_item_form() {
	item_form_draft.display_name = item_name_editor.getText().toStdString();
	item_form_draft.category	 = item_category_editor.getText().toStdString();
	item_form_draft.notes		 = item_notes_editor.getText().toStdString();
	item_form_draft.listing.marketplace =
		item_listing_marketplace_editor.getText().toStdString();
	item_form_draft.listing.url =
		item_listing_url_editor.getText().toStdString();
	item_form_draft.listing.note =
		item_listing_note_editor.getText().toStdString();
	item_form_draft.acquisition.source =
		item_acquisition_source_editor.getText().toStdString();
	item_form_draft.pending_photo_import_planned =
		has_ready_pending_photo(item_form_pending_photos);

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
				.draft			 = item_form_draft,
				.pending_sources = item_form_pending_photos},
			last_progress_events, never_cancelled);
	if (result.warning_acknowledgement_required())
		item_form_draft.warning_acknowledged = true;
	if (result.warning_acknowledgement_required()
		&& result.save_result.saved_record_id) {
		item_form_draft.reserved_new_id = result.save_result.saved_record_id;
	}
	if (result.item_saved())
		item_form_draft.existing_id = result.save_result.saved_record_id;
	apply_item_save_with_pending_photos_result(std::move(result));
}

void AppShellComponent::save_storage_form() {
	storage_form_draft.display_name =
		storage_name_editor.getText().toStdString();
	storage_form_draft.storage_type =
		storage_type_editor.getText().toStdString();
	storage_form_draft.location =
		storage_location_editor.getText().toStdString();
	storage_form_draft.notes = storage_notes_editor.getText().toStdString();
	storage_form_draft.archive_warning_acknowledged =
		storage_archive_warning_acknowledged;
	EntityEditResult result =
		save_storage_draft(EntityEditRequest{.current_session = session,
											 .identifiers = edit_identifiers,
											 .clock		  = edit_clock},
						   storage_form_draft);
	if (result.warning_acknowledgement_required)
		storage_archive_warning_acknowledged = true;
	if (result.warning_acknowledgement_required && result.saved_record_id)
		storage_form_draft.reserved_new_id = result.saved_record_id;
	if (result.succeeded() && result.saved_record_id)
		storage_form_draft.existing_id = result.saved_record_id;
	apply_entity_edit_result(std::move(result));
}

void AppShellComponent::apply_item_save_with_pending_photos_result(
	ItemSaveWithPendingPhotosResult result) {
	last_edit_diagnostics	 = result.save_result.diagnostics;
	item_form_pending_photos = std::move(result.pending_sources);
	std::erase_if(item_form_pending_photos,
				  [](const PendingPhotoSource& source) {
		return !source.staged_path.has_value();
	});
	if (result.warning_acknowledgement_required()) {
		last_edit_message = "Confirm warning and save again.";
		refresh_all();
		return;
	}
	if (result.save_result.failed()) {
		last_edit_message = "Save failed.";
		refresh_all();
		return;
	}

	last_photo_diagnostics.clear();
	if (result.import_attempted) {
		for (const EntityEditDiagnostic& diagnostic :
			 result.import_result.diagnostics) {
			last_photo_diagnostics.push_back(core::Diagnostic{
				.severity		   = diagnostic.severity,
				.code			   = diagnostic.code,
				.message		   = diagnostic.message,
				.technical_details = diagnostic.technical_details});
		}
		last_photo_diagnostics.insert(last_photo_diagnostics.end(),
									  result.cleanup_result.diagnostics.begin(),
									  result.cleanup_result.diagnostics.end());
		if (result.import_result.succeeded()) {
			last_photo_message =
				"Pending photo import completed: "
				+ std::to_string(result.import_result.summary.success_count)
				+ " imported, "
				+ std::to_string(result.import_result.summary.failure_count)
				+ " failed.";
		} else if (result.import_result.was_user_cancelled()) {
			last_photo_message = "Item saved, pending photo import cancelled.";
		} else {
			last_photo_message = "Item saved, but pending photo import failed.";
		}
		if (result.cleanup_attempted && result.cleanup_result.failed())
			last_photo_message += " Pending source cleanup needs attention.";
		if (!result.import_result.imported_photo_ids.empty())
			selected_photo_id = result.import_result.imported_photo_ids.front();
	} else if (!item_form_pending_photos.empty()) {
		last_photo_message =
			"Item saved, but no staged pending photos were ready.";
	}

	session = std::move(result.session);
	last_edit_message =
		result.save_result.metadata_changed ? "Saved." : "No changes.";
	last_edit_diagnostics.clear();
	if (item_form_draft.existing_id) {
		selected_item_id = *item_form_draft.existing_id;
		destination		 = RootDestination::ItemDetail;
	} else {
		destination =
			form_return_destination.value_or(RootDestination::Catalog);
	}
	refresh_all();
}

void AppShellComponent::apply_entity_edit_result(EntityEditResult result) {
	last_edit_diagnostics = std::move(result.diagnostics);
	if (result.warning_acknowledgement_required) {
		last_edit_message = "Confirm warning and save again.";
		refresh_all();
		return;
	}
	if (result.failed()) {
		last_edit_message = "Save failed.";
		refresh_all();
		return;
	}
	const RootDestination completed_destination = destination;
	const std::optional<core::StableIdentifier> saved_item_id =
		item_form_draft.existing_id;
	const std::optional<core::StableIdentifier> saved_storage_id =
		storage_form_draft.existing_id;
	session			  = std::move(result.session);
	last_edit_message = result.metadata_changed ? "Saved." : "No changes.";
	last_edit_diagnostics.clear();
	if (completed_destination == RootDestination::ItemForm && saved_item_id) {
		selected_item_id = *saved_item_id;
		destination		 = RootDestination::ItemDetail;
	} else if (completed_destination == RootDestination::StorageForm
			   && saved_storage_id) {
		selected_storage_id = *saved_storage_id;
		destination			= RootDestination::StorageDetail;
	} else {
		destination =
			form_return_destination.value_or(RootDestination::Catalog);
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
	switch (destination) {
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
				item_form_mode == FormMode::Create ? "Add item" : "Edit item";
			break;
		case RootDestination::StorageForm:
			title = storage_form_mode == FormMode::Create ? "Add storage"
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
		destination = RootDestination::BackupRecovery;
		title		= "Fatal recovery";
	}
	title_label.setText(title, juce::dontSendNotification);
	form_save_button.setButtonText(
		destination == RootDestination::ItemForm	  ? "Save item"
		: destination == RootDestination::StorageForm ? "Save storage"
													  : "Save");

	if (session.fatal()) {
		catalog_nav_button.setEnabled(false);
		storages_nav_button.setEnabled(false);
		add_nav_button.setEnabled(false);
		more_nav_button.setEnabled(true);
	} else {
		catalog_nav_button.setEnabled(true);
		storages_nav_button.setEnabled(true);
		add_nav_button.setEnabled(true);
		more_nav_button.setEnabled(true);
	}

	std::string status =
		"Load: " + std::string{persistence::to_string(session.load_status)};
	status += " · " + std::string{to_string(session.source)};
	status += " · items=" + std::to_string(session.repository.items.size());
	status +=
		" · storages=" + std::to_string(session.repository.storages.size());
	if (session.demo_catalog_active)
		status += " · demo catalog";
	status_label.setText(juce_text(status), juce::dontSendNotification);

	const juce::Colour selected_colour = accent_colour().withAlpha(0.65f);
	const juce::Colour normal_colour   = panel_colour();
	catalog_nav_button.setColour(juce::TextButton::buttonColourId,
								 destination == RootDestination::Catalog
									 ? selected_colour
									 : normal_colour);
	storages_nav_button.setColour(
		juce::TextButton::buttonColourId,
		destination == RootDestination::Storages
				|| destination == RootDestination::StorageDetail
			? selected_colour
			: normal_colour);
	add_nav_button.setColour(
		juce::TextButton::buttonColourId,
		destination == RootDestination::Add
				|| destination == RootDestination::ItemForm
				|| destination == RootDestination::StorageForm
			? selected_colour
			: normal_colour);
	more_nav_button.setColour(
		juce::TextButton::buttonColourId,
		destination == RootDestination::More ? selected_colour : normal_colour);
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
		if (!last_edit_message.empty()) {
			content->add_label(juce_text(last_edit_message), 42,
							   accent_colour().withAlpha(0.34f), true);
		}
		if (!last_edit_diagnostics.empty()) {
			content->add_label(
				juce_text(diagnostic_summary(last_edit_diagnostics)), 76,
				warning_panel_colour(), true);
		}
		if (destination != RootDestination::PhotoViewer
			&& !last_photo_message.empty()) {
			content->add_label(juce_text(last_photo_message), 54,
							   accent_colour().withAlpha(0.34f), true);
		}
		if (destination != RootDestination::PhotoViewer
			&& !last_photo_diagnostics.empty()) {
			content->add_label(
				juce_text(core_diagnostic_summary(last_photo_diagnostics)), 76,
				warning_panel_colour(), true);
		}
		if (destination != RootDestination::BackupRecovery
			&& !last_backup_message.empty()) {
			content->add_label(juce_text(last_backup_message), 62,
							   accent_colour().withAlpha(0.34f), true);
		}
		if (destination != RootDestination::BackupRecovery
			&& !last_backup_diagnostics.empty()) {
			content->add_label(
				juce_text(core_diagnostic_summary(last_backup_diagnostics)), 76,
				warning_panel_colour(), true);
		}
		switch (destination) {
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
