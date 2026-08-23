#pragma once

#include "Platform/PlatformServices.hpp"
#include "UI/AppShell/State.hpp"
#include "UI/Session/CatalogSessionState.hpp"
#include "UI/Session/ImagePreviewSession.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

namespace shuba::localization {
class Localization;
}

namespace shuba::ui {
class PreviewScheduler final {
public:
	struct Dependencies final {
		CatalogSessionState& session;
		PhotoDisplayState& photo_display;
		ImagePreviewCache& preview_cache;
		platform::InternalPhotoCodec& internal_photo_codec;
		platform::SourceImageDecodeService& source_decode_service;
		platform::JpegExportService& jpeg_export_service;
		platform::DocumentExportService& document_export_service;
		localization::Localization& localization;
		std::function<void()> refresh_content;
	};

	explicit PreviewScheduler(Dependencies dependencies);
	~PreviewScheduler();

	PreviewScheduler(const PreviewScheduler&) = delete;
	PreviewScheduler& operator=(const PreviewScheduler&) =
		delete;
	PreviewScheduler(PreviewScheduler&&) noexcept = delete;
	PreviewScheduler& operator=(PreviewScheduler&&) noexcept =
		delete;

	[[nodiscard]] std::optional<juce::String> failure_message(
		const ImagePreviewRequestIdentity& identity) const;
	void clear_failure(const ImagePreviewRequestIdentity& identity);
	void enqueue_internal_preview(core::StableIdentifier photo_id,
								  ImagePreviewSize target_size,
								  ImagePreviewRequestPriority priority);
	void enqueue_staged_preview(PendingPhotoSource source,
								ImagePreviewSize target_size,
								ImagePreviewRequestPriority priority);
	void enqueue_display(core::StableIdentifier photo_id);
	void cancel_display_requests();
	void invalidate_all();
	void release_disposable_preview_memory();
	void invalidate_internal_photo(const core::StableIdentifier& photo_id);
	void invalidate_staged_photo(const std::filesystem::path& staged_path);

private:
	class Impl;
	std::unique_ptr<Impl> impl;
};
}	 // namespace shuba::ui
