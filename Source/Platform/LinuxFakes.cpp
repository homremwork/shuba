#include "Platform/LinuxFakes.hpp"

#include <array>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace shuba::platform {
namespace {
[[nodiscard]] core::Diagnostic make_diagnostic(
	core::DiagnosticSeverity severity, std::string code, std::string message,
	std::string technical_details = {}) {
	return core::Diagnostic{.severity		   = severity,
							.code			   = std::move(code),
							.message		   = std::move(message),
							.technical_details = std::move(technical_details)};
}

[[nodiscard]] core::OperationResult make_failure(
	core::OperationResultCategory category, std::string code,
	std::string message, std::string technical_details = {}) {
	return core::OperationResult::failure(
		category, make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
								  std::move(code), std::move(message),
								  std::move(technical_details)));
}

[[nodiscard]] core::OperationResult make_action_failure(
	core::OperationResultCategory category, std::string code,
	std::string message, std::string technical_details = {}) {
	return core::OperationResult::failure(
		category,
		make_diagnostic(core::DiagnosticSeverity::ActionValidationError,
						std::move(code), std::move(message),
						std::move(technical_details)));
}

[[nodiscard]] PlatformValueResult<std::uint64_t> copy_local_file(
	const std::filesystem::path& source_path,
	const std::filesystem::path& destination_path,
	const PlatformOperationContext& context, ProgressSink& progress_sink,
	CancellationToken& cancellation_token,
	core::OperationResultCategory destination_failure_category,
	std::string_view destination_failure_code) {
	if (cancellation_token.cancellation_requested())
		return platform_value_user_cancelled<std::uint64_t>();

	std::error_code error;
	std::uint64_t total_size{};
	const std::uintmax_t file_size =
		std::filesystem::file_size(source_path, error);
	std::optional<std::uint64_t> total_units;
	if (!error) {
		total_size	= static_cast<std::uint64_t>(file_size);
		total_units = total_size;
	}

	std::filesystem::create_directories(destination_path.parent_path(), error);
	if (error) {
		return platform_value_failure<std::uint64_t>(
			destination_failure_category,
			make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
							std::string(destination_failure_code),
							"Destination directory could not be created.",
							error.message()));
	}

	std::ifstream input{source_path, std::ios::binary};
	if (!input) {
		return platform_value_failure<std::uint64_t>(
			core::OperationResultCategory::SourceUnavailable,
			make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
							"source-unavailable",
							"Source file could not be opened for reading.",
							source_path.string()));
	}

	std::ofstream output{destination_path, std::ios::binary | std::ios::trunc};
	if (!output) {
		return platform_value_failure<std::uint64_t>(
			destination_failure_category,
			make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
							std::string(destination_failure_code),
							"Destination file could not be opened for writing.",
							destination_path.string()));
	}

	progress_sink.publish_progress(
		ProgressEvent{.operation_id	  = context.operation_id,
					  .operation_type = context.operation_type,
					  .phase		  = "copy-started",
					  .message_id	  = ProgressMessageId::CopyStarted,
					  .current_units  = std::uint64_t{0},
					  .total_units	  = total_units,
					  .message		  = "Copy started.",
					  .cancellable	  = true});

	std::array<char, 4096> buffer{};
	std::uint64_t copied{};
	while (input) {
		if (cancellation_token.cancellation_requested()) {
			output.close();
			std::filesystem::remove(destination_path, error);
			return platform_value_user_cancelled<std::uint64_t>();
		}

		input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
		const std::streamsize bytes_read = input.gcount();
		if (bytes_read <= 0)
			break;

		output.write(buffer.data(), bytes_read);
		if (!output) {
			return platform_value_failure<std::uint64_t>(
				destination_failure_category,
				make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
								std::string(destination_failure_code),
								"Destination write failed.",
								destination_path.string()));
		}

		copied += static_cast<std::uint64_t>(bytes_read);
		progress_sink.publish_progress(
			ProgressEvent{.operation_id	  = context.operation_id,
						  .operation_type = context.operation_type,
						  .phase		  = "copying",
						  .message_id	  = ProgressMessageId::Copying,
						  .current_units  = copied,
						  .total_units	  = total_units,
						  .message		  = "Copying content.",
						  .cancellable	  = true});
	}

	if (input.bad()) {
		return platform_value_failure<std::uint64_t>(
			core::OperationResultCategory::SourceUnavailable,
			make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
							"source-read-failed",
							"Source file could not be read completely.",
							source_path.string()));
	}

	output.flush();
	if (!output) {
		return platform_value_failure<std::uint64_t>(
			destination_failure_category,
			make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
							std::string(destination_failure_code),
							"Destination flush failed.",
							destination_path.string()));
	}

	progress_sink.publish_progress(
		ProgressEvent{.operation_id	  = context.operation_id,
					  .operation_type = context.operation_type,
					  .phase		  = "copy-completed",
					  .message_id	  = ProgressMessageId::CopyCompleted,
					  .current_units  = copied,
					  .total_units	  = total_units,
					  .message		  = "Copy completed.",
					  .cancellable	  = false});

	return platform_value_success<std::uint64_t>(copied);
}

