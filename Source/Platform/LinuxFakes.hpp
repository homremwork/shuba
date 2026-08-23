#pragma once

#include "Core/Clock.hpp"
#include "Core/Identifier.hpp"
#include "Platform/PlatformServices.hpp"

#include <deque>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace shuba::platform {
class FixedCapabilityChecker final : public PlatformCapabilityChecker {
public:
	[[nodiscard]] PlatformCapabilityCheck check_capability(
		PlatformCapability capability) const override;

	void set_capability_state(PlatformCapability capability,
							  PlatformCapabilityState state,
							  std::string message = {});
	void clear_override(PlatformCapability capability);

private:
	std::map<PlatformCapability, PlatformCapabilityCheck> overrides;
};

class LinuxFakePathProvider final : public AppPrivatePathProvider {
public:
	explicit LinuxFakePathProvider(std::filesystem::path root_path);

	[[nodiscard]] PlatformValueResult<AppPrivatePaths>
	resolve_app_private_paths() const override;

	[[nodiscard]] const std::filesystem::path& root_path() const noexcept;

private:
	std::filesystem::path root;
};

class ScriptedIdentifierSource final : public core::IdentifierSource {
public:
	explicit ScriptedIdentifierSource(std::uint64_t fallback_seed = 1U);

	void script_stable_identifier(std::string value);
	void script_operation_identifier(std::string value);

	[[nodiscard]] core::StableIdentifier next_stable_identifier() override;
	[[nodiscard]] core::OperationIdentifier next_operation_identifier()
		override;

private:
	std::deque<core::StableIdentifier> stable_identifiers;
	std::deque<core::OperationIdentifier> operation_identifiers;
	core::RandomIdentifierSource fallback;
};

class LinuxFakePhotoSelectionService final : public PhotoSelectionService {
public:
	void script_selection_result(
		PlatformValueResult<std::vector<ContentSourceDescriptor>> result);
	void script_selection_success(std::vector<ContentSourceDescriptor> sources);
	void script_selection_cancellation();

	[[nodiscard]] core::OperationResult request_photo_selection(
		const PhotoSelectionRequest& request,
		PhotoSelectionCompletion completion) override;

private:
	std::deque<PlatformValueResult<std::vector<ContentSourceDescriptor>>>
		results;
};

class LinuxFakeDocumentImportService final : public DocumentImportService {
public:
	void script_import_result(
		PlatformValueResult<ContentSourceDescriptor> result);
	void script_import_success(ContentSourceDescriptor source);
	void script_import_cancellation();

	[[nodiscard]] core::OperationResult request_import_document_selection(
		const DocumentImportRequest& request,
		DocumentImportCompletion completion) override;

private:
	std::deque<PlatformValueResult<ContentSourceDescriptor>> results;
};

class LinuxFakeDocumentExportService final : public DocumentExportService {
public:
	void script_export_destination_result(
		PlatformValueResult<DocumentDestinationDescriptor> result);
	void script_export_destination_success(
		DocumentDestinationDescriptor destination);
	void script_export_destination_cancellation();

	[[nodiscard]] core::OperationResult request_export_destination_selection(
		const DocumentExportRequest& request,
		DocumentExportDestinationCompletion completion) override;
	[[nodiscard]] core::OperationResult copy_file_to_destination(
		const DocumentCopyRequest& request,
		const PlatformOperationContext& context, ProgressSink& progress_sink,
		CancellationToken& cancellation_token) override;

private:
	std::deque<PlatformValueResult<DocumentDestinationDescriptor>> results;
};

class LinuxFakeContentStagingService final : public ContentStagingService {
public:
	[[nodiscard]] PlatformValueResult<StagedContent> stage_content(
		const ContentStagingRequest& request,
		const PlatformOperationContext& context, ProgressSink& progress_sink,
		CancellationToken& cancellation_token) override;
};

class SyntheticSourceImageDecodeService final
	: public SourceImageDecodeService {
public:
	void set_decoded_pixels(ImagePixels pixels);
	void clear_decoded_pixels();
	[[nodiscard]] const std::optional<SourceImageDecodeSizing>&
	last_requested_sizing() const noexcept;

	[[nodiscard]] PlatformValueResult<ImagePixels> decode_source_image(
		const SourceImageDecodeRequest& request,
		const PlatformOperationContext& context, ProgressSink& progress_sink,
		CancellationToken& cancellation_token) override;

private:
	std::optional<ImagePixels> decoded_pixels;
	std::optional<SourceImageDecodeSizing> requested_sizing;
};

class MarkerInternalPhotoCodec final : public InternalPhotoCodec {
public:
	[[nodiscard]] PlatformValueResult<MediaWriteResult> encode_internal_photo(
		const InternalPhotoEncodeRequest& request,
		const PlatformOperationContext& context, ProgressSink& progress_sink,
		CancellationToken& cancellation_token) override;
	[[nodiscard]] PlatformValueResult<ImagePixels> decode_internal_photo(
		const InternalPhotoDecodeRequest& request,
		const PlatformOperationContext& context, ProgressSink& progress_sink,
		CancellationToken& cancellation_token) override;

private:
	std::map<std::filesystem::path, ImagePixels> encoded_pixels_by_path;
};

class MarkerJpegExportService final : public JpegExportService {
public:
	[[nodiscard]] PlatformValueResult<MediaWriteResult> write_jpeg(
		const JpegExportRequest& request,
		const PlatformOperationContext& context, ProgressSink& progress_sink,
		CancellationToken& cancellation_token) override;
};
}	 // namespace shuba::platform
