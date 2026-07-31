#pragma once

#include "Catalog/PhotoExport.hpp"
#include "Core/Clock.hpp"
#include "Core/Identifier.hpp"
#include "Core/OperationGate.hpp"
#include "Platform/PlatformServices.hpp"
#include "UI/AppShellState.hpp"
#include "UI/Session/CatalogSessionState.hpp"
#include "UI/Session/EntityEditTypes.hpp"
#include "UI/Session/ImagePreviewSession.hpp"
#include "UI/View/Primitives/Forms.hpp"
#include "UI/View/Primitives/PhotoManagement.hpp"
#include "UI/View/Primitives/Previews.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace shuba::localization {
class Localization;
}

namespace shuba::ui {
class AppShellContentComponent;

class AppShellScreenRenderer final {
public:
	struct Editors final {
		juce::TextEditor& item_name_editor;
		juce::TextEditor& item_category_editor;
		juce::TextEditor& item_notes_editor;
		juce::TextEditor& item_listing_marketplace_editor;
		juce::TextEditor& item_listing_url_editor;
		juce::TextEditor& item_listing_note_editor;
		juce::TextEditor& item_acquisition_source_editor;
		juce::TextEditor& storage_name_editor;
		juce::TextEditor& storage_type_editor;
		juce::TextEditor& storage_location_editor;
		juce::TextEditor& storage_notes_editor;
	};

	struct Queries final {
		std::function<std::string()> catalog_query;
		std::function<std::string()> storage_query;
	};

	struct Actions final {
		std::function<void(RootDestination)> select_root;
		std::function<void(core::StableIdentifier)> open_item_detail;
		std::function<void(core::StableIdentifier)> open_storage_detail;
		std::function<void(const domain::PhotoOwner&,
						   const std::optional<core::StableIdentifier>&)>
			open_photo_viewer;
		std::function<void(std::optional<core::StableIdentifier>)>
			open_new_item_form;
		std::function<void(core::StableIdentifier)> open_existing_item_form;
		std::function<void(std::optional<core::StableIdentifier>)>
			open_new_storage_form;
		std::function<void(core::StableIdentifier)> open_existing_storage_form;
		std::function<void(const domain::PhotoOwner&)> request_add_photos;
		std::function<void()> request_add_pending_item_photos;
		std::function<void()> request_add_pending_storage_photos;
		std::function<void(const core::StableIdentifier&)> request_export_photo;
		std::function<void()> request_export_backup;
		std::function<void()> request_export_diagnostic_archive;
		std::function<void()> request_import_backup;
		std::function<void()> retry_normal_startup;
		std::function<void()> confirm_staged_backup_import;
		std::function<void()> cleanup_item_pending_photos;
		std::function<void()> cleanup_storage_pending_photos;
		std::function<void(std::size_t)> remove_item_pending_photo;
		std::function<void(std::size_t)> remove_storage_pending_photo;
		std::function<void(std::size_t)> set_item_pending_photo_as_main;
		std::function<void(std::size_t)> set_storage_pending_photo_as_main;
		std::function<void(const core::StableIdentifier&)> request_delete_photo;
		std::function<void(const core::StableIdentifier&)> confirm_delete_photo;
		std::function<void()> cancel_delete_photo;
		std::function<void()> apply_catalog_filters;
		std::function<void()> reset_catalog_filters;
		std::function<void(EntityEditResult)> apply_entity_edit_result;
		std::function<void(EntityEditResult, const core::StableIdentifier&)>
			apply_photo_edit_result;
		std::function<void(core::StableIdentifier, ImagePreviewSize,
						   ImagePreviewRequestPriority)>
			request_internal_preview;
		std::function<void(PendingPhotoSource, ImagePreviewSize,
						   ImagePreviewRequestPriority)>
			request_staged_preview;
		std::function<std::optional<juce::String>(
			const ImagePreviewRequestIdentity&)>
			preview_failure_message;
		std::function<void(core::StableIdentifier)> request_photo_display;
		std::function<void()> refresh_all;
		std::function<void()> refresh_content;
	};

