#pragma once

#include "Platform/PlatformServices.hpp"

#include <memory>

namespace shuba::platform {
class JuceAndroidPathProvider final : public AppPrivatePathProvider {
public:
	[[nodiscard]] PlatformValueResult<AppPrivatePaths>
	resolve_app_private_paths() const override;
};

class JuceAndroidPhotoSelectionService final : public PhotoSelectionService {
public:
	JuceAndroidPhotoSelectionService();
	JuceAndroidPhotoSelectionService(const JuceAndroidPhotoSelectionService&) =
		delete;
	JuceAndroidPhotoSelectionService& operator=(
		const JuceAndroidPhotoSelectionService&) = delete;
	JuceAndroidPhotoSelectionService(
		JuceAndroidPhotoSelectionService&&) noexcept;
	JuceAndroidPhotoSelectionService& operator=(
		JuceAndroidPhotoSelectionService&&) noexcept;
	~JuceAndroidPhotoSelectionService() override;

	[[nodiscard]] core::OperationResult request_photo_selection(
		const PhotoSelectionRequest& request,
		PhotoSelectionCompletion completion) override;

private:
	struct ActiveChooser;
	std::unique_ptr<ActiveChooser> active_chooser;
};

class JuceAndroidDocumentImportService final : public DocumentImportService {
public:
	JuceAndroidDocumentImportService();
	JuceAndroidDocumentImportService(const JuceAndroidDocumentImportService&) =
		delete;
	JuceAndroidDocumentImportService& operator=(
		const JuceAndroidDocumentImportService&) = delete;
	JuceAndroidDocumentImportService(
		JuceAndroidDocumentImportService&&) noexcept;
	JuceAndroidDocumentImportService& operator=(
		JuceAndroidDocumentImportService&&) noexcept;
	~JuceAndroidDocumentImportService() override;

	[[nodiscard]] core::OperationResult request_import_document_selection(
		const DocumentImportRequest& request,
		DocumentImportCompletion completion) override;

private:
	struct ActiveChooser;
	std::unique_ptr<ActiveChooser> active_chooser;
};

class JuceAndroidDocumentExportService final : public DocumentExportService {
public:
	JuceAndroidDocumentExportService();
	JuceAndroidDocumentExportService(const JuceAndroidDocumentExportService&) =
		delete;
	JuceAndroidDocumentExportService& operator=(
		const JuceAndroidDocumentExportService&) = delete;
	JuceAndroidDocumentExportService(
		JuceAndroidDocumentExportService&&) noexcept;
	JuceAndroidDocumentExportService& operator=(
		JuceAndroidDocumentExportService&&) noexcept;
	~JuceAndroidDocumentExportService() override;

	[[nodiscard]] core::OperationResult request_export_destination_selection(
		const DocumentExportRequest& request,
		DocumentExportDestinationCompletion completion) override;
	[[nodiscard]] core::OperationResult copy_file_to_destination(
		const DocumentCopyRequest& request,
		const PlatformOperationContext& context, ProgressSink& progress_sink,
		CancellationToken& cancellation_token) override;

private:
	struct ActiveChooser;
	std::unique_ptr<ActiveChooser> active_chooser;
};

class JuceAndroidContentStagingService final : public ContentStagingService {
public:
	[[nodiscard]] PlatformValueResult<StagedContent> stage_content(
		const ContentStagingRequest& request,
		const PlatformOperationContext& context, ProgressSink& progress_sink,
		CancellationToken& cancellation_token) override;
};

class JuceAndroidSourceImageDecodeService final
	: public SourceImageDecodeService {
public:
	[[nodiscard]] PlatformValueResult<ImagePixels> decode_source_image(
		const SourceImageDecodeRequest& request,
		const PlatformOperationContext& context, ProgressSink& progress_sink,
		CancellationToken& cancellation_token) override;
};

class JuceJpegExportService final : public JpegExportService {
public:
	[[nodiscard]] PlatformValueResult<MediaWriteResult> write_jpeg(
		const JpegExportRequest& request,
		const PlatformOperationContext& context, ProgressSink& progress_sink,
		CancellationToken& cancellation_token) override;
};
}	 // namespace shuba::platform
