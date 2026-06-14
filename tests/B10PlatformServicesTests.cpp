#include "Core/Clock.hpp"
#include "Core/OperationGate.hpp"
#include "Platform/LinuxFakes.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace {
class TemporaryDirectory final {
public:
	explicit TemporaryDirectory(std::string leaf_prefix)
		: path_value(std::filesystem::temp_directory_path()
					 / (std::move(leaf_prefix) + "-"
						+ std::to_string(std::chrono::steady_clock::now()
											 .time_since_epoch()
											 .count()))) {
		std::error_code ignored;
		std::filesystem::remove_all(path_value, ignored);
		std::filesystem::create_directories(path_value);
	}

	TemporaryDirectory(const TemporaryDirectory&)				 = delete;
	TemporaryDirectory& operator=(const TemporaryDirectory&)	 = delete;
	TemporaryDirectory(TemporaryDirectory&&) noexcept			 = delete;
	TemporaryDirectory& operator=(TemporaryDirectory&&) noexcept = delete;

	~TemporaryDirectory() {
		std::error_code ignored;
		std::filesystem::remove_all(path_value, ignored);
	}

	[[nodiscard]] const std::filesystem::path& path() const noexcept {
		return path_value;
	}

private:
	std::filesystem::path path_value;
};

[[nodiscard]] shuba::core::OperationIdentifier operation_id(
	std::string value = "operation-001") {
	std::optional<shuba::core::OperationIdentifier> identifier =
		shuba::core::OperationIdentifier::try_create_file_safe(
			std::move(value));
	REQUIRE(identifier.has_value());
	return *identifier;
}

[[nodiscard]] std::string read_text(const std::filesystem::path& path) {
	std::ifstream input{path, std::ios::binary};
	REQUIRE(input.good());
	return std::string{std::istreambuf_iterator<char>{input},
					   std::istreambuf_iterator<char>{}};
}

void write_text(const std::filesystem::path& path, std::string_view text) {
	std::filesystem::create_directories(path.parent_path());
	std::ofstream output{path, std::ios::binary | std::ios::trunc};
	REQUIRE(output.good());
	output << text;
}

[[nodiscard]] shuba::platform::PlatformOperationContext context(
	shuba::platform::ProgressOperationType operation_type =
		shuba::platform::ProgressOperationType::PhotoImport) {
	return shuba::platform::PlatformOperationContext{
		.operation_id = operation_id(), .operation_type = operation_type};
}
}	 // namespace

TEST_CASE("B10 Linux path fake resolves app-private platform roots",
		  "[b10][platform][paths]") {
	using shuba::platform::AppPrivatePaths;
	using shuba::platform::LinuxFakePathProvider;
	using shuba::platform::PlatformValueResult;

	TemporaryDirectory temporary{"shuba-b10-paths"};
	LinuxFakePathProvider provider{temporary.path()};

	PlatformValueResult<AppPrivatePaths> result =
		provider.resolve_app_private_paths();
	REQUIRE(result.succeeded());
	REQUIRE(result.value.has_value());
	const AppPrivatePaths& paths = *result.value;

	REQUIRE(paths.app_private_root == temporary.path() / "app-private");
	REQUIRE(paths.active_catalog_root
			== temporary.path() / "app-private" / "active-catalog");
	REQUIRE(paths.operation_tmp_root
			== temporary.path() / "app-private" / "operation-tmp");
	REQUIRE(paths.staged_content_root
			== paths.operation_tmp_root / "staged-content");
	REQUIRE(paths.export_tmp_root == paths.operation_tmp_root / "exports");
	REQUIRE(paths.media_root == paths.active_catalog_root / "media/photos");
	REQUIRE(std::filesystem::is_directory(paths.media_root));
	REQUIRE(std::filesystem::is_directory(paths.staged_content_root));
}

