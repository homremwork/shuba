#pragma once

#include "Core/Identifier.hpp"
#include "Core/OperationGate.hpp"
#include "Core/Result.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace shuba::platform {
enum class PlatformCapability : std::uint8_t {
	AppPrivatePaths,
	PhotoSelection,
	DocumentImport,
	DocumentExport,
	ContentStaging,
	SourceImageDecode,
	InternalPhotoCodec,
	JpegExport,
	ZipArchive,
	CatalogReplacement,
	BroadMediaLibraryPermission,
	DirectCameraCapture,
};

enum class PlatformCapabilityState : std::uint8_t {
	Available,
	Unsupported,
	PermissionDenied,
};

enum class PlatformPermissionScope : std::uint8_t {
	None,
	AppPrivateStorage,
	PickerGrant,
	BroadMediaLibrary,
	Camera,
};

[[nodiscard]] std::string_view to_string(
	PlatformCapability capability) noexcept;
[[nodiscard]] std::string_view to_string(
	PlatformCapabilityState state) noexcept;
[[nodiscard]] std::string_view to_string(
	PlatformPermissionScope scope) noexcept;

[[nodiscard]] PlatformPermissionScope default_permission_scope(
	PlatformCapability capability) noexcept;

struct PlatformCapabilityCheck final {
	PlatformCapability capability{PlatformCapability::AppPrivatePaths};
	PlatformCapabilityState state{PlatformCapabilityState::Available};
	PlatformPermissionScope permission_scope{PlatformPermissionScope::None};
	std::string message;

	[[nodiscard]] bool available() const noexcept;
	[[nodiscard]] core::OperationResultCategory category() const noexcept;
};

[[nodiscard]] PlatformCapabilityCheck default_capability_check(
	PlatformCapability capability);

template<class Value>
struct PlatformValueResult final {
	core::OperationResultCategory category{
		core::OperationResultCategory::Success};
	std::vector<core::Diagnostic> diagnostics;
	std::optional<Value> value;

	[[nodiscard]] bool succeeded() const noexcept {
		return category == core::OperationResultCategory::Success
			   && value.has_value();
	}

	[[nodiscard]] bool was_user_cancelled() const noexcept {
		return category == core::OperationResultCategory::UserCancelled;
	}

	[[nodiscard]] bool failed() const noexcept {
		return category != core::OperationResultCategory::Success
			   && category != core::OperationResultCategory::UserCancelled;
	}

	[[nodiscard]] explicit operator bool() const noexcept {
		return succeeded();
	}

	void add_diagnostic(core::Diagnostic diagnostic) {
		diagnostics.push_back(std::move(diagnostic));
	}
};

template<class Value>
[[nodiscard]] PlatformValueResult<Value> platform_value_success(Value value) {
	return PlatformValueResult<Value>{
		.category = core::OperationResultCategory::Success,
		.value	  = std::move(value)};
}

template<class Value>
[[nodiscard]] PlatformValueResult<Value> platform_value_success(
	Value value, const std::vector<core::Diagnostic>& diagnostics) {
	return PlatformValueResult<Value>{
		.category	 = core::OperationResultCategory::Success,
		.diagnostics = diagnostics,
		.value		 = std::move(value)};
}

template<class Value>
[[nodiscard]] PlatformValueResult<Value> platform_value_user_cancelled() {
	return PlatformValueResult<Value>{
		.category = core::OperationResultCategory::UserCancelled};
}

template<class Value>
[[nodiscard]] PlatformValueResult<Value> platform_value_failure(
	core::OperationResultCategory category, core::Diagnostic diagnostic) {
	return PlatformValueResult<Value>{.category	   = category,
									  .diagnostics = {std::move(diagnostic)}};
}

enum class ProgressOperationType : std::uint8_t {
	MetadataWrite,
	PhotoImport,
	ImagePreview,
	JpegExport,
	BackupExport,
	BackupImport,
	DiagnosticExport,
	CatalogReplacement,
};

[[nodiscard]] std::string_view to_string(
	ProgressOperationType operation_type) noexcept;

