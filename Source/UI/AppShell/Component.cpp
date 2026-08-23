#include "UI/AppShell/Component.hpp"
#include "Platform/JpegXlPhotoCodec.hpp"
#include "UI/AppShell/EditCoordinator.hpp"
#include "UI/AppShell/PreviewScheduler.hpp"
#include "UI/AppShell/RouteCoordinator.hpp"
#include "UI/AppShell/ContentComponent.hpp"
#include "UI/View/Primitives/Palette.hpp"
#include "UI/View/SafeArea.hpp"
#include "UI/View/ScreenText.hpp"

#include "UI/Session/BackupRecoverySession.hpp"
#include "UI/Session/CatalogStartupSession.hpp"
#include "UI/Session/EntityEditSession.hpp"
#include "UI/Session/PhotoSession.hpp"
#include "UI/Session/StartupRecoverySession.hpp"

#include "Domain/Domain.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace shuba::ui {

std::unique_ptr<platform::ContentStagingService>
OperationWorkerServiceFactory::make_content_staging_service() const {
	return std::make_unique<platform::JuceAndroidContentStagingService>();
}

std::unique_ptr<platform::SourceByteFingerprintService>
OperationWorkerServiceFactory::make_source_fingerprint_service() const {
	return std::make_unique<platform::JuceMd5SourceByteFingerprintService>();
}

std::unique_ptr<platform::SourceImageDecodeService>
OperationWorkerServiceFactory::make_source_decode_service() const {
	return std::make_unique<platform::JuceAndroidSourceImageDecodeService>();
}

std::unique_ptr<platform::InternalPhotoCodec>
OperationWorkerServiceFactory::make_internal_photo_codec() const {
	return std::make_unique<platform::JpegXlInternalPhotoCodec>();
}

std::unique_ptr<platform::JpegExportService>
OperationWorkerServiceFactory::make_jpeg_export_service() const {
	return std::make_unique<platform::JuceJpegExportService>();
}

std::unique_ptr<platform::ZipArchiveService>
OperationWorkerServiceFactory::make_zip_archive_service() const {
	return std::make_unique<platform::JuceZipArchiveService>();
}

std::unique_ptr<platform::DocumentExportService>
OperationWorkerServiceFactory::make_document_export_service() const {
	return std::make_unique<platform::JuceAndroidDocumentExportService>();
}

core::StableIdentifier ShellIdentifierSource::next_stable_identifier() {
	return random_identifiers.next_stable_identifier();
}

core::OperationIdentifier ShellIdentifierSource::next_operation_identifier() {
	return random_identifiers.next_operation_identifier();
}

core::EpochMilliseconds ShellClock::now() const {
	return core::SystemClock{}.now();
}

