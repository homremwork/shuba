#pragma once

#include "Catalog/PhotoExport.hpp"
#include "Platform/AndroidPreviousExit.hpp"
#include "Platform/JuceAndroidServices.hpp"
#include "Platform/JuceHashing.hpp"
#include "Platform/JuceZipArchive.hpp"
#include "UI/AppShellPhotoCoordinator.hpp"
#include "UI/AppShellState.hpp"
#include "UI/Screens/AppShellScreenRenderer.hpp"
#include "UI/Session/BackupRecoveryTypes.hpp"
#include "UI/Session/CatalogSessionState.hpp"
#include "UI/Session/EntityEditTypes.hpp"
#include "UI/Session/ImagePreviewSession.hpp"
#include "UI/Session/PhotoSessionTypes.hpp"
#include "UI/View/AppShellChromeComponent.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace shuba::ui {
class AppShellContentComponent;
class AsyncImagePreviewScheduler;

class ShellIdentifierSource final : public core::IdentifierSource {
public:
	[[nodiscard]] core::StableIdentifier next_stable_identifier() override;
	[[nodiscard]] core::OperationIdentifier next_operation_identifier()
		override;

private:
	core::RandomIdentifierSource random_identifiers;
};

class ShellClock final : public core::Clock {
public:
	[[nodiscard]] core::EpochMilliseconds now() const override;
};