TEST_CASE("B10 capability checks encode first-version permission policy",
		  "[b10][platform][capabilities]") {
	using shuba::core::OperationResultCategory;
	using shuba::platform::default_permission_scope;
	using shuba::platform::FixedCapabilityChecker;
	using shuba::platform::PlatformCapability;
	using shuba::platform::PlatformCapabilityCheck;
	using shuba::platform::PlatformCapabilityState;
	using shuba::platform::PlatformPermissionScope;
	using shuba::platform::to_string;

	FixedCapabilityChecker checker;
	PlatformCapabilityCheck photo_selection =
		checker.check_capability(PlatformCapability::PhotoSelection);
	REQUIRE(photo_selection.available());
	REQUIRE(photo_selection.permission_scope
			== PlatformPermissionScope::PickerGrant);
	REQUIRE(photo_selection.category() == OperationResultCategory::Success);
	REQUIRE(to_string(PlatformCapability::PhotoSelection) == "photo selection");

	PlatformCapabilityCheck broad_media = checker.check_capability(
		PlatformCapability::BroadMediaLibraryPermission);
	REQUIRE_FALSE(broad_media.available());
	REQUIRE(broad_media.state == PlatformCapabilityState::Unsupported);
	REQUIRE(broad_media.permission_scope
			== PlatformPermissionScope::BroadMediaLibrary);
	REQUIRE(broad_media.category() == OperationResultCategory::Unsupported);

	checker.set_capability_state(PlatformCapability::SourceImageDecode,
								 PlatformCapabilityState::PermissionDenied,
								 "decoder permission denied");
	PlatformCapabilityCheck decode =
		checker.check_capability(PlatformCapability::SourceImageDecode);
	REQUIRE_FALSE(decode.available());
	REQUIRE(decode.category() == OperationResultCategory::PermissionDenied);
	REQUIRE(default_permission_scope(PlatformCapability::DirectCameraCapture)
			== PlatformPermissionScope::Camera);
}

TEST_CASE("B10 ZIP archive path safety rejects traversal and absolute forms",
		  "[b10][platform][zip]") {
	using shuba::platform::zip_archive_path_is_safe;

	REQUIRE(zip_archive_path_is_safe("manifest.json"));
	REQUIRE(zip_archive_path_is_safe("media/photos/photo-001.jxl"));
	REQUIRE_FALSE(zip_archive_path_is_safe(""));
	REQUIRE_FALSE(zip_archive_path_is_safe("/manifest.json"));
	REQUIRE_FALSE(zip_archive_path_is_safe("./manifest.json"));
	REQUIRE_FALSE(zip_archive_path_is_safe("data/../manifest.json"));
	REQUIRE_FALSE(zip_archive_path_is_safe("data//items.jsonl"));
	REQUIRE_FALSE(zip_archive_path_is_safe("C:/manifest.json"));
	REQUIRE_FALSE(zip_archive_path_is_safe(R"(data\items.jsonl)"));
}

