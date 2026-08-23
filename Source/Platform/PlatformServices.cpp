#include "Platform/PlatformServices.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace shuba::platform {
namespace {
constexpr std::array<SourceImageMimeMapping, 8>
	supported_source_image_mime_mapping_table{{
		{.mime_type = "image/jpeg", .file_extension = "jpg"},
		{.mime_type = "image/jpeg", .file_extension = "jpeg"},
		{.mime_type = "image/png", .file_extension = "png"},
		{.mime_type = "image/webp", .file_extension = "webp"},
		{.mime_type = "image/heic", .file_extension = "heic"},
		{.mime_type = "image/heif", .file_extension = "heif"},
		{.mime_type = "image/gif", .file_extension = "gif"},
		{.mime_type = "image/bmp", .file_extension = "bmp"},
	}};

[[nodiscard]] std::string to_lower_ascii(std::string_view text) {
	std::string result;
	result.reserve(text.size());
	for (const char value : text) {
		const unsigned char unsigned_value = static_cast<unsigned char>(value);
		result.push_back(static_cast<char>(std::tolower(unsigned_value)));
	}

	return result;
}

void add_unique_pattern(std::vector<std::string>& patterns,
						std::string pattern) {
	const std::vector<std::string>::const_iterator found =
		std::find(patterns.begin(), patterns.end(), pattern);
	if (found == patterns.end())
		patterns.push_back(std::move(pattern));
}

void add_image_patterns(std::vector<std::string>& patterns) {
	for (const SourceImageMimeMapping& mapping :
		 supported_source_image_mime_mapping_table) {
		add_unique_pattern(patterns,
						   "*." + std::string{mapping.file_extension});
	}
}

[[nodiscard]] bool add_patterns_for_supported_source_image_mime_type(
	std::vector<std::string>& patterns, std::string_view mime_type) {
	bool mapping_found{};
	for (const SourceImageMimeMapping& mapping :
		 supported_source_image_mime_mapping_table) {
		if (mapping.mime_type != mime_type)
			continue;
		mapping_found = true;
		add_unique_pattern(patterns,
						   "*." + std::string{mapping.file_extension});
	}
	return mapping_found;
}

[[nodiscard]] std::string join_patterns(
	const std::vector<std::string>& patterns) {
	if (patterns.empty())
		return "*";

	std::string result;
	for (const std::string& pattern : patterns) {
		if (!result.empty())
			result += ';';
		result += pattern;
	}

	return result;
}

[[nodiscard]] std::uint8_t bytes_per_pixel(PixelFormat format) noexcept {
	switch (format) {
		case PixelFormat::Rgba8:
			return 4U;
	}

	return 0U;
}
}	 // namespace
std::string_view to_string(PlatformCapability capability) noexcept {
	switch (capability) {
		case PlatformCapability::AppPrivatePaths:
			return "app-private paths";
		case PlatformCapability::PhotoSelection:
			return "photo selection";
		case PlatformCapability::DocumentImport:
			return "document import";
		case PlatformCapability::DocumentExport:
			return "document export";
		case PlatformCapability::ContentStaging:
			return "content staging";
		case PlatformCapability::SourceImageDecode:
			return "source image decode";
		case PlatformCapability::InternalPhotoCodec:
			return "internal photo codec";
		case PlatformCapability::JpegExport:
			return "JPEG export";
		case PlatformCapability::ZipArchive:
			return "ZIP archive";
		case PlatformCapability::CatalogReplacement:
			return "catalog replacement";
		case PlatformCapability::BroadMediaLibraryPermission:
			return "broad media library permission";
		case PlatformCapability::DirectCameraCapture:
			return "direct camera capture";
	}

	return "unknown platform capability";
}

std::string_view to_string(PlatformCapabilityState state) noexcept {
	switch (state) {
		case PlatformCapabilityState::Available:
			return "available";
		case PlatformCapabilityState::Unsupported:
			return "unsupported";
		case PlatformCapabilityState::PermissionDenied:
			return "permission denied";
	}

	return "unknown platform capability state";
}

