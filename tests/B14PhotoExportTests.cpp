#include "Catalog/PhotoExport.hpp"
#include "Platform/JuceAndroidServices.hpp"
#include "Platform/LinuxFakes.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

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

class FailingJpegExportService final
	: public shuba::platform::JpegExportService {
public:
	[[nodiscard]] shuba::platform::PlatformValueResult<
		shuba::platform::MediaWriteResult>
	write_jpeg(
		const shuba::platform::JpegExportRequest& request,
		const shuba::platform::PlatformOperationContext& context,
		shuba::platform::ProgressSink& progress_sink,
		shuba::platform::CancellationToken& cancellation_token) override {
		(void)request;
		(void)context;
		(void)progress_sink;
		(void)cancellation_token;
		return shuba::platform::platform_value_failure<
			shuba::platform::MediaWriteResult>(
			shuba::core::OperationResultCategory::CodecFailure,
			shuba::core::Diagnostic{
				.severity = shuba::core::DiagnosticSeverity::WriteBlockingError,
				.code	  = "scripted-jpeg-failure",
				.message  = "Scripted JPEG writer failure."});
	}
};

[[nodiscard]] shuba::core::StableIdentifier make_id(std::string text) {
	std::optional<shuba::core::StableIdentifier> identifier =
		shuba::core::StableIdentifier::try_create_file_safe(std::move(text));
	REQUIRE(identifier.has_value());
	return *identifier;
}

[[nodiscard]] shuba::domain::RecordTimestamps make_timestamps(
	std::int64_t created_at, std::int64_t updated_at) {
	return shuba::domain::RecordTimestamps{
		.created_at = shuba::core::EpochMilliseconds{created_at},
		.updated_at = shuba::core::EpochMilliseconds{updated_at}};
}

[[nodiscard]] shuba::persistence::ItemEnvelope make_item(std::string id) {
	return shuba::persistence::ItemEnvelope{
		.record =
			shuba::domain::ItemRecord{.id			= make_id(std::move(id)),
									  .display_name = "Export Owner",
									  .category		= "other",
									  .timestamps	= make_timestamps(1, 2)}};
}

[[nodiscard]] shuba::persistence::PhotoEnvelope make_photo(
	std::string id, const shuba::core::StableIdentifier& owner_id,
	std::int64_t sort_order = 1000) {
	return shuba::persistence::PhotoEnvelope{
		.record = shuba::domain::PhotoRecord{
			.id				  = make_id(std::move(id)),
			.owner_type		  = shuba::domain::PhotoOwnerType::Item,
			.owner_id		  = owner_id,
			.media_format	  = shuba::domain::PhotoMediaFormat::JpegXl,
			.sort_order		  = sort_order,
			.is_main		  = sort_order == 1000,
			.width			  = 2,
			.height			  = 2,
			.encoded_bytes	  = std::uint64_t{15},
			.source_mime_type = "image/jpeg",
			.timestamps		  = make_timestamps(10, 10)}};
}

void write_text(const std::filesystem::path& path, std::string_view text) {
	std::filesystem::create_directories(path.parent_path());
	std::ofstream output{path, std::ios::binary | std::ios::trunc};
	REQUIRE(output.good());
	output << text;
}

[[nodiscard]] std::string read_text(const std::filesystem::path& path) {
	std::ifstream input{path, std::ios::binary};
	REQUIRE(input.good());
	std::ostringstream buffer;
	buffer << input.rdbuf();
	return buffer.str();
}

[[nodiscard]] std::vector<std::uint8_t> read_bytes(
	const std::filesystem::path& path) {
	std::ifstream input{path, std::ios::binary};
	REQUIRE(input.good());
	return std::vector<std::uint8_t>{std::istreambuf_iterator<char>{input},
									 std::istreambuf_iterator<char>{}};
}

[[nodiscard]] shuba::platform::ImagePixels test_pixels() {
	return shuba::platform::ImagePixels{
		.width	= 2,
		.height = 2,
		.format = shuba::platform::PixelFormat::Rgba8,
		.bytes = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255,
				  255},
		.source_description = "synthetic jxl decode"};
}