Component::Component(CatalogSessionState session_state,
									 PlatformServices platform_services)
	: session(std::move(session_state))
	, path_provider(platform_services.path_provider)
	, android_previous_exit_service(
		  platform_services.android_previous_exit_service)
	, localization(platform_services.localization)
	, app_version(std::move(platform_services.app_version))
	, platform_name(std::move(platform_services.platform_name))
	, debug_demo_seed_enabled(platform_services.debug_demo_seed_enabled)
	, internal_photo_codec(platform_services.internal_photo_codec)
	, content(std::make_unique<ContentComponent>()) {
	setOpaque(true);
	setWantsKeyboardFocus(true);
	setSize(480, 720);

	chrome = std::make_unique<ChromeComponent>(
		ChromeComponent::Callbacks{
			.catalog_search_changed = [this] { schedule_content_refresh(); },
			.storage_search_changed = [this] { schedule_content_refresh(); },
			.catalog_clear =
				[this] {
		chrome->clear_catalog_query_without_notification();
		refresh_content();
	},
			.catalog_filter		   = [this] { toggle_catalog_filters(); },
			.catalog_apply_filters = [this] { apply_catalog_filters(); },
			.catalog_clear_filters =
				[this] {
		reset_catalog_filters();
		refresh_all();
		schedule_catalog_search_focus_release();
	},
			.catalog_close_filters = [this] { close_catalog_filters(); },
			.storage_clear =
				[this] {
		chrome->clear_storage_query_without_notification();
		refresh_content();
	},
			.form_cancel =
				[this] {
		if (!shell_operation.active() && route_coordinator != nullptr) {
			route_coordinator->return_from_form(
				route.form_return_destination.value_or(
					RootDestination::Catalog));
		}
	},
			.form_save =
				[this] {
		if (edit_coordinator == nullptr)
			return;
		if (route.destination == RootDestination::ItemForm)
			edit_coordinator->save_item_form();
		else if (route.destination == RootDestination::StorageForm)
			edit_coordinator->save_storage_form();
	},
			.select_catalog = [this] { select_root(RootDestination::Catalog); },
			.select_storages =
				[this] { select_root(RootDestination::Storages); },
			.select_add	 = [this] { select_root(RootDestination::Add); },
			.select_more = [this] { select_root(RootDestination::More); }},
		localization);
	addAndMakeVisible(*chrome);
	shell_operation_progress =
		std::make_unique<ShellOperationProgressComponent>(
			[this] { request_shell_operation_cancellation(); });
	addChildComponent(*shell_operation_progress);
	shell_operation_runner = std::make_unique<OperationRunner>(
		OperationRunner::Dependencies{
			.operation_gate			= ui_operation_gate,
			.worker_service_factory = shell_operation_worker_services,
			.progress =
				[this](std::uint64_t generation,
					   const platform::ProgressEvent& event) {
		apply_shell_operation_progress(generation, event);
	},
			.failure = [this](ShellOperationJobType job_type,
							  std::uint64_t generation, std::string failure) {
		apply_shell_operation_failure(job_type, generation, std::move(failure));
	}});

	photo_coordinator = std::make_unique<PhotoCoordinator>(
		PhotoCoordinator::Dependencies{
			.session					= session,
			.route						= route,
			.item_form					= item_form,
			.storage_form				= storage_form,
			.feedback					= feedback,
			.photo_display				= photo_display,
			.preview_cache				= preview_cache,
			.identifiers				= edit_identifiers,
			.clock						= edit_clock,
			.operation_gate				= ui_operation_gate,
			.photo_selection_service	= photo_selection_service,
			.document_export_service	= document_export_service,
			.content_staging_service	= content_staging_service,
			.source_fingerprint_service = source_fingerprint_service,
			.source_decode_service		= source_decode_service,
			.jpeg_export_service		= jpeg_export_service,
			.internal_photo_codec		= internal_photo_codec,
			.progress_events			= progress_events,
			.cancellation_token			= never_cancelled,
			.shell_operation_runner		= *shell_operation_runner,
			.shell_operation_state		= shell_operation,
			.localization				= localization,
			.invalidate_all_previews	= [this] { invalidate_all_previews(); },
			.invalidate_internal_photo_preview =
				[this](const core::StableIdentifier& photo_id) {
		invalidate_internal_photo_preview(photo_id);
	},
			.invalidate_staged_photo_preview =
				[this](const std::filesystem::path& staged_path) {
		invalidate_staged_photo_preview(staged_path);
	},
			.refresh_all = [this] { refresh_all(); },
			.begin_shell_operation =
				[this](ShellOperationJobType job_type,
					   std::uint64_t generation) {
		begin_shell_operation(job_type, generation);
	},
			.complete_shell_operation = [this] {
		complete_shell_operation();
	}});

	preview_scheduler = std::make_unique<PreviewScheduler>(
		PreviewScheduler::Dependencies{
			.session				 = session,
			.photo_display			 = photo_display,
			.preview_cache			 = preview_cache,
			.internal_photo_codec	 = internal_photo_codec,
			.source_decode_service	 = source_decode_service,
			.jpeg_export_service	 = jpeg_export_service,
			.document_export_service = document_export_service,
			.localization			 = localization,
			.refresh_content		 = [this] { schedule_content_refresh(); }});

	route_coordinator = std::make_unique<RouteCoordinator>(
		RouteCoordinator::Dependencies{
			.session		= session,
			.route			= route,
			.backup			= backup,
			.feedback		= feedback,
			.photo_display	= photo_display,
			.storage_detail = storage_detail,
			.cleanup_item_pending_photos =
				[this] { cleanup_item_pending_photos(); },
			.cleanup_storage_pending_photos =
				[this] { cleanup_storage_pending_photos(); },
			.refresh_all = [this] { refresh_all(); }});

	edit_coordinator = std::make_unique<EditCoordinator>(
		EditCoordinator::Dependencies{
			.session					= session,
			.route						= route,
			.item_form					= item_form,
			.storage_form				= storage_form,
			.feedback					= feedback,
			.identifiers				= edit_identifiers,
			.clock						= edit_clock,
			.operation_gate				= ui_operation_gate,
			.content_staging_service	= content_staging_service,
			.source_fingerprint_service = source_fingerprint_service,
			.source_decode_service		= source_decode_service,
			.internal_photo_codec		= internal_photo_codec,
			.shell_operation_runner		= *shell_operation_runner,
			.shell_operation_state		= shell_operation,
			.localization				= localization,
			.editors =
				EditCoordinator::Editors{
					.item_name_editor	  = item_name_editor,
					.item_category_editor = item_category_editor,
					.item_notes_editor	  = item_notes_editor,
					.item_listing_marketplace_editor =
						item_listing_marketplace_editor,
					.item_listing_url_editor  = item_listing_url_editor,
					.item_listing_note_editor = item_listing_note_editor,
					.item_acquisition_source_editor =
						item_acquisition_source_editor,
					.storage_name_editor	 = storage_name_editor,
					.storage_type_editor	 = storage_type_editor,
					.storage_location_editor = storage_location_editor,
					.storage_notes_editor	 = storage_notes_editor},
			.cleanup_item_pending_photos =
				[this] { cleanup_item_pending_photos(); },
			.cleanup_storage_pending_photos =
				[this] { cleanup_storage_pending_photos(); },
			.invalidate_all_previews = [this] { invalidate_all_previews(); },
			.refresh_all			 = [this] { refresh_all(); },
			.refresh_content		 = [this] { refresh_content(); },
			.begin_shell_operation =
				[this](ShellOperationJobType job_type,
					   std::uint64_t generation) {
		begin_shell_operation(job_type, generation);
	},
			.complete_shell_operation = [this] {
		complete_shell_operation();
	}});
	screen_renderer = std::make_unique<ScreenRenderer>(
		ScreenRenderer::Dependencies{
			.session = session,
			.route = route,
			.catalog_filter_state = catalog_filter_state,
			.item_form = item_form,
			.storage_form = storage_form,
			.feedback = feedback,
			.backup = backup,
			.photo_display = photo_display,
			.storage_detail = storage_detail,
			.preview_cache = preview_cache,
			.edit_identifiers = edit_identifiers,
			.edit_clock = edit_clock,
			.shell_operation_state = shell_operation,
			.localization = localization,
			.content = *content,
			.editors = ScreenRenderer::Editors{
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
			.queries = ScreenRenderer::Queries{
				.catalog_query = [this] {
					return chrome != nullptr ? chrome->catalog_query()
									 : std::string{};
				},
				.storage_query = [this] {
					return chrome != nullptr ? chrome->storage_query()
									 : std::string{};
				}},
			.actions = ScreenRenderer::Actions{
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
					[this](const domain::PhotoOwner& owner,
						   const std::optional<core::StableIdentifier>& photo_id) {
						open_photo_viewer(owner, photo_id);
					},
				.open_new_item_form =
					[this](std::optional<core::StableIdentifier> storage_id) {
						if (edit_coordinator != nullptr)
							edit_coordinator->open_new_item_form(
								std::move(storage_id));
					},
				.open_existing_item_form =
					[this](core::StableIdentifier item_id) {
						if (edit_coordinator != nullptr)
							edit_coordinator->open_existing_item_form(
								std::move(item_id));
					},
				.open_new_storage_form =
					[this](std::optional<core::StableIdentifier> parent_id) {
						if (edit_coordinator != nullptr)
							edit_coordinator->open_new_storage_form(
								std::move(parent_id));
					},
				.open_existing_storage_form =
					[this](core::StableIdentifier storage_id) {
						if (edit_coordinator != nullptr)
							edit_coordinator->open_existing_storage_form(
								std::move(storage_id));
					},
				.request_add_photos = [this](const domain::PhotoOwner& owner) {
					request_add_photos(owner);
				},
				.request_add_pending_item_photos = [this] {
					request_add_pending_item_photos();
				},
				.request_add_pending_storage_photos = [this] {
					request_add_pending_storage_photos();
				},
				.request_export_photo =
					[this](const core::StableIdentifier& photo_id) {
						request_export_photo(photo_id);
					},
				.request_export_backup = [this] { request_export_backup(); },
				.request_export_diagnostic_archive = [this] {
					request_export_diagnostic_archive();
				},
				.request_import_backup = [this] { request_import_backup(); },
				.retry_normal_startup = [this] { retry_normal_startup(); },
				.confirm_staged_backup_import = [this] {
					confirm_staged_backup_import();
				},
				.cleanup_item_pending_photos = [this] {
					cleanup_item_pending_photos();
				},
				.cleanup_storage_pending_photos = [this] {
					cleanup_storage_pending_photos();
				},
				.remove_item_pending_photo = [this](std::size_t index) {
					remove_item_pending_photo(index);
				},
				.remove_storage_pending_photo = [this](std::size_t index) {
					remove_storage_pending_photo(index);
				},
				.set_item_pending_photo_as_main = [this](std::size_t index) {
					if (edit_coordinator != nullptr)
							edit_coordinator->set_item_pending_photo_as_main(index);
				},
				.set_storage_pending_photo_as_main = [this](std::size_t index) {
					if (edit_coordinator != nullptr)
							edit_coordinator->set_storage_pending_photo_as_main(index);
				},
				.request_delete_photo =
					[this](const core::StableIdentifier& photo_id) {
						request_delete_photo_confirmation(photo_id);
					},
				.confirm_delete_photo =
					[this](const core::StableIdentifier& photo_id) {
						confirm_delete_photo(photo_id);
					},
				.cancel_delete_photo = [this] {
					cancel_delete_photo_confirmation();
				},
				.apply_catalog_filters = [this] { apply_catalog_filters(); },
				.reset_catalog_filters = [this] { reset_catalog_filters(); },
				.apply_entity_edit_result = [this](EntityEditResult result) {
					if (edit_coordinator != nullptr)
					edit_coordinator->apply_entity_edit_result(
						std::move(result));
				},
				.apply_photo_edit_result =
					[this](EntityEditResult result,
						   const core::StableIdentifier& selected_photo_id) {
						apply_photo_edit_result(std::move(result),
											selected_photo_id);
					},
				.request_internal_preview =
					[this](core::StableIdentifier photo_id,
						   ImagePreviewSize target_size,
						   ImagePreviewRequestPriority priority) {
						request_internal_preview_async(
							std::move(photo_id), target_size, priority);
					},
				.request_staged_preview =
					[this](PendingPhotoSource source,
						   ImagePreviewSize target_size,
						   ImagePreviewRequestPriority priority) {
						request_staged_preview_async(
							std::move(source), target_size, priority);
					},
				.preview_failure_message =
					[this](const ImagePreviewRequestIdentity& identity) {
						return preview_failure_message(identity);
					},
				.request_photo_display = [this](core::StableIdentifier photo_id) {
					request_photo_display_async(std::move(photo_id));
				},
				.refresh_all = [this] { refresh_all(); },
				.refresh_content = [this] { refresh_content(); }}});

	viewport.setViewedComponent(content.get(), false);
	viewport.setScrollBarsShown(true, false);
	viewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::nonHover);
	addAndMakeVisible(viewport);

	refresh_all();
	clear_controlled_startup_attempt_marker();
}