std::string_view to_string(PlatformPermissionScope scope) noexcept {
	switch (scope) {
		case PlatformPermissionScope::None:
			return "none";
		case PlatformPermissionScope::AppPrivateStorage:
			return "app-private storage";
		case PlatformPermissionScope::PickerGrant:
			return "picker grant";
		case PlatformPermissionScope::BroadMediaLibrary:
			return "broad media library";
		case PlatformPermissionScope::Camera:
			return "camera";
	}

	return "unknown platform permission scope";
}

PlatformPermissionScope default_permission_scope(
	PlatformCapability capability) noexcept {
	switch (capability) {
		case PlatformCapability::AppPrivatePaths:
		case PlatformCapability::ContentStaging:
		case PlatformCapability::InternalPhotoCodec:
		case PlatformCapability::JpegExport:
		case PlatformCapability::ZipArchive:
		case PlatformCapability::CatalogReplacement:
			return PlatformPermissionScope::AppPrivateStorage;
		case PlatformCapability::PhotoSelection:
		case PlatformCapability::DocumentImport:
		case PlatformCapability::DocumentExport:
			return PlatformPermissionScope::PickerGrant;
		case PlatformCapability::SourceImageDecode:
			return PlatformPermissionScope::None;
		case PlatformCapability::BroadMediaLibraryPermission:
			return PlatformPermissionScope::BroadMediaLibrary;
		case PlatformCapability::DirectCameraCapture:
			return PlatformPermissionScope::Camera;
	}

	return PlatformPermissionScope::None;
}

bool PlatformCapabilityCheck::available() const noexcept {
	return state == PlatformCapabilityState::Available;
}

core::OperationResultCategory PlatformCapabilityCheck::category()
	const noexcept {
	switch (state) {
		case PlatformCapabilityState::Available:
			return core::OperationResultCategory::Success;
		case PlatformCapabilityState::Unsupported:
			return core::OperationResultCategory::Unsupported;
		case PlatformCapabilityState::PermissionDenied:
			return core::OperationResultCategory::PermissionDenied;
	}

	return core::OperationResultCategory::InternalError;
}

PlatformCapabilityCheck default_capability_check(
	PlatformCapability capability) {
	PlatformCapabilityState state = PlatformCapabilityState::Available;
	std::string message;

	if (capability == PlatformCapability::BroadMediaLibraryPermission
		|| capability == PlatformCapability::DirectCameraCapture) {
		state	= PlatformCapabilityState::Unsupported;
		message = "Capability is postponed for the first implementation.";
	}

	return PlatformCapabilityCheck{
		.capability		  = capability,
		.state			  = state,
		.permission_scope = default_permission_scope(capability),
		.message		  = std::move(message)};
}

std::string_view to_string(ProgressOperationType operation_type) noexcept {
	switch (operation_type) {
		case ProgressOperationType::MetadataWrite:
			return "metadata write";
		case ProgressOperationType::PhotoImport:
			return "photo import";
		case ProgressOperationType::ImagePreview:
			return "image preview";
		case ProgressOperationType::JpegExport:
			return "JPEG export";
		case ProgressOperationType::BackupExport:
			return "backup export";
		case ProgressOperationType::BackupImport:
			return "backup import";
		case ProgressOperationType::DiagnosticExport:
			return "diagnostic export";
		case ProgressOperationType::CatalogReplacement:
			return "catalog replacement";
	}

	return "unknown progress operation type";
}

void NullProgressSink::publish_progress(ProgressEvent event) {
	(void)event;
}

void ProgressCollector::publish_progress(ProgressEvent event) {
	recorded_events.push_back(std::move(event));
}

void ProgressCollector::clear() noexcept {
	recorded_events.clear();
}

const std::vector<ProgressEvent>& ProgressCollector::events() const noexcept {
	return recorded_events;
}

bool ManualCancellationToken::cancellation_requested() const noexcept {
	return requested;
}

void ManualCancellationToken::request_cancellation() noexcept {
	requested = true;
}

