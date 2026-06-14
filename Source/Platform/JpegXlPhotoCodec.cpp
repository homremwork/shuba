#include "Platform/JpegXlPhotoCodec.hpp"

#include <jxl/decode.h>
#include <jxl/decode_cxx.h>
#include <jxl/encode.h>
#include <jxl/encode_cxx.h>
#include <jxl/resizable_parallel_runner.h>
#include <jxl/resizable_parallel_runner_cxx.h>
#include <jxl/thread_parallel_runner.h>
#include <jxl/thread_parallel_runner_cxx.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

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
[[nodiscard]] PlatformValueResult<Value> codec_failure(
	std::string code, std::string message, std::string technical_details = {}) {
	return platform_value_failure<Value>(
		core::OperationResultCategory::CodecFailure,
		make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
						std::move(code), std::move(message),
						std::move(technical_details)));
}

[[nodiscard]] std::uint64_t elapsed_milliseconds_since(
	std::chrono::steady_clock::time_point started_at) {
	const std::chrono::steady_clock::duration elapsed =
		std::chrono::steady_clock::now() - started_at;
	return static_cast<std::uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

[[nodiscard]] PlatformValueResult<std::vector<std::uint8_t>> read_binary_file(
	const std::filesystem::path& path) {
	std::ifstream input{path, std::ios::binary};
	if (!input) {
		return platform_value_failure<std::vector<std::uint8_t>>(
			core::OperationResultCategory::SourceUnavailable,
			make_diagnostic(
				core::DiagnosticSeverity::WriteBlockingError,
				"jxl-source-unavailable",
				"JPEG XL media file could not be opened for reading.",
				path.string()));
	}

	std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>{input},
									std::istreambuf_iterator<char>{}};
	if (input.bad()) {
		return platform_value_failure<std::vector<std::uint8_t>>(
			core::OperationResultCategory::SourceUnavailable,
			make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
							"jxl-source-read-failed",
							"JPEG XL media file could not be read completely.",
							path.string()));
	}

	if (bytes.empty())
		return codec_failure<std::vector<std::uint8_t>>(
			"empty-jxl-source", "JPEG XL media file is empty.", path.string());

	return platform_value_success(std::move(bytes));
}