	struct Dependencies final {
		CatalogSessionState& session;
		AppShellRouteState& route;
		AppShellCatalogFilterState& catalog_filter_state;
		AppShellItemFormState& item_form;
		AppShellStorageFormState& storage_form;
		AppShellFeedbackState& feedback;
		AppShellBackupState& backup;
		AppShellPhotoDisplayState& photo_display;
		AppShellStorageDetailState& storage_detail;
		ImagePreviewCache& preview_cache;
		core::IdentifierSource& edit_identifiers;
		core::Clock& edit_clock;
		core::OperationGate& ui_operation_gate;
		platform::InternalPhotoCodec& internal_photo_codec;
		platform::SourceImageDecodeService& source_decode_service;
		platform::JpegExportService& jpeg_export_service;
		platform::DocumentExportService& document_export_service;
		platform::ProgressCollector& last_progress_events;
		platform::NeverCancelledToken& never_cancelled;
		localization::Localization& localization;
		AppShellContentComponent& content;
		Editors editors;
		Queries queries;
		Actions actions;
	};

	explicit AppShellScreenRenderer(Dependencies dependencies);

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

private:
	struct PreviewCardBuildResult final {
		PreviewCardContent content;
	};
	struct ImagePreviewRenderState final {
		juce::Image image;
		juce::String placeholder;
		PreviewImageVisualState state{PreviewImageVisualState::Loading};
	};