enum class ProgressMessageId : std::uint8_t {
	CopyStarted,
	Copying,
	CopyCompleted,
	SourceFingerprintStarted,
	SourceFingerprintCompleted,
	SourceDecodeStarted,
	SourceDecodeCompleted,
	SyntheticSourceDecodeStarted,
	SyntheticSourceDecodeCompleted,
	InternalPhotoDecodeStarted,
	InternalPhotoDecodeCompleted,
	JpegWriteStarted,
	JpegWriteCompleted,
	JpegXlEncodeStarted,
	JpegXlEncodeCompleted,
	JpegXlDecodeStarted,
	JpegXlDecodeCompleted,
	ZipBuildStarted,
	ZipBuildWriting,
	ZipBuildValidating,
	ZipInspecting,
	ZipExtracting,
	ZipExtractCompleted,
	MediaWriteStarted,
	MediaWriteCompleted,
	PhotoImportStarted,
	PhotoImportSourceStarted,
	PhotoImportCommitting,
	PhotoImportCompleted,
	PendingPhotoStagingStarted,
	PendingPhotoSourceStarted,
	PendingPhotoStagingCompleted,
	JpegExportStarted,
	JpegExportDecoding,
	JpegExportWritingTemporary,
	JpegExportCopying,
	JpegExportCompleted,
	BackupPreparing,
	BackupCopying,
	BackupCompleted,
	DiagnosticPreparing,
	DiagnosticCompleted,
	BackupImportStaging,
	BackupImportValidated,
	CatalogReplacementValidating,
	CatalogReplacementRollbackCopy,
	CatalogReplacementParkingActive,
	CatalogReplacementMovingStaged,
	CatalogReplacementLoading,
	CatalogReplacementCompleted,
	Count,
};

struct PlatformOperationContext final {
	core::OperationIdentifier operation_id;
	ProgressOperationType operation_type{ProgressOperationType::MetadataWrite};

	friend bool operator==(const PlatformOperationContext&,
						   const PlatformOperationContext&) = default;
};

struct ProgressEvent final {
	core::OperationIdentifier operation_id;
	ProgressOperationType operation_type{ProgressOperationType::MetadataWrite};
	std::string phase;
	std::optional<ProgressMessageId> message_id;
	std::optional<std::uint64_t> current_units;
	std::optional<std::uint64_t> total_units;
	std::string message;
	bool cancellable{};

	friend bool operator==(const ProgressEvent&,
						   const ProgressEvent&) = default;
};

class ProgressSink {
public:
	ProgressSink()									 = default;
	ProgressSink(const ProgressSink&)				 = default;
	ProgressSink& operator=(const ProgressSink&)	 = default;
	ProgressSink(ProgressSink&&) noexcept			 = default;
	ProgressSink& operator=(ProgressSink&&) noexcept = default;
	virtual ~ProgressSink()							 = default;

	virtual void publish_progress(ProgressEvent event) = 0;
};

class NullProgressSink final : public ProgressSink {
public:
	void publish_progress(ProgressEvent event) override;
};

class ProgressCollector final : public ProgressSink {
public:
	void publish_progress(ProgressEvent event) override;
	void clear() noexcept;

	[[nodiscard]] const std::vector<ProgressEvent>& events() const noexcept;

private:
	std::vector<ProgressEvent> recorded_events;
};

class CancellationToken {
public:
	CancellationToken()										   = default;
	CancellationToken(const CancellationToken&)				   = default;
	CancellationToken& operator=(const CancellationToken&)	   = default;
	CancellationToken(CancellationToken&&) noexcept			   = default;
	CancellationToken& operator=(CancellationToken&&) noexcept = default;
	virtual ~CancellationToken()							   = default;

	[[nodiscard]] virtual bool cancellation_requested() const noexcept = 0;
};

class ManualCancellationToken final : public CancellationToken {
public:
	[[nodiscard]] bool cancellation_requested() const noexcept override;

	void request_cancellation() noexcept;
	void reset_cancellation() noexcept;

private:
	bool requested{};
};

class NeverCancelledToken final : public CancellationToken {
public:
	[[nodiscard]] bool cancellation_requested() const noexcept override;
};

struct PlatformOperationStartRequest final {
	core::OperationKind operation_kind{core::OperationKind::MetadataWrite};
	core::OperationIdentifier operation_id;
	ProgressOperationType operation_type{ProgressOperationType::MetadataWrite};
};

