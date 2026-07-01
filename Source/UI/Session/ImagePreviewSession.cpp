#include "UI/Session/ImagePreviewSession.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace shuba::ui {
namespace {
[[nodiscard]] std::string normalized_preview_path(
	const std::filesystem::path& path) {
	return path.lexically_normal().generic_string();
}

[[nodiscard]] bool identity_matches_internal_photo(
	const ImagePreviewRequestIdentity& identity,
	const core::StableIdentifier& photo_id) {
	return identity.kind == ImagePreviewRequestKind::InternalPhoto
		   && identity.source_key == photo_id.value();
}

[[nodiscard]] bool identity_matches_staged_photo(
	const ImagePreviewRequestIdentity& identity,
	const std::filesystem::path& staged_path) {
	return identity.kind == ImagePreviewRequestKind::StagedPhoto
		   && identity.source_key == normalized_preview_path(staged_path);
}

void append_fingerprint_part(std::string& fingerprint, std::string part) {
	if (!fingerprint.empty())
		fingerprint += ';';
	fingerprint += std::move(part);
}

[[nodiscard]] std::string staged_source_fingerprint(
	const PendingPhotoSource& source) {
	std::string fingerprint;
	if (source.byte_count.has_value()) {
		append_fingerprint_part(
			fingerprint,
			"declared-bytes=" + std::to_string(*source.byte_count));
	}
	if (source.staged_source.has_value()
		&& source.staged_source->byte_count.has_value()
		&& source.staged_source->byte_count != source.byte_count) {
		append_fingerprint_part(
			fingerprint,
			"source-bytes="
				+ std::to_string(*source.staged_source->byte_count));
	}

	if (!source.staged_path.has_value()) {
		append_fingerprint_part(fingerprint, "path=missing");
		return fingerprint;
	}

	std::error_code error;
	const std::uintmax_t file_size =
		std::filesystem::file_size(*source.staged_path, error);
	if (error) {
		append_fingerprint_part(fingerprint, "file-bytes=unavailable");
	} else {
		append_fingerprint_part(
			fingerprint,
			"file-bytes="
				+ std::to_string(static_cast<std::uint64_t>(file_size)));
	}

	error.clear();
	const std::filesystem::file_time_type modified_time =
		std::filesystem::last_write_time(*source.staged_path, error);
	if (error) {
		append_fingerprint_part(fingerprint, "mtime=unavailable");
	} else {
		const long long modified_ticks =
			static_cast<long long>(modified_time.time_since_epoch().count());
		append_fingerprint_part(fingerprint,
								"mtime=" + std::to_string(modified_ticks));
	}

	return fingerprint;
}

[[nodiscard]] bool staged_path_readable(
	const std::filesystem::path& staged_path) {
	std::error_code error;
	return std::filesystem::is_regular_file(staged_path, error) && !error;
}

[[nodiscard]] std::optional<std::uint64_t> preview_pixel_bytes(
	const platform::ImagePixels& pixels) noexcept {
	return platform::image_pixel_byte_count(pixels.width, pixels.height,
											platform::PixelFormat::Rgba8);
}

[[nodiscard]] core::Diagnostic make_preview_diagnostic(
	core::DiagnosticSeverity severity, std::string code, std::string message,
	std::string technical_details = {}) {
	return core::Diagnostic{.severity		   = severity,
							.code			   = std::move(code),
							.message		   = std::move(message),
							.technical_details = std::move(technical_details)};
}

[[nodiscard]] catalog::BrokenPhotoPlaceholder make_preview_placeholder(
	std::string message, std::string diagnostic_code) {
	return catalog::BrokenPhotoPlaceholder{
		.title			 = "Photo preview unavailable",
		.message		 = std::move(message),
		.diagnostic_code = std::move(diagnostic_code)};
}

[[nodiscard]] std::string dimensions_text(std::uint32_t width,
										  std::uint32_t height) {
	return std::to_string(width) + "x" + std::to_string(height);
}

[[nodiscard]] std::string metrics_message(
	const ImagePreviewLoadMetrics& metrics) {
	return "Image preview decoded "
		   + dimensions_text(metrics.decoded_width, metrics.decoded_height)
		   + " to "
		   + dimensions_text(metrics.preview_width, metrics.preview_height)
		   + " in " + std::to_string(metrics.decode_elapsed_milliseconds)
		   + " ms.";
}

[[nodiscard]] InternalPhotoPreviewLoadResult invalid_preview_size_result(
	ImagePreviewRequestIdentity identity) {
	return InternalPhotoPreviewLoadResult{
		.status		 = ImagePreviewLoadStatus::Broken,
		.category	 = core::OperationResultCategory::ValidationFailure,
		.identity	 = std::move(identity),
		.diagnostics = {make_preview_diagnostic(
			core::DiagnosticSeverity::ActionValidationError,
			"image_preview_invalid_target_size",
			"Image preview target size must have non-zero dimensions.")},
		.placeholder =
			make_preview_placeholder("The requested preview size is invalid.",
									 "image_preview_invalid_target_size")};
}

[[nodiscard]] StagedPhotoPreviewLoadResult invalid_staged_preview_size_result(
	ImagePreviewRequestIdentity identity,
	std::optional<std::filesystem::path> staged_path) {
	return StagedPhotoPreviewLoadResult{
		.status		 = ImagePreviewLoadStatus::Broken,
		.category	 = core::OperationResultCategory::ValidationFailure,
		.identity	 = std::move(identity),
		.diagnostics = {make_preview_diagnostic(
			core::DiagnosticSeverity::ActionValidationError,
			"image_preview_invalid_target_size",
			"Image preview target size must have non-zero dimensions.")},
		.placeholder =
			make_preview_placeholder("The requested preview size is invalid.",
									 "image_preview_invalid_target_size"),
		.staged_path = std::move(staged_path)};
}

[[nodiscard]] StagedPhotoPreviewLoadResult staged_preview_failure_result(
	ImagePreviewRequestIdentity identity,
	std::optional<std::filesystem::path> staged_path,
	core::OperationResultCategory category, core::Diagnostic diagnostic,
	catalog::BrokenPhotoPlaceholder placeholder) {
	return StagedPhotoPreviewLoadResult{
		.status		 = ImagePreviewLoadStatus::Broken,
		.category	 = category,
		.identity	 = std::move(identity),
		.diagnostics = {std::move(diagnostic)},
		.placeholder = std::move(placeholder),
		.staged_path = std::move(staged_path)};
}

[[nodiscard]] platform::StagedContent staged_content_for_preview(
	const PendingPhotoSource& source) {
	return platform::StagedContent{.staged_path	 = *source.staged_path,
								   .display_name = source.display_name,
								   .byte_count	 = source.byte_count};
}
}	 // namespace