[[nodiscard]] core::OperationResult require_completion_callback(
	const bool has_completion_callback) {
	if (has_completion_callback)
		return core::OperationResult::success();

	return make_action_failure(core::OperationResultCategory::ValidationFailure,
							   "missing-selection-completion",
							   "Selection completion callback is required.");
}

[[nodiscard]] PlatformValueResult<MediaWriteResult> write_marker_file(
	const std::filesystem::path& output_path, std::string_view marker,
	const PlatformOperationContext& context, ProgressSink& progress_sink,
	CancellationToken& cancellation_token) {
	if (cancellation_token.cancellation_requested())
		return platform_value_user_cancelled<MediaWriteResult>();

	std::error_code error;
	std::filesystem::create_directories(output_path.parent_path(), error);
	if (error) {
		return platform_value_failure<MediaWriteResult>(
			core::OperationResultCategory::TemporaryStorageFailure,
			make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
							"media-directory-unavailable",
							"Media output directory could not be created.",
							error.message()));
	}

	std::ofstream output{output_path, std::ios::binary | std::ios::trunc};
	if (!output) {
		return platform_value_failure<MediaWriteResult>(
			core::OperationResultCategory::TemporaryStorageFailure,
			make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
							"media-output-unavailable",
							"Media output file could not be opened.",
							output_path.string()));
	}

	progress_sink.publish_progress(
		ProgressEvent{.operation_id	  = context.operation_id,
					  .operation_type = context.operation_type,
					  .phase		  = "media-write-started",
					  .message_id	  = ProgressMessageId::MediaWriteStarted,
					  .current_units  = std::uint64_t{0},
					  .total_units = static_cast<std::uint64_t>(marker.size()),
					  .message	   = "Media write started.",
					  .cancellable = true});

	output.write(marker.data(), static_cast<std::streamsize>(marker.size()));
	output.flush();
	if (!output) {
		return platform_value_failure<MediaWriteResult>(
			core::OperationResultCategory::TemporaryStorageFailure,
			make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
							"media-write-failed",
							"Media marker file could not be written.",
							output_path.string()));
	}

	progress_sink.publish_progress(ProgressEvent{
		.operation_id	= context.operation_id,
		.operation_type = context.operation_type,
		.phase			= "media-write-completed",
		.message_id		= ProgressMessageId::MediaWriteCompleted,
		.current_units	= static_cast<std::uint64_t>(marker.size()),
		.total_units	= static_cast<std::uint64_t>(marker.size()),
		.message		= "Media write completed.",
		.cancellable	= false});

	return platform_value_success(MediaWriteResult{
		.file_path	   = output_path,
		.bytes_written = static_cast<std::uint64_t>(marker.size())});
}
}	 // namespace

PlatformCapabilityCheck FixedCapabilityChecker::check_capability(
	PlatformCapability capability) const {
	const std::map<PlatformCapability, PlatformCapabilityCheck>::const_iterator
		found = overrides.find(capability);
	if (found != overrides.end())
		return found->second;

	return default_capability_check(capability);
}

void FixedCapabilityChecker::set_capability_state(PlatformCapability capability,
												  PlatformCapabilityState state,
												  std::string message) {
	overrides[capability] = PlatformCapabilityCheck{
		.capability		  = capability,
		.state			  = state,
		.permission_scope = default_permission_scope(capability),
		.message		  = std::move(message)};
}

void FixedCapabilityChecker::clear_override(PlatformCapability capability) {
	overrides.erase(capability);
}

LinuxFakePathProvider::LinuxFakePathProvider(std::filesystem::path root_path)
	: root(std::move(root_path)) {}