struct ExportHarness final {
	TemporaryDirectory temporary{"shuba-b14-export"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	shuba::platform::AppPrivatePaths paths{
		*path_provider.resolve_app_private_paths().value};
	shuba::platform::MarkerInternalPhotoCodec codec;
	shuba::platform::JuceJpegExportService jpeg;
	shuba::platform::LinuxFakeDocumentExportService document_export;
	shuba::core::OperationGate gate;
	shuba::platform::ScriptedIdentifierSource identifiers;
	shuba::platform::ProgressCollector progress;
	shuba::platform::ManualCancellationToken cancellation;

	[[nodiscard]] shuba::catalog::PhotoExportUseCase use_case(
		shuba::platform::JpegExportService& jpeg_service) {
		return shuba::catalog::PhotoExportUseCase{
			identifiers, gate, codec, jpeg_service, document_export};
	}
};

[[nodiscard]] shuba::catalog::CatalogRepositoryState state_with_photo(
	const shuba::persistence::ItemEnvelope& item,
	const shuba::persistence::PhotoEnvelope& photo,
	std::vector<std::string> readable_media) {
	return shuba::catalog::build_catalog_repository(
		shuba::catalog::CatalogRepositoryInput{
			.items	= {item},
			.photos = {photo},
			.media	= shuba::catalog::CatalogMediaSnapshot{
				.complete_scan_available	= true,
				.readable_photo_media_files = std::move(readable_media)}});
}
}	 // namespace

TEST_CASE("B14 display decode returns pixels and broken placeholders",
		  "[b14][photo-display]") {
	using shuba::catalog::PhotoDisplayRequest;
	using shuba::catalog::PhotoDisplayStatus;

	ExportHarness harness;
	harness.identifiers.script_operation_identifier("operation-b14-display");
	const shuba::persistence::ItemEnvelope item = make_item("item-display");
	const shuba::persistence::PhotoEnvelope photo =
		make_photo("photo-display", item.record.id);
	const std::filesystem::path media_path =
		harness.paths.active_catalog_root / "media/photos/photo-display.jxl";
	write_text(media_path, "SHUBA-FAKE-JXL\n");

	shuba::platform::PlatformOperationContext encode_context{
		.operation_id = *shuba::core::OperationIdentifier::try_create_file_safe(
			"operation-b14-seed"),
		.operation_type = shuba::platform::ProgressOperationType::PhotoImport};
	shuba::platform::PlatformValueResult<shuba::platform::MediaWriteResult>
		encoded = harness.codec.encode_internal_photo(
			shuba::platform::InternalPhotoEncodeRequest{
				.pixels = test_pixels(), .output_path = media_path},
			encode_context, harness.progress, harness.cancellation);
	REQUIRE(encoded.succeeded());

	const shuba::catalog::CatalogRepositoryState state =
		state_with_photo(item, photo, {"media/photos/photo-display.jxl"});
	shuba::catalog::PhotoExportUseCase use_case =
		harness.use_case(harness.jpeg);
	shuba::catalog::PhotoDisplayResult displayed =
		use_case.load_photo_for_display(
			PhotoDisplayRequest{.current_state = state,
								.paths		   = harness.paths,
								.photo_id	   = photo.record.id},
			harness.progress, harness.cancellation);
	REQUIRE(displayed.succeeded());
	REQUIRE(displayed.status == PhotoDisplayStatus::Decoded);
	REQUIRE(displayed.pixels.has_value());
	REQUIRE(displayed.pixels->bytes == test_pixels().bytes);

	shuba::catalog::PhotoDisplayResult missing =
		use_case.load_photo_for_display(
			PhotoDisplayRequest{.current_state = state,
								.paths		   = harness.paths,
								.photo_id	   = make_id("photo-missing")},
			harness.progress, harness.cancellation);
	REQUIRE(missing.failed());
	REQUIRE(missing.status == PhotoDisplayStatus::Broken);
	REQUIRE(missing.placeholder.has_value());
	REQUIRE(missing.placeholder->diagnostic_code
			== "photo_display_missing_record");
}