class AppShellComponent final
	: public juce::Component
	, private juce::Timer {
public:
	struct PlatformServices final {
		platform::InternalPhotoCodec& internal_photo_codec;
		platform::AppPrivatePathProvider& path_provider;
		platform::AndroidPreviousExitService& android_previous_exit_service;
		std::string app_version;
		std::string platform_name;
		bool debug_demo_seed_enabled{};
	};

	AppShellComponent(CatalogSessionState session_state,
					  PlatformServices platform_services);
	~AppShellComponent() override;

	void paint(juce::Graphics& graphics) override;
	void resized() override;

private:
	void refresh_all();
	void clear_controlled_startup_attempt_marker();
	void refresh_controls();
	void refresh_content();
	void build_catalog_content();
	void build_filter_panel();
	void build_storages_content();
	void build_item_detail_content();
	void build_storage_detail_content();
	void build_item_form_content();
	void build_storage_form_content();
	void build_photo_viewer_content();
	void build_backup_recovery_content();
	void build_add_content();
	void build_more_content();
	void select_root(RootDestination destination_value);
	void open_item_detail(core::StableIdentifier item_id);
	void open_storage_detail(core::StableIdentifier storage_id);
	void open_photo_viewer(
		domain::PhotoOwner owner,
		std::optional<core::StableIdentifier> requested_photo_id);
	void open_new_item_form(std::optional<core::StableIdentifier> storage_id);
	void open_existing_item_form(core::StableIdentifier item_id);
	void open_new_storage_form(std::optional<core::StableIdentifier> parent_id);
	void open_existing_storage_form(core::StableIdentifier storage_id);
	void request_add_photos(domain::PhotoOwner owner);
	void request_add_pending_item_photos();
	void request_add_pending_storage_photos();
	void set_item_pending_photo_as_main(std::size_t pending_photo_index);
	void set_storage_pending_photo_as_main(std::size_t pending_photo_index);
	void request_export_photo(core::StableIdentifier photo_id);
	void request_delete_photo_confirmation(core::StableIdentifier photo_id);
	void confirm_delete_photo(core::StableIdentifier photo_id);
	void cancel_delete_photo_confirmation();
	void request_export_backup();
	void request_export_diagnostic_archive();
	void request_import_backup();
	void retry_normal_startup();
	void apply_backup_export_result(BackupExportSessionResult result,
									bool diagnostic_archive);
	void apply_backup_import_staging_result(
		BackupImportStagingSessionResult result);
	void confirm_staged_backup_import();
	void apply_backup_import_replacement_result(
		BackupImportReplacementSessionResult result);
	void apply_photo_edit_result(EntityEditResult result,
								 core::StableIdentifier selected_photo_id);
	void cleanup_item_pending_photos();
	void cleanup_storage_pending_photos();
	void remove_item_pending_photo(std::size_t pending_photo_index);
	void remove_storage_pending_photo(std::size_t pending_photo_index);
	void set_pending_photo_as_main(
		AppShellManagedPhotoDeckState& photo_deck,
		std::vector<PendingPhotoSource>& pending_photos,
		std::size_t pending_photo_index);
	void toggle_catalog_filters();
	void apply_catalog_filters();
	void close_catalog_filters();
	void reset_catalog_filters();
	void release_catalog_search_focus();
	void schedule_catalog_search_focus_release();
	[[nodiscard]] juce::String catalog_draft_result_count_text() const;
	void reset_item_form();
	void reset_storage_form();
	void load_item_form_from_record(const persistence::ItemEnvelope& item);
	void load_storage_form_from_record(
		const persistence::StorageEnvelope& storage);
	void save_item_form();
	void save_storage_form();
	void apply_item_save_with_pending_photos_result(
		ItemSaveWithPendingPhotosResult result);
	void apply_storage_save_with_pending_photos_result(
		StorageSaveWithPendingPhotosResult result);
	void apply_entity_edit_result(EntityEditResult result);
	void request_internal_preview_async(core::StableIdentifier photo_id,
										ImagePreviewSize target_size,
										ImagePreviewRequestPriority priority);
	void request_staged_preview_async(PendingPhotoSource source,
									  ImagePreviewSize target_size,
									  ImagePreviewRequestPriority priority);
	[[nodiscard]] std::optional<juce::String> preview_failure_message(
		const ImagePreviewRequestIdentity& identity) const;
	void request_photo_display_async(core::StableIdentifier photo_id);
	void invalidate_preview_failure(
		const ImagePreviewRequestIdentity& identity);
	void invalidate_all_previews();
	void invalidate_internal_photo_preview(
		const core::StableIdentifier& photo_id);
	void invalidate_staged_photo_preview(
		const std::filesystem::path& staged_path);
	void schedule_content_refresh();
	void timerCallback() override;

	CatalogSessionState session;
	AppShellRouteState route;
	AppShellCatalogFilterState catalog_filter_state;
	AppShellItemFormState item_form;
	AppShellStorageFormState storage_form;
	AppShellFeedbackState feedback;
	AppShellBackupState backup;
	AppShellPhotoDisplayState photo_display;
	AppShellStorageDetailState storage_detail;
	ImagePreviewCache preview_cache;
	std::unique_ptr<AsyncImagePreviewScheduler> preview_scheduler;
	ShellIdentifierSource edit_identifiers;
	ShellClock edit_clock;
	platform::AppPrivatePathProvider& path_provider;
	platform::AndroidPreviousExitService& android_previous_exit_service;
	std::string app_version;
	std::string platform_name;
	bool debug_demo_seed_enabled{};
	core::OperationGate ui_operation_gate;
	platform::JuceAndroidPhotoSelectionService photo_selection_service;
	platform::JuceAndroidDocumentExportService document_export_service;
	platform::JuceAndroidContentStagingService content_staging_service;
	platform::JuceMd5SourceByteFingerprintService source_fingerprint_service;
	platform::JuceAndroidSourceImageDecodeService source_decode_service;
	platform::JuceJpegExportService jpeg_export_service;
	platform::JuceZipArchiveService zip_archive_service;
	platform::JuceAndroidDocumentImportService document_import_service;
	platform::InternalPhotoCodec& internal_photo_codec;
	platform::ProgressCollector last_progress_events;
	platform::NeverCancelledToken never_cancelled;
	std::unique_ptr<AppShellPhotoCoordinator> photo_coordinator;
	std::unique_ptr<AppShellScreenRenderer> screen_renderer;
	std::unique_ptr<AppShellChromeComponent> chrome;
	juce::Viewport viewport;
	juce::TextEditor item_name_editor;
	juce::TextEditor item_category_editor;
	juce::TextEditor item_notes_editor;
	juce::TextEditor item_listing_marketplace_editor;
	juce::TextEditor item_listing_url_editor;
	juce::TextEditor item_listing_note_editor;
	juce::TextEditor item_acquisition_source_editor;
	juce::TextEditor storage_name_editor;
	juce::TextEditor storage_type_editor;
	juce::TextEditor storage_location_editor;
	juce::TextEditor storage_notes_editor;
	std::unique_ptr<AppShellContentComponent> content;
};
}	 // namespace shuba::ui