std::string_view to_string(ImagePreviewLoadStatus status) noexcept {
	switch (status) {
		case ImagePreviewLoadStatus::Loaded:
			return "loaded";
		case ImagePreviewLoadStatus::Broken:
			return "broken";
		case ImagePreviewLoadStatus::Cancelled:
			return "cancelled";
	}

	return "unknown image preview load status";
}

bool InternalPhotoPreviewLoadResult::succeeded() const noexcept {
	return status == ImagePreviewLoadStatus::Loaded
		   && category == core::OperationResultCategory::Success
		   && pixels.has_value();
}

bool InternalPhotoPreviewLoadResult::was_user_cancelled() const noexcept {
	return status == ImagePreviewLoadStatus::Cancelled
		   || category == core::OperationResultCategory::UserCancelled;
}

bool InternalPhotoPreviewLoadResult::failed() const noexcept {
	return !succeeded() && !was_user_cancelled();
}

bool StagedPhotoPreviewLoadResult::succeeded() const noexcept {
	return status == ImagePreviewLoadStatus::Loaded
		   && category == core::OperationResultCategory::Success
		   && pixels.has_value();
}

bool StagedPhotoPreviewLoadResult::was_user_cancelled() const noexcept {
	return status == ImagePreviewLoadStatus::Cancelled
		   || category == core::OperationResultCategory::UserCancelled;
}

bool StagedPhotoPreviewLoadResult::failed() const noexcept {
	return !succeeded() && !was_user_cancelled();
}

ImagePreviewRequestIdentity make_internal_photo_preview_identity(
	const core::StableIdentifier& photo_id, ImagePreviewSize target_size) {
	return ImagePreviewRequestIdentity{
		.kind		 = ImagePreviewRequestKind::InternalPhoto,
		.source_key	 = photo_id.value(),
		.target_size = target_size};
}

ImagePreviewRequestIdentity make_staged_photo_preview_identity(
	const std::filesystem::path& staged_path, ImagePreviewSize target_size) {
	return ImagePreviewRequestIdentity{
		.kind		 = ImagePreviewRequestKind::StagedPhoto,
		.source_key	 = normalized_preview_path(staged_path),
		.target_size = target_size};
}

