#include "Platform/JpegXlPhotoCodec.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

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

[[nodiscard]] shuba::core::OperationIdentifier operation_id(std::string value) {
	std::optional<shuba::core::OperationIdentifier> identifier =
		shuba::core::OperationIdentifier::try_create_file_safe(
			std::move(value));
	REQUIRE(identifier.has_value());
	return *identifier;
}

[[nodiscard]] shuba::platform::ImagePixels test_pixels() {
	return shuba::platform::ImagePixels{
		.width	= 3,
		.height = 2,
		.format = shuba::platform::PixelFormat::Rgba8,
		.bytes	= {255, 0,	 0, 255, 0, 255, 0,	  255, 0,	0, 255, 255,
				   255, 255, 0, 255, 0, 255, 255, 255, 255, 0, 255, 255},
		.source_description = "R05 synthetic RGBA8 source"};
}

[[nodiscard]] bool has_progress_message(
	const shuba::platform::ProgressCollector& progress,
	shuba::platform::ProgressMessageId message_id) {
	for (const shuba::platform::ProgressEvent& event : progress.events())
		if (event.message_id == message_id)
			return true;
	return false;
}
}	 // namespace

TEST_CASE("R05 libjxl 0.12 codec encodes and decodes the application contract",
		  "[r05][jpeg-xl][round-trip]") {
	TemporaryDirectory temporary{"shuba-r05-jpeg-xl"};
	const std::filesystem::path output_path =
		temporary.path() / "round-trip.jxl";
	shuba::platform::JpegXlInternalPhotoCodec codec;
	shuba::platform::ProgressCollector progress;
	shuba::platform::NeverCancelledToken cancellation;
	const shuba::platform::ImagePixels source = test_pixels();
	const shuba::platform::PlatformOperationContext encode_context{
		.operation_id	= operation_id("operation-r05-encode"),
		.operation_type = shuba::platform::ProgressOperationType::PhotoImport};

	shuba::platform::PlatformValueResult<shuba::platform::MediaWriteResult>
		encoded = codec.encode_internal_photo(
			shuba::platform::InternalPhotoEncodeRequest{
				.pixels		 = source,
				.output_path = output_path,
				.settings =
					shuba::platform::default_internal_photo_encode_settings()},
			encode_context, progress, cancellation);

	REQUIRE(encoded.succeeded());
	REQUIRE(encoded.value->file_path == output_path);
	REQUIRE(encoded.value->bytes_written > std::uint64_t{0});
	REQUIRE(encoded.value->width == source.width);
	REQUIRE(encoded.value->height == source.height);
	REQUIRE(std::filesystem::file_size(output_path)
			== encoded.value->bytes_written);
	REQUIRE(has_progress_message(
		progress, shuba::platform::ProgressMessageId::JpegXlEncodeStarted));
	REQUIRE(has_progress_message(
		progress, shuba::platform::ProgressMessageId::JpegXlEncodeCompleted));

	progress.clear();
	const shuba::platform::PlatformOperationContext decode_context{
		.operation_id	= operation_id("operation-r05-decode"),
		.operation_type = shuba::platform::ProgressOperationType::ImagePreview};
	shuba::platform::PlatformValueResult<shuba::platform::ImagePixels> decoded =
		codec.decode_internal_photo(
			shuba::platform::InternalPhotoDecodeRequest{.input_path =
															output_path},
			decode_context, progress, cancellation);

	REQUIRE(decoded.succeeded());
	REQUIRE(decoded.value->width == source.width);
	REQUIRE(decoded.value->height == source.height);
	REQUIRE(decoded.value->format == shuba::platform::PixelFormat::Rgba8);
	REQUIRE(decoded.value->bytes.size() == source.bytes.size());
	REQUIRE(shuba::platform::validate_image_pixels(*decoded.value).valid());
	REQUIRE(has_progress_message(
		progress, shuba::platform::ProgressMessageId::JpegXlDecodeStarted));
	REQUIRE(has_progress_message(
		progress, shuba::platform::ProgressMessageId::JpegXlDecodeCompleted));
}

TEST_CASE("R05 real JPEG XL codec preserves cancellation before work",
		  "[r05][jpeg-xl][cancellation]") {
	TemporaryDirectory temporary{"shuba-r05-jpeg-xl-cancel"};
	const std::filesystem::path output_path =
		temporary.path() / "cancelled.jxl";
	shuba::platform::JpegXlInternalPhotoCodec codec;
	shuba::platform::ProgressCollector progress;
	shuba::platform::ManualCancellationToken cancellation;
	cancellation.request_cancellation();
	const shuba::platform::PlatformOperationContext context{
		.operation_id	= operation_id("operation-r05-cancel"),
		.operation_type = shuba::platform::ProgressOperationType::PhotoImport};

	shuba::platform::PlatformValueResult<shuba::platform::MediaWriteResult>
		encoded = codec.encode_internal_photo(
			shuba::platform::InternalPhotoEncodeRequest{
				.pixels		 = test_pixels(),
				.output_path = output_path,
				.settings =
					shuba::platform::default_internal_photo_encode_settings()},
			context, progress, cancellation);

	REQUIRE(encoded.was_user_cancelled());
	REQUIRE_FALSE(std::filesystem::exists(output_path));
	REQUIRE(progress.events().empty());
}
