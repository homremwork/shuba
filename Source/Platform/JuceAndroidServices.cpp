#include "Platform/JuceAndroidServices.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#if JUCE_ANDROID && __has_include(<android/imagedecoder.h>)
#include <android/bitmap.h>
#include <android/imagedecoder.h>
#include <fcntl.h>
#include <unistd.h>
#define SHUBA_ANDROID_IMAGE_DECODER_AVAILABLE 1
#else
#define SHUBA_ANDROID_IMAGE_DECODER_AVAILABLE 0
#endif

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace shuba::platform {
namespace {
constexpr std::size_t copy_buffer_size = 32768U;

[[nodiscard]] core::Diagnostic make_diagnostic(
	core::DiagnosticSeverity severity, std::string code, std::string message,
	std::string technical_details = {}) {
	return core::Diagnostic{.severity		   = severity,
							.code			   = std::move(code),
							.message		   = std::move(message),
							.technical_details = std::move(technical_details)};
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

[[nodiscard]] core::OperationResult make_write_failure(
	core::OperationResultCategory category, std::string code,
	std::string message, std::string technical_details = {}) {
	return core::OperationResult::failure(
		category, make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
								  std::move(code), std::move(message),
								  std::move(technical_details)));
}

[[nodiscard]] core::OperationResult require_completion_callback(
	const bool has_completion_callback) {
	if (has_completion_callback)
		return core::OperationResult::success();

	return make_action_failure(core::OperationResultCategory::ValidationFailure,
							   "missing-selection-completion",
							   "Selection completion callback is required.");
}

[[nodiscard]] core::OperationResult require_no_active_chooser(
	const bool has_active_chooser) {
	if (!has_active_chooser)
		return core::OperationResult::success();

	return make_action_failure(
		core::OperationResultCategory::ValidationFailure, "android-picker-busy",
		"Another Android picker request is already active.");
}

[[nodiscard]] std::filesystem::path filesystem_path_from_file(
	const juce::File& file) {
	return std::filesystem::path{file.getFullPathName().toStdString()};
}

[[nodiscard]] juce::File file_from_path(const std::filesystem::path& path) {
	return juce::File{juce::String{path.string()}};
}

[[nodiscard]] PlatformValueResult<AppPrivatePaths> path_failure(
	std::string code, std::string message, juce::Result result) {
	return platform_value_failure<AppPrivatePaths>(
		core::OperationResultCategory::TemporaryStorageFailure,
		make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
						std::move(code), std::move(message),
						result.getErrorMessage().toStdString()));
}

[[nodiscard]] PlatformValueResult<AppPrivatePaths> ensure_directory(
	const juce::File& directory, std::string code) {
	juce::Result result = directory.createDirectory();
	if (result.wasOk())
		return PlatformValueResult<AppPrivatePaths>{};

	return path_failure(std::move(code),
						"Android app-private directory could not be created.",
						std::move(result));
}

[[nodiscard]] std::optional<std::uint64_t> document_size(
	const juce::AndroidDocumentInfo& info) {
	if (!info.isSizeInBytesValid())
		return std::nullopt;
	if (info.getSizeInBytes() < 0)
		return std::nullopt;

	return static_cast<std::uint64_t>(info.getSizeInBytes());
}

[[nodiscard]] juce::URL url_from_opaque_handle(std::string_view handle) {
	return juce::URL::createWithoutParsing(juce::String{std::string{handle}});
}

[[nodiscard]] std::string display_name_from_url(const juce::URL& url) {
	const juce::String file_name = url.getFileName();
	return file_name.toStdString();
}

[[nodiscard]] ContentSourceDescriptor shallow_source_descriptor_from_url(
	const juce::URL& url) {
	if (url.isLocalFile())
		return make_local_file_source(
			filesystem_path_from_file(url.getLocalFile()),
			display_name_from_url(url));

	std::string display_name = display_name_from_url(url);
	if (display_name.empty())
		display_name = "selected-photo";
	return make_opaque_content_source(url.toString(true).toStdString(),
									  std::move(display_name), std::nullopt,
									  true);
}

[[nodiscard]] DocumentDestinationDescriptor destination_descriptor_from_url(
	const juce::URL& url) {
	if (url.isLocalFile())
		return make_local_file_destination(
			filesystem_path_from_file(url.getLocalFile()),
			display_name_from_url(url));

	std::string display_name = display_name_from_url(url);
	const juce::AndroidDocument document =
		juce::AndroidDocument::fromDocument(url);
	if (document.hasValue()) {
		const juce::AndroidDocumentInfo info = document.getInfo();
		if (info.getName().isNotEmpty())
			display_name = info.getName().toStdString();
	}

	return make_opaque_document_destination(url.toString(true).toStdString(),
											std::move(display_name));
}

[[nodiscard]] ContentSourceDescriptor
document_import_source_descriptor_from_url(const juce::URL& url) {
	if (url.isLocalFile())
		return make_local_file_source(
			filesystem_path_from_file(url.getLocalFile()),
			display_name_from_url(url));

	std::string display_name = display_name_from_url(url);
	std::optional<std::uint64_t> byte_count;
	const juce::AndroidDocument document =
		juce::AndroidDocument::fromDocument(url);
	if (document.hasValue()) {
		const juce::AndroidDocumentInfo info = document.getInfo();
		if (info.getName().isNotEmpty())
			display_name = info.getName().toStdString();
		byte_count = document_size(info);
	}

	return make_opaque_content_source(url.toString(true).toStdString(),
									  std::move(display_name), byte_count,
									  true);
}

[[nodiscard]] PlatformValueResult<std::uint64_t> copy_stream(
	juce::InputStream& input, juce::OutputStream& output,
	const std::optional<std::uint64_t> total_units,
	const PlatformOperationContext& context, ProgressSink& progress_sink,
	CancellationToken& cancellation_token,
	core::OperationResultCategory destination_failure_category,
	std::string destination_failure_code) {
	if (cancellation_token.cancellation_requested())
		return platform_value_user_cancelled<std::uint64_t>();

	progress_sink.publish_progress(
		ProgressEvent{.operation_id	  = context.operation_id,
					  .operation_type = context.operation_type,
					  .phase		  = "copy-started",
					  .message_id	  = ProgressMessageId::CopyStarted,
					  .current_units  = std::uint64_t{0},
					  .total_units	  = total_units,
					  .message		  = "Copy started.",
					  .cancellable	  = true});

	std::array<char, copy_buffer_size> buffer{};
	std::uint64_t copied{};
	while (!input.isExhausted()) {
		if (cancellation_token.cancellation_requested())
			return platform_value_user_cancelled<std::uint64_t>();

		const int bytes_read =
			input.read(buffer.data(), static_cast<int>(buffer.size()));
		if (bytes_read <= 0)
			break;

		if (!output.write(buffer.data(),
						  static_cast<std::size_t>(bytes_read))) {
			return platform_value_failure<std::uint64_t>(
				destination_failure_category,
				make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
								std::move(destination_failure_code),
								"Destination write failed."));
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

	output.flush();
	progress_sink.publish_progress(
		ProgressEvent{.operation_id	  = context.operation_id,
					  .operation_type = context.operation_type,
					  .phase		  = "copy-completed",
					  .message_id	  = ProgressMessageId::CopyCompleted,
					  .current_units  = copied,
					  .total_units	  = total_units,
					  .message		  = "Copy completed.",
					  .cancellable	  = false});

	return platform_value_success(copied);
}

struct OpenInputResult final {
	PlatformValueResult<std::uint64_t> status;
	std::unique_ptr<juce::InputStream> stream;
	std::optional<std::uint64_t> total_units;
	std::string display_name;
};

[[nodiscard]] std::uint64_t elapsed_milliseconds_since(
	std::chrono::steady_clock::time_point started_at) {
	const std::chrono::steady_clock::duration elapsed =
		std::chrono::steady_clock::now() - started_at;
	return static_cast<std::uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

[[nodiscard]] OpenInputResult open_input_stream(
	const ContentSourceDescriptor& source) {
	std::unique_ptr<juce::InputStream> stream;
	std::optional<std::uint64_t> total_units = source.byte_count;
	std::string display_name				 = source.display_name;

	if (source.kind == PlatformContentHandleKind::LocalFile) {
		stream = file_from_path(source.local_path).createInputStream();
	} else {
		const juce::URL url = url_from_opaque_handle(source.opaque_handle);
		const juce::AndroidDocument document =
			juce::AndroidDocument::fromDocument(url);
		if (document.hasValue()) {
			const juce::AndroidDocumentInfo info = document.getInfo();
			if (info.getName().isNotEmpty())
				display_name = info.getName().toStdString();
			if (!total_units.has_value())
				total_units = document_size(info);
			stream = document.createInputStream();
		}

		if (stream == nullptr) {
			stream = url.createInputStream(juce::URL::InputStreamOptions(
				juce::URL::ParameterHandling::inAddress));
		}
	}

	if (stream == nullptr) {
		return OpenInputResult{
			.status = platform_value_failure<std::uint64_t>(
				core::OperationResultCategory::SourceUnavailable,
				make_diagnostic(
					core::DiagnosticSeverity::WriteBlockingError,
					"source-unavailable",
					"Source content could not be opened for reading."))};
	}

	const std::int64_t stream_length = stream->getTotalLength();
	if (!total_units.has_value() && stream_length >= 0)
		total_units = static_cast<std::uint64_t>(stream_length);

	return OpenInputResult{.status = platform_value_success(std::uint64_t{0}),
						   .stream = std::move(stream),
						   .total_units	 = total_units,
						   .display_name = std::move(display_name)};
}

[[nodiscard]] std::unique_ptr<juce::OutputStream> open_output_stream(
	const DocumentDestinationDescriptor& destination) {
	if (destination.kind == PlatformContentHandleKind::LocalFile) {
		std::unique_ptr<juce::FileOutputStream> output =
			file_from_path(destination.local_path).createOutputStream();
		if (output != nullptr && output->openedOk()) {
			output->setPosition(0);
			output->truncate();
			return output;
		}

		return nullptr;
	}

	const juce::URL url = url_from_opaque_handle(destination.opaque_handle);
	const juce::AndroidDocument document =
		juce::AndroidDocument::fromDocument(url);
	if (document.hasValue()) {
		std::unique_ptr<juce::OutputStream> output =
			document.createOutputStream();
		if (output != nullptr)
			return output;
	}

	return url.createOutputStream();
}

#if SHUBA_ANDROID_IMAGE_DECODER_AVAILABLE
class NativeFileDescriptor final {
public:
	explicit NativeFileDescriptor(int descriptor) noexcept
		: value(descriptor) {}
	NativeFileDescriptor(const NativeFileDescriptor&)			 = delete;
	NativeFileDescriptor& operator=(const NativeFileDescriptor&) = delete;
	NativeFileDescriptor(NativeFileDescriptor&& other) noexcept
		: value(other.value) {
		other.value = -1;
	}
	NativeFileDescriptor& operator=(NativeFileDescriptor&& other) noexcept {
		if (this != &other) {
			close_if_needed();
			value		= other.value;
			other.value = -1;
		}
		return *this;
	}
	~NativeFileDescriptor() { close_if_needed(); }

	[[nodiscard]] int get() const noexcept { return value; }
	[[nodiscard]] bool valid() const noexcept { return value >= 0; }

private:
	void close_if_needed() noexcept {
		if (value >= 0)
			::close(value);
		value = -1;
	}

	int value{-1};
};

struct NativeImageDecoderDeleter final {
	void operator()(AImageDecoder* decoder) const noexcept {
		AImageDecoder_delete(decoder);
	}
};

using NativeImageDecoderPtr =
	std::unique_ptr<AImageDecoder, NativeImageDecoderDeleter>;

[[nodiscard]] std::string image_decoder_result_text(int result) {
	const char* text = AImageDecoder_resultToString(result);
	if (text == nullptr)
		return std::to_string(result);

	return text;
}
#endif
}	 // namespace
PlatformValueResult<AppPrivatePaths>
JuceAndroidPathProvider::resolve_app_private_paths() const {
	const juce::File app_private_root =
		juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
			.getChildFile("shuba-catalog");
	const juce::File active_catalog_root =
		app_private_root.getChildFile("active-catalog");
	const juce::File operation_tmp_root =
		app_private_root.getChildFile("operation-tmp");
	const juce::File staged_content_root =
		operation_tmp_root.getChildFile("staged-content");
	const juce::File export_tmp_root =
		operation_tmp_root.getChildFile("exports");
	const juce::File media_root =
		active_catalog_root.getChildFile("media").getChildFile("photos");

	const std::array<std::pair<juce::File, std::string>, 5> directories{{
		{active_catalog_root, "active-catalog-root-unavailable"},
		{operation_tmp_root, "operation-tmp-root-unavailable"},
		{staged_content_root, "staged-content-root-unavailable"},
		{export_tmp_root, "export-tmp-root-unavailable"},
		{media_root, "media-root-unavailable"},
	}};
	for (const std::pair<juce::File, std::string>& directory : directories) {
		PlatformValueResult<AppPrivatePaths> created =
			ensure_directory(directory.first, directory.second);
		if (created.failed())
			return created;
	}

	return platform_value_success(AppPrivatePaths{
		.app_private_root	 = filesystem_path_from_file(app_private_root),
		.active_catalog_root = filesystem_path_from_file(active_catalog_root),
		.operation_tmp_root	 = filesystem_path_from_file(operation_tmp_root),
		.staged_content_root = filesystem_path_from_file(staged_content_root),
		.export_tmp_root	 = filesystem_path_from_file(export_tmp_root),
		.media_root			 = filesystem_path_from_file(media_root)});
}

struct JuceAndroidPhotoSelectionService::ActiveChooser final {
	std::unique_ptr<juce::FileChooser> chooser;
};

JuceAndroidPhotoSelectionService::JuceAndroidPhotoSelectionService() = default;
JuceAndroidPhotoSelectionService::JuceAndroidPhotoSelectionService(
	JuceAndroidPhotoSelectionService&&) noexcept = default;
JuceAndroidPhotoSelectionService& JuceAndroidPhotoSelectionService::operator=(
	JuceAndroidPhotoSelectionService&&) noexcept					  = default;
JuceAndroidPhotoSelectionService::~JuceAndroidPhotoSelectionService() = default;

void JuceAndroidPhotoSelectionService::
	register_supported_source_image_mime_mappings() {
#if JUCE_ANDROID
	static std::once_flag registration_once;
	std::call_once(registration_once, [] {
		for (const SourceImageMimeMapping& mapping :
			 supported_source_image_mime_mappings()) {
			juce::FileChooser::registerCustomMimeTypeForFileExtension(
				juce::String{mapping.mime_type.data(),
							 mapping.mime_type.size()},
				juce::String{mapping.file_extension.data(),
							 mapping.file_extension.size()});
		}
	});
#endif
}

core::OperationResult JuceAndroidPhotoSelectionService::request_photo_selection(
	const PhotoSelectionRequest& request, PhotoSelectionCompletion completion) {
	core::OperationResult callback_result =
		require_completion_callback(static_cast<bool>(completion));
	if (callback_result.failed())
		return callback_result;

	core::OperationResult busy_result =
		require_no_active_chooser(active_chooser != nullptr);
	if (busy_result.failed())
		return busy_result;

	register_supported_source_image_mime_mappings();
	active_chooser			= std::make_unique<ActiveChooser>();
	active_chooser->chooser = std::make_unique<juce::FileChooser>(
		"Select photos", juce::File{},
		juce::String{file_patterns_for_mime_types(request.accepted_mime_types)},
		true);

	int flags = juce::FileBrowserComponent::openMode
				| juce::FileBrowserComponent::canSelectFiles;
	if (request.allow_multiple)
		flags |= juce::FileBrowserComponent::canSelectMultipleItems;

	active_chooser->chooser->launchAsync(
		flags, [this, completion = std::move(completion)](
				   const juce::FileChooser& chooser) mutable {
		const juce::Array<juce::URL> urls = chooser.getURLResults();
		std::unique_ptr<ActiveChooser> finished_chooser =
			std::move(active_chooser);
		if (urls.isEmpty()) {
			completion(platform_value_user_cancelled<
					   std::vector<ContentSourceDescriptor>>());
			return;
		}

		std::vector<ContentSourceDescriptor> descriptors;
		descriptors.reserve(static_cast<std::size_t>(urls.size()));
		for (const juce::URL& url : urls)
			descriptors.push_back(shallow_source_descriptor_from_url(url));

		completion(platform_value_success(std::move(descriptors)));
	});

	return core::OperationResult::success();
}

struct JuceAndroidDocumentImportService::ActiveChooser final {
	std::unique_ptr<juce::FileChooser> chooser;
};

JuceAndroidDocumentImportService::JuceAndroidDocumentImportService() = default;
JuceAndroidDocumentImportService::JuceAndroidDocumentImportService(
	JuceAndroidDocumentImportService&&) noexcept = default;
JuceAndroidDocumentImportService& JuceAndroidDocumentImportService::operator=(
	JuceAndroidDocumentImportService&&) noexcept					  = default;
JuceAndroidDocumentImportService::~JuceAndroidDocumentImportService() = default;

core::OperationResult
JuceAndroidDocumentImportService::request_import_document_selection(
	const DocumentImportRequest& request, DocumentImportCompletion completion) {
	core::OperationResult callback_result =
		require_completion_callback(static_cast<bool>(completion));
	if (callback_result.failed())
		return callback_result;

	core::OperationResult busy_result =
		require_no_active_chooser(active_chooser != nullptr);
	if (busy_result.failed())
		return busy_result;

	active_chooser			= std::make_unique<ActiveChooser>();
	active_chooser->chooser = std::make_unique<juce::FileChooser>(
		juce::String{"Select document for " + request.purpose}, juce::File{},
		juce::String{file_patterns_for_mime_types(request.accepted_mime_types)},
		true);
	active_chooser->chooser->launchAsync(
		juce::FileBrowserComponent::openMode
			| juce::FileBrowserComponent::canSelectFiles,
		[this, completion = std::move(completion)](
			const juce::FileChooser& chooser) mutable {
		const juce::Array<juce::URL> urls = chooser.getURLResults();
		std::unique_ptr<ActiveChooser> finished_chooser =
			std::move(active_chooser);
		if (urls.isEmpty()) {
			completion(
				platform_value_user_cancelled<ContentSourceDescriptor>());
			return;
		}

		completion(platform_value_success(
			document_import_source_descriptor_from_url(urls[0])));
	});

	return core::OperationResult::success();
}

struct JuceAndroidDocumentExportService::ActiveChooser final {
	std::unique_ptr<juce::FileChooser> chooser;
};

JuceAndroidDocumentExportService::JuceAndroidDocumentExportService() = default;
JuceAndroidDocumentExportService::JuceAndroidDocumentExportService(
	JuceAndroidDocumentExportService&&) noexcept = default;
JuceAndroidDocumentExportService& JuceAndroidDocumentExportService::operator=(
	JuceAndroidDocumentExportService&&) noexcept					  = default;
JuceAndroidDocumentExportService::~JuceAndroidDocumentExportService() = default;

core::OperationResult
JuceAndroidDocumentExportService::request_export_destination_selection(
	const DocumentExportRequest& request,
	DocumentExportDestinationCompletion completion) {
	core::OperationResult callback_result =
		require_completion_callback(static_cast<bool>(completion));
	if (callback_result.failed())
		return callback_result;

	core::OperationResult busy_result =
		require_no_active_chooser(active_chooser != nullptr);
	if (busy_result.failed())
		return busy_result;

	const std::string suggested_file_name = sanitize_platform_file_name(
		request.suggested_file_name, "shuba-export");
	active_chooser			= std::make_unique<ActiveChooser>();
	active_chooser->chooser = std::make_unique<juce::FileChooser>(
		juce::String{"Create document for " + request.purpose},
		juce::File{juce::String{suggested_file_name}},
		juce::String{file_patterns_for_mime_types({request.mime_type})}, true);
	active_chooser->chooser->launchAsync(
		juce::FileBrowserComponent::saveMode
			| juce::FileBrowserComponent::canSelectFiles,
		[this, completion = std::move(completion)](
			const juce::FileChooser& chooser) mutable {
		const juce::Array<juce::URL> urls = chooser.getURLResults();
		std::unique_ptr<ActiveChooser> finished_chooser =
			std::move(active_chooser);
		if (urls.isEmpty()) {
			completion(
				platform_value_user_cancelled<DocumentDestinationDescriptor>());
			return;
		}

		completion(
			platform_value_success(destination_descriptor_from_url(urls[0])));
	});

	return core::OperationResult::success();
}

core::OperationResult
JuceAndroidDocumentExportService::copy_file_to_destination(
	const DocumentCopyRequest& request, const PlatformOperationContext& context,
	ProgressSink& progress_sink, CancellationToken& cancellation_token) {
	ContentSourceDescriptor source = make_local_file_source(
		request.temp_source_path, request.temp_source_path.filename().string());
	OpenInputResult input = open_input_stream(source);
	if (input.stream == nullptr)
		return core::OperationResult::failure(
			input.status.category, std::move(input.status.diagnostics.front()));

	std::unique_ptr<juce::OutputStream> output =
		open_output_stream(request.destination);
	if (output == nullptr) {
		return make_write_failure(
			core::OperationResultCategory::DestinationUnavailable,
			"destination-unavailable",
			"Destination document could not be opened for writing.");
	}

	PlatformValueResult<std::uint64_t> copy_result =
		copy_stream(*input.stream, *output, input.total_units, context,
					progress_sink, cancellation_token,
					core::OperationResultCategory::DestinationUnavailable,
					"destination-write-failed");
	if (copy_result.succeeded())
		return core::OperationResult::success();
	if (copy_result.was_user_cancelled())
		return core::OperationResult::user_cancelled();

	return core::OperationResult::failure(
		copy_result.category, std::move(copy_result.diagnostics.front()));
}

PlatformValueResult<StagedContent>
JuceAndroidContentStagingService::stage_content(
	const ContentStagingRequest& request,
	const PlatformOperationContext& context, ProgressSink& progress_sink,
	CancellationToken& cancellation_token) {
	if (request.target_directory.empty()) {
		return platform_value_failure<StagedContent>(
			core::OperationResultCategory::ValidationFailure,
			make_diagnostic(core::DiagnosticSeverity::ActionValidationError,
							"empty-staging-directory",
							"Staging target directory is required."));
	}

	OpenInputResult input = open_input_stream(request.source);
	if (input.stream == nullptr) {
		return platform_value_failure<StagedContent>(
			input.status.category, std::move(input.status.diagnostics.front()));
	}

	const std::string safe_target_file_name =
		sanitize_platform_file_name(request.target_file_name, "staged-content");
	const std::filesystem::path target_path =
		request.target_directory / safe_target_file_name;
	const juce::File target_file = file_from_path(target_path);
	juce::Result directory_result =
		target_file.getParentDirectory().createDirectory();
	if (directory_result.failed()) {
		return platform_value_failure<StagedContent>(
			core::OperationResultCategory::TemporaryStorageFailure,
			make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
							"staging-directory-unavailable",
							"Staging directory could not be created.",
							directory_result.getErrorMessage().toStdString()));
	}

	std::unique_ptr<juce::FileOutputStream> output =
		target_file.createOutputStream();
	if (output == nullptr || output->failedToOpen()) {
		return platform_value_failure<StagedContent>(
			core::OperationResultCategory::TemporaryStorageFailure,
			make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
							"staging-output-unavailable",
							"Staging output file could not be opened.",
							target_path.string()));
	}
	output->setPosition(0);
	output->truncate();

	PlatformValueResult<std::uint64_t> copy_result =
		copy_stream(*input.stream, *output, input.total_units, context,
					progress_sink, cancellation_token,
					core::OperationResultCategory::TemporaryStorageFailure,
					"staging-write-failed");
	if (copy_result.was_user_cancelled()) {
		output.reset();
		target_file.deleteFile();
		return platform_value_user_cancelled<StagedContent>();
	}
	if (copy_result.failed()) {
		output.reset();
		target_file.deleteFile();
		return platform_value_failure<StagedContent>(
			copy_result.category, std::move(copy_result.diagnostics.front()));
	}

	return platform_value_success(
		StagedContent{.staged_path	= target_path,
					  .display_name = std::move(input.display_name),
					  .byte_count	= copy_result.value});
}