ImagePreviewRequestIdentity make_staged_photo_preview_identity(
	const PendingPhotoSource& source, ImagePreviewSize target_size) {
	ImagePreviewRequestIdentity identity = make_staged_photo_preview_identity(
		source.staged_path.value_or(std::filesystem::path{}), target_size);
	identity.source_fingerprint = staged_source_fingerprint(source);
	return identity;
}

bool valid_image_preview_size(ImagePreviewSize size) noexcept {
	return size.max_width > 0U && size.max_height > 0U;
}

std::optional<platform::ImagePixels> scale_image_pixels_for_preview(
	const platform::ImagePixels& pixels, ImagePreviewSize target_size) {
	if (!valid_image_preview_size(target_size))
		return std::nullopt;
	if (pixels.format != platform::PixelFormat::Rgba8)
		return std::nullopt;
	const platform::ImagePixelsValidation validation =
		platform::validate_image_pixels(pixels);
	if (!validation.valid())
		return std::nullopt;

	std::uint32_t target_width	= pixels.width;
	std::uint32_t target_height = pixels.height;
	if (pixels.width > target_size.max_width
		|| pixels.height > target_size.max_height) {
		const std::uint64_t width_limited_height =
			static_cast<std::uint64_t>(pixels.height) * target_size.max_width
			/ pixels.width;
		const std::uint64_t height_limited_width =
			static_cast<std::uint64_t>(pixels.width) * target_size.max_height
			/ pixels.height;
		if (width_limited_height > 0U
			&& width_limited_height <= target_size.max_height) {
			target_width  = target_size.max_width;
			target_height = static_cast<std::uint32_t>(width_limited_height);
		} else {
			target_width = static_cast<std::uint32_t>(
				std::max<std::uint64_t>(1U, height_limited_width));
			target_height = target_size.max_height;
		}
	}

	platform::ImagePixels scaled{
		.width	= target_width,
		.height = target_height,
		.format = platform::PixelFormat::Rgba8,
		.source_description =
			pixels.source_description.empty()
				? std::string{"scaled preview"}
				: pixels.source_description + " scaled preview",
		.orientation_description = pixels.orientation_description,
		.elapsed_milliseconds	 = pixels.elapsed_milliseconds};
	const std::optional<std::uint64_t> byte_count = preview_pixel_bytes(scaled);
	if (!byte_count.has_value())
		return std::nullopt;
	scaled.bytes.resize(static_cast<std::size_t>(*byte_count));

	for (std::uint32_t y = 0U; y < target_height; ++y) {
		const std::uint32_t source_y = static_cast<std::uint32_t>(
			static_cast<std::uint64_t>(y) * pixels.height / target_height);
		for (std::uint32_t x = 0U; x < target_width; ++x) {
			const std::uint32_t source_x = static_cast<std::uint32_t>(
				static_cast<std::uint64_t>(x) * pixels.width / target_width);
			const std::size_t source_offset =
				(static_cast<std::size_t>(source_y) * pixels.width + source_x)
				* 4U;
			const std::size_t target_offset =
				(static_cast<std::size_t>(y) * target_width + x) * 4U;
			scaled.bytes[target_offset]		 = pixels.bytes[source_offset];
			scaled.bytes[target_offset + 1U] = pixels.bytes[source_offset + 1U];
			scaled.bytes[target_offset + 2U] = pixels.bytes[source_offset + 2U];
			scaled.bytes[target_offset + 3U] = pixels.bytes[source_offset + 3U];
		}
	}

	return scaled;
}

ImagePreviewCache::ImagePreviewCache(ImagePreviewCacheSettings settings_value)
	: cache_settings(settings_value) {}

const ImagePreviewCacheSettings& ImagePreviewCache::settings() const noexcept {
	return cache_settings;
}

ImagePreviewCacheStats ImagePreviewCache::stats() const noexcept {
	return ImagePreviewCacheStats{.entry_count = entries.size(),
								  .pixel_bytes = approximate_pixel_bytes};
}

bool ImagePreviewCache::empty() const noexcept {
	return entries.empty();
}

bool ImagePreviewCache::contains(
	const ImagePreviewRequestIdentity& identity) const {
	return find_entry(identity) != entries.end();
}