void ManualCancellationToken::reset_cancellation() noexcept {
	requested = false;
}

bool NeverCancelledToken::cancellation_requested() const noexcept {
	return false;
}

const PlatformOperationContext& ScopedPlatformOperation::context()
	const noexcept {
	return operation_context;
}

bool ScopedPlatformOperation::cancellation_requested() const noexcept {
	return cancellation != nullptr && cancellation->cancellation_requested();
}

void ScopedPlatformOperation::publish_progress(
	std::string phase, std::optional<ProgressMessageId> message_id,
	std::optional<std::uint64_t> current_units,
	std::optional<std::uint64_t> total_units, std::string message,
	bool cancellable) const {
	if (progress == nullptr)
		return;

	progress->publish_progress(
		ProgressEvent{.operation_id	  = operation_context.operation_id,
					  .operation_type = operation_context.operation_type,
					  .phase		  = std::move(phase),
					  .message_id	  = message_id,
					  .current_units  = current_units,
					  .total_units	  = total_units,
					  .message		  = std::move(message),
					  .cancellable	  = cancellable});
}

void ScopedPlatformOperation::release() noexcept {
	gate_lease.release();
}

ScopedPlatformOperation::ScopedPlatformOperation(
	core::OperationGate::Lease lease, PlatformOperationContext context,
	ProgressSink& progress_sink, CancellationToken& cancellation_token)
	: gate_lease(std::move(lease))
	, operation_context(std::move(context))
	, progress(&progress_sink)
	, cancellation(&cancellation_token) {}

bool PlatformOperationStartResult::succeeded() const noexcept {
	return category == core::OperationResultCategory::Success
		   && operation.has_value();
}

bool PlatformOperationStartResult::failed() const noexcept {
	return category != core::OperationResultCategory::Success
		   && category != core::OperationResultCategory::UserCancelled;
}

PlatformOperationStartResult::operator bool() const noexcept {
	return succeeded();
}

PlatformOperationStartResult try_start_platform_operation(
	core::OperationGate& gate, const PlatformOperationStartRequest& request,
	ProgressSink& progress_sink, CancellationToken& cancellation_token) {
	std::optional<core::OperationGate::Lease> lease =
		gate.try_acquire(request.operation_kind, request.operation_id);
	if (!lease.has_value()) {
		return PlatformOperationStartResult{
			.category	 = core::OperationResultCategory::ValidationFailure,
			.diagnostics = {core::Diagnostic{
				.severity = core::DiagnosticSeverity::ActionValidationError,
				.code	  = "operation-busy",
				.message  = "Another exclusive operation is already running.",
				.technical_details =
					"The platform operation gate rejected a concurrent "
					"operation."}}};
	}

	PlatformOperationContext context{.operation_id	 = request.operation_id,
									 .operation_type = request.operation_type};
	ScopedPlatformOperation operation{std::move(*lease), std::move(context),
									  progress_sink, cancellation_token};
	PlatformOperationStartResult result;
	result.operation.emplace(std::move(operation));
	return result;
}

std::string_view to_string(PlatformContentHandleKind kind) noexcept {
	switch (kind) {
		case PlatformContentHandleKind::LocalFile:
			return "local file";
		case PlatformContentHandleKind::OpaqueHandle:
			return "opaque handle";
	}

	return "unknown platform content handle kind";
}

ContentSourceDescriptor make_local_file_source(std::filesystem::path local_path,
											   std::string display_name) {
	if (display_name.empty())
		display_name = local_path.filename().string();

	return ContentSourceDescriptor{.kind = PlatformContentHandleKind::LocalFile,
								   .local_path	 = std::move(local_path),
								   .display_name = std::move(display_name),
								   .transient	 = true};
}

DocumentDestinationDescriptor make_local_file_destination(
	std::filesystem::path local_path, std::string display_name) {
	if (display_name.empty())
		display_name = local_path.filename().string();

	return DocumentDestinationDescriptor{
		.kind		  = PlatformContentHandleKind::LocalFile,
		.local_path	  = std::move(local_path),
		.display_name = std::move(display_name)};
}