	[[nodiscard]] std::string catalog_query() const;
	[[nodiscard]] std::string storage_query() const;
	[[nodiscard]] PreviewCardBuildResult build_item_result_preview_card(
		const catalog::SearchResult& result,
		ImagePreviewRequestPriority preview_priority);
	[[nodiscard]] PreviewCardBuildResult build_storage_result_preview_card(
		const catalog::SearchResult& result,
		ImagePreviewRequestPriority preview_priority);
	[[nodiscard]] CompactStorageCardContent build_storage_result_compact_card(
		const catalog::SearchResult& result,
		ImagePreviewRequestPriority preview_priority);
	[[nodiscard]] PreviewCardBuildResult build_item_preview_card(
		const persistence::ItemEnvelope& item,
		const catalog::ItemProjection& projection,
		ImagePreviewRequestPriority preview_priority);
	[[nodiscard]] PreviewCardBuildResult build_storage_preview_card(
		const persistence::StorageEnvelope& storage,
		const catalog::StorageProjection& projection,
		ImagePreviewRequestPriority preview_priority);
	[[nodiscard]] CompactStorageCardContent build_storage_compact_card(
		const persistence::StorageEnvelope& storage,
		const catalog::StorageProjection& projection,
		ImagePreviewRequestPriority preview_priority);
	void apply_representative_preview(
		const catalog::SearchResult& result,
		ImagePreviewRequestPriority preview_priority,
		PreviewCardBuildResult& card);
	void apply_representative_preview(
		const catalog::SearchResult& result,
		ImagePreviewRequestPriority preview_priority,
		CompactStorageCardContent& card);
	void apply_representative_preview(
		catalog::PhotoPresenceState photo_presence,
		const std::optional<core::StableIdentifier>& representative_photo_id,
		const std::optional<core::StableIdentifier>&
			representative_usable_photo_id,
		ImagePreviewRequestPriority preview_priority,
		PreviewCardBuildResult& card);
	void apply_representative_preview(
		catalog::PhotoPresenceState photo_presence,
		const std::optional<core::StableIdentifier>& representative_photo_id,
		const std::optional<core::StableIdentifier>&
			representative_usable_photo_id,
		ImagePreviewRequestPriority preview_priority,
		CompactStorageCardContent& card);
	[[nodiscard]] ImagePreviewRenderState load_internal_preview_image(
		const core::StableIdentifier& photo_id, ImagePreviewSize target_size,
		ImagePreviewRequestPriority priority);
	[[nodiscard]] ImagePreviewRenderState load_staged_preview_image(
		const PendingPhotoSource& source, ImagePreviewSize target_size,
		ImagePreviewRequestPriority priority);
	[[nodiscard]] std::vector<StagedPhotoCardEntry>
	build_staged_photo_card_entries(
		std::span<const PendingPhotoSource> sources,
		ImagePreviewSize target_size,
		std::optional<std::size_t> immediate_preview_index,
		bool load_default_previews);
	[[nodiscard]] std::vector<CurrentPhotoCardEntry>
	build_current_photo_card_entries(
		const domain::PhotoOwner& owner, ImagePreviewSize target_size,
		std::optional<std::size_t> immediate_preview_index,
		bool load_default_previews);
	void add_photo_management_deck(
		std::optional<domain::PhotoOwner> owner,
		std::span<const PendingPhotoSource> pending_sources,
		AppShellManagedPhotoDeckState& deck_state,
		std::function<void()> add_staged_handler,
		std::function<void()> clear_staged_handler,
		std::function<void(std::size_t)> remove_staged_handler,
		std::function<void(std::size_t)> set_main_staged_handler);
	[[nodiscard]] std::size_t current_photo_count_for_owner(
		const std::optional<domain::PhotoOwner>& owner) const;
	void add_owner_photo_carousel(const domain::PhotoOwner& owner,
								  catalog::PhotoPresenceState photo_presence,
								  juce::String empty_title,
								  juce::String empty_caption);
	[[nodiscard]] std::optional<core::StableIdentifier>
	selected_photo_id_for_owner(const domain::PhotoOwner& owner) const;
	[[nodiscard]] std::optional<core::StableIdentifier>
	selected_usable_photo_id_for_owner(const domain::PhotoOwner& owner) const;
	void select_root(RootDestination destination);
	void open_item_detail(core::StableIdentifier item_id);
	void open_storage_detail(core::StableIdentifier storage_id);
	void open_photo_viewer(
		const domain::PhotoOwner& owner,
		const std::optional<core::StableIdentifier>& requested_photo_id);
	void open_new_item_form(std::optional<core::StableIdentifier> storage_id);
	void open_existing_item_form(core::StableIdentifier item_id);
	void open_new_storage_form(std::optional<core::StableIdentifier> parent_id);
	void open_existing_storage_form(core::StableIdentifier storage_id);
	void request_add_photos(const domain::PhotoOwner& owner);
	void request_add_pending_item_photos();
	void request_add_pending_storage_photos();
	void request_export_photo(const core::StableIdentifier& photo_id);
	void request_export_backup();
	void request_export_diagnostic_archive();
	void request_import_backup();
	void retry_normal_startup();
	void confirm_staged_backup_import();
	void cleanup_item_pending_photos();
	void cleanup_storage_pending_photos();
	void remove_item_pending_photo(std::size_t pending_photo_index);
	void remove_storage_pending_photo(std::size_t pending_photo_index);
	void set_item_pending_photo_as_main(std::size_t pending_photo_index);
	void set_storage_pending_photo_as_main(std::size_t pending_photo_index);
	void request_delete_photo(const core::StableIdentifier& photo_id);
	void confirm_delete_photo(const core::StableIdentifier& photo_id);
	void cancel_delete_photo();
	void apply_catalog_filters();
	void reset_catalog_filters();
	void apply_entity_edit_result(EntityEditResult result);
	void apply_photo_edit_result(
		EntityEditResult result,
		const core::StableIdentifier& selected_photo_id);
	void refresh_all();
	void refresh_content();

