#pragma once

#include "UI/Session/PhotoSessionTypes.hpp"

namespace shuba::ui {
[[nodiscard]] PendingPhotoStagingResult stage_pending_photos_for_session(
	const PendingPhotoStagingRequest& request,
	platform::ProgressSink& progress_sink,
	platform::CancellationToken& cancellation_token);
[[nodiscard]] PendingPhotoCleanupResult cleanup_pending_photo_sources(
	std::vector<PendingPhotoSource>& pending_sources);
[[nodiscard]] ItemSaveWithPendingPhotosResult
save_item_draft_and_import_pending_photos(
	const ItemSaveWithPendingPhotosRequest& request,
	platform::ProgressSink& progress_sink,
	platform::CancellationToken& cancellation_token);
[[nodiscard]] PhotoImportSessionResult import_photos_into_session(
	const PhotoImportSessionRequest& request,
	platform::ProgressSink& progress_sink,
	platform::CancellationToken& cancellation_token);
[[nodiscard]] EntityEditResult set_main_photo_in_session(
	const EntityEditRequest& request, const core::StableIdentifier& photo_id);
}	 // namespace shuba::ui