ContentSourceDescriptor make_opaque_content_source(
	std::string opaque_handle, std::string display_name,
	std::optional<std::uint64_t> byte_count, bool transient) {
	return ContentSourceDescriptor{
		.kind		   = PlatformContentHandleKind::OpaqueHandle,
		.opaque_handle = std::move(opaque_handle),
		.display_name  = std::move(display_name),
		.byte_count	   = byte_count,
		.transient	   = transient};
}

DocumentDestinationDescriptor make_opaque_document_destination(
	std::string opaque_handle, std::string display_name) {
	return DocumentDestinationDescriptor{
		.kind		   = PlatformContentHandleKind::OpaqueHandle,
		.opaque_handle = std::move(opaque_handle),
		.display_name  = std::move(display_name)};
}

std::string sanitize_platform_file_name(std::string_view text,
										std::string_view fallback) {
	std::string result;
	result.reserve(text.size());
	bool last_was_separator{};

	for (const char value : text) {
		const unsigned char unsigned_value = static_cast<unsigned char>(value);
		const bool safe_ascii = std::isalnum(unsigned_value) != 0
								|| value == '_' || value == '-' || value == '.';
		if (safe_ascii) {
			result.push_back(value);
			last_was_separator = false;
			continue;
		}

		if (!last_was_separator) {
			result.push_back('-');
			last_was_separator = true;
		}
	}

	while (!result.empty() && (result.front() == '.' || result.front() == '-'))
		result.erase(result.begin());
	while (!result.empty() && (result.back() == '.' || result.back() == '-'))
		result.pop_back();

	constexpr std::size_t maximum_file_name_length = 96U;
	if (result.size() > maximum_file_name_length)
		result.resize(maximum_file_name_length);
	while (!result.empty() && (result.back() == '.' || result.back() == '-'))
		result.pop_back();

	if (!result.empty())
		return result;

	if (!fallback.empty() && fallback != text)
		return sanitize_platform_file_name(fallback, "content");

	return "content";
}

std::string file_extension_or_empty(std::string_view display_name) {
	const std::size_t last_separator = display_name.find_last_of("/\\");
	const std::size_t last_dot		 = display_name.find_last_of('.');
	if (last_dot == std::string_view::npos
		|| last_dot + 1U >= display_name.size())
		return {};
	if (last_separator != std::string_view::npos && last_dot < last_separator)
		return {};

	std::string extension;
	extension.reserve(display_name.size() - last_dot);
	for (std::size_t index = last_dot; index < display_name.size(); ++index) {
		const unsigned char unsigned_value =
			static_cast<unsigned char>(display_name[index]);
		if (index != last_dot && std::isalnum(unsigned_value) == 0)
			return {};
		extension.push_back(static_cast<char>(std::tolower(unsigned_value)));
	}

	constexpr std::size_t maximum_extension_length = 12U;
	if (extension.size() > maximum_extension_length)
		return {};

	return extension;
}

std::string make_staged_content_file_name(
	std::string_view prefix, const core::OperationIdentifier& operation_id,
	std::size_t sequence, std::string_view display_name) {
	std::string result = sanitize_platform_file_name(prefix, "content");
	result += '-';
	result += operation_id.value();
	result += '-';
	result += std::to_string(sequence);
	result += file_extension_or_empty(display_name);
	return result;
}

std::string file_patterns_for_mime_types(
	const std::vector<std::string>& mime_types) {
	if (mime_types.empty())
		return "*";

	std::vector<std::string> patterns;
	for (const std::string& mime_type : mime_types) {
		const std::string lower_mime_type = to_lower_ascii(mime_type);
		if (lower_mime_type.empty() || lower_mime_type == "*"
			|| lower_mime_type == "*/*") {
			return "*";
		}

		if (lower_mime_type == "image/*") {
			add_image_patterns(patterns);
		} else if (add_patterns_for_supported_source_image_mime_type(
					   patterns, lower_mime_type)) {
		} else if (lower_mime_type == "application/zip"
				   || lower_mime_type == "application/x-zip-compressed") {
			add_unique_pattern(patterns, "*.zip");
		} else if (lower_mime_type == "text/plain") {
			add_unique_pattern(patterns, "*.txt");
		} else {
			return "*";
		}
	}

	return join_patterns(patterns);
}