const platform::ImagePixels* ImagePreviewCache::find(
	const ImagePreviewRequestIdentity& identity) {
	std::vector<ImagePreviewCacheEntry>::iterator found = find_entry(identity);
	if (found == entries.end())
		return nullptr;

	if (found != entries.begin()) {
		ImagePreviewCacheEntry entry = std::move(*found);
		entries.erase(found);
		entries.insert(entries.begin(), std::move(entry));
	}
	return &entries.front().pixels;
}

bool ImagePreviewCache::put(ImagePreviewRequestIdentity identity,
							platform::ImagePixels pixels) {
	if (!valid_image_preview_size(identity.target_size))
		return false;
	const platform::ImagePixelsValidation validation =
		platform::validate_image_pixels(pixels);
	if (!validation.valid())
		return false;
	const std::optional<std::uint64_t> entry_bytes =
		preview_pixel_bytes(pixels);
	if (!entry_bytes.has_value())
		return false;
	if (cache_settings.maximum_entries == 0U
		|| *entry_bytes > cache_settings.maximum_pixel_bytes) {
		return false;
	}

	remove(identity);
	entries.insert(
		entries.begin(),
		ImagePreviewCacheEntry{.identity				= std::move(identity),
							   .pixels					= std::move(pixels),
							   .approximate_pixel_bytes = *entry_bytes});
	approximate_pixel_bytes += *entry_bytes;
	enforce_limits();
	return true;
}

void ImagePreviewCache::remove(const ImagePreviewRequestIdentity& identity) {
	std::vector<ImagePreviewCacheEntry>::iterator found = find_entry(identity);
	if (found == entries.end())
		return;

	approximate_pixel_bytes -= found->approximate_pixel_bytes;
	entries.erase(found);
}

void ImagePreviewCache::remove_internal_photo(
	const core::StableIdentifier& photo_id) {
	std::erase_if(entries,
				  [this, &photo_id](const ImagePreviewCacheEntry& entry) {
		if (!identity_matches_internal_photo(entry.identity, photo_id))
			return false;
		approximate_pixel_bytes -= entry.approximate_pixel_bytes;
		return true;
	});
}

void ImagePreviewCache::remove_staged_photo(
	const std::filesystem::path& staged_path) {
	std::erase_if(entries,
				  [this, &staged_path](const ImagePreviewCacheEntry& entry) {
		if (!identity_matches_staged_photo(entry.identity, staged_path))
			return false;
		approximate_pixel_bytes -= entry.approximate_pixel_bytes;
		return true;
	});
}

void ImagePreviewCache::clear() noexcept {
	entries.clear();
	approximate_pixel_bytes = 0U;
}

std::vector<ImagePreviewCacheEntry>::iterator ImagePreviewCache::find_entry(
	const ImagePreviewRequestIdentity& identity) {
	return std::ranges::find_if(
		entries, [&identity](const ImagePreviewCacheEntry& entry) {
		return entry.identity == identity;
	});
}

std::vector<ImagePreviewCacheEntry>::const_iterator
ImagePreviewCache::find_entry(
	const ImagePreviewRequestIdentity& identity) const {
	return std::ranges::find_if(
		entries, [&identity](const ImagePreviewCacheEntry& entry) {
		return entry.identity == identity;
	});
}

void ImagePreviewCache::enforce_limits() {
	while (
		!entries.empty()
		&& (entries.size() > cache_settings.maximum_entries
			|| approximate_pixel_bytes > cache_settings.maximum_pixel_bytes)) {
		approximate_pixel_bytes -= entries.back().approximate_pixel_bytes;
		entries.pop_back();
	}
}