TEST_CASE(
	"B14 exports JPEG through temp file and destination without metadata "
	"mutation",
	"[b14][photo-export][success]") {
	using shuba::catalog::PhotoExportRequest;
	using shuba::catalog::PhotoExportStatus;
	using shuba::platform::make_local_file_destination;

	ExportHarness harness;
	const shuba::persistence::ItemEnvelope item = make_item("item-export");
	const shuba::persistence::PhotoEnvelope photo =
		make_photo("photo-export", item.record.id);
	const std::filesystem::path media_path =
		harness.paths.active_catalog_root / "media/photos/photo-export.jxl";
	write_text(media_path, "SHUBA-FAKE-JXL\n");
	shuba::platform::PlatformOperationContext encode_context{
		.operation_id = *shuba::core::OperationIdentifier::try_create_file_safe(
			"operation-b14-encode"),
		.operation_type = shuba::platform::ProgressOperationType::PhotoImport};
	REQUIRE(harness.codec
				.encode_internal_photo(
					shuba::platform::InternalPhotoEncodeRequest{
						.pixels = test_pixels(), .output_path = media_path},
					encode_context, harness.progress, harness.cancellation)
				.succeeded());
	const shuba::catalog::CatalogRepositoryState state =
		state_with_photo(item, photo, {"media/photos/photo-export.jxl"});
	harness.identifiers.script_operation_identifier("operation-b14-export");
	const std::filesystem::path photos_jsonl_path =
		harness.paths.active_catalog_root / "data/photos.jsonl";
	write_text(photos_jsonl_path, "canonical-photo-metadata\n");
	const std::filesystem::path destination_path =
		harness.temporary.path() / "selected-export.jpg";
	const std::string before_photos_json = read_text(photos_jsonl_path);

	shuba::catalog::PhotoExportUseCase use_case =
		harness.use_case(harness.jpeg);
	shuba::catalog::PhotoExportResult result = use_case.export_photo_as_jpeg(
		PhotoExportRequest{
			.current_state = state,
			.paths		   = harness.paths,
			.photo_id	   = photo.record.id,
			.destination   = make_local_file_destination(destination_path),
			.jpeg_quality  = 90},
		harness.progress, harness.cancellation);

	REQUIRE(result.succeeded());
	REQUIRE(result.status == PhotoExportStatus::Exported);
	REQUIRE(result.temp_jpeg_written);
	REQUIRE(result.destination_copied);
	REQUIRE(result.temp_cleanup_attempted);
	REQUIRE_FALSE(std::filesystem::exists(*result.temp_jpeg_path));
	REQUIRE(std::filesystem::exists(destination_path));
	const std::vector<std::uint8_t> exported_bytes =
		read_bytes(destination_path);
	REQUIRE(exported_bytes.size() > 4U);
	REQUIRE(exported_bytes[0] == 0xffU);
	REQUIRE(exported_bytes[1] == 0xd8U);
	REQUIRE_FALSE(result.metadata_changed);
	REQUIRE(read_text(harness.paths.active_catalog_root / "data/photos.jsonl")
			== before_photos_json);
	REQUIRE_FALSE(harness.progress.events().empty());
	REQUIRE(
		shuba::catalog::suggested_jpeg_export_file_name(state, photo.record.id)
		== "Export-Owner-photo-1.jpg");
}

TEST_CASE("B14 JPEG export failure leaves destination and metadata unchanged",
		  "[b14][photo-export][failure]") {
	using shuba::catalog::PhotoExportRequest;
	using shuba::catalog::PhotoExportStatus;
	using shuba::platform::make_local_file_destination;

	ExportHarness harness;
	FailingJpegExportService failing_jpeg;
	const shuba::persistence::ItemEnvelope item = make_item("item-failure");
	const shuba::persistence::PhotoEnvelope photo =
		make_photo("photo-failure", item.record.id);
	const std::filesystem::path media_path =
		harness.paths.active_catalog_root / "media/photos/photo-failure.jxl";
	write_text(media_path, "SHUBA-FAKE-JXL\n");
	shuba::platform::PlatformOperationContext encode_context{
		.operation_id = *shuba::core::OperationIdentifier::try_create_file_safe(
			"operation-b14-encode-failure"),
		.operation_type = shuba::platform::ProgressOperationType::PhotoImport};
	REQUIRE(harness.codec
				.encode_internal_photo(
					shuba::platform::InternalPhotoEncodeRequest{
						.pixels = test_pixels(), .output_path = media_path},
					encode_context, harness.progress, harness.cancellation)
				.succeeded());
	const shuba::catalog::CatalogRepositoryState state =
		state_with_photo(item, photo, {"media/photos/photo-failure.jxl"});
	harness.identifiers.script_operation_identifier("operation-b14-failure");
	const std::filesystem::path photos_jsonl_path =
		harness.paths.active_catalog_root / "data/photos.jsonl";
	write_text(photos_jsonl_path, "canonical-photo-metadata\n");
	const std::filesystem::path destination_path =
		harness.temporary.path() / "failed-export.jpg";
	const std::string before_photos_json = read_text(photos_jsonl_path);

	shuba::catalog::PhotoExportUseCase use_case =
		harness.use_case(failing_jpeg);
	shuba::catalog::PhotoExportResult result = use_case.export_photo_as_jpeg(
		PhotoExportRequest{
			.current_state = state,
			.paths		   = harness.paths,
			.photo_id	   = photo.record.id,
			.destination   = make_local_file_destination(destination_path),
			.jpeg_quality  = 90},
		harness.progress, harness.cancellation);

	REQUIRE(result.failed());
	REQUIRE(result.status == PhotoExportStatus::Failed);
	REQUIRE(result.category
			== shuba::core::OperationResultCategory::CodecFailure);
	REQUIRE_FALSE(result.destination_copied);
	REQUIRE_FALSE(std::filesystem::exists(destination_path));
	REQUIRE(read_text(harness.paths.active_catalog_root / "data/photos.jsonl")
			== before_photos_json);
	REQUIRE_FALSE(result.metadata_changed);
}

