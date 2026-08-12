#pragma once

#include "Persistence/MetadataSchema.hpp"
#include "Platform/PlatformServices.hpp"
#include "UI/AppShellPhotoOperationRunner.hpp"
#include "UI/AppShellState.hpp"
#include "UI/Session/CatalogSessionState.hpp"
#include "UI/Session/EntityEditTypes.hpp"
#include "UI/Session/PhotoSessionTypes.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

namespace shuba::localization {
class Localization;
}

namespace shuba::ui {
class AppShellEditCoordinator final {
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

	struct Dependencies final {
		CatalogSessionState& session;
		AppShellRouteState& route;
		AppShellItemFormState& item_form;
		AppShellStorageFormState& storage_form;
		AppShellFeedbackState& feedback;
		core::IdentifierSource& identifiers;
		core::Clock& clock;
		core::OperationGate& operation_gate;
		platform::ContentStagingService& content_staging_service;
		platform::SourceByteFingerprintService& source_fingerprint_service;
		platform::SourceImageDecodeService& source_decode_service;
		platform::InternalPhotoCodec& internal_photo_codec;
		AppShellPhotoOperationRunner& photo_operation_runner;
		AppShellPhotoOperationState& photo_operation_state;
		localization::Localization& localization;
		Editors editors;
		std::function<void()> cleanup_item_pending_photos;
		std::function<void()> cleanup_storage_pending_photos;
		std::function<void()> invalidate_all_previews;
		std::function<void()> refresh_all;
		std::function<void()> refresh_content;
		std::function<void(PhotoOperationJobType, std::uint64_t)>
			begin_photo_operation;
		std::function<void()> complete_photo_operation;
	};

	explicit AppShellEditCoordinator(Dependencies dependencies);

	void open_new_item_form(std::optional<core::StableIdentifier> storage_id);
	void open_existing_item_form(core::StableIdentifier item_id);
	void open_new_storage_form(std::optional<core::StableIdentifier> parent_id);
	void open_existing_storage_form(core::StableIdentifier storage_id);
	void set_item_pending_photo_as_main(std::size_t pending_photo_index);
	void set_storage_pending_photo_as_main(std::size_t pending_photo_index);
	void save_item_form();
	void save_storage_form();
	void apply_entity_edit_result(EntityEditResult result);

private:
	void set_pending_photo_as_main(
		AppShellManagedPhotoDeckState& photo_deck,
		std::vector<PendingPhotoSource>& pending_photos,
		std::size_t pending_photo_index);
	void clear_edit_feedback();
	void prepare_item_save_feedback_for_submission();
	void reset_item_form();
	void reset_storage_form();
	void load_item_form_from_record(const persistence::ItemEnvelope& item);
	void load_storage_form_from_record(
		const persistence::StorageEnvelope& storage);
	void apply_item_save_with_pending_photos_result(
		ItemSaveWithPendingPhotosResult result);
	void apply_storage_save_with_pending_photos_result(
		StorageSaveWithPendingPhotosResult result);

	CatalogSessionState& session;
	AppShellRouteState& route;
	AppShellItemFormState& item_form;
	AppShellStorageFormState& storage_form;
	AppShellFeedbackState& feedback;
	core::IdentifierSource& identifiers;
	core::Clock& clock;
	core::OperationGate& operation_gate;
	platform::ContentStagingService& content_staging_service;
	platform::SourceByteFingerprintService& source_fingerprint_service;
	platform::SourceImageDecodeService& source_decode_service;
	platform::InternalPhotoCodec& internal_photo_codec;
	AppShellPhotoOperationRunner& photo_operation_runner;
	AppShellPhotoOperationState& photo_operation_state;
	localization::Localization& localization;
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
	std::function<void()> cleanup_item_pending_photos_handler;
	std::function<void()> cleanup_storage_pending_photos_handler;
	std::function<void()> invalidate_all_previews_handler;
	std::function<void()> refresh_all_handler;
	std::function<void()> refresh_content_handler;
	std::function<void(PhotoOperationJobType, std::uint64_t)>
		begin_photo_operation_handler;
	std::function<void()> complete_photo_operation_handler;
};
}	 // namespace shuba::ui