TEST_CASE("B10 selection and document fakes return scripted neutral handles",
		  "[b10][platform][selection]") {
	using shuba::core::OperationResult;
	using shuba::platform::ContentSourceDescriptor;
	using shuba::platform::DocumentDestinationDescriptor;
	using shuba::platform::DocumentExportRequest;
	using shuba::platform::DocumentImportRequest;
	using shuba::platform::LinuxFakeDocumentExportService;
	using shuba::platform::LinuxFakeDocumentImportService;
	using shuba::platform::LinuxFakePhotoSelectionService;
	using shuba::platform::make_local_file_destination;
	using shuba::platform::make_local_file_source;
	using shuba::platform::PhotoSelectionRequest;
	using shuba::platform::PlatformContentHandleKind;
	using shuba::platform::PlatformValueResult;

	TemporaryDirectory temporary{"shuba-b10-selection"};
	const std::filesystem::path image_path	= temporary.path() / "image.jpg";
	const std::filesystem::path backup_path = temporary.path() / "backup.zip";
	const std::filesystem::path export_path = temporary.path() / "out.zip";
	write_text(image_path, "image-bytes");
	write_text(backup_path, "zip-bytes");

	LinuxFakePhotoSelectionService photo_selection;
	photo_selection.script_selection_success(
		{make_local_file_source(image_path, "selected image")});
	photo_selection.script_selection_cancellation();

	std::optional<PlatformValueResult<std::vector<ContentSourceDescriptor>>>
		photos;
	OperationResult photo_start = photo_selection.request_photo_selection(
		PhotoSelectionRequest{.allow_multiple	   = true,
							  .accepted_mime_types = {"image/jpeg"}},
		[&photos](
			PlatformValueResult<std::vector<ContentSourceDescriptor>> result) {
		photos = std::move(result);
	});
	REQUIRE(photo_start.succeeded());
	REQUIRE(photos.has_value());
	REQUIRE(photos->succeeded());
	REQUIRE(photos->value->size() == 1);
	REQUIRE(photos->value->front().kind
			== PlatformContentHandleKind::LocalFile);
	REQUIRE(photos->value->front().opaque_handle.empty());
	REQUIRE(photos->value->front().local_path == image_path);

	std::optional<PlatformValueResult<std::vector<ContentSourceDescriptor>>>
		cancelled_photos;
	OperationResult cancelled_photo_start =
		photo_selection.request_photo_selection(
			PhotoSelectionRequest{},
			[&cancelled_photos](
				PlatformValueResult<std::vector<ContentSourceDescriptor>>
					result) { cancelled_photos = std::move(result); });
	REQUIRE(cancelled_photo_start.succeeded());
	REQUIRE(cancelled_photos.has_value());
	REQUIRE(cancelled_photos->was_user_cancelled());

	OperationResult missing_photo_completion =
		photo_selection.request_photo_selection(PhotoSelectionRequest{}, {});
	REQUIRE(missing_photo_completion.failed());

	LinuxFakeDocumentImportService import_service;
	import_service.script_import_success(make_local_file_source(backup_path));
	std::optional<PlatformValueResult<ContentSourceDescriptor>> import_source;
	OperationResult import_start =
		import_service.request_import_document_selection(
			DocumentImportRequest{.accepted_mime_types = {"application/zip"},
								  .purpose			   = "backup import"},
			[&import_source](
				PlatformValueResult<ContentSourceDescriptor> result) {
		import_source = std::move(result);
	});
	REQUIRE(import_start.succeeded());
	REQUIRE(import_source.has_value());
	REQUIRE(import_source->succeeded());
	REQUIRE(import_source->value->local_path == backup_path);

	LinuxFakeDocumentExportService export_service;
	export_service.script_export_destination_success(
		make_local_file_destination(export_path, "backup export"));
	std::optional<PlatformValueResult<DocumentDestinationDescriptor>>
		destination;
	OperationResult export_start =
		export_service.request_export_destination_selection(
			DocumentExportRequest{.suggested_file_name = "backup.zip",
								  .mime_type		   = "application/zip",
								  .purpose			   = "backup export"},
			[&destination](
				PlatformValueResult<DocumentDestinationDescriptor> result) {
		destination = std::move(result);
	});
	REQUIRE(export_start.succeeded());
	REQUIRE(destination.has_value());
	REQUIRE(destination->succeeded());
	REQUIRE(destination->value->local_path == export_path);

	std::optional<PlatformValueResult<DocumentDestinationDescriptor>>
		missing_destination;
	OperationResult missing_export_start =
		export_service.request_export_destination_selection(
			DocumentExportRequest{},
			[&missing_destination](
				PlatformValueResult<DocumentDestinationDescriptor> result) {
		missing_destination = std::move(result);
	});
	REQUIRE(missing_export_start.succeeded());
	REQUIRE(missing_destination.has_value());
	REQUIRE(missing_destination->failed());
}

