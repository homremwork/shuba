#include "Platform/JuceAndroidServices.hpp"

#include <juce_graphics/juce_graphics.h>

#include <chrono>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
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

template<class Value>
[[nodiscard]] PlatformValueResult<Value> jpeg_failure(
	core::OperationResultCategory category, core::DiagnosticSeverity severity,
	std::string code, std::string message, std::string technical_details = {}) {
	return platform_value_failure<Value>(
		category, make_diagnostic(severity, std::move(code), std::move(message),
								  std::move(technical_details)));
}

[[nodiscard]] std::uint64_t elapsed_milliseconds_since(
	std::chrono::steady_clock::time_point started_at) {
	const std::chrono::steady_clock::duration elapsed =
		std::chrono::steady_clock::now() - started_at;
	return static_cast<std::uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

[[nodiscard]] std::optional<int> checked_image_dimension(
	std::uint32_t dimension) noexcept {
	if (dimension == 0U
		|| dimension
			   > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
		return std::nullopt;
	}

	return static_cast<int>(dimension);
}

[[nodiscard]] PlatformValueResult<juce::Image> make_juce_image(
	const ImagePixels& pixels) {
	ImagePixelsValidation validation = validate_image_pixels(pixels);
	if (!validation.valid()) {
		return jpeg_failure<juce::Image>(
			core::OperationResultCategory::ValidationFailure,
			core::DiagnosticSeverity::ActionValidationError,
			"invalid-jpeg-export-pixels",
			"Source image pixels are not valid for JPEG export.",
			std::string{to_string(validation.issue)});
	}

	std::optional<int> width  = checked_image_dimension(pixels.width);
	std::optional<int> height = checked_image_dimension(pixels.height);
	if (!width.has_value() || !height.has_value()) {
		return jpeg_failure<juce::Image>(
			core::OperationResultCategory::ValidationFailure,
			core::DiagnosticSeverity::ActionValidationError,
			"jpeg-export-dimensions-too-large",
			"Source image dimensions are too large for JUCE JPEG export.");
	}

	juce::Image image{juce::Image::RGB, *width, *height, false};
	juce::Image::BitmapData bitmap{image, juce::Image::BitmapData::writeOnly};
	for (int y = 0; y < *height; ++y) {
		const std::size_t source_row_offset =
			static_cast<std::size_t>(y) * static_cast<std::size_t>(*width) * 4U;
		juce::uint8* row = bitmap.getLinePointer(y);
		for (int x = 0; x < *width; ++x) {
			const std::size_t source_offset =
				source_row_offset + static_cast<std::size_t>(x) * 4U;
			juce::uint8* target =
				row + static_cast<std::ptrdiff_t>(x) * bitmap.pixelStride;
			juce::PixelRGB* target_pixel =
				reinterpret_cast<juce::PixelRGB*>(target);
			target_pixel->setARGB(0xffU, pixels.bytes[source_offset],
								  pixels.bytes[source_offset + 1U],
								  pixels.bytes[source_offset + 2U]);
		}
	}

	return platform_value_success(std::move(image));
}

[[nodiscard]] PlatformValueResult<std::uint64_t> file_size_result(
	const std::filesystem::path& output_path) {
	std::error_code error;
	const std::uintmax_t size = std::filesystem::file_size(output_path, error);
	if (error) {
		return jpeg_failure<std::uint64_t>(
			core::OperationResultCategory::TemporaryStorageFailure,
			core::DiagnosticSeverity::WriteBlockingError,
			"jpeg-export-size-unavailable",
			"Temporary JPEG export size could not be read.", error.message());
	}
	if (size == 0U) {
		return jpeg_failure<std::uint64_t>(
			core::OperationResultCategory::TemporaryStorageFailure,
			core::DiagnosticSeverity::WriteBlockingError,
			"jpeg-export-empty-output",
			"JUCE JPEG writer produced an empty output file.",
			output_path.string());
	}

	return platform_value_success(static_cast<std::uint64_t>(size));
}
}	 // namespace