std::span<const SourceImageMimeMapping>
supported_source_image_mime_mappings() noexcept {
	return supported_source_image_mime_mapping_table;
}

std::vector<std::string> supported_source_image_picker_mime_types() {
	std::vector<std::string> mime_types;
	mime_types.reserve(supported_source_image_mime_mapping_table.size());
	for (const SourceImageMimeMapping& mapping :
		 supported_source_image_mime_mapping_table) {
		const std::string mime_type{mapping.mime_type};
		const std::vector<std::string>::const_iterator found =
			std::find(mime_types.begin(), mime_types.end(), mime_type);
		if (found == mime_types.end())
			mime_types.push_back(mime_type);
	}
	return mime_types;
}

std::string supported_source_image_picker_file_patterns() {
	std::vector<std::string> patterns;
	patterns.reserve(supported_source_image_mime_mapping_table.size());
	add_image_patterns(patterns);
	return join_patterns(patterns);
}

bool zip_archive_path_is_safe(std::string_view archive_path) noexcept {
	if (archive_path.empty())
		return false;
	if (archive_path.front() == '/' || archive_path.front() == '\\')
		return false;
	if (archive_path.find('\\') != std::string_view::npos)
		return false;

	std::string_view normalized_path = archive_path;
	if (normalized_path.ends_with('/')) {
		if (normalized_path.size() == 1U)
			return false;
		normalized_path.remove_suffix(1U);
	}
	if (normalized_path.empty())
		return false;

	std::size_t component_begin = 0U;
	bool first_component		= true;
	while (component_begin <= normalized_path.size()) {
		const std::size_t separator =
			normalized_path.find('/', component_begin);
		const std::size_t component_end	 = separator == std::string_view::npos
											   ? normalized_path.size()
											   : separator;
		const std::string_view component = normalized_path.substr(
			component_begin, component_end - component_begin);
		if (component.empty() || component == "." || component == "..")
			return false;
		if (first_component && component.find(':') != std::string_view::npos)
			return false;

		if (separator == std::string_view::npos)
			break;
		component_begin = separator + 1U;
		first_component = false;
	}

	return true;
}

std::string_view to_string(PixelFormat format) noexcept {
	switch (format) {
		case PixelFormat::Rgba8:
			return "rgba8";
	}

	return "unknown pixel format";
}

std::string_view to_string(ImagePixelsValidationIssue issue) noexcept {
	switch (issue) {
		case ImagePixelsValidationIssue::None:
			return "none";
		case ImagePixelsValidationIssue::EmptyDimensions:
			return "empty dimensions";
		case ImagePixelsValidationIssue::UnsupportedFormat:
			return "unsupported format";
		case ImagePixelsValidationIssue::ByteCountOverflow:
			return "byte count overflow";
		case ImagePixelsValidationIssue::ByteCountMismatch:
			return "byte count mismatch";
	}

	return "unknown image pixel validation issue";
}

bool ImagePixelsValidation::valid() const noexcept {
	return issue == ImagePixelsValidationIssue::None;
}

ImagePixelsValidation::operator bool() const noexcept {
	return valid();
}

std::optional<std::uint64_t> image_pixel_byte_count(
	std::uint32_t width, std::uint32_t height, PixelFormat format) noexcept {
	const std::uint8_t pixel_bytes = bytes_per_pixel(format);
	if (pixel_bytes == 0U)
		return std::nullopt;

	const std::uint64_t wide_width	= width;
	const std::uint64_t wide_height = height;
	if (wide_width != 0U
		&& wide_height > std::numeric_limits<std::uint64_t>::max() / wide_width)
		return std::nullopt;

	const std::uint64_t pixels = wide_width * wide_height;
	if (pixels != 0U
		&& static_cast<std::uint64_t>(pixel_bytes)
			   > std::numeric_limits<std::uint64_t>::max() / pixels)
		return std::nullopt;

	return pixels * static_cast<std::uint64_t>(pixel_bytes);
}