TEST_CASE("B10 content staging and document export fakes copy with progress",
		  "[b10][platform][staging]") {
	using shuba::core::OperationResult;
	using shuba::platform::AppPrivatePaths;
	using shuba::platform::ContentStagingRequest;
	using shuba::platform::DocumentCopyRequest;
	using shuba::platform::LinuxFakeContentStagingService;
	using shuba::platform::LinuxFakeDocumentExportService;
	using shuba::platform::LinuxFakePathProvider;
	using shuba::platform::make_local_file_destination;
	using shuba::platform::make_local_file_source;
	using shuba::platform::ManualCancellationToken;
	using shuba::platform::PlatformOperationContext;
	using shuba::platform::PlatformValueResult;
	using shuba::platform::ProgressCollector;
	using shuba::platform::ProgressOperationType;
	using shuba::platform::StagedContent;

	TemporaryDirectory temporary{"shuba-b10-staging"};
	LinuxFakePathProvider path_provider{temporary.path()};
	PlatformValueResult<AppPrivatePaths> paths_result =
		path_provider.resolve_app_private_paths();
	REQUIRE(paths_result.succeeded());
	const AppPrivatePaths& paths = *paths_result.value;

	const std::filesystem::path source_path = temporary.path() / "source.bin";
	write_text(source_path, "abcdef");

	ProgressCollector progress;
	ManualCancellationToken cancellation;
	PlatformOperationContext operation_context =
		context(ProgressOperationType::BackupImport);
	LinuxFakeContentStagingService staging;

	PlatformValueResult<StagedContent> staged = staging.stage_content(
		ContentStagingRequest{
			.source = make_local_file_source(source_path, "backup source"),
			.target_directory = paths.staged_content_root,
			.target_file_name = "staged.zip"},
		operation_context, progress, cancellation);
	REQUIRE(staged.succeeded());
	REQUIRE(staged.value->staged_path
			== paths.staged_content_root / "staged.zip");
	REQUIRE(staged.value->byte_count == std::uint64_t{6});
	REQUIRE(read_text(staged.value->staged_path) == "abcdef");
	REQUIRE(progress.events().size() >= 2);
	REQUIRE(progress.events().front().operation_id
			== operation_context.operation_id);
	REQUIRE(progress.events().back().phase == "copy-completed");
	REQUIRE_FALSE(progress.events().back().cancellable);

	LinuxFakeDocumentExportService export_service;
	const std::filesystem::path exported_path = temporary.path() / "export.zip";
	OperationResult exported = export_service.copy_file_to_destination(
		DocumentCopyRequest{
			.temp_source_path = staged.value->staged_path,
			.destination	  = make_local_file_destination(exported_path)},
		operation_context, progress, cancellation);
	REQUIRE(exported.succeeded());
	REQUIRE(read_text(exported_path) == "abcdef");

	cancellation.request_cancellation();
	PlatformValueResult<StagedContent> cancelled = staging.stage_content(
		ContentStagingRequest{
			.source = make_local_file_source(source_path, "backup source"),
			.target_directory = paths.staged_content_root,
			.target_file_name = "cancelled.zip"},
		operation_context, progress, cancellation);
	REQUIRE(cancelled.was_user_cancelled());
	REQUIRE_FALSE(
		std::filesystem::exists(paths.staged_content_root / "cancelled.zip"));
}

TEST_CASE("B10 media fakes provide synthetic decode codec and JPEG export",
		  "[b10][platform][media]") {
	using shuba::platform::ImagePixels;
	using shuba::platform::InternalPhotoDecodeRequest;
	using shuba::platform::InternalPhotoEncodeRequest;
	using shuba::platform::JpegExportRequest;
	using shuba::platform::ManualCancellationToken;
	using shuba::platform::MarkerInternalPhotoCodec;
	using shuba::platform::MarkerJpegExportService;
	using shuba::platform::MediaWriteResult;
	using shuba::platform::PixelFormat;
	using shuba::platform::PlatformValueResult;
	using shuba::platform::ProgressCollector;
	using shuba::platform::ProgressOperationType;
	using shuba::platform::SourceImageDecodeRequest;
	using shuba::platform::StagedContent;
	using shuba::platform::SyntheticSourceImageDecodeService;

	TemporaryDirectory temporary{"shuba-b10-media"};
	const std::filesystem::path staged_source = temporary.path() / "source.png";
	write_text(staged_source, "source");

	ImagePixels pixels{.width			   = 2,
					   .height			   = 1,
					   .format			   = PixelFormat::Rgba8,
					   .bytes			   = {1, 2, 3, 4, 5, 6, 7, 8},
					   .source_description = "synthetic"};

	ProgressCollector progress;
	ManualCancellationToken cancellation;
	SyntheticSourceImageDecodeService decoder;
	decoder.set_decoded_pixels(pixels);
	PlatformValueResult<ImagePixels> decoded = decoder.decode_source_image(
		SourceImageDecodeRequest{
			.content = StagedContent{.staged_path  = staged_source,
									 .display_name = "source.png"}},
		context(), progress, cancellation);
	REQUIRE(decoded.succeeded());
	REQUIRE(*decoded.value == pixels);

	MarkerInternalPhotoCodec codec;
	const std::filesystem::path internal_path = temporary.path() / "photo.jxl";
	PlatformValueResult<MediaWriteResult> encoded = codec.encode_internal_photo(
		InternalPhotoEncodeRequest{.pixels		= pixels,
								   .output_path = internal_path},
		context(), progress, cancellation);
	REQUIRE(encoded.succeeded());
	REQUIRE(read_text(internal_path) == "SHUBA-FAKE-JXL\n");

	PlatformValueResult<ImagePixels> decoded_internal =
		codec.decode_internal_photo(
			InternalPhotoDecodeRequest{.input_path = internal_path}, context(),
			progress, cancellation);
	REQUIRE(decoded_internal.succeeded());
	REQUIRE(*decoded_internal.value == pixels);

	MarkerJpegExportService jpeg;
	const std::filesystem::path jpeg_path = temporary.path() / "photo.jpg";
	PlatformValueResult<MediaWriteResult> exported_jpeg = jpeg.write_jpeg(
		JpegExportRequest{
			.pixels = pixels, .output_path = jpeg_path, .quality = 85},
		context(ProgressOperationType::JpegExport), progress, cancellation);
	REQUIRE(exported_jpeg.succeeded());
	REQUIRE(read_text(jpeg_path) == "SHUBA-FAKE-JPEG\n");
}