Component::~Component() {
	stopTimer();
	const std::shared_ptr<CallbackLifetimeToken> picker_lifetime =
		std::move(document_picker_callback_lifetime);
	if (picker_lifetime != nullptr)
		picker_lifetime->invalidate_and_wait();
	photo_coordinator.reset();
	edit_coordinator.reset();
	if (shell_operation_runner != nullptr)
		shell_operation_runner->stop();
	shell_operation_runner.reset();
	preview_scheduler.reset();
}

void Component::paint(juce::Graphics& graphics) {
	graphics.fillAll(background_colour());
}

void Component::resized() {
	juce::Rectangle<int> bounds =
		fullscreen_safe_content_bounds(*this).reduced(10);
	if (chrome != nullptr) {
		chrome->setBounds(getLocalBounds());
		bounds = chrome->layout_shell(bounds);
	}
	if (shell_operation_progress != nullptr && shell_operation.active()) {
		shell_operation_progress->setBounds(bounds.removeFromTop(82));
		bounds.removeFromTop(6);
	} else if (shell_operation_progress != nullptr) {
		shell_operation_progress->setBounds(0, 0, 0, 0);
	}
	viewport.setBounds(bounds);
	if (content) {
		content->set_viewport_height_hint(bounds.getHeight());
		content->setSize(viewport.getWidth(), content->getHeight());
	}
}