TEST_CASE("B14 export rejects missing media before temp JPEG write",
		  "[b14][photo-export][missing-media]") {
	using shuba::catalog::PhotoExportRequest;
	using shuba::platform::make_local_file_destination;

	ExportHarness harness;
	const shuba::persistence::ItemEnvelope item = make_item("item-broken");
	const shuba::persistence::PhotoEnvelope photo =
		make_photo("photo-broken", item.record.id);
	const shuba::catalog::CatalogRepositoryState state =
		state_with_photo(item, photo, {});
	harness.identifiers.script_operation_identifier("operation-b14-missing");
	const std::filesystem::path destination_path =
		harness.temporary.path() / "missing-media-export.jpg";

	shuba::catalog::PhotoExportUseCase use_case =
		harness.use_case(harness.jpeg);
	shuba::catalog::PhotoExportResult result = use_case.export_photo_as_jpeg(
		PhotoExportRequest{
			.current_state = state,
			.paths		   = harness.paths,
			.photo_id	   = photo.record.id,
			.destination   = make_local_file_destination(destination_path),
			.jpeg_quality  = 90},
		harness.progress, harness.cancellation);

	REQUIRE(result.failed());
	REQUIRE(result.category
			== shuba::core::OperationResultCategory::SourceUnavailable);
	REQUIRE_FALSE(result.temp_jpeg_written);
	REQUIRE_FALSE(result.destination_copied);
	REQUIRE_FALSE(std::filesystem::exists(destination_path));
}

TEST_CASE("B14 export rejects invalid JPEG quality before temp JPEG write",
		  "[b14][photo-export][validation]") {
	using shuba::catalog::PhotoExportRequest;
	using shuba::catalog::PhotoExportStatus;
	using shuba::platform::make_local_file_destination;

	ExportHarness harness;
	const shuba::persistence::ItemEnvelope item = make_item("item-quality");
	const shuba::persistence::PhotoEnvelope photo =
		make_photo("photo-quality", item.record.id);
	const shuba::catalog::CatalogRepositoryState state =
		state_with_photo(item, photo, {"media/photos/photo-quality.jxl"});
	harness.identifiers.script_operation_identifier("operation-b14-quality");
	const std::filesystem::path destination_path =
		harness.temporary.path() / "quality-export.jpg";

	shuba::catalog::PhotoExportUseCase use_case =
		harness.use_case(harness.jpeg);
	shuba::catalog::PhotoExportResult result = use_case.export_photo_as_jpeg(
		PhotoExportRequest{
			.current_state = state,
			.paths		   = harness.paths,
			.photo_id	   = photo.record.id,
			.destination   = make_local_file_destination(destination_path),
			.jpeg_quality  = 0},
		harness.progress, harness.cancellation);

	REQUIRE(result.failed());
	REQUIRE(result.status == PhotoExportStatus::Failed);
	REQUIRE(result.category
			== shuba::core::OperationResultCategory::ValidationFailure);
	REQUIRE_FALSE(result.temp_jpeg_written);
	REQUIRE_FALSE(result.destination_copied);
	REQUIRE_FALSE(std::filesystem::exists(destination_path));
}