ImagePixelsValidation validate_image_pixels(
	const ImagePixels& pixels) noexcept {
	if (pixels.width == 0U || pixels.height == 0U) {
		return ImagePixelsValidation{
			.issue = ImagePixelsValidationIssue::EmptyDimensions,
			.actual_byte_count =
				static_cast<std::uint64_t>(pixels.bytes.size())};
	}

	if (bytes_per_pixel(pixels.format) == 0U) {
		return ImagePixelsValidation{
			.issue = ImagePixelsValidationIssue::UnsupportedFormat,
			.actual_byte_count =
				static_cast<std::uint64_t>(pixels.bytes.size())};
	}

	std::optional<std::uint64_t> expected =
		image_pixel_byte_count(pixels.width, pixels.height, pixels.format);
	if (!expected.has_value()) {
		return ImagePixelsValidation{
			.issue = ImagePixelsValidationIssue::ByteCountOverflow,
			.actual_byte_count =
				static_cast<std::uint64_t>(pixels.bytes.size())};
	}

	const std::uint64_t actual =
		static_cast<std::uint64_t>(pixels.bytes.size());
	if (actual != *expected) {
		return ImagePixelsValidation{
			.issue = ImagePixelsValidationIssue::ByteCountMismatch,
			.expected_byte_count = *expected,
			.actual_byte_count	 = actual};
	}

	return ImagePixelsValidation{.expected_byte_count = *expected,
								 .actual_byte_count	  = actual};
}

bool validate_source_image_decode_sizing(
	const SourceImageDecodeSizing& sizing) noexcept {
	return sizing.maximum_longest_edge != 0U;
}

std::optional<SourceImageDecodeTargetSize> source_image_decode_target_size(
	std::uint32_t source_width, std::uint32_t source_height,
	const SourceImageDecodeSizing& sizing) noexcept {
	if (source_width == 0U || source_height == 0U
		|| !validate_source_image_decode_sizing(sizing))
		return std::nullopt;

	const std::uint32_t source_longest_edge =
		std::max(source_width, source_height);
	std::uint32_t target_width	= source_width;
	std::uint32_t target_height = source_height;
	if (source_longest_edge > sizing.maximum_longest_edge) {
		if (source_width >= source_height) {
			target_width  = sizing.maximum_longest_edge;
			target_height = static_cast<std::uint32_t>(std::max(
				std::uint64_t{1}, (static_cast<std::uint64_t>(source_height)
								   * sizing.maximum_longest_edge)
									  / source_width));
		} else {
			target_height = sizing.maximum_longest_edge;
			target_width  = static_cast<std::uint32_t>(std::max(
				std::uint64_t{1}, (static_cast<std::uint64_t>(source_width)
								   * sizing.maximum_longest_edge)
									  / source_height));
		}
	}

	const std::optional<std::uint64_t> target_byte_count =
		image_pixel_byte_count(target_width, target_height, PixelFormat::Rgba8);
	if (!target_byte_count.has_value()
		|| *target_byte_count > static_cast<std::uint64_t>(
			   std::numeric_limits<std::size_t>::max()))
		return std::nullopt;

	return SourceImageDecodeTargetSize{.width  = target_width,
									   .height = target_height};
}

InternalPhotoEncodeSettings default_internal_photo_encode_settings() noexcept {
	return InternalPhotoEncodeSettings{};
}

bool validate_internal_photo_encode_settings(
	const InternalPhotoEncodeSettings& settings) noexcept {
	return std::isfinite(settings.quality) && settings.quality >= 1.0F
		   && settings.quality <= 100.0F && settings.effort >= 1U
		   && settings.effort <= 9U;
}

bool validate_jpeg_export_quality(std::uint8_t quality) noexcept {
	return quality >= 1U && quality <= 100U;
}
}	 // namespace shuba::platform