bool Component::handle_system_back() {
	if (route_coordinator == nullptr)
		return false;

	const bool selected_viewer_owner_is_item =
		route.selected_photo_owner.has_value()
		&& route.selected_photo_owner->type == domain::PhotoOwnerType::Item;
	const bool selected_viewer_owner_is_storage =
		route.selected_photo_owner.has_value()
		&& route.selected_photo_owner->type == domain::PhotoOwnerType::Storage;
	const BackDecision decision =
		decide_back_navigation(BackNavigationState{
			.destination				  = route.destination,
			.shell_operation_active		  = shell_operation.active(),
			.session_fatal				  = session.fatal(),
			.catalog_filter_panel_visible = catalog_filter_state.panel_visible,
			.photo_deletion_confirmation_pending =
				photo_display.pending_delete_photo_id.has_value(),
			.selected_viewer_owner_is_item = selected_viewer_owner_is_item,
			.selected_viewer_owner_is_storage =
				selected_viewer_owner_is_storage,
			.staged_import_confirmation_pending =
				backup.pending_import_staging.has_value(),
			.contextual_return_available =
				!route.contextual_return_locations.empty(),
			.form_return_destination = route.form_return_destination.value_or(
				RootDestination::Catalog)});
	if (!decision.consumed())
		return false;

	if (decision.action == BackAction::CloseCatalogFilterPanel) {
		close_catalog_filters();
		return true;
	}
	if (decision.action == BackAction::CancelPhotoDeletion) {
		cancel_delete_photo_confirmation();
		return true;
	}

	return route_coordinator->handle_system_back(decision);
}

