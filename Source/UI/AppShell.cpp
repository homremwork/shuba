#include "UI/AppShell.hpp"
#include "UI/AppShellEditCoordinator.hpp"
#include "Platform/JpegXlPhotoCodec.hpp"
#include "UI/AppShellPreviewScheduler.hpp"
#include "UI/AppShellRouteCoordinator.hpp"
#include "UI/View/AppShellContentComponent.hpp"
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
AppShellPhotoOperationWorkerServiceFactory::make_content_staging_service() const {
	return std::make_unique<platform::JuceAndroidContentStagingService>();
}

std::unique_ptr<platform::SourceByteFingerprintService>
AppShellPhotoOperationWorkerServiceFactory::make_source_fingerprint_service()
	const {
	return std::make_unique<platform::JuceMd5SourceByteFingerprintService>();
}

std::unique_ptr<platform::SourceImageDecodeService>
AppShellPhotoOperationWorkerServiceFactory::make_source_decode_service() const {
	return std::make_unique<platform::JuceAndroidSourceImageDecodeService>();
}

std::unique_ptr<platform::InternalPhotoCodec>
AppShellPhotoOperationWorkerServiceFactory::make_internal_photo_codec() const {
	return std::make_unique<platform::JpegXlInternalPhotoCodec>();
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

AppShellComponent::AppShellComponent(CatalogSessionState session_state,
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
	, content(std::make_unique<AppShellContentComponent>()) {
	setOpaque(true);
	setWantsKeyboardFocus(true);
	setSize(480, 720);

	chrome = std::make_unique<AppShellChromeComponent>(
		AppShellChromeComponent::Callbacks{
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
			.back =
				[this] {
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
	},
			.form_cancel =
				[this] {
		select_root(
			route.form_return_destination.value_or(RootDestination::Catalog));
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
	photo_operation_progress =
		std::make_unique<PhotoOperationProgressComponent>([this] {
			request_photo_operation_cancellation();
		});
	addChildComponent(*photo_operation_progress);
	photo_operation_runner = std::make_unique<AppShellPhotoOperationRunner>(
		AppShellPhotoOperationRunner::Dependencies{
			.operation_gate = ui_operation_gate,
			.worker_service_factory = photo_operation_worker_services,
			.progress = [this](std::uint64_t generation,
									  const platform::ProgressEvent& event) {
				apply_photo_operation_progress(generation, event);
			},
			.failure = [this](std::string failure) {
				feedback.photo_diagnostics = {core::Diagnostic{
					.severity = core::DiagnosticSeverity::WriteBlockingError,
					.code = "photo_operation_worker_failed",
					.message = localization.text(
						localization::MessageId::PhotoOperationFailed),
					.technical_details = std::move(failure)}};
				feedback.photo_message = localization.text(
					localization::MessageId::PhotoOperationFailed);
				complete_photo_operation();
			}});

	photo_coordinator = std::make_unique<AppShellPhotoCoordinator>(
		AppShellPhotoCoordinator::Dependencies{
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
			.progress_events			= last_progress_events,
			.cancellation_token		= never_cancelled,
			.photo_operation_runner	= *photo_operation_runner,
			.photo_operation_state	= photo_operation,
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
			.begin_photo_operation =
				[this](PhotoOperationJobType job_type, std::uint64_t generation) {
					begin_photo_operation(job_type, generation);
				},
			.complete_photo_operation = [this] { complete_photo_operation(); }});

	preview_scheduler = std::make_unique<AppShellPreviewScheduler>(
		AppShellPreviewScheduler::Dependencies{
			.session				 = session,
			.photo_display			 = photo_display,
			.preview_cache			 = preview_cache,
			.internal_photo_codec	 = internal_photo_codec,
			.source_decode_service	 = source_decode_service,
			.jpeg_export_service	 = jpeg_export_service,
			.document_export_service = document_export_service,
			.localization			 = localization,
			.refresh_content		 = [this] { refresh_content(); }});

	route_coordinator = std::make_unique<AppShellRouteCoordinator>(
		AppShellRouteCoordinator::Dependencies{
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

	edit_coordinator = std::make_unique<AppShellEditCoordinator>(
		AppShellEditCoordinator::Dependencies{
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
			.photo_operation_runner	= *photo_operation_runner,
			.photo_operation_state	= photo_operation,
			.localization				= localization,
			.editors =
				AppShellEditCoordinator::Editors{
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
			.refresh_all				 = [this] { refresh_all(); },
			.refresh_content		 = [this] { refresh_content(); },
			.begin_photo_operation =
				[this](PhotoOperationJobType job_type, std::uint64_t generation) {
					begin_photo_operation(job_type, generation);
				},
			.complete_photo_operation = [this] { complete_photo_operation(); }});
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
			.preview_cache = preview_cache,
			.edit_identifiers = edit_identifiers,
			.edit_clock = edit_clock,
			.photo_operation_state = photo_operation,
			.internal_photo_codec = internal_photo_codec,
			.source_decode_service = source_decode_service,
			.jpeg_export_service = jpeg_export_service,
			.document_export_service = document_export_service,
			.last_progress_events = last_progress_events,
			.never_cancelled = never_cancelled,
			.localization = localization,
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

AppShellComponent::~AppShellComponent() {
	stopTimer();
	photo_coordinator.reset();
	edit_coordinator.reset();
	if (photo_operation_runner != nullptr)
		photo_operation_runner->stop();
	photo_operation_runner.reset();
	preview_scheduler.reset();
}

void AppShellComponent::paint(juce::Graphics& graphics) {
	graphics.fillAll(background_colour());
}

void AppShellComponent::resized() {
	juce::Rectangle<int> bounds =
		fullscreen_safe_content_bounds(*this).reduced(10);
	if (chrome != nullptr) {
		chrome->setBounds(getLocalBounds());
		bounds = chrome->layout_shell(bounds);
	}
	if (photo_operation_progress != nullptr && photo_operation.active()) {
		photo_operation_progress->setBounds(bounds.removeFromTop(82));
		bounds.removeFromTop(6);
	} else if (photo_operation_progress != nullptr) {
		photo_operation_progress->setBounds(0, 0, 0, 0);
	}
	viewport.setBounds(bounds);
	if (content) {
		content->set_viewport_height_hint(bounds.getHeight());
		content->setSize(viewport.getWidth(), content->getHeight());
	}
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
	if (!photo_operation.active() && route_coordinator != nullptr)
		route_coordinator->select_root(destination_value);
}

void AppShellComponent::open_item_detail(core::StableIdentifier item_id) {
	if (!photo_operation.active() && route_coordinator != nullptr)
		route_coordinator->open_item_detail(std::move(item_id));
}

void AppShellComponent::open_storage_detail(core::StableIdentifier storage_id) {
	if (!photo_operation.active() && route_coordinator != nullptr)
		route_coordinator->open_storage_detail(std::move(storage_id));
}

void AppShellComponent::open_photo_viewer(
	const domain::PhotoOwner& owner,
	const std::optional<core::StableIdentifier>& requested_photo_id) {
	if (!photo_operation.active() && route_coordinator != nullptr)
		route_coordinator->open_photo_viewer(owner, requested_photo_id);
}

void AppShellComponent::request_add_photos(const domain::PhotoOwner& owner) {
	if (photo_coordinator != nullptr)
		photo_coordinator->request_add_photos(owner);
}

void AppShellComponent::request_add_pending_item_photos() {
	if (photo_coordinator != nullptr)
		photo_coordinator->request_add_pending_item_photos();
}

void AppShellComponent::request_add_pending_storage_photos() {
	if (photo_coordinator != nullptr)
		photo_coordinator->request_add_pending_storage_photos();
}

void AppShellComponent::request_export_photo(
	const core::StableIdentifier& photo_id) {
	if (!photo_operation.active() && photo_coordinator != nullptr)
		photo_coordinator->request_export_photo(photo_id);
}

void AppShellComponent::request_delete_photo_confirmation(
	const core::StableIdentifier& photo_id) {
	if (!photo_operation.active() && photo_coordinator != nullptr)
		photo_coordinator->request_delete_photo_confirmation(photo_id);
}

void AppShellComponent::confirm_delete_photo(
	const core::StableIdentifier& photo_id) {
	if (!photo_operation.active() && photo_coordinator != nullptr)
		photo_coordinator->confirm_delete_photo(photo_id);
}

void AppShellComponent::cancel_delete_photo_confirmation() {
	if (!photo_operation.active() && photo_coordinator != nullptr)
		photo_coordinator->cancel_delete_photo_confirmation();
}

void AppShellComponent::apply_photo_edit_result(
	EntityEditResult result,
	const core::StableIdentifier& selected_photo_id_value) {
	if (photo_coordinator != nullptr) {
		photo_coordinator->apply_photo_edit_result(std::move(result),
												   selected_photo_id_value);
	}
}

void AppShellComponent::cleanup_item_pending_photos() {
	if (!photo_operation.active() && photo_coordinator != nullptr)
		photo_coordinator->cleanup_item_pending_photos();
}

void AppShellComponent::cleanup_storage_pending_photos() {
	if (!photo_operation.active() && photo_coordinator != nullptr)
		photo_coordinator->cleanup_storage_pending_photos();
}

void AppShellComponent::remove_item_pending_photo(
	std::size_t pending_photo_index) {
	if (!photo_operation.active() && photo_coordinator != nullptr)
		photo_coordinator->remove_item_pending_photo(pending_photo_index);
}

void AppShellComponent::remove_storage_pending_photo(
	std::size_t pending_photo_index) {
	if (!photo_operation.active() && photo_coordinator != nullptr)
		photo_coordinator->remove_storage_pending_photo(pending_photo_index);
}

void AppShellComponent::reset_catalog_filters() {
	catalog_filter_state.applied	   = catalog::CatalogSearchFilters{};
	catalog_filter_state.draft		   = catalog_filter_state.applied;
	catalog_filter_state.panel_visible = false;
}

void AppShellComponent::toggle_catalog_filters() {
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

void AppShellComponent::apply_catalog_filters() {
	catalog_filter_state.applied	   = catalog_filter_state.draft;
	catalog_filter_state.panel_visible = false;
	refresh_all();
	schedule_catalog_search_focus_release();
}

void AppShellComponent::close_catalog_filters() {
	catalog_filter_state.draft		   = catalog_filter_state.applied;
	catalog_filter_state.panel_visible = false;
	refresh_all();
	schedule_catalog_search_focus_release();
}

void AppShellComponent::release_catalog_search_focus() {
	if (chrome != nullptr)
		chrome->release_catalog_search_focus();
	grabKeyboardFocus();
}

void AppShellComponent::schedule_catalog_search_focus_release() {
	release_catalog_search_focus();
	juce::Component::SafePointer<AppShellComponent> safe_this{this};
	juce::MessageManager::callAsync([safe_this] {
		if (safe_this != nullptr)
			safe_this->release_catalog_search_focus();
	});
}

juce::String AppShellComponent::catalog_draft_result_count_text() const {
	catalog::CatalogSearchOptions options{
		.include_storage_results_for_empty_query = true};
	const catalog::CatalogSearchResultSet results = catalog::search_catalog(
		session.search_index,
		chrome != nullptr ? chrome->catalog_query() : std::string{},
		catalog_filter_state.draft, options);
	return juce_text(localization.draft_result_count(results.total_count));
}

void AppShellComponent::request_internal_preview_async(
	core::StableIdentifier photo_id, ImagePreviewSize target_size,
	ImagePreviewRequestPriority priority) {
	if (preview_scheduler != nullptr)
		preview_scheduler->enqueue_internal_preview(std::move(photo_id),
													target_size, priority);
}

void AppShellComponent::request_staged_preview_async(
	PendingPhotoSource source, ImagePreviewSize target_size,
	ImagePreviewRequestPriority priority) {
	if (preview_scheduler != nullptr)
		preview_scheduler->enqueue_staged_preview(std::move(source),
											  target_size, priority);
}

void AppShellComponent::begin_photo_operation(PhotoOperationJobType job_type,
									  std::uint64_t generation) {
	photo_operation.generation = generation;
	photo_operation.state = PhotoOperationState::Running;
	photo_operation.job_type = job_type;
	photo_operation.operation_id.reset();
	photo_operation.latest_progress.reset();
	update_photo_operation_progress_surface();
	refresh_all();
}

void AppShellComponent::complete_photo_operation() {
	photo_operation.state = PhotoOperationState::Idle;
	photo_operation.job_type.reset();
	photo_operation.operation_id.reset();
	photo_operation.latest_progress.reset();
	update_photo_operation_progress_surface();
	refresh_all();
}

void AppShellComponent::request_photo_operation_cancellation() {
	if (!photo_operation.active() || photo_operation_runner == nullptr
		|| !photo_operation.latest_progress.has_value()
		|| !photo_operation.latest_progress->cancellable) {
		return;
	}
	photo_operation.state = PhotoOperationState::CancellationRequested;
	photo_operation_runner->request_cancellation();
	feedback.photo_message = localization.text(
		localization::MessageId::PhotoOperationCancelling);
	update_photo_operation_progress_surface();
	refresh_all();
}

void AppShellComponent::apply_photo_operation_progress(
	std::uint64_t generation, platform::ProgressEvent event) {
	if (photo_operation.generation != generation || !photo_operation.active())
		return;
	photo_operation.operation_id = event.operation_id;
	photo_operation.latest_progress = std::move(event);
	update_photo_operation_progress_surface();
}

void AppShellComponent::update_photo_operation_progress_surface() {
	if (photo_operation_progress == nullptr)
		return;
	const bool cancellation_available =
		photo_operation.state == PhotoOperationState::Running
		&& photo_operation.latest_progress.has_value()
		&& photo_operation.latest_progress->cancellable;
	const juce::String summary = photo_operation.latest_progress.has_value()
		? juce_text(
			  localization.progress_summary(*photo_operation.latest_progress))
		: juce::String{};
	photo_operation_progress->update_model(PhotoOperationProgressModel{
		.heading = juce_text(localization.text(
			localization::MessageId::PhotoOperationHeading)),
		.summary = summary,
		.cancel_label = juce_text(localization.text(
			localization::MessageId::PhotoOperationCancel)),
		.active = photo_operation.active(),
		.cancellation_available = cancellation_available});
	resized();
}

std::optional<juce::String> AppShellComponent::preview_failure_message(
	const ImagePreviewRequestIdentity& identity) const {
	return preview_scheduler == nullptr
			   ? std::nullopt
			   : preview_scheduler->failure_message(identity);
}

void AppShellComponent::request_photo_display_async(
	core::StableIdentifier photo_id) {
	if (preview_scheduler != nullptr)
		preview_scheduler->enqueue_display(std::move(photo_id));
}

void AppShellComponent::invalidate_preview_failure(
	const ImagePreviewRequestIdentity& identity) {
	if (preview_scheduler != nullptr)
		preview_scheduler->clear_failure(identity);
}

void AppShellComponent::invalidate_all_previews() {
	if (preview_scheduler != nullptr)
		preview_scheduler->invalidate_all();
	else
		preview_cache.clear();
}

void AppShellComponent::invalidate_internal_photo_preview(
	const core::StableIdentifier& photo_id) {
	if (preview_scheduler != nullptr)
		preview_scheduler->invalidate_internal_photo(photo_id);
	else
		preview_cache.remove_internal_photo(photo_id);
}

void AppShellComponent::invalidate_staged_photo_preview(
	const std::filesystem::path& staged_path) {
	if (preview_scheduler != nullptr)
		preview_scheduler->invalidate_staged_photo(staged_path);
	else
		preview_cache.remove_staged_photo(staged_path);
}

void AppShellComponent::schedule_content_refresh() {
	startTimer(85);
}

void AppShellComponent::timerCallback() {
	stopTimer();
	refresh_controls();
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

void AppShellComponent::clear_controlled_startup_attempt_marker() {
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

void AppShellComponent::refresh_controls() {
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
		chrome->update_model(AppShellChromeComponent::Model{
			.destination				= route.destination,
			.item_form_mode				= item_form.mode,
			.storage_form_mode			= storage_form.mode,
			.title						= title,
			.status						= juce_text(status),
			.catalog_draft_result_count = draft_result_count,
			.session_fatal				= session.fatal(),
			.photo_operation_active	= photo_operation.active(),
			.catalog_filters_active =
				has_catalog_filters(catalog_filter_state.applied),
			.catalog_filter_panel_visible =
				catalog_filter_state.panel_visible});
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
