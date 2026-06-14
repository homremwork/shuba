#include "Core/Identifier.hpp"
#include "Platform/PlatformServices.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

TEST_CASE(
	"B11 opaque descriptors keep Android document handles platform neutral",
	"[b11][platform][descriptors]") {
	using shuba::platform::ContentSourceDescriptor;
	using shuba::platform::DocumentDestinationDescriptor;
	using shuba::platform::make_opaque_content_source;
	using shuba::platform::make_opaque_document_destination;
	using shuba::platform::PlatformContentHandleKind;

	ContentSourceDescriptor source = make_opaque_content_source(
		"content://provider/images/1", "photo.jpg", std::uint64_t{42}, true);
	REQUIRE(source.kind == PlatformContentHandleKind::OpaqueHandle);
	REQUIRE(source.local_path.empty());
	REQUIRE(source.opaque_handle == "content://provider/images/1");
	REQUIRE(source.display_name == "photo.jpg");
	REQUIRE(source.byte_count == std::uint64_t{42});
	REQUIRE(source.transient);

	DocumentDestinationDescriptor destination =
		make_opaque_document_destination(
			"content://provider/exports/backup.zip", "backup.zip");
	REQUIRE(destination.kind == PlatformContentHandleKind::OpaqueHandle);
	REQUIRE(destination.local_path.empty());
	REQUIRE(destination.opaque_handle
			== "content://provider/exports/backup.zip");
	REQUIRE(destination.display_name == "backup.zip");
}

TEST_CASE("B11 staging names are file safe and preserve useful extensions",
		  "[b11][platform][staging]") {
	using shuba::core::OperationIdentifier;
	using shuba::platform::file_extension_or_empty;
	using shuba::platform::make_staged_content_file_name;
	using shuba::platform::sanitize_platform_file_name;

	std::optional<OperationIdentifier> operation_id =
		OperationIdentifier::try_create_file_safe("operation-001");
	REQUIRE(operation_id.has_value());

	REQUIRE(sanitize_platform_file_name(" ../Bad Name?.zip ", "fallback")
			== "Bad-Name-.zip");
	REQUIRE(sanitize_platform_file_name("///", "fallback") == "fallback");
	REQUIRE(file_extension_or_empty("Photo.JPG") == ".jpg");
	REQUIRE(file_extension_or_empty("no-extension") == "");

	const std::string staged_name = make_staged_content_file_name(
		"photo source", *operation_id, 7U, "Camera Image.HEIC");
	REQUIRE(staged_name == "photo-source-operation-001-7.heic");
}

TEST_CASE("B11 MIME filters map to JUCE FileChooser wildcard patterns",
		  "[b11][platform][picker]") {
	using shuba::platform::file_patterns_for_mime_types;

	REQUIRE(file_patterns_for_mime_types({"image/jpeg", "image/png"})
			== "*.jpg;*.jpeg;*.png");
	REQUIRE(file_patterns_for_mime_types({"application/zip"}) == "*.zip");
	REQUIRE(file_patterns_for_mime_types({"image/*"})
			== "*.jpg;*.jpeg;*.png;*.webp;*.heic;*.heif;*.gif;*.bmp");
	REQUIRE(file_patterns_for_mime_types({"application/octet-stream"}) == "*");
	REQUIRE(file_patterns_for_mime_types({}) == "*");
}