void Component::handle_application_suspended() {
	stopTimer();
	if (preview_scheduler != nullptr)
		preview_scheduler->release_disposable_preview_memory();
	else
		preview_cache.clear();
}

void Component::handle_application_resumed() {
	if (preview_scheduler != nullptr)
		schedule_content_refresh();
}

void Component::build_catalog_content() {
	if (screen_renderer != nullptr)
		screen_renderer->build_catalog_content();
}

void Component::build_filter_panel() {
	if (screen_renderer != nullptr)
		screen_renderer->build_filter_panel();
}

void Component::build_storages_content() {
	if (screen_renderer != nullptr)
		screen_renderer->build_storages_content();
}

void Component::build_item_detail_content() {
	if (screen_renderer != nullptr)
		screen_renderer->build_item_detail_content();
}

void Component::build_storage_detail_content() {
	if (screen_renderer != nullptr)
		screen_renderer->build_storage_detail_content();
}

void Component::build_item_form_content() {
	if (screen_renderer != nullptr)
		screen_renderer->build_item_form_content();
}

void Component::build_storage_form_content() {
	if (screen_renderer != nullptr)
		screen_renderer->build_storage_form_content();
}

void Component::build_photo_viewer_content() {
	if (screen_renderer != nullptr)
		screen_renderer->build_photo_viewer_content();
}

void Component::build_backup_recovery_content() {
	if (screen_renderer != nullptr)
		screen_renderer->build_backup_recovery_content();
}

void Component::build_add_content() {
	if (screen_renderer != nullptr)
		screen_renderer->build_add_content();
}

void Component::build_more_content() {
	if (screen_renderer != nullptr)
		screen_renderer->build_more_content();
}

void Component::select_root(RootDestination destination_value) {
	if (!shell_operation.active() && route_coordinator != nullptr)
		route_coordinator->select_root(destination_value);
}

void Component::open_item_detail(core::StableIdentifier item_id) {
	if (!shell_operation.active() && route_coordinator != nullptr)
		route_coordinator->open_item_detail(std::move(item_id));
}

void Component::open_storage_detail(core::StableIdentifier storage_id) {
	if (!shell_operation.active() && route_coordinator != nullptr)
		route_coordinator->open_storage_detail(std::move(storage_id));
}

void Component::open_photo_viewer(
	const domain::PhotoOwner& owner,
	const std::optional<core::StableIdentifier>& requested_photo_id) {
	if (!shell_operation.active() && route_coordinator != nullptr)
		route_coordinator->open_photo_viewer(owner, requested_photo_id);
}

void Component::request_add_photos(const domain::PhotoOwner& owner) {
	if (photo_coordinator != nullptr)
		photo_coordinator->request_add_photos(owner);
}

void Component::request_add_pending_item_photos() {
	if (photo_coordinator != nullptr)
		photo_coordinator->request_add_pending_item_photos();
}

void Component::request_add_pending_storage_photos() {
	if (photo_coordinator != nullptr)
		photo_coordinator->request_add_pending_storage_photos();
}

void Component::request_export_photo(
	const core::StableIdentifier& photo_id) {
	if (!shell_operation.active() && photo_coordinator != nullptr)
		photo_coordinator->request_export_photo(photo_id);
}

void Component::request_delete_photo_confirmation(
	const core::StableIdentifier& photo_id) {
	if (!shell_operation.active() && photo_coordinator != nullptr)
		photo_coordinator->request_delete_photo_confirmation(photo_id);
}

