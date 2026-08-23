#include "Core/Identifier.hpp"
#include "Platform/PlatformServices.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {
[[nodiscard]] std::string read_source_file(
	const std::filesystem::path& relative_path) {
	const std::filesystem::path source_root =
		std::filesystem::path{__FILE__}.parent_path().parent_path();
	std::ifstream input{source_root / relative_path, std::ios::binary};
	REQUIRE(input.is_open());
	std::ostringstream contents;
	contents << input.rdbuf();
	return contents.str();
}

[[nodiscard]] std::string function_body(const std::string& source,
										std::string_view signature,
										std::string_view following_signature) {
	const std::size_t begin = source.find(signature);
	REQUIRE(begin != std::string::npos);
	const std::size_t end = source.find(following_signature, begin);
	REQUIRE(end != std::string::npos);
	return source.substr(begin, end - begin);
}
}	 // namespace

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

TEST_CASE("R13 Android photo picker completion performs shallow capture only",
		  "[r13][platform][picker][static-contract]") {
	const std::string source =
		read_source_file("Source/Platform/JuceAndroidServices.cpp");
	const std::string shallow_conversion = function_body(
		source, "ContentSourceDescriptor shallow_source_descriptor_from_url(",
		"DocumentDestinationDescriptor destination_descriptor_from_url(");
	REQUIRE(shallow_conversion.find("AndroidDocument::fromDocument")
			== std::string::npos);
	REQUIRE(shallow_conversion.find("getInfo()") == std::string::npos);
	REQUIRE(shallow_conversion.find("createInputStream") == std::string::npos);

	const std::string picker = function_body(
		source,
		"core::OperationResult "
		"JuceAndroidPhotoSelectionService::request_photo_selection(",
		"struct JuceAndroidDocumentImportService::ActiveChooser");
	REQUIRE(picker.find("shallow_source_descriptor_from_url")
			!= std::string::npos);
	REQUIRE(picker.find("AndroidDocument::fromDocument") == std::string::npos);
	REQUIRE(picker.find("getInfo()") == std::string::npos);
	REQUIRE(picker.find("createInputStream") == std::string::npos);
	REQUIRE(picker.find("open_input_stream") == std::string::npos);

	const std::string document_import = function_body(
		source,
		"JuceAndroidDocumentImportService::request_import_document_selection(",
		"struct JuceAndroidDocumentExportService::ActiveChooser");
	REQUIRE(document_import.find("document_import_source_descriptor_from_url")
			!= std::string::npos);
}

TEST_CASE("R13 progress application cannot rebuild the routed content tree",
		  "[r13][ui][progress][static-contract]") {
	const std::string source = read_source_file("Source/UI/AppShell/Component.cpp");
	const std::string progress_application = function_body(
		source, "void Component::apply_shell_operation_progress(",
		"void Component::update_shell_operation_progress_surface()");
	REQUIRE(progress_application.find("MessageManager::callAsync")
			== std::string::npos);
	REQUIRE(progress_application.find("refresh_all()") == std::string::npos);
	REQUIRE(progress_application.find("refresh_content()")
			== std::string::npos);
	REQUIRE(
		progress_application.find("update_shell_operation_progress_surface()")
		!= std::string::npos);

	const std::string stable_surface = function_body(
		source,
		"void Component::update_shell_operation_progress_surface()",
		"std::optional<juce::String> "
		"Component::preview_failure_message(");
	REQUIRE(stable_surface.find("shell_operation_progress->update_model")
			!= std::string::npos);
	REQUIRE(stable_surface.find("refresh_all()") == std::string::npos);
	REQUIRE(stable_surface.find("refresh_content()") == std::string::npos);
	REQUIRE(stable_surface.find("clear_rows()") == std::string::npos);
}

TEST_CASE("JI.4 preview completions use the shell timer refresh boundary",
		  "[ji4][ui][preview][static-contract]") {
	const std::string shell_source = read_source_file("Source/UI/AppShell/Component.cpp");
	const std::string scheduler_construction = function_body(
		shell_source,
		"preview_scheduler = std::make_unique<PreviewScheduler>(",
		"route_coordinator = std::make_unique<RouteCoordinator>(");
	REQUIRE(scheduler_construction.find(
				".refresh_content\t\t = [this] { schedule_content_refresh(); }")
			!= std::string::npos);
	REQUIRE(scheduler_construction.find("[this] { refresh_content(); }")
			== std::string::npos);

	const std::string renderer_construction = function_body(
		shell_source,
		"screen_renderer = std::make_unique<ScreenRenderer>(",
		"viewport.setViewedComponent(content.get(), false);");
	REQUIRE(
		renderer_construction.find(".refresh_all = [this] { refresh_all(); },")
		!= std::string::npos);
	REQUIRE(renderer_construction.find(
				".refresh_content = [this] { refresh_content(); }")
			!= std::string::npos);

	const std::string timer_callback =
		function_body(shell_source, "void Component::timerCallback()",
					  "void Component::refresh_all()");
	REQUIRE(timer_callback.find("stopTimer()") != std::string::npos);
	REQUIRE(timer_callback.find("refresh_content()") != std::string::npos);
}

TEST_CASE("Maintenance document picker callbacks use shell lifetime guards",
		  "[shell-operation][ui][lifetime][static-contract]") {
	const std::string source =
		read_source_file("Source/UI/Screens/MaintenanceScreens.cpp");
	for (const std::string_view signature :
		 {"void Component::request_export_backup()",
		  "void Component::request_export_diagnostic_archive()",
		  "void Component::request_import_backup()"}) {
		const std::size_t begin = source.find(signature);
		REQUIRE(begin != std::string::npos);
		const std::size_t callback_begin =
			source.find("callback_lifetime", begin);
		REQUIRE(callback_begin != std::string::npos);
		REQUIRE(
			source.find("CallbackLifetimeLease::try_acquire", callback_begin)
			!= std::string::npos);
	}

	const std::string shell_source = read_source_file("Source/UI/AppShell/Component.cpp");
	const std::string destructor =
		function_body(shell_source, "Component::~Component()",
					  "void Component::paint(");
	REQUIRE(destructor.find("picker_lifetime->invalidate_and_wait()")
			!= std::string::npos);
}