PlatformValueResult<AppPrivatePaths>
LinuxFakePathProvider::resolve_app_private_paths() const {
	if (root.empty()) {
		return platform_value_failure<AppPrivatePaths>(
			core::OperationResultCategory::TemporaryStorageFailure,
			make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
							"empty-path-root",
							"Linux fake path root is empty."));
	}

	AppPrivatePaths paths{
		.app_private_root	 = root / "app-private",
		.active_catalog_root = root / "app-private" / "active-catalog",
		.operation_tmp_root	 = root / "app-private" / "operation-tmp",
		.staged_content_root =
			root / "app-private" / "operation-tmp" / "staged-content",
		.export_tmp_root = root / "app-private" / "operation-tmp" / "exports",
		.media_root =
			root / "app-private" / "active-catalog" / "media" / "photos"};

	std::error_code error;
	std::filesystem::create_directories(paths.active_catalog_root, error);
	if (!error)
		std::filesystem::create_directories(paths.operation_tmp_root, error);
	if (!error)
		std::filesystem::create_directories(paths.staged_content_root, error);
	if (!error)
		std::filesystem::create_directories(paths.export_tmp_root, error);
	if (!error)
		std::filesystem::create_directories(paths.media_root, error);

	if (error) {
		return platform_value_failure<AppPrivatePaths>(
			core::OperationResultCategory::TemporaryStorageFailure,
			make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
							"path-root-unavailable",
							"Linux fake path roots could not be created.",
							error.message()));
	}

	return platform_value_success(std::move(paths));
}

const std::filesystem::path& LinuxFakePathProvider::root_path() const noexcept {
	return root;
}

ScriptedIdentifierSource::ScriptedIdentifierSource(std::uint64_t fallback_seed)
	: fallback(fallback_seed) {}

void ScriptedIdentifierSource::script_stable_identifier(std::string value) {
	std::optional<core::StableIdentifier> identifier =
		core::StableIdentifier::try_create_file_safe(std::move(value));
	if (!identifier.has_value())
		throw std::invalid_argument("scripted stable identifier is invalid");

	stable_identifiers.push_back(std::move(*identifier));
}

void ScriptedIdentifierSource::script_operation_identifier(std::string value) {
	std::optional<core::OperationIdentifier> identifier =
		core::OperationIdentifier::try_create_file_safe(std::move(value));
	if (!identifier.has_value())
		throw std::invalid_argument("scripted operation identifier is invalid");

	operation_identifiers.push_back(std::move(*identifier));
}

core::StableIdentifier ScriptedIdentifierSource::next_stable_identifier() {
	if (stable_identifiers.empty())
		return fallback.next_stable_identifier();

	core::StableIdentifier identifier = std::move(stable_identifiers.front());
	stable_identifiers.pop_front();
	return identifier;
}

core::OperationIdentifier
ScriptedIdentifierSource::next_operation_identifier() {
	if (operation_identifiers.empty())
		return fallback.next_operation_identifier();

	core::OperationIdentifier identifier =
		std::move(operation_identifiers.front());
	operation_identifiers.pop_front();
	return identifier;
}

void LinuxFakePhotoSelectionService::script_selection_result(
	PlatformValueResult<std::vector<ContentSourceDescriptor>> result) {
	results.push_back(std::move(result));
}

void LinuxFakePhotoSelectionService::script_selection_success(
	std::vector<ContentSourceDescriptor> sources) {
	script_selection_result(platform_value_success(std::move(sources)));
}

void LinuxFakePhotoSelectionService::script_selection_cancellation() {
	script_selection_result(
		platform_value_user_cancelled<std::vector<ContentSourceDescriptor>>());
}

core::OperationResult LinuxFakePhotoSelectionService::request_photo_selection(
	const PhotoSelectionRequest& request, PhotoSelectionCompletion completion) {
	(void)request;
	core::OperationResult callback_result =
		require_completion_callback(static_cast<bool>(completion));
	if (callback_result.failed())
		return callback_result;

	if (results.empty())
		completion(
			platform_value_success(std::vector<ContentSourceDescriptor>{}));
	else {
		PlatformValueResult<std::vector<ContentSourceDescriptor>> result =
			std::move(results.front());
		results.pop_front();
		completion(std::move(result));
	}

	return core::OperationResult::success();
}

void LinuxFakeDocumentImportService::script_import_result(
	PlatformValueResult<ContentSourceDescriptor> result) {
	results.push_back(std::move(result));
}

void LinuxFakeDocumentImportService::script_import_success(
	ContentSourceDescriptor source) {
	script_import_result(platform_value_success(std::move(source)));
}

void LinuxFakeDocumentImportService::script_import_cancellation() {
	script_import_result(
		platform_value_user_cancelled<ContentSourceDescriptor>());
}