void Component::confirm_delete_photo(
	const core::StableIdentifier& photo_id) {
	if (!shell_operation.active() && photo_coordinator != nullptr)
		photo_coordinator->confirm_delete_photo(photo_id);
}

void Component::cancel_delete_photo_confirmation() {
	if (!shell_operation.active() && photo_coordinator != nullptr)
		photo_coordinator->cancel_delete_photo_confirmation();
}

void Component::apply_photo_edit_result(
	EntityEditResult result,
	const core::StableIdentifier& selected_photo_id_value) {
	if (photo_coordinator != nullptr) {
		photo_coordinator->apply_photo_edit_result(std::move(result),
												   selected_photo_id_value);
	}
}

void Component::cleanup_item_pending_photos() {
	if (!shell_operation.active() && photo_coordinator != nullptr)
		photo_coordinator->cleanup_item_pending_photos();
}

void Component::cleanup_storage_pending_photos() {
	if (!shell_operation.active() && photo_coordinator != nullptr)
		photo_coordinator->cleanup_storage_pending_photos();
}

void Component::remove_item_pending_photo(
	std::size_t pending_photo_index) {
	if (!shell_operation.active() && photo_coordinator != nullptr)
		photo_coordinator->remove_item_pending_photo(pending_photo_index);
}

void Component::remove_storage_pending_photo(
	std::size_t pending_photo_index) {
	if (!shell_operation.active() && photo_coordinator != nullptr)
		photo_coordinator->remove_storage_pending_photo(pending_photo_index);
}

void Component::reset_catalog_filters() {
	catalog_filter_state.applied	   = catalog::CatalogSearchFilters{};
	catalog_filter_state.draft		   = catalog_filter_state.applied;
	catalog_filter_state.panel_visible = false;
}

void Component::toggle_catalog_filters() {
	const bool opening_filters = !catalog_filter_state.panel_visible;
	if (opening_filters) {
		schedule_catalog_search_focus_release();
		catalog_filter_state.draft = catalog_filter_state.applied;
	}
	catalog_filter_state.panel_visible = !catalog_filter_state.panel_visible;
	refresh_all();
	if (!catalog_filter_state.panel_visible)
		schedule_catalog_search_focus_release();
}

void Component::apply_catalog_filters() {
	catalog_filter_state.applied	   = catalog_filter_state.draft;
	catalog_filter_state.panel_visible = false;
	refresh_all();
	schedule_catalog_search_focus_release();
}

void Component::close_catalog_filters() {
	catalog_filter_state.draft		   = catalog_filter_state.applied;
	catalog_filter_state.panel_visible = false;
	refresh_all();
	schedule_catalog_search_focus_release();
}

void Component::release_catalog_search_focus() {
	if (chrome != nullptr)
		chrome->release_catalog_search_focus();
	grabKeyboardFocus();
}

void Component::schedule_catalog_search_focus_release() {
	release_catalog_search_focus();
	juce::Component::SafePointer<Component> safe_this{this};
	juce::MessageManager::callAsync([safe_this] {
		if (safe_this != nullptr)
			safe_this->release_catalog_search_focus();
	});
}

juce::String Component::catalog_draft_result_count_text() const {
	catalog::CatalogSearchOptions options{
		.include_storage_results_for_empty_query = true};
	const catalog::CatalogSearchResultSet results = catalog::search_catalog(
		session.search_index,
		chrome != nullptr ? chrome->catalog_query() : std::string{},
		catalog_filter_state.draft, options);
	return juce_text(localization.draft_result_count(results.total_count));
}

void Component::request_internal_preview_async(
	core::StableIdentifier photo_id, ImagePreviewSize target_size,
	ImagePreviewRequestPriority priority) {
	if (preview_scheduler != nullptr)
		preview_scheduler->enqueue_internal_preview(std::move(photo_id),
													target_size, priority);
}

void Component::request_staged_preview_async(
	PendingPhotoSource source, ImagePreviewSize target_size,
	ImagePreviewRequestPriority priority) {
	if (preview_scheduler != nullptr)
		preview_scheduler->enqueue_staged_preview(std::move(source),
												  target_size, priority);
}

void Component::begin_shell_operation(ShellOperationJobType job_type,
											  std::uint64_t generation) {
	shell_operation.generation = generation;
	shell_operation.state	   = ShellOperationState::Running;
	shell_operation.job_type   = job_type;
	shell_operation.operation_id.reset();
	shell_operation.latest_progress.reset();
	update_shell_operation_progress_surface();
	refresh_all();
}