struct PlatformOperationStartResult;

class ScopedPlatformOperation final {
public:
	ScopedPlatformOperation(const ScopedPlatformOperation&)			   = delete;
	ScopedPlatformOperation& operator=(const ScopedPlatformOperation&) = delete;
	ScopedPlatformOperation(ScopedPlatformOperation&&) noexcept = default;
	ScopedPlatformOperation& operator=(ScopedPlatformOperation&&) noexcept =
		default;
	~ScopedPlatformOperation() = default;

	[[nodiscard]] const PlatformOperationContext& context() const noexcept;
	[[nodiscard]] bool cancellation_requested() const noexcept;

	void publish_progress(std::string phase,
						  std::optional<ProgressMessageId> message_id,
						  std::optional<std::uint64_t> current_units,
						  std::optional<std::uint64_t> total_units,
						  std::string message, bool cancellable) const;
	void release() noexcept;

private:
	friend struct PlatformOperationStartResult;
	friend PlatformOperationStartResult try_start_platform_operation(
		core::OperationGate& gate, const PlatformOperationStartRequest& request,
		ProgressSink& progress_sink, CancellationToken& cancellation_token);

	ScopedPlatformOperation(core::OperationGate::Lease lease,
							PlatformOperationContext context,
							ProgressSink& progress_sink,
							CancellationToken& cancellation_token);

	core::OperationGate::Lease gate_lease;
	PlatformOperationContext operation_context;
	ProgressSink* progress{};
	CancellationToken* cancellation{};
};

struct PlatformOperationStartResult final {
	core::OperationResultCategory category{
		core::OperationResultCategory::Success};
	std::vector<core::Diagnostic> diagnostics;
	std::optional<ScopedPlatformOperation> operation;

	[[nodiscard]] bool succeeded() const noexcept;
	[[nodiscard]] bool failed() const noexcept;
	[[nodiscard]] explicit operator bool() const noexcept;
};

[[nodiscard]] PlatformOperationStartResult try_start_platform_operation(
	core::OperationGate& gate, const PlatformOperationStartRequest& request,
	ProgressSink& progress_sink, CancellationToken& cancellation_token);

struct AppPrivatePaths final {
	std::filesystem::path app_private_root;
	std::filesystem::path active_catalog_root;
	std::filesystem::path operation_tmp_root;
	std::filesystem::path staged_content_root;
	std::filesystem::path export_tmp_root;
	std::filesystem::path media_root;

	friend bool operator==(const AppPrivatePaths&,
						   const AppPrivatePaths&) = default;
};

enum class PlatformContentHandleKind : std::uint8_t {
	LocalFile,
	OpaqueHandle,
};

[[nodiscard]] std::string_view to_string(
	PlatformContentHandleKind kind) noexcept;

struct ContentSourceDescriptor final {
	PlatformContentHandleKind kind{PlatformContentHandleKind::LocalFile};
	std::filesystem::path local_path;
	std::string opaque_handle;
	std::string display_name;
	std::optional<std::uint64_t> byte_count;
	bool transient{};

	friend bool operator==(const ContentSourceDescriptor&,
						   const ContentSourceDescriptor&) = default;
};

struct DocumentDestinationDescriptor final {
	PlatformContentHandleKind kind{PlatformContentHandleKind::LocalFile};
	std::filesystem::path local_path;
	std::string opaque_handle;
	std::string display_name;

	friend bool operator==(const DocumentDestinationDescriptor&,
						   const DocumentDestinationDescriptor&) = default;
};

[[nodiscard]] ContentSourceDescriptor make_local_file_source(
	std::filesystem::path local_path, std::string display_name = {});
[[nodiscard]] DocumentDestinationDescriptor make_local_file_destination(
	std::filesystem::path local_path, std::string display_name = {});
[[nodiscard]] ContentSourceDescriptor make_opaque_content_source(
	std::string opaque_handle, std::string display_name = {},
	std::optional<std::uint64_t> byte_count = std::nullopt,
	bool transient							= true);
[[nodiscard]] DocumentDestinationDescriptor make_opaque_document_destination(
	std::string opaque_handle, std::string display_name = {});
[[nodiscard]] std::string sanitize_platform_file_name(
	std::string_view text, std::string_view fallback);