core::OperationResult
LinuxFakeDocumentImportService::request_import_document_selection(
	const DocumentImportRequest& request, DocumentImportCompletion completion) {
	(void)request;
	core::OperationResult callback_result =
		require_completion_callback(static_cast<bool>(completion));
	if (callback_result.failed())
		return callback_result;

	if (results.empty()) {
		completion(platform_value_failure<ContentSourceDescriptor>(
			core::OperationResultCategory::SourceUnavailable,
			make_diagnostic(core::DiagnosticSeverity::ActionValidationError,
							"no-scripted-import-document",
							"No fake import document was scripted.")));
		return core::OperationResult::success();
	}

	PlatformValueResult<ContentSourceDescriptor> result =
		std::move(results.front());
	results.pop_front();
	completion(std::move(result));
	return core::OperationResult::success();
}

void LinuxFakeDocumentExportService::script_export_destination_result(
	PlatformValueResult<DocumentDestinationDescriptor> result) {
	results.push_back(std::move(result));
}

void LinuxFakeDocumentExportService::script_export_destination_success(
	DocumentDestinationDescriptor destination) {
	script_export_destination_result(
		platform_value_success(std::move(destination)));
}

void LinuxFakeDocumentExportService::script_export_destination_cancellation() {
	script_export_destination_result(
		platform_value_user_cancelled<DocumentDestinationDescriptor>());
}

core::OperationResult
LinuxFakeDocumentExportService::request_export_destination_selection(
	const DocumentExportRequest& request,
	DocumentExportDestinationCompletion completion) {
	(void)request;
	core::OperationResult callback_result =
		require_completion_callback(static_cast<bool>(completion));
	if (callback_result.failed())
		return callback_result;

	if (results.empty()) {
		completion(platform_value_failure<DocumentDestinationDescriptor>(
			core::OperationResultCategory::DestinationUnavailable,
			make_diagnostic(core::DiagnosticSeverity::ActionValidationError,
							"no-scripted-export-destination",
							"No fake export destination was scripted.")));
		return core::OperationResult::success();
	}

	PlatformValueResult<DocumentDestinationDescriptor> result =
		std::move(results.front());
	results.pop_front();
	completion(std::move(result));
	return core::OperationResult::success();
}

core::OperationResult LinuxFakeDocumentExportService::copy_file_to_destination(
	const DocumentCopyRequest& request, const PlatformOperationContext& context,
	ProgressSink& progress_sink, CancellationToken& cancellation_token) {
	if (request.destination.kind != PlatformContentHandleKind::LocalFile) {
		return make_failure(
			core::OperationResultCategory::Unsupported,
			"unsupported-destination-handle",
			"Linux fake document export supports local destinations only.");
	}

	PlatformValueResult<std::uint64_t> copy_result = copy_local_file(
		request.temp_source_path, request.destination.local_path, context,
		progress_sink, cancellation_token,
		core::OperationResultCategory::DestinationUnavailable,
		"destination-unavailable");
	if (copy_result.succeeded())
		return core::OperationResult::success();
	if (copy_result.was_user_cancelled())
		return core::OperationResult::user_cancelled();

	return core::OperationResult::failure(
		copy_result.category, std::move(copy_result.diagnostics.front()));
}

PlatformValueResult<StagedContent>
LinuxFakeContentStagingService::stage_content(
	const ContentStagingRequest& request,
	const PlatformOperationContext& context, ProgressSink& progress_sink,
	CancellationToken& cancellation_token) {
	if (request.source.kind != PlatformContentHandleKind::LocalFile) {
		return platform_value_failure<StagedContent>(
			core::OperationResultCategory::Unsupported,
			make_diagnostic(
				core::DiagnosticSeverity::WriteBlockingError,
				"unsupported-source-handle",
				"Linux fake staging supports local file sources only."));
	}

	if (request.allow_no_copy_optimization) {
		return platform_value_success(
			StagedContent{.staged_path	= request.source.local_path,
						  .display_name = request.source.display_name,
						  .byte_count	= request.source.byte_count});
	}

	const std::filesystem::path target_path =
		request.target_directory / request.target_file_name;
	PlatformValueResult<std::uint64_t> copy_result =
		copy_local_file(request.source.local_path, target_path, context,
						progress_sink, cancellation_token,
						core::OperationResultCategory::TemporaryStorageFailure,
						"temporary-storage-failure");
	if (copy_result.succeeded()) {
		return platform_value_success(
			StagedContent{.staged_path	= target_path,
						  .display_name = request.source.display_name,
						  .byte_count	= copy_result.value});
	}
	if (copy_result.was_user_cancelled())
		return platform_value_user_cancelled<StagedContent>();

	return platform_value_failure<StagedContent>(
		copy_result.category, std::move(copy_result.diagnostics.front()));
}