	CatalogSessionState& session;
	AppShellRouteState& route;
	AppShellCatalogFilterState& catalog_filter_state;
	AppShellItemFormState& item_form;
	AppShellStorageFormState& storage_form;
	AppShellFeedbackState& feedback;
	AppShellBackupState& backup;
	AppShellPhotoDisplayState& photo_display;
	AppShellStorageDetailState& storage_detail;
	ImagePreviewCache& preview_cache;
	core::IdentifierSource& edit_identifiers;
	core::Clock& edit_clock;
	core::OperationGate& ui_operation_gate;
	platform::InternalPhotoCodec& internal_photo_codec;
	platform::SourceImageDecodeService& source_decode_service;
	platform::JpegExportService& jpeg_export_service;
	platform::DocumentExportService& document_export_service;
	platform::ProgressCollector& last_progress_events;
	platform::NeverCancelledToken& never_cancelled;
	localization::Localization& localization;
	AppShellContentComponent* content{};
	juce::TextEditor& item_name_editor;
	juce::TextEditor& item_category_editor;
	juce::TextEditor& item_notes_editor;
	juce::TextEditor& item_listing_marketplace_editor;
	juce::TextEditor& item_listing_url_editor;
	juce::TextEditor& item_listing_note_editor;
	juce::TextEditor& item_acquisition_source_editor;
	juce::TextEditor& storage_name_editor;
	juce::TextEditor& storage_type_editor;
	juce::TextEditor& storage_location_editor;
	juce::TextEditor& storage_notes_editor;
	std::function<std::string()> catalog_query_provider;
	std::function<std::string()> storage_query_provider;
	std::function<void(RootDestination)> select_root_handler;
	std::function<void(core::StableIdentifier)> open_item_detail_handler;
	std::function<void(core::StableIdentifier)> open_storage_detail_handler;
	std::function<void(const domain::PhotoOwner&,
					   const std::optional<core::StableIdentifier>&)>
		open_photo_viewer_handler;
	std::function<void(std::optional<core::StableIdentifier>)>
		open_new_item_form_handler;
	std::function<void(core::StableIdentifier)> open_existing_item_form_handler;
	std::function<void(std::optional<core::StableIdentifier>)>
		open_new_storage_form_handler;
	std::function<void(core::StableIdentifier)>
		open_existing_storage_form_handler;
	std::function<void(const domain::PhotoOwner&)> request_add_photos_handler;
	std::function<void()> request_add_pending_item_photos_handler;
	std::function<void()> request_add_pending_storage_photos_handler;
	std::function<void(const core::StableIdentifier&)>
		request_export_photo_handler;
	std::function<void()> request_export_backup_handler;
	std::function<void()> request_export_diagnostic_archive_handler;
	std::function<void()> request_import_backup_handler;
	std::function<void()> retry_normal_startup_handler;
	std::function<void()> confirm_staged_backup_import_handler;
	std::function<void()> cleanup_item_pending_photos_handler;
	std::function<void()> cleanup_storage_pending_photos_handler;
	std::function<void(std::size_t)> remove_item_pending_photo_handler;
	std::function<void(std::size_t)> remove_storage_pending_photo_handler;
	std::function<void(std::size_t)> set_item_pending_photo_as_main_handler;
	std::function<void(std::size_t)> set_storage_pending_photo_as_main_handler;
	std::function<void(const core::StableIdentifier&)>
		request_delete_photo_handler;
	std::function<void(const core::StableIdentifier&)>
		confirm_delete_photo_handler;
	std::function<void()> cancel_delete_photo_handler;
	std::function<void()> apply_catalog_filters_handler;
	std::function<void()> reset_catalog_filters_handler;
	std::function<void(EntityEditResult)> apply_entity_edit_result_handler;
	std::function<void(EntityEditResult, const core::StableIdentifier&)>
		apply_photo_edit_result_handler;
	std::function<void(core::StableIdentifier, ImagePreviewSize,
					   ImagePreviewRequestPriority)>
		request_internal_preview_handler;
	std::function<void(PendingPhotoSource, ImagePreviewSize,
					   ImagePreviewRequestPriority)>
		request_staged_preview_handler;
	std::function<std::optional<juce::String>(
		const ImagePreviewRequestIdentity&)>
		preview_failure_message_handler;
	std::function<void(core::StableIdentifier)> request_photo_display_handler;
	std::function<void()> refresh_all_handler;
	std::function<void()> refresh_content_handler;
};
}	 // namespace shuba::ui