[[nodiscard]] std::string file_extension_or_empty(
	std::string_view display_name);
[[nodiscard]] std::string make_staged_content_file_name(
	std::string_view prefix, const core::OperationIdentifier& operation_id,
	std::size_t sequence, std::string_view display_name);
[[nodiscard]] std::string file_patterns_for_mime_types(
	const std::vector<std::string>& mime_types);

struct PhotoSelectionRequest final {
	bool allow_multiple{true};
	std::vector<std::string> accepted_mime_types;

	friend bool operator==(const PhotoSelectionRequest&,
						   const PhotoSelectionRequest&) = default;
};

struct DocumentImportRequest final {
	std::vector<std::string> accepted_mime_types;
	std::string purpose;

	friend bool operator==(const DocumentImportRequest&,
						   const DocumentImportRequest&) = default;
};

struct DocumentExportRequest final {
	std::string suggested_file_name;
	std::string mime_type;
	std::string purpose;

	friend bool operator==(const DocumentExportRequest&,
						   const DocumentExportRequest&) = default;
};

struct DocumentCopyRequest final {
	std::filesystem::path temp_source_path;
	DocumentDestinationDescriptor destination;

	friend bool operator==(const DocumentCopyRequest&,
						   const DocumentCopyRequest&) = default;
};

struct ZipArchiveEntrySource final {
	std::filesystem::path source_path;
	std::string archive_path;

	friend bool operator==(const ZipArchiveEntrySource&,
						   const ZipArchiveEntrySource&) = default;
};

struct ZipArchiveBuildRequest final {
	std::filesystem::path output_path;
	std::vector<ZipArchiveEntrySource> entries;
	int compression_level{6};

	friend bool operator==(const ZipArchiveBuildRequest&,
						   const ZipArchiveBuildRequest&) = default;
};

struct ZipArchiveValidationRequest final {
	std::filesystem::path archive_path;
	std::vector<std::string> required_entries;
	bool reject_unsafe_paths{true};

	friend bool operator==(const ZipArchiveValidationRequest&,
						   const ZipArchiveValidationRequest&) = default;
};

struct ZipArchiveExtractRequest final {
	std::filesystem::path archive_path;
	std::filesystem::path target_directory;
	bool overwrite_files{};

	friend bool operator==(const ZipArchiveExtractRequest&,
						   const ZipArchiveExtractRequest&) = default;
};

struct ZipArchiveEntryInfo final {
	std::string archive_path;
	std::uint64_t uncompressed_bytes{};
	bool directory{};

	friend bool operator==(const ZipArchiveEntryInfo&,
						   const ZipArchiveEntryInfo&) = default;
};

struct ZipArchiveInspection final {
	std::vector<ZipArchiveEntryInfo> entries;
	std::uint64_t archive_byte_count{};
	std::uint64_t largest_entry_byte_count{};
	bool classic_zip64_risk_observed{};

	friend bool operator==(const ZipArchiveInspection&,
						   const ZipArchiveInspection&) = default;
};

[[nodiscard]] bool zip_archive_path_is_safe(
	std::string_view archive_path) noexcept;

using PhotoSelectionCompletion = std::function<void(
	PlatformValueResult<std::vector<ContentSourceDescriptor>> result)>;
using DocumentImportCompletion =
	std::function<void(PlatformValueResult<ContentSourceDescriptor> result)>;
using DocumentExportDestinationCompletion = std::function<void(
	PlatformValueResult<DocumentDestinationDescriptor> result)>;

struct ContentStagingRequest final {
	ContentSourceDescriptor source;
	std::filesystem::path target_directory;
	std::string target_file_name;
	bool allow_no_copy_optimization{};

	friend bool operator==(const ContentStagingRequest&,
						   const ContentStagingRequest&) = default;
};

struct StagedContent final {
	std::filesystem::path staged_path;
	std::string display_name;
	std::optional<std::uint64_t> byte_count;

	friend bool operator==(const StagedContent&,
						   const StagedContent&) = default;
};

struct SourceByteFingerprint final {
	std::string source_md5;

	friend bool operator==(const SourceByteFingerprint&,
						   const SourceByteFingerprint&) = default;
};