PlatformValueResult<MediaWriteResult> JuceJpegExportService::write_jpeg(
	const JpegExportRequest& request, const PlatformOperationContext& context,
	ProgressSink& progress_sink, CancellationToken& cancellation_token) {
	if (cancellation_token.cancellation_requested())
		return platform_value_user_cancelled<MediaWriteResult>();

	if (request.output_path.empty()) {
		return jpeg_failure<MediaWriteResult>(
			core::OperationResultCategory::ValidationFailure,
			core::DiagnosticSeverity::ActionValidationError,
			"empty-jpeg-output-path", "JPEG output path is required.");
	}

	if (!validate_jpeg_export_quality(request.quality)) {
		return jpeg_failure<MediaWriteResult>(
			core::OperationResultCategory::ValidationFailure,
			core::DiagnosticSeverity::ActionValidationError,
			"invalid-jpeg-quality",
			"JPEG export quality must be in the range 1..100.");
	}

	const std::chrono::steady_clock::time_point started_at =
		std::chrono::steady_clock::now();
	progress_sink.publish_progress(ProgressEvent{
		.operation_id	= context.operation_id,
		.operation_type = context.operation_type,
		.phase			= "jpeg-write-started",
		.message_id		= ProgressMessageId::JpegWriteStarted,
		.current_units	= std::uint64_t{0},
		.total_units = static_cast<std::uint64_t>(request.pixels.bytes.size()),
		.message	 = "JPEG write started.",
		.cancellable = true});

	PlatformValueResult<juce::Image> image = make_juce_image(request.pixels);
	if (image.failed())
		return platform_value_failure<MediaWriteResult>(
			image.category, std::move(image.diagnostics.front()));

	if (cancellation_token.cancellation_requested())
		return platform_value_user_cancelled<MediaWriteResult>();

	std::error_code error;
	std::filesystem::create_directories(request.output_path.parent_path(),
										error);
	if (error) {
		return jpeg_failure<MediaWriteResult>(
			core::OperationResultCategory::TemporaryStorageFailure,
			core::DiagnosticSeverity::WriteBlockingError,
			"jpeg-output-directory-unavailable",
			"JPEG output directory could not be created.", error.message());
	}

	juce::File output_file{juce::String{request.output_path.string()}};
	std::unique_ptr<juce::FileOutputStream> output =
		output_file.createOutputStream();
	if (output == nullptr || output->failedToOpen()) {
		return jpeg_failure<MediaWriteResult>(
			core::OperationResultCategory::TemporaryStorageFailure,
			core::DiagnosticSeverity::WriteBlockingError,
			"jpeg-output-unavailable", "JPEG output file could not be opened.",
			request.output_path.string());
	}
	output->setPosition(0);
	output->truncate();

	juce::JPEGImageFormat jpeg_format;
	jpeg_format.setQuality(static_cast<float>(request.quality) / 100.0F);
	if (!jpeg_format.writeImageToStream(*image.value, *output)) {
		output.reset();
		std::filesystem::remove(request.output_path, error);
		return jpeg_failure<MediaWriteResult>(
			core::OperationResultCategory::CodecFailure,
			core::DiagnosticSeverity::WriteBlockingError, "jpeg-write-failed",
			"JUCE JPEG writer failed to encode the image.",
			request.output_path.string());
	}
	output->flush();
	if (output->getStatus().failed()) {
		const std::string details =
			output->getStatus().getErrorMessage().toStdString();
		output.reset();
		std::filesystem::remove(request.output_path, error);
		return jpeg_failure<MediaWriteResult>(
			core::OperationResultCategory::TemporaryStorageFailure,
			core::DiagnosticSeverity::WriteBlockingError, "jpeg-flush-failed",
			"JPEG output file could not be flushed.", details);
	}
	output.reset();

	PlatformValueResult<std::uint64_t> file_size =
		file_size_result(request.output_path);
	if (file_size.failed()) {
		std::filesystem::remove(request.output_path, error);
		return platform_value_failure<MediaWriteResult>(
			file_size.category, std::move(file_size.diagnostics.front()));
	}

	progress_sink.publish_progress(ProgressEvent{
		.operation_id	= context.operation_id,
		.operation_type = context.operation_type,
		.phase			= "jpeg-write-completed",
		.message_id		= ProgressMessageId::JpegWriteCompleted,
		.current_units =
			static_cast<std::uint64_t>(request.pixels.bytes.size()),
		.total_units = static_cast<std::uint64_t>(request.pixels.bytes.size()),
		.message	 = "JPEG write completed.",
		.cancellable = false});

	return platform_value_success(MediaWriteResult{
		.file_path			  = request.output_path,
		.bytes_written		  = *file_size.value,
		.width				  = request.pixels.width,
		.height				  = request.pixels.height,
		.elapsed_milliseconds = elapsed_milliseconds_since(started_at),
		.codec_description	  = "JUCE JPEGImageFormat RGB export"});
}
}	 // namespace shuba::platform