[[nodiscard]] PlatformValueResult<ImagePixels> decode_jpeg_xl_bytes(
	const std::vector<std::uint8_t>& encoded,
	const std::string& source_description,
	const std::uint64_t started_elapsed_offset = 0U) {
	const std::chrono::steady_clock::time_point started_at =
		std::chrono::steady_clock::now();

	JxlDecoderPtr decoder = JxlDecoderMake(nullptr);
	if (decoder == nullptr)
		return codec_failure<ImagePixels>(
			"jxl-decoder-create-failed",
			"JPEG XL decoder could not be created.");

	JxlResizableParallelRunnerPtr runner =
		JxlResizableParallelRunnerMake(nullptr);
	if (runner == nullptr)
		return codec_failure<ImagePixels>(
			"jxl-runner-create-failed",
			"JPEG XL parallel runner could not be created.");

	if (JxlDecoderSetParallelRunner(decoder.get(), JxlResizableParallelRunner,
									runner.get())
		!= JXL_DEC_SUCCESS) {
		return codec_failure<ImagePixels>(
			"jxl-decoder-runner-failed",
			"JPEG XL decoder parallel runner setup failed.");
	}

	if (JxlDecoderSubscribeEvents(decoder.get(),
								  JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE)
		!= JXL_DEC_SUCCESS) {
		return codec_failure<ImagePixels>(
			"jxl-decoder-subscribe-failed",
			"JPEG XL decoder event subscription failed.");
	}

	JxlDecoderSetInput(decoder.get(), encoded.data(), encoded.size());
	JxlDecoderCloseInput(decoder.get());

	JxlPixelFormat pixel_format{4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
	JxlBasicInfo basic_info{};
	std::vector<std::uint8_t> pixels;
	bool have_basic_info{};

	for (;;) {
		const JxlDecoderStatus status = JxlDecoderProcessInput(decoder.get());
		switch (status) {
			case JXL_DEC_ERROR:
				return codec_failure<ImagePixels>(
					"jxl-decode-error", "JPEG XL decoder reported an error.");
			case JXL_DEC_NEED_MORE_INPUT:
				return codec_failure<ImagePixels>(
					"jxl-decode-incomplete",
					"JPEG XL decoder unexpectedly requested more input.");
			case JXL_DEC_BASIC_INFO:
				if (JxlDecoderGetBasicInfo(decoder.get(), &basic_info)
					!= JXL_DEC_SUCCESS) {
					return codec_failure<ImagePixels>(
						"jxl-basic-info-failed",
						"JPEG XL basic image information could not be read.");
				}
				have_basic_info = true;
				JxlResizableParallelRunnerSetThreads(
					runner.get(), JxlResizableParallelRunnerSuggestThreads(
									  basic_info.xsize, basic_info.ysize));
				break;
			case JXL_DEC_NEED_IMAGE_OUT_BUFFER: {
				std::size_t output_size{};
				if (JxlDecoderImageOutBufferSize(decoder.get(), &pixel_format,
												 &output_size)
					!= JXL_DEC_SUCCESS) {
					return codec_failure<ImagePixels>(
						"jxl-output-size-failed",
						"JPEG XL output pixel buffer size could not be read.");
				}
				pixels.resize(output_size);
				if (JxlDecoderSetImageOutBuffer(decoder.get(), &pixel_format,
												pixels.data(), pixels.size())
					!= JXL_DEC_SUCCESS) {
					return codec_failure<ImagePixels>(
						"jxl-output-buffer-failed",
						"JPEG XL output pixel buffer could not be attached.");
				}
				break;
			}
			case JXL_DEC_FULL_IMAGE:
				break;
			case JXL_DEC_SUCCESS: {
				if (!have_basic_info || pixels.empty()) {
					return codec_failure<ImagePixels>(
						"jxl-decode-empty",
						"JPEG XL decode completed without image pixels.");
				}

				ImagePixels decoded{
					.width				= basic_info.xsize,
					.height				= basic_info.ysize,
					.format				= PixelFormat::Rgba8,
					.bytes				= std::move(pixels),
					.source_description = source_description,
					.orientation_description =
						"JPEG XL decoded to canonical RGBA8 pixels.",
					.elapsed_milliseconds =
						started_elapsed_offset
						+ elapsed_milliseconds_since(started_at)};
				ImagePixelsValidation validation =
					validate_image_pixels(decoded);
				if (!validation.valid()) {
					return codec_failure<ImagePixels>(
						"jxl-decoded-pixels-invalid",
						"JPEG XL decoded pixel buffer size is invalid.",
						std::string{to_string(validation.issue)});
				}
				return platform_value_success(std::move(decoded));
			}
			default:
				return codec_failure<ImagePixels>(
					"jxl-decode-unexpected-status",
					"JPEG XL decoder returned an unexpected status.",
					std::to_string(static_cast<int>(status)));
		}
	}
}

[[nodiscard]] PlatformValueResult<MediaWriteResult> write_binary_file(
	const std::filesystem::path& output_path,
	const std::vector<std::uint8_t>& bytes, std::uint32_t width,
	std::uint32_t height, std::uint64_t elapsed_milliseconds) {
	std::error_code error;
	std::filesystem::create_directories(output_path.parent_path(), error);
	if (error) {
		return platform_value_failure<MediaWriteResult>(
			core::OperationResultCategory::TemporaryStorageFailure,
			make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
							"jxl-output-directory-unavailable",
							"JPEG XL output directory could not be created.",
							error.message()));
	}

	std::ofstream output{output_path, std::ios::binary | std::ios::trunc};
	if (!output) {
		return platform_value_failure<MediaWriteResult>(
			core::OperationResultCategory::TemporaryStorageFailure,
			make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
							"jxl-output-unavailable",
							"JPEG XL output file could not be opened.",
							output_path.string()));
	}

	output.write(reinterpret_cast<const char*>(bytes.data()),
				 static_cast<std::streamsize>(bytes.size()));
	output.flush();
	if (!output) {
		return platform_value_failure<MediaWriteResult>(
			core::OperationResultCategory::TemporaryStorageFailure,
			make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
							"jxl-output-write-failed",
							"JPEG XL output file could not be written.",
							output_path.string()));
	}

	return platform_value_success(MediaWriteResult{
		.file_path			  = output_path,
		.bytes_written		  = static_cast<std::uint64_t>(bytes.size()),
		.width				  = width,
		.height				  = height,
		.elapsed_milliseconds = elapsed_milliseconds,
		.codec_description	  = "libjxl RGBA8 high-quality lossy encode"});
}
}	 // namespace