void Component::complete_shell_operation() {
	shell_operation.state = ShellOperationState::Idle;
	shell_operation.job_type.reset();
	shell_operation.operation_id.reset();
	shell_operation.latest_progress.reset();
	update_shell_operation_progress_surface();
	refresh_all();
}

void Component::apply_shell_operation_failure(
	ShellOperationJobType job_type, std::uint64_t generation,
	std::string details) {
	if (!shell_operation.active() || shell_operation.generation != generation
		|| shell_operation.job_type != job_type) {
		return;
	}

	const core::Diagnostic diagnostic{
		.severity = core::DiagnosticSeverity::WriteBlockingError,
		.code	  = "shell_operation_worker_failed",
		.message =
			localization.text(localization::MessageId::ShellOperationFailed),
		.technical_details = std::move(details)};
	const std::string message =
		localization.text(localization::MessageId::ShellOperationFailed);
	if (job_type == ShellOperationJobType::JpegExport
		|| job_type == ShellOperationJobType::DirectImport
		|| job_type == ShellOperationJobType::PendingItemStaging
		|| job_type == ShellOperationJobType::PendingStorageStaging
		|| job_type == ShellOperationJobType::ItemSaveWithPendingPhotos
		|| job_type == ShellOperationJobType::StorageSaveWithPendingPhotos) {
		feedback.photo_diagnostics = {diagnostic};
		feedback.photo_message	   = message;
	} else {
		feedback.backup_diagnostics = {diagnostic};
		feedback.backup_message		= message;
	}
	complete_shell_operation();
}

void Component::request_shell_operation_cancellation() {
	if (!shell_operation.active() || shell_operation_runner == nullptr
		|| !shell_operation.latest_progress.has_value()
		|| !shell_operation.latest_progress->cancellable) {
		return;
	}
	shell_operation.state = ShellOperationState::CancellationRequested;
	shell_operation_runner->request_cancellation();
	const std::string cancelling_message =
		localization.text(localization::MessageId::ShellOperationCancelling);
	if (shell_operation.job_type == ShellOperationJobType::JpegExport
		|| shell_operation.job_type == ShellOperationJobType::DirectImport
		|| shell_operation.job_type == ShellOperationJobType::PendingItemStaging
		|| shell_operation.job_type
			   == ShellOperationJobType::PendingStorageStaging
		|| shell_operation.job_type
			   == ShellOperationJobType::ItemSaveWithPendingPhotos
		|| shell_operation.job_type
			   == ShellOperationJobType::StorageSaveWithPendingPhotos) {
		feedback.photo_message = cancelling_message;
	} else {
		feedback.backup_message = cancelling_message;
	}
	update_shell_operation_progress_surface();
	refresh_all();
}

void Component::apply_shell_operation_progress(
	std::uint64_t generation, platform::ProgressEvent event) {
	if (shell_operation.generation != generation || !shell_operation.active())
		return;
	shell_operation.operation_id	= event.operation_id;
	shell_operation.latest_progress = std::move(event);
	update_shell_operation_progress_surface();
}

void Component::update_shell_operation_progress_surface() {
	if (shell_operation_progress == nullptr)
		return;
	const bool cancellation_available =
		shell_operation.state == ShellOperationState::Running
		&& shell_operation.latest_progress.has_value()
		&& shell_operation.latest_progress->cancellable;
	const juce::String summary = shell_operation.latest_progress.has_value()
									 ? juce_text(localization.progress_summary(
										   *shell_operation.latest_progress))
									 : juce::String{};
	shell_operation_progress->update_model(ShellOperationProgressModel{
		.heading = juce_text(
			localization.text(localization::MessageId::ShellOperationHeading)),
		.summary	  = summary,
		.cancel_label = juce_text(
			localization.text(localization::MessageId::ShellOperationCancel)),
		.active					= shell_operation.active(),
		.cancellation_available = cancellation_available});
	resized();
}

std::optional<juce::String> Component::preview_failure_message(
	const ImagePreviewRequestIdentity& identity) const {
	return preview_scheduler == nullptr
			   ? std::nullopt
			   : preview_scheduler->failure_message(identity);
}

void Component::request_photo_display_async(
	core::StableIdentifier photo_id) {
	if (preview_scheduler != nullptr)
		preview_scheduler->enqueue_display(std::move(photo_id));
}