InternalPhotoPreviewLoadResult load_internal_photo_preview(
	const InternalPhotoPreviewLoadRequest& request, ImagePreviewCache& cache,
	catalog::PhotoExportUseCase& photo_export_use_case,
	platform::ProgressSink& progress_sink,
	platform::CancellationToken& cancellation_token) {
	ImagePreviewRequestIdentity identity = make_internal_photo_preview_identity(
		request.photo_id, request.target_size);
	if (!valid_image_preview_size(request.target_size))
		return invalid_preview_size_result(std::move(identity));

	const platform::ImagePixels* cached_pixels = cache.find(identity);
	if (cached_pixels != nullptr) {
		return InternalPhotoPreviewLoadResult{
			.status	   = ImagePreviewLoadStatus::Loaded,
			.category  = core::OperationResultCategory::Success,
			.identity  = std::move(identity),
			.pixels	   = *cached_pixels,
			.cache_hit = true};
	}

	catalog::PhotoDisplayResult display_result =
		photo_export_use_case.load_photo_for_display(
			catalog::PhotoDisplayRequest{.current_state = request.current_state,
										 .paths			= request.paths,
										 .photo_id		= request.photo_id},
			progress_sink, cancellation_token);
	if (display_result.was_user_cancelled()) {
		return InternalPhotoPreviewLoadResult{
			.status		 = ImagePreviewLoadStatus::Cancelled,
			.category	 = core::OperationResultCategory::UserCancelled,
			.identity	 = std::move(identity),
			.diagnostics = std::move(display_result.diagnostics),
			.media_path	 = std::move(display_result.media_path)};
	}
	if (display_result.failed()) {
		return InternalPhotoPreviewLoadResult{
			.status		 = ImagePreviewLoadStatus::Broken,
			.category	 = display_result.category,
			.identity	 = std::move(identity),
			.diagnostics = std::move(display_result.diagnostics),
			.placeholder = std::move(display_result.placeholder),
			.media_path	 = std::move(display_result.media_path)};
	}

	std::optional<platform::ImagePixels> scaled_pixels =
		scale_image_pixels_for_preview(*display_result.pixels,
									   request.target_size);
	if (!scaled_pixels.has_value()) {
		return InternalPhotoPreviewLoadResult{
			.status		 = ImagePreviewLoadStatus::Broken,
			.category	 = core::OperationResultCategory::CodecFailure,
			.identity	 = std::move(identity),
			.diagnostics = {make_preview_diagnostic(
				core::DiagnosticSeverity::WriteBlockingError,
				"image_preview_scale_failed",
				"Decoded photo pixels could not be scaled for preview.",
				dimensions_text(display_result.pixels->width,
								display_result.pixels->height))},
			.placeholder = make_preview_placeholder(
				"The internal photo media could not be prepared for preview.",
				"image_preview_scale_failed"),
			.media_path = std::move(display_result.media_path)};
	}

	ImagePreviewLoadMetrics metrics{
		.decoded_width	= display_result.pixels->width,
		.decoded_height = display_result.pixels->height,
		.preview_width	= scaled_pixels->width,
		.preview_height = scaled_pixels->height,
		.decode_elapsed_milliseconds =
			display_result.pixels->elapsed_milliseconds};
	platform::ImagePixels preview_pixels = std::move(*scaled_pixels);
	preview_pixels.source_description	 = metrics_message(metrics);
	const bool cache_stored				 = cache.put(identity, preview_pixels);
	if (!cache_stored) {
		display_result.diagnostics.push_back(make_preview_diagnostic(
			core::DiagnosticSeverity::RecoverableWarning,
			"image_preview_cache_rejected",
			"Scaled image preview was loaded but not stored in cache.",
			identity.source_key));
	}

	return InternalPhotoPreviewLoadResult{
		.status		  = ImagePreviewLoadStatus::Loaded,
		.category	  = core::OperationResultCategory::Success,
		.identity	  = std::move(identity),
		.diagnostics  = std::move(display_result.diagnostics),
		.pixels		  = std::move(preview_pixels),
		.media_path	  = std::move(display_result.media_path),
		.metrics	  = metrics,
		.cache_stored = cache_stored};
}