struct SourceByteFingerprintRequest final {
	std::filesystem::path source_path;

	friend bool operator==(const SourceByteFingerprintRequest&,
						   const SourceByteFingerprintRequest&) = default;
};

enum class PixelFormat : std::uint8_t {
	Rgba8,
};

[[nodiscard]] std::string_view to_string(PixelFormat format) noexcept;

enum class ImagePixelsValidationIssue : std::uint8_t {
	None,
	EmptyDimensions,
	UnsupportedFormat,
	ByteCountOverflow,
	ByteCountMismatch,
};

[[nodiscard]] std::string_view to_string(
	ImagePixelsValidationIssue issue) noexcept;

struct ImagePixels final {
	std::uint32_t width{};
	std::uint32_t height{};
	PixelFormat format{PixelFormat::Rgba8};
	// Rgba8 bytes are stored as straight, non-premultiplied red, green,
	// blue, alpha channels. Display conversion treats ordinary photo pixels as
	// opaque RGB and must not receive premultiplied/dimmed colour channels.
	std::vector<std::uint8_t> bytes;
	std::string source_description;
	std::string orientation_description;
	std::uint64_t elapsed_milliseconds{};

	friend bool operator==(const ImagePixels&, const ImagePixels&) = default;
};

struct ImagePixelsValidation final {
	ImagePixelsValidationIssue issue{ImagePixelsValidationIssue::None};
	std::uint64_t expected_byte_count{};
	std::uint64_t actual_byte_count{};

	[[nodiscard]] bool valid() const noexcept;
	[[nodiscard]] explicit operator bool() const noexcept;
};

[[nodiscard]] std::optional<std::uint64_t> image_pixel_byte_count(
	std::uint32_t width, std::uint32_t height, PixelFormat format) noexcept;
[[nodiscard]] ImagePixelsValidation validate_image_pixels(
	const ImagePixels& pixels) noexcept;

struct SourceImageDecodeRequest final {
	StagedContent content;

	friend bool operator==(const SourceImageDecodeRequest&,
						   const SourceImageDecodeRequest&) = default;
};

struct InternalPhotoEncodeSettings final {
	float quality{92.0F};
	std::uint8_t effort{7};
	bool validate_after_encode{true};

	friend bool operator==(const InternalPhotoEncodeSettings&,
						   const InternalPhotoEncodeSettings&) = default;
};

[[nodiscard]] InternalPhotoEncodeSettings
default_internal_photo_encode_settings() noexcept;
[[nodiscard]] bool validate_internal_photo_encode_settings(
	const InternalPhotoEncodeSettings& settings) noexcept;

struct InternalPhotoEncodeRequest final {
	ImagePixels pixels;
	std::filesystem::path output_path;
	InternalPhotoEncodeSettings settings;

	friend bool operator==(const InternalPhotoEncodeRequest&,
						   const InternalPhotoEncodeRequest&) = default;
};

struct InternalPhotoDecodeRequest final {
	std::filesystem::path input_path;

	friend bool operator==(const InternalPhotoDecodeRequest&,
						   const InternalPhotoDecodeRequest&) = default;
};

struct JpegExportRequest final {
	ImagePixels pixels;
	std::filesystem::path output_path;
	std::uint8_t quality{90};

	friend bool operator==(const JpegExportRequest&,
						   const JpegExportRequest&) = default;
};

[[nodiscard]] bool validate_jpeg_export_quality(std::uint8_t quality) noexcept;

struct MediaWriteResult final {
	std::filesystem::path file_path;
	std::uint64_t bytes_written{};
	std::uint32_t width{};
	std::uint32_t height{};
	std::uint64_t elapsed_milliseconds{};
	std::string codec_description;

	friend bool operator==(const MediaWriteResult&,
						   const MediaWriteResult&) = default;
};

class PlatformCapabilityChecker {
public:
	PlatformCapabilityChecker()									= default;
	PlatformCapabilityChecker(const PlatformCapabilityChecker&) = default;
	PlatformCapabilityChecker& operator=(const PlatformCapabilityChecker&) =
		default;
	PlatformCapabilityChecker(PlatformCapabilityChecker&&) noexcept = default;
	PlatformCapabilityChecker& operator=(PlatformCapabilityChecker&&) noexcept =
		default;
	virtual ~PlatformCapabilityChecker() = default;

