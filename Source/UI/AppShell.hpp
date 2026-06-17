#pragma once

#include "Catalog/PhotoExport.hpp"
#include "Platform/JuceAndroidServices.hpp"
#include "Platform/JuceZipArchive.hpp"
#include "UI/CatalogSession.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace shuba::ui {
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
	};

	AppShellComponent(CatalogSessionState session_state,
					  PlatformServices platform_services);
	~AppShellComponent() override;

	void paint(juce::Graphics& graphics) override;
	void resized() override;

private:
	struct ContentComponent;

	enum class RootDestination : std::uint8_t {
		Catalog,
		Storages,
		Add,
		More,
		ItemDetail,
		StorageDetail,
		ItemForm,
		StorageForm,
		PhotoViewer,
		BackupRecovery,
	};

	enum class FormMode : std::uint8_t {
		Create,
		Edit,
	};

	void refresh_all();
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
	void request_export_photo(core::StableIdentifier photo_id);
	void request_export_backup();
	void request_export_diagnostic_archive();
	void request_import_backup();
	void apply_backup_export_result(BackupExportSessionResult result,
									bool diagnostic_archive);
	void apply_backup_import_staging_result(
		BackupImportStagingSessionResult result);
	void confirm_staged_backup_import();
	void apply_backup_import_replacement_result(
		BackupImportReplacementSessionResult result);
	void apply_photo_import_result(PhotoImportSessionResult result);
	void apply_photo_edit_result(EntityEditResult result,
								 core::StableIdentifier selected_photo_id);
	void reset_catalog_filters();
	void reset_item_form();
	void reset_storage_form();
	void load_item_form_from_record(const persistence::ItemEnvelope& item);
	void load_storage_form_from_record(
		const persistence::StorageEnvelope& storage);
	void apply_entity_edit_result(EntityEditResult result);
	void schedule_content_refresh();
	void timerCallback() override;

	CatalogSessionState session;
	RootDestination destination{RootDestination::Catalog};
	std::optional<core::StableIdentifier> selected_item_id;
	std::optional<core::StableIdentifier> selected_storage_id;
	std::optional<domain::PhotoOwner> selected_photo_owner;
	std::optional<core::StableIdentifier> selected_photo_id;
	std::optional<RootDestination> form_return_destination;
	catalog::CatalogSearchFilters catalog_filters;
	catalog::CatalogSearchFilters catalog_filter_draft;
	ItemDraft item_form_draft;
	StorageDraft storage_form_draft;
	FormMode item_form_mode{FormMode::Create};
	FormMode storage_form_mode{FormMode::Create};
	std::string last_edit_message;
	std::vector<EntityEditDiagnostic> last_edit_diagnostics;
	ShellIdentifierSource edit_identifiers;
	ShellClock edit_clock;
	core::OperationGate ui_operation_gate;
	platform::JuceAndroidPhotoSelectionService photo_selection_service;
	platform::JuceAndroidDocumentExportService document_export_service;
	platform::JuceAndroidContentStagingService content_staging_service;
	platform::JuceAndroidSourceImageDecodeService source_decode_service;
	platform::JuceJpegExportService jpeg_export_service;
	platform::JuceZipArchiveService zip_archive_service;
	platform::JuceAndroidDocumentImportService document_import_service;
	platform::InternalPhotoCodec& internal_photo_codec;
	platform::ProgressCollector last_progress_events;
	platform::NeverCancelledToken never_cancelled;
	std::string last_photo_message;
	std::vector<core::Diagnostic> last_photo_diagnostics;
	std::string last_backup_message;
	std::vector<core::Diagnostic> last_backup_diagnostics;
	std::optional<catalog::BackupImportStagingResult> pending_import_staging;
	bool pending_import_degraded_acknowledged{};
	catalog::PhotoDisplayResult last_photo_display_result;
	std::optional<core::StableIdentifier> last_display_photo_id;
	bool catalog_filter_panel_visible{};
	bool storage_detail_include_nested{true};
	bool item_listing_expanded{};
	bool item_finance_expanded{};
	bool storage_archive_warning_acknowledged{};

	juce::Label title_label;
	juce::Label status_label;
	juce::TextEditor catalog_search_editor;
	juce::TextEditor storage_search_editor;
	juce::TextButton catalog_clear_button{"Clear"};
	juce::TextButton catalog_filter_button{"Filters"};
	juce::TextButton catalog_clear_filters_button{"Clear filters"};
	juce::TextButton storage_clear_button{"Clear"};
	juce::TextButton back_button{"Back"};
	juce::Viewport viewport;
	juce::TextEditor item_name_editor;
	juce::TextEditor item_category_editor;
	juce::TextEditor item_tags_editor;
	juce::TextEditor item_notes_editor;
	juce::TextEditor item_listing_marketplace_editor;
	juce::TextEditor item_listing_url_editor;
	juce::TextEditor item_listing_note_editor;
	juce::TextEditor item_acquisition_source_editor;
	juce::TextEditor storage_name_editor;
	juce::TextEditor storage_type_editor;
	juce::TextEditor storage_location_editor;
	juce::TextEditor storage_tags_editor;
	juce::TextEditor storage_notes_editor;
	std::unique_ptr<ContentComponent> content;
	juce::TextButton catalog_nav_button{"Catalog"};
	juce::TextButton storages_nav_button{"Storages"};
	juce::TextButton add_nav_button{"Add"};
	juce::TextButton more_nav_button{"More"};
};
}	 // namespace shuba::ui