StagedPhotoPreviewLoadResult load_staged_photo_preview(
	const StagedPhotoPreviewLoadRequest& request, ImagePreviewCache& cache,
	platform::SourceImageDecodeService& decode_service,
	platform::ProgressSink& progress_sink,
	platform::CancellationToken& cancellation_token) {
	ImagePreviewRequestIdentity identity =
		make_staged_photo_preview_identity(request.source, request.target_size);
	std::optional<std::filesystem::path> staged_path =
		request.source.staged_path;
	if (!valid_image_preview_size(request.target_size)) {
		return invalid_staged_preview_size_result(std::move(identity),
												  std::move(staged_path));
	}

	if (!request.source.ready_for_import()
		|| !request.source.staged_path.has_value()) {
		return staged_preview_failure_result(
			std::move(identity), std::move(staged_path),
			core::OperationResultCategory::ValidationFailure,
			make_preview_diagnostic(
				core::DiagnosticSeverity::ActionValidationError,
				"staged_photo_preview_source_not_ready",
				"Staged photo preview source is not ready for preview.",
				request.source.display_name),
			make_preview_placeholder(
				"The staged photo source is not ready for preview.",
				"staged_photo_preview_source_not_ready"));
	}

	const platform::ImagePixels* cached_pixels = cache.find(identity);
	if (cached_pixels != nullptr) {
		return StagedPhotoPreviewLoadResult{
			.status		 = ImagePreviewLoadStatus::Loaded,
			.category	 = core::OperationResultCategory::Success,
			.identity	 = std::move(identity),
			.pixels		 = *cached_pixels,
			.staged_path = std::move(staged_path),
			.cache_hit	 = true};
	}

	if (!staged_path_readable(*request.source.staged_path)) {
		return staged_preview_failure_result(
			std::move(identity), std::move(staged_path),
			core::OperationResultCategory::SourceUnavailable,
			make_preview_diagnostic(
				core::DiagnosticSeverity::RecoverableWarning,
				"staged_photo_preview_source_missing",
				"Staged photo source file is missing or unreadable.",
				request.source.staged_path->string()),
			make_preview_placeholder("The staged photo source file is missing.",
									 "staged_photo_preview_source_missing"));
	}

	platform::PlatformValueResult<platform::ImagePixels> decoded =
		decode_service.decode_source_image(
			platform::SourceImageDecodeRequest{
				.content = staged_content_for_preview(request.source)},
			platform::PlatformOperationContext{
				.operation_id = request.identifiers.next_operation_identifier(),
				.operation_type =
					platform::ProgressOperationType::ImagePreview},
			progress_sink, cancellation_token);
	if (decoded.was_user_cancelled()) {
		return StagedPhotoPreviewLoadResult{
			.status		 = ImagePreviewLoadStatus::Cancelled,
			.category	 = core::OperationResultCategory::UserCancelled,
			.identity	 = std::move(identity),
			.diagnostics = std::move(decoded.diagnostics),
			.staged_path = std::move(staged_path)};
	}
	if (decoded.failed()) {
		const std::string diagnostic_code =
			decoded.diagnostics.empty() ? "staged_photo_preview_decode_failed"
										: decoded.diagnostics.front().code;
		return StagedPhotoPreviewLoadResult{
			.status		 = ImagePreviewLoadStatus::Broken,
			.category	 = decoded.category,
			.identity	 = std::move(identity),
			.diagnostics = std::move(decoded.diagnostics),
			.placeholder = make_preview_placeholder(
				"The staged photo source could not be decoded for preview.",
				diagnostic_code),
			.staged_path = std::move(staged_path)};
	}

	std::optional<platform::ImagePixels> scaled_pixels =
		scale_image_pixels_for_preview(*decoded.value, request.target_size);
	if (!scaled_pixels.has_value()) {
		return StagedPhotoPreviewLoadResult{
			.status		 = ImagePreviewLoadStatus::Broken,
			.category	 = core::OperationResultCategory::CodecFailure,
			.identity	 = std::move(identity),
			.diagnostics = {make_preview_diagnostic(
				core::DiagnosticSeverity::WriteBlockingError,
				"staged_photo_preview_scale_failed",
				"Decoded staged photo pixels could not be scaled for preview.",
				dimensions_text(decoded.value->width, decoded.value->height))},
			.placeholder = make_preview_placeholder(
				"The staged photo source could not be prepared for preview.",
				"staged_photo_preview_scale_failed"),
			.staged_path = std::move(staged_path)};
	}

	ImagePreviewLoadMetrics metrics{
		.decoded_width				 = decoded.value->width,
		.decoded_height				 = decoded.value->height,
		.preview_width				 = scaled_pixels->width,
		.preview_height				 = scaled_pixels->height,
		.decode_elapsed_milliseconds = decoded.value->elapsed_milliseconds};
	platform::ImagePixels preview_pixels = std::move(*scaled_pixels);
	preview_pixels.source_description	 = metrics_message(metrics);
	const bool cache_stored				 = cache.put(identity, preview_pixels);
	if (!cache_stored) {
		decoded.diagnostics.push_back(make_preview_diagnostic(
			core::DiagnosticSeverity::RecoverableWarning,
			"image_preview_cache_rejected",
			"Scaled staged photo preview was loaded but not stored in cache.",
			identity.source_key));
	}

	return StagedPhotoPreviewLoadResult{
		.status		  = ImagePreviewLoadStatus::Loaded,
		.category	  = core::OperationResultCategory::Success,
		.identity	  = std::move(identity),
		.diagnostics  = std::move(decoded.diagnostics),
		.pixels		  = std::move(preview_pixels),
		.staged_path  = std::move(staged_path),
		.metrics	  = metrics,
		.cache_stored = cache_stored};
}
}	 // namespace shuba::ui
