#include "Platform/PlatformServices.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <optional>

TEST_CASE("B12 image pixel buffers are validated before real media codecs",
		  "[b12][media][pixels]") {
	using shuba::platform::image_pixel_byte_count;
	using shuba::platform::ImagePixels;
	using shuba::platform::ImagePixelsValidation;
	using shuba::platform::ImagePixelsValidationIssue;
	using shuba::platform::PixelFormat;
	using shuba::platform::to_string;
	using shuba::platform::validate_image_pixels;

	ImagePixels valid{.width  = 2,
					  .height = 2,
					  .format = PixelFormat::Rgba8,
					  .bytes  = {0, 0, 0, 255, 10, 20, 30, 255, 40, 50, 60, 255,
								 70, 80, 90, 255},
					  .source_description = "contract"};
	ImagePixelsValidation valid_result = validate_image_pixels(valid);
	REQUIRE(valid_result.valid());
	REQUIRE(valid_result.expected_byte_count == std::uint64_t{16});
	REQUIRE(valid_result.actual_byte_count == std::uint64_t{16});
	REQUIRE(image_pixel_byte_count(2U, 2U, PixelFormat::Rgba8)
			== std::optional<std::uint64_t>{16});

	valid.bytes.pop_back();
	ImagePixelsValidation mismatch = validate_image_pixels(valid);
	REQUIRE_FALSE(mismatch.valid());
	REQUIRE(mismatch.issue == ImagePixelsValidationIssue::ByteCountMismatch);
	REQUIRE(to_string(mismatch.issue) == "byte count mismatch");

	valid.width					= 0U;
	ImagePixelsValidation empty = validate_image_pixels(valid);
	REQUIRE_FALSE(empty);
	REQUIRE(empty.issue == ImagePixelsValidationIssue::EmptyDimensions);
}

TEST_CASE("B12 pixel byte-count validation rejects arithmetic overflow",
		  "[b12][media][pixels]") {
	using shuba::platform::image_pixel_byte_count;
	using shuba::platform::ImagePixels;
	using shuba::platform::ImagePixelsValidation;
	using shuba::platform::ImagePixelsValidationIssue;
	using shuba::platform::PixelFormat;
	using shuba::platform::validate_image_pixels;

	const std::uint32_t huge_dimension =
		std::numeric_limits<std::uint32_t>::max();
	REQUIRE_FALSE(image_pixel_byte_count(huge_dimension, huge_dimension,
										 PixelFormat::Rgba8)
					  .has_value());

	const ImagePixels overflowing_pixels{.width	 = huge_dimension,
										 .height = huge_dimension,
										 .format = PixelFormat::Rgba8,
										 .bytes	 = {}};
	ImagePixelsValidation overflow = validate_image_pixels(overflowing_pixels);
	REQUIRE_FALSE(overflow.valid());
	REQUIRE(overflow.issue == ImagePixelsValidationIssue::ByteCountOverflow);
	REQUIRE(overflow.actual_byte_count == std::uint64_t{0});
}

TEST_CASE("B12 default JPEG XL policy stays platform-neutral",
		  "[b12][media][policy]") {
	using shuba::platform::default_internal_photo_encode_settings;
	using shuba::platform::InternalPhotoEncodeSettings;
	using shuba::platform::validate_internal_photo_encode_settings;

	InternalPhotoEncodeSettings settings =
		default_internal_photo_encode_settings();
	REQUIRE(settings.quality == 92.0F);
	REQUIRE(settings.effort == 7U);
	REQUIRE(settings.validate_after_encode);
	REQUIRE(validate_internal_photo_encode_settings(settings));

	settings.quality = 0.0F;
	REQUIRE_FALSE(validate_internal_photo_encode_settings(settings));
	settings.quality = 92.0F;
	settings.effort	 = 10U;
	REQUIRE_FALSE(validate_internal_photo_encode_settings(settings));
}

TEST_CASE(
	"JI.8/JI.10 source-image decode sizing bounds output dimensions safely",
	"[ji8][ji10][b12][media][decode-sizing]") {
	using shuba::platform::default_durable_photo_maximum_longest_edge;
	using shuba::platform::default_durable_photo_source_image_decode_sizing;
	using shuba::platform::source_image_decode_target_size;
	using shuba::platform::SourceImageDecodeSizing;
	using shuba::platform::SourceImageDecodeTargetSize;
	using shuba::platform::validate_source_image_decode_sizing;

	const SourceImageDecodeSizing policy =
		default_durable_photo_source_image_decode_sizing();
	REQUIRE(policy.maximum_longest_edge
			== default_durable_photo_maximum_longest_edge);
	REQUIRE(validate_source_image_decode_sizing(policy));

	const std::optional<SourceImageDecodeTargetSize> in_bound =
		source_image_decode_target_size(4032U, 3024U, policy);
	REQUIRE((in_bound
			 == SourceImageDecodeTargetSize{.width = 4032U, .height = 3024U}));

	const std::optional<SourceImageDecodeTargetSize> landscape =
		source_image_decode_target_size(6001U, 4001U, policy);
	REQUIRE((landscape
			 == SourceImageDecodeTargetSize{.width = 4096U, .height = 2730U}));

	const std::optional<SourceImageDecodeTargetSize> portrait =
		source_image_decode_target_size(4001U, 6001U, policy);
	REQUIRE((portrait
			 == SourceImageDecodeTargetSize{.width = 2730U, .height = 4096U}));

	const std::optional<SourceImageDecodeTargetSize> square =
		source_image_decode_target_size(8192U, 8192U, policy);
	REQUIRE((square
			 == SourceImageDecodeTargetSize{.width = 4096U, .height = 4096U}));

	const SourceImageDecodeSizing preview_policy{.maximum_longest_edge = 640U};
	const std::optional<SourceImageDecodeTargetSize> odd_landscape =
		source_image_decode_target_size(1001U, 667U, preview_policy);
	REQUIRE((odd_landscape
			 == SourceImageDecodeTargetSize{.width = 640U, .height = 426U}));

	const std::optional<SourceImageDecodeTargetSize> exact_boundary =
		source_image_decode_target_size(4096U, 1U, policy);
	REQUIRE((exact_boundary
			 == SourceImageDecodeTargetSize{.width = 4096U, .height = 1U}));

	REQUIRE_FALSE(validate_source_image_decode_sizing(
		SourceImageDecodeSizing{.maximum_longest_edge = 0U}));
	REQUIRE_FALSE(source_image_decode_target_size(0U, 1U, policy).has_value());
	REQUIRE_FALSE(source_image_decode_target_size(1U, 0U, policy).has_value());
	REQUIRE_FALSE(
		source_image_decode_target_size(1U, 1U, SourceImageDecodeSizing{})
			.has_value());

	const SourceImageDecodeSizing overflowing_policy{
		.maximum_longest_edge = std::numeric_limits<std::uint32_t>::max()};
	REQUIRE_FALSE(source_image_decode_target_size(
					  std::numeric_limits<std::uint32_t>::max(),
					  std::numeric_limits<std::uint32_t>::max(),
					  overflowing_policy)
					  .has_value());
}