PlatformValueResult<MediaWriteResult>
JpegXlInternalPhotoCodec::encode_internal_photo(
	const InternalPhotoEncodeRequest& request,
	const PlatformOperationContext& context, ProgressSink& progress_sink,
	CancellationToken& cancellation_token) {
	if (cancellation_token.cancellation_requested())
		return platform_value_user_cancelled<MediaWriteResult>();

	ImagePixelsValidation pixels_validation =
		validate_image_pixels(request.pixels);
	if (!pixels_validation.valid()) {
		return platform_value_failure<MediaWriteResult>(
			core::OperationResultCategory::ValidationFailure,
			make_diagnostic(
				core::DiagnosticSeverity::ActionValidationError,
				"invalid-source-pixels",
				"Source image pixels are not valid for JPEG XL encode.",
				std::string{to_string(pixels_validation.issue)}));
	}

	if (!validate_internal_photo_encode_settings(request.settings)) {
		return platform_value_failure<MediaWriteResult>(
			core::OperationResultCategory::ValidationFailure,
			make_diagnostic(core::DiagnosticSeverity::ActionValidationError,
							"invalid-jxl-encode-settings",
							"JPEG XL encode settings are outside the accepted "
							"spike range."));
	}

	if (request.output_path.empty()) {
		return platform_value_failure<MediaWriteResult>(
			core::OperationResultCategory::ValidationFailure,
			make_diagnostic(core::DiagnosticSeverity::ActionValidationError,
							"empty-jxl-output-path",
							"JPEG XL output path is required."));
	}

	const std::chrono::steady_clock::time_point started_at =
		std::chrono::steady_clock::now();
	progress_sink.publish_progress(
		ProgressEvent{.operation_id	  = context.operation_id,
					  .operation_type = context.operation_type,
					  .phase		  = "jxl-encode-started",
					  .current_units  = std::uint64_t{0},
					  .total_units	  = pixels_validation.expected_byte_count,
					  .message		  = "JPEG XL encode started.",
					  .cancellable	  = true});

	JxlEncoderPtr encoder = JxlEncoderMake(nullptr);
	if (encoder == nullptr)
		return codec_failure<MediaWriteResult>(
			"jxl-encoder-create-failed",
			"JPEG XL encoder could not be created.");

	JxlThreadParallelRunnerPtr runner = JxlThreadParallelRunnerMake(
		nullptr, JxlThreadParallelRunnerDefaultNumWorkerThreads());
	if (runner == nullptr)
		return codec_failure<MediaWriteResult>(
			"jxl-runner-create-failed",
			"JPEG XL parallel runner could not be created.");

	if (JxlEncoderSetParallelRunner(encoder.get(), JxlThreadParallelRunner,
									runner.get())
		!= JXL_ENC_SUCCESS) {
		return codec_failure<MediaWriteResult>(
			"jxl-encoder-runner-failed",
			"JPEG XL encoder parallel runner setup failed.");
	}

	JxlBasicInfo basic_info{};
	JxlEncoderInitBasicInfo(&basic_info);
	basic_info.xsize					= request.pixels.width;
	basic_info.ysize					= request.pixels.height;
	basic_info.bits_per_sample			= 8;
	basic_info.exponent_bits_per_sample = 0;
	basic_info.num_color_channels		= 3;
	basic_info.num_extra_channels		= 1;
	basic_info.alpha_bits				= 8;
	basic_info.uses_original_profile	= JXL_FALSE;
	if (JxlEncoderSetBasicInfo(encoder.get(), &basic_info) != JXL_ENC_SUCCESS) {
		return codec_failure<MediaWriteResult>(
			"jxl-basic-info-failed",
			"JPEG XL encoder basic image information setup failed.");
	}

	JxlColorEncoding color_encoding{};
	JxlColorEncodingSetToSRGB(&color_encoding, JXL_FALSE);
	if (JxlEncoderSetColorEncoding(encoder.get(), &color_encoding)
		!= JXL_ENC_SUCCESS) {
		return codec_failure<MediaWriteResult>(
			"jxl-color-encoding-failed",
			"JPEG XL encoder color encoding setup failed.");
	}

	JxlEncoderFrameSettings* frame_settings =
		JxlEncoderFrameSettingsCreate(encoder.get(), nullptr);
	if (frame_settings == nullptr)
		return codec_failure<MediaWriteResult>(
			"jxl-frame-settings-failed",
			"JPEG XL encoder frame settings could not be created.");

	if (JxlEncoderSetFrameLossless(frame_settings, JXL_FALSE)
		!= JXL_ENC_SUCCESS) {
		return codec_failure<MediaWriteResult>(
			"jxl-lossy-setting-failed",
			"JPEG XL lossy frame setting could not be applied.");
	}

	const float distance =
		JxlEncoderDistanceFromQuality(request.settings.quality);
	if (JxlEncoderSetFrameDistance(frame_settings, distance)
		!= JXL_ENC_SUCCESS) {
		return codec_failure<MediaWriteResult>(
			"jxl-quality-setting-failed",
			"JPEG XL quality setting could not be applied.");
	}

	if (JxlEncoderFrameSettingsSetOption(
			frame_settings, JXL_ENC_FRAME_SETTING_EFFORT,
			static_cast<std::int64_t>(request.settings.effort))
		!= JXL_ENC_SUCCESS) {
		return codec_failure<MediaWriteResult>(
			"jxl-effort-setting-failed",
			"JPEG XL effort setting could not be applied.");
	}

	JxlPixelFormat pixel_format{4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
	if (JxlEncoderAddImageFrame(frame_settings, &pixel_format,
								request.pixels.bytes.data(),
								request.pixels.bytes.size())
		!= JXL_ENC_SUCCESS) {
		return codec_failure<MediaWriteResult>(
			"jxl-add-frame-failed", "JPEG XL image frame could not be added.");
	}

	JxlEncoderCloseInput(encoder.get());

	std::vector<std::uint8_t> encoded(4096U);
	std::uint8_t* next_output	   = encoded.data();
	std::size_t available_output   = encoded.size();
	JxlEncoderStatus encode_status = JXL_ENC_NEED_MORE_OUTPUT;
	while ((encode_status = JxlEncoderProcessOutput(encoder.get(), &next_output,
													&available_output))
		   == JXL_ENC_NEED_MORE_OUTPUT) {
		if (cancellation_token.cancellation_requested())
			return platform_value_user_cancelled<MediaWriteResult>();

		const std::size_t offset =
			static_cast<std::size_t>(next_output - encoded.data());
		if (encoded.size() > std::numeric_limits<std::size_t>::max() / 2U)
			return codec_failure<MediaWriteResult>(
				"jxl-output-too-large", "JPEG XL output grew too large.");
		encoded.resize(encoded.size() * 2U);
		next_output		 = encoded.data() + offset;
		available_output = encoded.size() - offset;
	}

	if (encode_status != JXL_ENC_SUCCESS) {
		return codec_failure<MediaWriteResult>(
			"jxl-process-output-failed",
			"JPEG XL encoder failed while producing output.",
			std::to_string(static_cast<int>(encode_status)));
	}

	encoded.resize(static_cast<std::size_t>(next_output - encoded.data()));
	if (encoded.empty())
		return codec_failure<MediaWriteResult>(
			"jxl-empty-output", "JPEG XL encoder produced empty output.");

	const std::uint64_t elapsed_before_write =
		elapsed_milliseconds_since(started_at);
	PlatformValueResult<MediaWriteResult> written =
		write_binary_file(request.output_path, encoded, request.pixels.width,
						  request.pixels.height, elapsed_before_write);
	if (written.failed())
		return written;

	if (request.settings.validate_after_encode) {
		PlatformValueResult<ImagePixels> decoded =
			decode_jpeg_xl_bytes(encoded, "libjxl immediate encode validation",
								 elapsed_before_write);
		if (decoded.failed()) {
			std::error_code ignored;
			std::filesystem::remove(request.output_path, ignored);
			return platform_value_failure<MediaWriteResult>(
				decoded.category, std::move(decoded.diagnostics.front()));
		}

		if (decoded.value->width != request.pixels.width
			|| decoded.value->height != request.pixels.height) {
			std::error_code ignored;
			std::filesystem::remove(request.output_path, ignored);
			return codec_failure<MediaWriteResult>(
				"jxl-validation-dimensions-mismatch",
				"Immediate JPEG XL decode validation changed image "
				"dimensions.");
		}

		written.value->elapsed_milliseconds =
			decoded.value->elapsed_milliseconds;
	}

	progress_sink.publish_progress(ProgressEvent{
		.operation_id	= context.operation_id,
		.operation_type = context.operation_type,
		.phase			= "jxl-encode-completed",
		.current_units =
			static_cast<std::uint64_t>(request.pixels.bytes.size()),
		.total_units = pixels_validation.expected_byte_count,
		.message	 = "JPEG XL encode completed.",
		.cancellable = false});
	return written;
}

PlatformValueResult<ImagePixels>
JpegXlInternalPhotoCodec::decode_internal_photo(
	const InternalPhotoDecodeRequest& request,
	const PlatformOperationContext& context, ProgressSink& progress_sink,
	CancellationToken& cancellation_token) {
	if (cancellation_token.cancellation_requested())
		return platform_value_user_cancelled<ImagePixels>();

	progress_sink.publish_progress(
		ProgressEvent{.operation_id	  = context.operation_id,
					  .operation_type = context.operation_type,
					  .phase		  = "jxl-decode-started",
					  .current_units  = std::uint64_t{0},
					  .message		  = "JPEG XL decode started.",
					  .cancellable	  = true});

	PlatformValueResult<std::vector<std::uint8_t>> encoded =
		read_binary_file(request.input_path);
	if (encoded.failed())
		return platform_value_failure<ImagePixels>(
			encoded.category, std::move(encoded.diagnostics.front()));

	PlatformValueResult<ImagePixels> decoded =
		decode_jpeg_xl_bytes(*encoded.value, "libjxl internal JPEG XL decode");
	if (decoded.succeeded()) {
		progress_sink.publish_progress(ProgressEvent{
			.operation_id	= context.operation_id,
			.operation_type = context.operation_type,
			.phase			= "jxl-decode-completed",
			.current_units =
				static_cast<std::uint64_t>(decoded.value->bytes.size()),
			.total_units =
				static_cast<std::uint64_t>(decoded.value->bytes.size()),
			.message	 = "JPEG XL decode completed.",
			.cancellable = false});
	}

	return decoded;
}
}	 // namespace shuba::platform
