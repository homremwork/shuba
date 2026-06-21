#pragma once

#include "Catalog/PhotoExport.hpp"
#include "Core/Clock.hpp"
#include "Core/Identifier.hpp"
#include "Core/OperationGate.hpp"
#include "Platform/PlatformServices.hpp"
#include "UI/AppShellState.hpp"
#include "UI/Session/CatalogSessionState.hpp"

#include <cstddef>
#include <functional>

namespace shuba::ui {
class AppShellPhotoCoordinator final {
public:
	struct Dependencies final {
		CatalogSessionState& session;
		AppShellRouteState& route;
		AppShellItemFormState& item_form;
		AppShellFeedbackState& feedback;
		AppShellPhotoDisplayState& photo_display;
		core::IdentifierSource& identifiers;
		core::Clock& clock;
		core::OperationGate& operation_gate;
		platform::PhotoSelectionService& photo_selection_service;
		platform::DocumentExportService& document_export_service;
		platform::ContentStagingService& content_staging_service;
		platform::SourceImageDecodeService& source_decode_service;
		platform::JpegExportService& jpeg_export_service;
		platform::InternalPhotoCodec& internal_photo_codec;
		platform::ProgressCollector& progress_events;
		platform::CancellationToken& cancellation_token;
		std::function<void()> refresh_all;
	};

	explicit AppShellPhotoCoordinator(Dependencies dependencies);

	void request_add_photos(domain::PhotoOwner owner);
	void request_add_pending_item_photos();
	void request_export_photo(core::StableIdentifier photo_id);
	void apply_photo_edit_result(EntityEditResult result,
								 core::StableIdentifier selected_photo_id);
	void cleanup_item_pending_photos();
	void remove_item_pending_photo(std::size_t pending_photo_index);

private:
	void apply_pending_photo_staging_result(PendingPhotoStagingResult result);
	void apply_photo_import_result(PhotoImportSessionResult result);
	void refresh_all();

	CatalogSessionState& session;
	AppShellRouteState& route;
	AppShellItemFormState& item_form;
	AppShellFeedbackState& feedback;
	AppShellPhotoDisplayState& photo_display;
	core::IdentifierSource& identifiers;
	core::Clock& clock;
	core::OperationGate& operation_gate;
	platform::PhotoSelectionService& photo_selection_service;
	platform::DocumentExportService& document_export_service;
	platform::ContentStagingService& content_staging_service;
	platform::SourceImageDecodeService& source_decode_service;
	platform::JpegExportService& jpeg_export_service;
	platform::InternalPhotoCodec& internal_photo_codec;
	platform::ProgressCollector& progress_events;
	platform::CancellationToken& cancellation_token;
	std::function<void()> refresh_all_handler;
};
}	 // namespace shuba::ui
