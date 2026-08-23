#pragma once

#include "Catalog/PhotoExport.hpp"
#include "Core/Clock.hpp"
#include "Core/Identifier.hpp"
#include "Core/OperationGate.hpp"
#include "Platform/PlatformServices.hpp"
#include "UI/AppShellOperationRunner.hpp"
#include "UI/AppShellState.hpp"
#include "UI/CallbackLifetime.hpp"
#include "UI/Session/CatalogSessionState.hpp"
#include "UI/Session/ImagePreviewSession.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace shuba::localization {
class Localization;
}

namespace shuba::ui {
class AppShellPhotoCoordinator final {
public:
	struct Dependencies final {
		CatalogSessionState& session;
		AppShellRouteState& route;
		AppShellItemFormState& item_form;
		AppShellStorageFormState& storage_form;
		AppShellFeedbackState& feedback;
		AppShellPhotoDisplayState& photo_display;
		ImagePreviewCache& preview_cache;
		core::IdentifierSource& identifiers;
		core::Clock& clock;
		core::OperationGate& operation_gate;
		platform::PhotoSelectionService& photo_selection_service;
		platform::DocumentExportService& document_export_service;
		platform::ContentStagingService& content_staging_service;
		platform::SourceByteFingerprintService& source_fingerprint_service;
		platform::SourceImageDecodeService& source_decode_service;
		platform::JpegExportService& jpeg_export_service;
		platform::InternalPhotoCodec& internal_photo_codec;
		platform::ProgressCollector& progress_events;
		platform::CancellationToken& cancellation_token;
		AppShellOperationRunner& shell_operation_runner;
		AppShellOperationState& shell_operation_state;
		localization::Localization& localization;
		std::function<void()> invalidate_all_previews;
		std::function<void(const core::StableIdentifier&)>
			invalidate_internal_photo_preview;
		std::function<void(const std::filesystem::path&)>
			invalidate_staged_photo_preview;
		std::function<void()> refresh_all;
		std::function<void(ShellOperationJobType, std::uint64_t)>
			begin_shell_operation;
		std::function<void()> complete_shell_operation;
	};

	explicit AppShellPhotoCoordinator(Dependencies dependencies);
	~AppShellPhotoCoordinator();

	AppShellPhotoCoordinator(const AppShellPhotoCoordinator&) = delete;
	AppShellPhotoCoordinator& operator=(const AppShellPhotoCoordinator&) =
		delete;
	AppShellPhotoCoordinator(AppShellPhotoCoordinator&&) noexcept = delete;
	AppShellPhotoCoordinator& operator=(AppShellPhotoCoordinator&&) noexcept =
		delete;

	void request_add_photos(const domain::PhotoOwner& owner);
	void request_add_pending_item_photos();
	void request_add_pending_storage_photos();
	void request_export_photo(const core::StableIdentifier& photo_id);
	void request_delete_photo_confirmation(
		const core::StableIdentifier& photo_id);
	void confirm_delete_photo(const core::StableIdentifier& photo_id);
	void cancel_delete_photo_confirmation();
	void apply_photo_edit_result(
		EntityEditResult result,
		const core::StableIdentifier& selected_photo_id);
	void cleanup_item_pending_photos();
	void cleanup_storage_pending_photos();
	void remove_item_pending_photo(std::size_t pending_photo_index);
	void remove_storage_pending_photo(std::size_t pending_photo_index);

private:
	enum class PendingPhotoDraftTarget : std::uint8_t {
		Item,
		Storage,
	};

	void request_add_pending_photos(PendingPhotoDraftTarget target);
	void apply_pending_photo_staging_result(PendingPhotoStagingResult result,
											PendingPhotoDraftTarget target);
	void apply_busy_result();
	void begin_shell_operation(ShellOperationJobType job_type,
							   std::uint64_t generation);
	void complete_shell_operation();
	[[nodiscard]] std::vector<PendingPhotoSource> pending_sources_for(
		PendingPhotoDraftTarget target) const;
	[[nodiscard]] std::optional<domain::PhotoOwner> owner_for_pending_target(
		PendingPhotoDraftTarget target) const;
	void cleanup_pending_photos(std::vector<PendingPhotoSource>& pending_photos,
								AppShellManagedPhotoDeckState& photo_deck);
	void remove_pending_photo(std::vector<PendingPhotoSource>& pending_photos,
							  AppShellManagedPhotoDeckState& photo_deck,
							  std::size_t pending_photo_index);
	void apply_photo_import_result(PhotoImportSessionResult result);
	void apply_photo_delete_result(
		EntityEditResult result,
		std::optional<core::StableIdentifier> next_photo_id);
	void refresh_all();

	CatalogSessionState& session;
	AppShellRouteState& route;
	AppShellItemFormState& item_form;
	AppShellStorageFormState& storage_form;
	AppShellFeedbackState& feedback;
	AppShellPhotoDisplayState& photo_display;
	ImagePreviewCache& preview_cache;
	core::IdentifierSource& identifiers;
	core::Clock& clock;
	core::OperationGate& operation_gate;
	platform::PhotoSelectionService& photo_selection_service;
	platform::DocumentExportService& document_export_service;
	platform::ContentStagingService& content_staging_service;
	platform::SourceByteFingerprintService& source_fingerprint_service;
	platform::SourceImageDecodeService& source_decode_service;
	platform::JpegExportService& jpeg_export_service;
	platform::InternalPhotoCodec& internal_photo_codec;
	platform::ProgressCollector& progress_events;
	platform::CancellationToken& cancellation_token;
	AppShellOperationRunner& shell_operation_runner;
	AppShellOperationState& shell_operation_state;
	localization::Localization& localization;
	std::function<void()> invalidate_all_previews_handler;
	std::function<void(const core::StableIdentifier&)>
		invalidate_internal_photo_preview_handler;
	std::function<void(const std::filesystem::path&)>
		invalidate_staged_photo_preview_handler;
	std::function<void()> refresh_all_handler;
	std::function<void(ShellOperationJobType, std::uint64_t)>
		begin_shell_operation_handler;
	std::function<void()> complete_shell_operation_handler;
	std::shared_ptr<CallbackLifetimeToken> lifetime_token{
		std::make_shared<CallbackLifetimeToken>()};
};
}	 // namespace shuba::ui