	[[nodiscard]] virtual PlatformCapabilityCheck check_capability(
		PlatformCapability capability) const = 0;
};

class AppPrivatePathProvider {
public:
	AppPrivatePathProvider()										 = default;
	AppPrivatePathProvider(const AppPrivatePathProvider&)			 = default;
	AppPrivatePathProvider& operator=(const AppPrivatePathProvider&) = default;
	AppPrivatePathProvider(AppPrivatePathProvider&&) noexcept		 = default;
	AppPrivatePathProvider& operator=(AppPrivatePathProvider&&) noexcept =
		default;
	virtual ~AppPrivatePathProvider() = default;

	[[nodiscard]] virtual PlatformValueResult<AppPrivatePaths>
	resolve_app_private_paths() const = 0;
};

class PhotoSelectionService {
public:
	PhotoSelectionService()										   = default;
	PhotoSelectionService(const PhotoSelectionService&)			   = default;
	PhotoSelectionService& operator=(const PhotoSelectionService&) = default;
	PhotoSelectionService(PhotoSelectionService&&) noexcept		   = default;
	PhotoSelectionService& operator=(PhotoSelectionService&&) noexcept =
		default;
	virtual ~PhotoSelectionService() = default;

	[[nodiscard]] virtual core::OperationResult request_photo_selection(
		const PhotoSelectionRequest& request,
		PhotoSelectionCompletion completion) = 0;
};

class DocumentImportService {
public:
	DocumentImportService()										   = default;
	DocumentImportService(const DocumentImportService&)			   = default;
	DocumentImportService& operator=(const DocumentImportService&) = default;
	DocumentImportService(DocumentImportService&&) noexcept		   = default;
	DocumentImportService& operator=(DocumentImportService&&) noexcept =
		default;
	virtual ~DocumentImportService() = default;

	[[nodiscard]] virtual core::OperationResult
	request_import_document_selection(const DocumentImportRequest& request,
									  DocumentImportCompletion completion) = 0;
};

class DocumentExportService {
public:
	DocumentExportService()										   = default;
	DocumentExportService(const DocumentExportService&)			   = default;
	DocumentExportService& operator=(const DocumentExportService&) = default;
	DocumentExportService(DocumentExportService&&) noexcept		   = default;
	DocumentExportService& operator=(DocumentExportService&&) noexcept =
		default;
	virtual ~DocumentExportService() = default;

	[[nodiscard]] virtual core::OperationResult
	request_export_destination_selection(
		const DocumentExportRequest& request,
		DocumentExportDestinationCompletion completion) = 0;
	[[nodiscard]] virtual core::OperationResult copy_file_to_destination(
		const DocumentCopyRequest& request,
		const PlatformOperationContext& context, ProgressSink& progress_sink,
		CancellationToken& cancellation_token) = 0;
};

class ContentStagingService {
public:
	ContentStagingService()										   = default;
	ContentStagingService(const ContentStagingService&)			   = default;
	ContentStagingService& operator=(const ContentStagingService&) = default;
	ContentStagingService(ContentStagingService&&) noexcept		   = default;
	ContentStagingService& operator=(ContentStagingService&&) noexcept =
		default;
	virtual ~ContentStagingService() = default;

	[[nodiscard]] virtual PlatformValueResult<StagedContent> stage_content(
		const ContentStagingRequest& request,
		const PlatformOperationContext& context, ProgressSink& progress_sink,
		CancellationToken& cancellation_token) = 0;
};

class SourceByteFingerprintService {
public:
	SourceByteFingerprintService()									  = default;
	SourceByteFingerprintService(const SourceByteFingerprintService&) = default;
	SourceByteFingerprintService& operator=(
		const SourceByteFingerprintService&) = default;
	SourceByteFingerprintService(SourceByteFingerprintService&&) noexcept =
		default;
	SourceByteFingerprintService& operator=(
		SourceByteFingerprintService&&) noexcept = default;
	virtual ~SourceByteFingerprintService()		 = default;