TEST_CASE("B10 operation helper integrates gate progress and cancellation",
		  "[b10][platform][operation-gate]") {
	using shuba::core::ManualClock;
	using shuba::core::OperationGate;
	using shuba::core::OperationKind;
	using shuba::core::OperationResultCategory;
	using shuba::platform::ManualCancellationToken;
	using shuba::platform::PlatformOperationStartRequest;
	using shuba::platform::PlatformOperationStartResult;
	using shuba::platform::ProgressCollector;
	using shuba::platform::ProgressOperationType;
	using shuba::platform::ScriptedIdentifierSource;
	using shuba::platform::try_start_platform_operation;

	ManualClock clock{std::chrono::milliseconds{10}};
	REQUIRE(clock.now() == std::chrono::milliseconds{10});

	ScriptedIdentifierSource identifiers;
	identifiers.script_stable_identifier("item-001");
	identifiers.script_operation_identifier("operation-001");
	REQUIRE(identifiers.next_stable_identifier().value() == "item-001");
	const shuba::core::OperationIdentifier first_operation =
		identifiers.next_operation_identifier();
	const shuba::core::OperationIdentifier second_operation =
		operation_id("operation-002");

	OperationGate gate;
	ProgressCollector progress;
	ManualCancellationToken cancellation;
	PlatformOperationStartResult started = try_start_platform_operation(
		gate,
		PlatformOperationStartRequest{
			.operation_kind = OperationKind::BackupImport,
			.operation_id	= first_operation,
			.operation_type = ProgressOperationType::BackupImport},
		progress, cancellation);
	REQUIRE(started.succeeded());
	REQUIRE(gate.is_busy());
	REQUIRE(started.operation.has_value());

	started.operation->publish_progress(
		"stage", std::uint64_t{1}, std::uint64_t{3}, "Staging import.", true);
	REQUIRE(progress.events().size() == 1);
	REQUIRE(progress.events().front().operation_id == first_operation);
	REQUIRE(progress.events().front().operation_type
			== ProgressOperationType::BackupImport);

	cancellation.request_cancellation();
	REQUIRE(started.operation->cancellation_requested());

	PlatformOperationStartResult blocked = try_start_platform_operation(
		gate,
		PlatformOperationStartRequest{
			.operation_kind = OperationKind::BackupExport,
			.operation_id	= second_operation,
			.operation_type = ProgressOperationType::BackupExport},
		progress, cancellation);
	REQUIRE(blocked.failed());
	REQUIRE(blocked.category == OperationResultCategory::ValidationFailure);
	REQUIRE(blocked.diagnostics.front().code == "operation-busy");

	started.operation->release();
	REQUIRE_FALSE(gate.is_busy());
}