void Component::invalidate_preview_failure(
	const ImagePreviewRequestIdentity& identity) {
	if (preview_scheduler != nullptr)
		preview_scheduler->clear_failure(identity);
}

void Component::invalidate_all_previews() {
	if (preview_scheduler != nullptr)
		preview_scheduler->invalidate_all();
	else
		preview_cache.clear();
}

void Component::invalidate_internal_photo_preview(
	const core::StableIdentifier& photo_id) {
	if (preview_scheduler != nullptr)
		preview_scheduler->invalidate_internal_photo(photo_id);
	else
		preview_cache.remove_internal_photo(photo_id);
}

void Component::invalidate_staged_photo_preview(
	const std::filesystem::path& staged_path) {
	if (preview_scheduler != nullptr)
		preview_scheduler->invalidate_staged_photo(staged_path);
	else
		preview_cache.remove_staged_photo(staged_path);
}

void Component::schedule_content_refresh() {
	startTimer(85);
}

void Component::timerCallback() {
	stopTimer();
	refresh_controls();
	refresh_content();
	resized();
	repaint();
}

void Component::refresh_all() {
	stopTimer();
	refresh_controls();
	refresh_content();
	resized();
	repaint();
}

void Component::clear_controlled_startup_attempt_marker() {
	if (!session.paths.has_value()
		|| session.source
			   == CatalogSessionStartupSource::StartupCrashSafeMode) {
		return;
	}

	StartupRecoveryFileResult cleared =
		clear_startup_attempt_marker(*session.paths);
	session.startup_diagnostics.insert(session.startup_diagnostics.end(),
									   cleared.diagnostics.begin(),
									   cleared.diagnostics.end());
}

void Component::refresh_controls() {
	juce::String title;
	switch (route.destination) {
		case RootDestination::Catalog:
			title = juce_text(
				localization.text(localization::MessageId::NavigationCatalog));
			break;
		case RootDestination::Storages:
			title = juce_text(
				localization.text(localization::MessageId::NavigationStorages));
			break;
		case RootDestination::Add:
			title = juce_text(
				localization.text(localization::MessageId::NavigationAdd));
			break;
		case RootDestination::More:
			title = juce_text(
				localization.text(localization::MessageId::NavigationMore));
			break;
		case RootDestination::ItemDetail:
			title = juce_text(
				localization.text(localization::MessageId::TitleItemDetail));
			break;
		case RootDestination::StorageDetail:
			title = juce_text(
				localization.text(localization::MessageId::TitleStorageDetail));
			break;
		case RootDestination::ItemForm:
			title = juce_text(localization.text(
				item_form.mode == FormMode::Create
					? localization::MessageId::TitleAddItem
					: localization::MessageId::TitleEditItem));
			break;
		case RootDestination::StorageForm:
			title = juce_text(localization.text(
				storage_form.mode == FormMode::Create
					? localization::MessageId::TitleAddStorage
					: localization::MessageId::TitleEditStorage));
			break;
		case RootDestination::PhotoViewer:
			title = juce_text(
				localization.text(localization::MessageId::TitlePhotoViewer));
			break;
		case RootDestination::BackupRecovery:
			title = juce_text(localization.text(
				session.fatal()
					? localization::MessageId::TitleFatalRecovery
					: localization::MessageId::TitleBackupRecovery));
			break;
	}
	if (session.fatal()) {
		route.destination = RootDestination::BackupRecovery;
		title			  = juce_text(
			localization.text(localization::MessageId::TitleFatalRecovery));
	}

	const std::string status =
		localization.shell_status(localization::ShellStatusFields{
			.load_status		 = session.load_status,
			.source				 = session.source,
			.item_count			 = session.repository.items.size(),
			.storage_count		 = session.repository.storages.size(),
			.demo_catalog_active = session.demo_catalog_active});
	if (chrome != nullptr) {
		const juce::String draft_result_count =
			catalog_filter_state.panel_visible
				? catalog_draft_result_count_text()
				: juce::String{};
		chrome->update_model(ChromeComponent::Model{
			.destination				= route.destination,
			.item_form_mode				= item_form.mode,
			.storage_form_mode			= storage_form.mode,
			.title						= title,
			.status						= juce_text(status),
			.catalog_draft_result_count = draft_result_count,
			.session_fatal				= session.fatal(),
			.shell_operation_active		= shell_operation.active(),
			.catalog_filters_active =
				has_catalog_filters(catalog_filter_state.applied),
			.catalog_filter_panel_visible =
				catalog_filter_state.panel_visible});
	}
}

void Component::refresh_content() {
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