	[[nodiscard]] virtual PlatformValueResult<SourceByteFingerprint>
	fingerprint_source_bytes(const SourceByteFingerprintRequest& request,
							 const PlatformOperationContext& context,
							 ProgressSink& progress_sink,
							 CancellationToken& cancellation_token) = 0;
};

class SourceImageDecodeService {
public:
	SourceImageDecodeService()								  = default;
	SourceImageDecodeService(const SourceImageDecodeService&) = default;
	SourceImageDecodeService& operator=(const SourceImageDecodeService&) =
		default;
	SourceImageDecodeService(SourceImageDecodeService&&) noexcept = default;
	SourceImageDecodeService& operator=(SourceImageDecodeService&&) noexcept =
		default;
	virtual ~SourceImageDecodeService() = default;

	[[nodiscard]] virtual PlatformValueResult<ImagePixels> decode_source_image(
		const SourceImageDecodeRequest& request,
		const PlatformOperationContext& context, ProgressSink& progress_sink,
		CancellationToken& cancellation_token) = 0;
};

class InternalPhotoCodec {
public:
	InternalPhotoCodec()										 = default;
	InternalPhotoCodec(const InternalPhotoCodec&)				 = default;
	InternalPhotoCodec& operator=(const InternalPhotoCodec&)	 = default;
	InternalPhotoCodec(InternalPhotoCodec&&) noexcept			 = default;
	InternalPhotoCodec& operator=(InternalPhotoCodec&&) noexcept = default;
	virtual ~InternalPhotoCodec()								 = default;

	[[nodiscard]] virtual PlatformValueResult<MediaWriteResult>
	encode_internal_photo(const InternalPhotoEncodeRequest& request,
						  const PlatformOperationContext& context,
						  ProgressSink& progress_sink,
						  CancellationToken& cancellation_token) = 0;
	[[nodiscard]] virtual PlatformValueResult<ImagePixels>
	decode_internal_photo(const InternalPhotoDecodeRequest& request,
						  const PlatformOperationContext& context,
						  ProgressSink& progress_sink,
						  CancellationToken& cancellation_token) = 0;
};

class JpegExportService {
public:
	JpegExportService()										   = default;
	JpegExportService(const JpegExportService&)				   = default;
	JpegExportService& operator=(const JpegExportService&)	   = default;
	JpegExportService(JpegExportService&&) noexcept			   = default;
	JpegExportService& operator=(JpegExportService&&) noexcept = default;
	virtual ~JpegExportService()							   = default;

	[[nodiscard]] virtual PlatformValueResult<MediaWriteResult> write_jpeg(
		const JpegExportRequest& request,
		const PlatformOperationContext& context, ProgressSink& progress_sink,
		CancellationToken& cancellation_token) = 0;
};

class ZipArchiveService {
public:
	ZipArchiveService()										   = default;
	ZipArchiveService(const ZipArchiveService&)				   = default;
	ZipArchiveService& operator=(const ZipArchiveService&)	   = default;
	ZipArchiveService(ZipArchiveService&&) noexcept			   = default;
	ZipArchiveService& operator=(ZipArchiveService&&) noexcept = default;
	virtual ~ZipArchiveService()							   = default;

	[[nodiscard]] virtual PlatformValueResult<ZipArchiveInspection>
	build_zip_archive(const ZipArchiveBuildRequest& request,
					  const PlatformOperationContext& context,
					  ProgressSink& progress_sink,
					  CancellationToken& cancellation_token) = 0;
	[[nodiscard]] virtual PlatformValueResult<ZipArchiveInspection>
	inspect_zip_archive(const std::filesystem::path& archive_path,
						const PlatformOperationContext& context,
						ProgressSink& progress_sink,
						CancellationToken& cancellation_token) = 0;
	[[nodiscard]] virtual PlatformValueResult<ZipArchiveInspection>
	validate_zip_archive(const ZipArchiveValidationRequest& request,
						 const PlatformOperationContext& context,
						 ProgressSink& progress_sink,
						 CancellationToken& cancellation_token) = 0;
	[[nodiscard]] virtual PlatformValueResult<ZipArchiveInspection>
	extract_zip_archive(const ZipArchiveExtractRequest& request,
						const PlatformOperationContext& context,
						ProgressSink& progress_sink,
						CancellationToken& cancellation_token) = 0;
};
}	 // namespace shuba::platform