void SyntheticSourceImageDecodeService::set_decoded_pixels(ImagePixels pixels) {
	decoded_pixels = std::move(pixels);
}

void SyntheticSourceImageDecodeService::clear_decoded_pixels() {
	decoded_pixels.reset();
}

PlatformValueResult<ImagePixels>
SyntheticSourceImageDecodeService::decode_source_image(
	const SourceImageDecodeRequest& request,
	const PlatformOperationContext& context, ProgressSink& progress_sink,
	CancellationToken& cancellation_token) {
	if (cancellation_token.cancellation_requested())
		return platform_value_user_cancelled<ImagePixels>();

	progress_sink.publish_progress(ProgressEvent{
		.operation_id	= context.operation_id,
		.operation_type = context.operation_type,
		.phase			= "decode-started",
		.message_id		= ProgressMessageId::SyntheticSourceDecodeStarted,
		.current_units	= std::uint64_t{0},
		.total_units	= std::uint64_t{1},
		.message		= "Source image decode started.",
		.cancellable	= true});

	if (!decoded_pixels.has_value()) {
		return platform_value_failure<ImagePixels>(
			core::OperationResultCategory::CodecFailure,
			make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
							"synthetic-decode-not-scripted",
							"No synthetic decode pixels were scripted.",
							request.content.staged_path.string()));
	}

	ImagePixels pixels = *decoded_pixels;
	progress_sink.publish_progress(ProgressEvent{
		.operation_id	= context.operation_id,
		.operation_type = context.operation_type,
		.phase			= "decode-completed",
		.message_id		= ProgressMessageId::SyntheticSourceDecodeCompleted,
		.current_units	= std::uint64_t{1},
		.total_units	= std::uint64_t{1},
		.message		= "Source image decode completed.",
		.cancellable	= false});

	return platform_value_success(std::move(pixels));
}

PlatformValueResult<MediaWriteResult>
MarkerInternalPhotoCodec::encode_internal_photo(
	const InternalPhotoEncodeRequest& request,
	const PlatformOperationContext& context, ProgressSink& progress_sink,
	CancellationToken& cancellation_token) {
	PlatformValueResult<MediaWriteResult> result =
		write_marker_file(request.output_path, "SHUBA-FAKE-JXL\n", context,
						  progress_sink, cancellation_token);
	if (result.succeeded())
		encoded_pixels_by_path[request.output_path] = request.pixels;

	return result;
}

PlatformValueResult<ImagePixels>
MarkerInternalPhotoCodec::decode_internal_photo(
	const InternalPhotoDecodeRequest& request,
	const PlatformOperationContext& context, ProgressSink& progress_sink,
	CancellationToken& cancellation_token) {
	if (cancellation_token.cancellation_requested())
		return platform_value_user_cancelled<ImagePixels>();

	progress_sink.publish_progress(ProgressEvent{
		.operation_id	= context.operation_id,
		.operation_type = context.operation_type,
		.phase			= "internal-decode-started",
		.message_id		= ProgressMessageId::InternalPhotoDecodeStarted,
		.current_units	= std::uint64_t{0},
		.total_units	= std::uint64_t{1},
		.message		= "Internal photo decode started.",
		.cancellable	= true});

	const std::map<std::filesystem::path, ImagePixels>::const_iterator found =
		encoded_pixels_by_path.find(request.input_path);
	if (found == encoded_pixels_by_path.end()) {
		return platform_value_failure<ImagePixels>(
			core::OperationResultCategory::CodecFailure,
			make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
							"fake-internal-photo-missing",
							"Fake internal photo pixels are unavailable.",
							request.input_path.string()));
	}

	progress_sink.publish_progress(ProgressEvent{
		.operation_id	= context.operation_id,
		.operation_type = context.operation_type,
		.phase			= "internal-decode-completed",
		.message_id		= ProgressMessageId::InternalPhotoDecodeCompleted,
		.current_units	= std::uint64_t{1},
		.total_units	= std::uint64_t{1},
		.message		= "Internal photo decode completed.",
		.cancellable	= false});

	return platform_value_success(found->second);
}

PlatformValueResult<MediaWriteResult> MarkerJpegExportService::write_jpeg(
	const JpegExportRequest& request, const PlatformOperationContext& context,
	ProgressSink& progress_sink, CancellationToken& cancellation_token) {
	(void)request;
	return write_marker_file(request.output_path, "SHUBA-FAKE-JPEG\n", context,
							 progress_sink, cancellation_token);
}
}	 // namespace shuba::platform