PlatformValueResult<ImagePixels>
JuceAndroidSourceImageDecodeService::decode_source_image(
	const SourceImageDecodeRequest& request,
	const PlatformOperationContext& context, ProgressSink& progress_sink,
	CancellationToken& cancellation_token) {
#if SHUBA_ANDROID_IMAGE_DECODER_AVAILABLE
	if (cancellation_token.cancellation_requested())
		return platform_value_user_cancelled<ImagePixels>();

	if (request.content.staged_path.empty()) {
		return platform_value_failure<ImagePixels>(
			core::OperationResultCategory::ValidationFailure,
			make_diagnostic(core::DiagnosticSeverity::ActionValidationError,
							"empty-staged-source-path",
							"Staged source image path is required."));
	}

	const std::chrono::steady_clock::time_point started_at =
		std::chrono::steady_clock::now();
	progress_sink.publish_progress(
		ProgressEvent{.operation_id	  = context.operation_id,
					  .operation_type = context.operation_type,
					  .phase		  = "source-decode-started",
					  .message_id	  = ProgressMessageId::SourceDecodeStarted,
					  .current_units  = std::uint64_t{0},
					  .message		  = "Source image decode started.",
					  .cancellable	  = true});

	const std::string staged_path_text = request.content.staged_path.string();
	NativeFileDescriptor descriptor{
		::open(staged_path_text.c_str(), O_RDONLY | O_CLOEXEC)};
	if (!descriptor.valid()) {
		return platform_value_failure<ImagePixels>(
			core::OperationResultCategory::SourceUnavailable,
			make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
							"staged-source-unavailable",
							"Staged source image could not be opened.",
							std::strerror(errno)));
	}

	AImageDecoder* raw_decoder{};
	int result = AImageDecoder_createFromFd(descriptor.get(), &raw_decoder);
	if (result != ANDROID_IMAGE_DECODER_SUCCESS) {
		return platform_value_failure<ImagePixels>(
			core::OperationResultCategory::CodecFailure,
			make_diagnostic(
				core::DiagnosticSeverity::WriteBlockingError,
				"source-decoder-create-failed",
				"Android source image decoder could not read the staged file.",
				image_decoder_result_text(result)));
	}
	NativeImageDecoderPtr decoder{raw_decoder};

	result = AImageDecoder_setAndroidBitmapFormat(
		decoder.get(), ANDROID_BITMAP_FORMAT_RGBA_8888);
	if (result != ANDROID_IMAGE_DECODER_SUCCESS) {
		return platform_value_failure<ImagePixels>(
			core::OperationResultCategory::CodecFailure,
			make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
							"source-decoder-format-failed",
							"Android source image decoder could not select "
							"RGBA8888 output.",
							image_decoder_result_text(result)));
	}

	result = AImageDecoder_setUnpremultipliedRequired(decoder.get(), true);
	if (result != ANDROID_IMAGE_DECODER_SUCCESS) {
		return platform_value_failure<ImagePixels>(
			core::OperationResultCategory::CodecFailure,
			make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
							"source-decoder-unpremultiplied-failed",
							"Android source image decoder could not select "
							"unpremultiplied RGBA8888 output.",
							image_decoder_result_text(result)));
	}

	const AImageDecoderHeaderInfo* header =
		AImageDecoder_getHeaderInfo(decoder.get());
	const int32_t width	 = AImageDecoderHeaderInfo_getWidth(header);
	const int32_t height = AImageDecoderHeaderInfo_getHeight(header);
	if (width <= 0 || height <= 0) {
		return platform_value_failure<ImagePixels>(
			core::OperationResultCategory::CodecFailure,
			make_diagnostic(
				core::DiagnosticSeverity::WriteBlockingError,
				"source-decoder-empty-dimensions",
				"Android source image decoder reported empty dimensions."));
	}

	const std::uint32_t decoded_width  = static_cast<std::uint32_t>(width);
	const std::uint32_t decoded_height = static_cast<std::uint32_t>(height);
	std::uint32_t output_width		   = decoded_width;
	std::uint32_t output_height		   = decoded_height;
	if (request.sizing.has_value()) {
		const std::optional<SourceImageDecodeTargetSize> target_size =
			source_image_decode_target_size(decoded_width, decoded_height,
											*request.sizing);
		if (!target_size.has_value()) {
			return platform_value_failure<ImagePixels>(
				core::OperationResultCategory::ValidationFailure,
				make_diagnostic(core::DiagnosticSeverity::ActionValidationError,
								"source-decode-sizing-invalid",
								"Source image decode sizing is invalid or "
								"exceeds the supported "
								"pixel-buffer range."));
		}

		if (target_size->width != decoded_width
			|| target_size->height != decoded_height) {
			result = AImageDecoder_setTargetSize(
				decoder.get(), static_cast<int32_t>(target_size->width),
				static_cast<int32_t>(target_size->height));
			if (result != ANDROID_IMAGE_DECODER_SUCCESS) {
				return platform_value_failure<ImagePixels>(
					core::OperationResultCategory::CodecFailure,
					make_diagnostic(
						core::DiagnosticSeverity::WriteBlockingError,
						"source-decoder-target-size-failed",
						"Android source image decoder could not select the "
						"requested bounded output size.",
						image_decoder_result_text(result)));
			}
		}
		output_width  = target_size->width;
		output_height = target_size->height;
	}

	std::optional<std::uint64_t> compact_byte_count =
		image_pixel_byte_count(output_width, output_height, PixelFormat::Rgba8);
	if (!compact_byte_count.has_value()
		|| *compact_byte_count > static_cast<std::uint64_t>(
			   std::numeric_limits<std::size_t>::max())) {
		return platform_value_failure<ImagePixels>(
			core::OperationResultCategory::CodecFailure,
			make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
							"source-decoder-image-too-large",
							"Decoded source image pixel buffer is too large."));
	}

	const std::size_t minimum_stride =
		AImageDecoder_getMinimumStride(decoder.get());
	const std::size_t compact_row_bytes =
		static_cast<std::size_t>(output_width) * 4U;
	if (minimum_stride < compact_row_bytes) {
		return platform_value_failure<ImagePixels>(
			core::OperationResultCategory::CodecFailure,
			make_diagnostic(
				core::DiagnosticSeverity::WriteBlockingError,
				"source-decoder-invalid-stride",
				"Android source image decoder reported an invalid stride."));
	}

	if (minimum_stride > std::numeric_limits<std::size_t>::max()
							 / static_cast<std::size_t>(output_height)) {
		return platform_value_failure<ImagePixels>(
			core::OperationResultCategory::CodecFailure,
			make_diagnostic(
				core::DiagnosticSeverity::WriteBlockingError,
				"source-decoder-stride-overflow",
				"Decoded source image stride buffer is too large."));
	}

	std::vector<std::uint8_t> strided_pixels(
		minimum_stride * static_cast<std::size_t>(output_height));
	result = AImageDecoder_decodeImage(decoder.get(), strided_pixels.data(),
									   minimum_stride, strided_pixels.size());
	if (result != ANDROID_IMAGE_DECODER_SUCCESS) {
		return platform_value_failure<ImagePixels>(
			core::OperationResultCategory::CodecFailure,
			make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
							"source-decode-failed",
							"Android source image decoder could not decode the "
							"staged file.",
							image_decoder_result_text(result)));
	}

	std::vector<std::uint8_t> compact_pixels(
		static_cast<std::size_t>(*compact_byte_count));
	for (std::uint32_t row = 0; row < output_height; ++row) {
		const std::size_t source_offset =
			static_cast<std::size_t>(row) * minimum_stride;
		const std::size_t target_offset =
			static_cast<std::size_t>(row) * compact_row_bytes;
		std::copy_n(strided_pixels.data() + source_offset, compact_row_bytes,
					compact_pixels.data() + target_offset);
	}

	const char* mime = AImageDecoderHeaderInfo_getMimeType(header);
	std::string source_description = "Android NDK AImageDecoder";
	if (mime != nullptr && std::strlen(mime) > 0U) {
		source_description += " ";
		source_description += mime;
	}

	ImagePixels decoded{
		.width				= output_width,
		.height				= output_height,
		.format				= PixelFormat::Rgba8,
		.bytes				= std::move(compact_pixels),
		.source_description = std::move(source_description),
		.orientation_description =
			"Android NDK ImageDecoder straight unpremultiplied RGBA8888 "
			"output; "
			"representative EXIF orientation remains manual B12 validation.",
		.elapsed_milliseconds = elapsed_milliseconds_since(started_at)};
	ImagePixelsValidation validation = validate_image_pixels(decoded);
	if (!validation.valid()) {
		return platform_value_failure<ImagePixels>(
			core::OperationResultCategory::CodecFailure,
			make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
							"source-decoded-pixels-invalid",
							"Decoded source image pixel buffer is invalid.",
							std::string{to_string(validation.issue)}));
	}

	progress_sink.publish_progress(
		ProgressEvent{.operation_id	  = context.operation_id,
					  .operation_type = context.operation_type,
					  .phase		  = "source-decode-completed",
					  .message_id	 = ProgressMessageId::SourceDecodeCompleted,
					  .current_units = validation.actual_byte_count,
					  .total_units	 = validation.expected_byte_count,
					  .message		 = "Source image decode completed.",
					  .cancellable	 = false});
	return platform_value_success(std::move(decoded));
#else
	(void)request;
	(void)context;
	(void)progress_sink;
	(void)cancellation_token;
	return platform_value_failure<ImagePixels>(
		core::OperationResultCategory::Unsupported,
		make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
						"android-image-decoder-unavailable",
						"Android NDK ImageDecoder headers are unavailable for "
						"this build."));
#endif
}
}	 // namespace shuba::platform
