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
