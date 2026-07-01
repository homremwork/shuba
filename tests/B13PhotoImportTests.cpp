#include "Catalog/PhotoImport.hpp"
#include "Platform/JuceHashing.hpp"
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

class CancellingInternalPhotoCodec final
	: public shuba::platform::InternalPhotoCodec {
public:
	explicit CancellingInternalPhotoCodec(
		shuba::platform::ManualCancellationToken& cancellation_token)
		: cancellation(cancellation_token) {}

	[[nodiscard]] shuba::platform::PlatformValueResult<
		shuba::platform::MediaWriteResult>
	encode_internal_photo(
		const shuba::platform::InternalPhotoEncodeRequest& request,
		const shuba::platform::PlatformOperationContext& context,
		shuba::platform::ProgressSink& progress_sink,
		shuba::platform::CancellationToken& cancellation_token) override {
		shuba::platform::PlatformValueResult<shuba::platform::MediaWriteResult>
			result = marker.encode_internal_photo(
				request, context, progress_sink, cancellation_token);
		if (result.succeeded())
			cancellation.request_cancellation();
		return result;
	}

	[[nodiscard]] shuba::platform::PlatformValueResult<
		shuba::platform::ImagePixels>
	decode_internal_photo(
		const shuba::platform::InternalPhotoDecodeRequest& request,
		const shuba::platform::PlatformOperationContext& context,
		shuba::platform::ProgressSink& progress_sink,
		shuba::platform::CancellationToken& cancellation_token) override {
		return marker.decode_internal_photo(request, context, progress_sink,
											cancellation_token);
	}

private:
	shuba::platform::ManualCancellationToken& cancellation;
	shuba::platform::MarkerInternalPhotoCodec marker;
};

[[nodiscard]] shuba::core::StableIdentifier make_id(std::string text) {
	std::optional<shuba::core::StableIdentifier> identifier =
		shuba::core::StableIdentifier::try_create_file_safe(std::move(text));
	REQUIRE(identifier.has_value());
	return *identifier;
}

[[nodiscard]] shuba::core::OperationIdentifier make_operation_id(
	std::string text) {
	std::optional<shuba::core::OperationIdentifier> identifier =
		shuba::core::OperationIdentifier::try_create_file_safe(std::move(text));
	REQUIRE(identifier.has_value());
	return *identifier;
}

[[nodiscard]] bool has_diagnostic_code(
	const std::vector<shuba::core::Diagnostic>& diagnostics,
	std::string_view code) {
	for (const shuba::core::Diagnostic& diagnostic : diagnostics)
		if (diagnostic.code == code)
			return true;
	return false;
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
									  .display_name = "Imported photo owner",
									  .category		= "other",
									  .timestamps	= make_timestamps(1, 2)}};
}

[[nodiscard]] shuba::persistence::PhotoEnvelope make_photo(
	std::string id, const shuba::core::StableIdentifier& owner_id,
	std::int64_t sort_order, bool is_main, std::string source_md5 = {}) {
	return shuba::persistence::PhotoEnvelope{
		.record = shuba::domain::PhotoRecord{
			.id			= make_id(std::move(id)),
			.owner_type = shuba::domain::PhotoOwnerType::Item,
			.owner_id	= owner_id,
			.sort_order = sort_order,
			.is_main	= is_main,
			.source_md5 = std::move(source_md5),
			.timestamps = make_timestamps(10, 10)}};
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

[[nodiscard]] shuba::platform::ImagePixels test_pixels() {
	return shuba::platform::ImagePixels{
		.width				= 2,
		.height				= 1,
		.format				= shuba::platform::PixelFormat::Rgba8,
		.bytes				= {1, 2, 3, 255, 4, 5, 6, 255},
		.source_description = "synthetic source"};
}

struct ImportHarness final {
	TemporaryDirectory temporary{"shuba-b13-import"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	shuba::platform::AppPrivatePaths paths{
		*path_provider.resolve_app_private_paths().value};
	shuba::platform::LinuxFakeContentStagingService staging;
	shuba::platform::JuceMd5SourceByteFingerprintService fingerprinting;
	shuba::platform::SyntheticSourceImageDecodeService decoder;
	shuba::platform::MarkerInternalPhotoCodec codec;
	shuba::core::ManualClock clock{shuba::core::EpochMilliseconds{1000}};
	shuba::core::OperationGate gate;
	shuba::platform::ScriptedIdentifierSource identifiers;
	shuba::platform::ProgressCollector progress;
	shuba::platform::ManualCancellationToken cancellation;

	ImportHarness() { decoder.set_decoded_pixels(test_pixels()); }

	[[nodiscard]] shuba::catalog::PhotoImportUseCase use_case() {
		return shuba::catalog::PhotoImportUseCase{
			identifiers, clock, gate, staging, fingerprinting, decoder, codec};
	}
};

[[nodiscard]] shuba::catalog::CatalogRepositoryState state_with_item(
	const shuba::persistence::ItemEnvelope& item,
	std::vector<shuba::persistence::PhotoEnvelope> photos = {},
	std::vector<std::string> readable_media				  = {}) {
	return shuba::catalog::build_catalog_repository(
		shuba::catalog::CatalogRepositoryInput{
			.items	= {item},
			.photos = std::move(photos),
			.media	= shuba::catalog::CatalogMediaSnapshot{
				.complete_scan_available	= true,
				.readable_photo_media_files = std::move(readable_media)}});
}
}	 // namespace

TEST_CASE("B13 imports media before metadata and marks first owner photo main",
		  "[b13][photo-import][success]") {
	using shuba::catalog::PhotoImportPhotoStatus;
	using shuba::catalog::PhotoImportRequest;
	using shuba::domain::PhotoOwner;
	using shuba::domain::PhotoOwnerType;
	using shuba::platform::make_local_file_source;

	ImportHarness harness;
	const std::filesystem::path source_path =
		harness.temporary.path() / "photo.jpg";
	write_text(source_path, "source-bytes");
	const shuba::persistence::ItemEnvelope item		   = make_item("item-001");
	const shuba::catalog::CatalogRepositoryState state = state_with_item(item);
	harness.identifiers.script_operation_identifier("operation-b13-001");
	harness.identifiers.script_stable_identifier("photo-001");

	shuba::catalog::PhotoImportUseCase use_case = harness.use_case();
	shuba::catalog::PhotoImportSummary summary	= use_case.import_photos(
		PhotoImportRequest{.current_state = state,
						   .paths		  = harness.paths,
						   .owner	= PhotoOwner{.type = PhotoOwnerType::Item,
												 .id   = item.record.id},
						   .sources = {make_local_file_source(
							   source_path, "Camera Photo.JPG")},
						   .create_previous_copy = false},
		harness.progress, harness.cancellation);

	REQUIRE(summary.succeeded());
	REQUIRE(summary.success_count == 1);
	REQUIRE(summary.failure_count == 0);
	REQUIRE(summary.photos.size() == 1);
	REQUIRE(summary.photos.front().status == PhotoImportPhotoStatus::Imported);
	REQUIRE(summary.photos.front().metadata_committed);
	REQUIRE(summary.photos.front().media_written);
	REQUIRE(summary.photos.front().staged_source_cleanup_attempted);
	REQUIRE(summary.photos.front().staged_source_path.has_value());
	REQUIRE_FALSE(
		std::filesystem::exists(*summary.photos.front().staged_source_path));

	const std::filesystem::path media_path =
		harness.paths.active_catalog_root / "media/photos/photo-001.jxl";
	REQUIRE(std::filesystem::exists(media_path));
	REQUIRE(read_text(media_path) == "SHUBA-FAKE-JXL\n");
	const std::string photos_text =
		read_text(harness.paths.active_catalog_root / "data/photos.jsonl");
	REQUIRE(photos_text.find(R"("id":"photo-001")") != std::string::npos);
	REQUIRE(photos_text.find(R"("isMain":true)") != std::string::npos);
	REQUIRE(photos_text.find(R"("sourceMimeType":"image/jpeg")")
			!= std::string::npos);
	REQUIRE(
		photos_text.find(R"("sourceMd5":"be897b804568f7c80a0d999d836657bb")")
		!= std::string::npos);

	const shuba::persistence::PhotoEnvelope* photo =
		shuba::catalog::find_photo_envelope(summary.updated_state,
											make_id("photo-001"));
	REQUIRE(photo != nullptr);
	REQUIRE(photo->record.sort_order == 1000);
	REQUIRE(photo->record.is_main);
	REQUIRE(photo->record.width == 2);
	REQUIRE(photo->record.height == 1);
	REQUIRE(photo->record.encoded_bytes == std::uint64_t{15});
	REQUIRE(photo->record.source_mime_type == "image/jpeg");
	REQUIRE(photo->record.source_md5 == "be897b804568f7c80a0d999d836657bb");
	const shuba::catalog::ItemProjection& projection =
		summary.updated_state.item_projections.at("item-001");
	REQUIRE(projection.representative_usable_photo_id->value() == "photo-001");
	REQUIRE_FALSE(harness.progress.events().empty());
}

TEST_CASE("B13 missing owner rejects before staging source or writing media",
		  "[b13][photo-import][validation]") {
	using shuba::catalog::PhotoImportRequest;
	using shuba::domain::PhotoOwner;
	using shuba::domain::PhotoOwnerType;
	using shuba::platform::make_local_file_source;

	ImportHarness harness;
	const std::filesystem::path source_path =
		harness.temporary.path() / "missing-owner.jpg";
	write_text(source_path, "source-bytes");
	const shuba::catalog::CatalogRepositoryState empty_state =
		shuba::catalog::build_catalog_repository(
			shuba::catalog::CatalogRepositoryInput{});
	harness.identifiers.script_operation_identifier(
		"operation-b13-missing-owner");

	shuba::catalog::PhotoImportUseCase use_case = harness.use_case();
	shuba::catalog::PhotoImportSummary summary	= use_case.import_photos(
		PhotoImportRequest{
			.current_state = empty_state,
			.paths		   = harness.paths,
			.owner		   = PhotoOwner{.type = PhotoOwnerType::Item,
										.id	  = make_id("item-missing-owner")},
			.sources	   = {make_local_file_source(source_path,
													 "missing-owner.jpg")},
			.create_previous_copy = false},
		harness.progress, harness.cancellation);

	REQUIRE(summary.failed());
	REQUIRE(summary.category
			== shuba::core::OperationResultCategory::ValidationFailure);
	REQUIRE(summary.success_count == 0);
	REQUIRE_FALSE(summary.metadata_changed);
	REQUIRE_FALSE(summary.diagnostics.empty());
	REQUIRE(std::filesystem::is_empty(harness.paths.staged_content_root));
	REQUIRE(std::filesystem::is_empty(harness.paths.media_root));
}

TEST_CASE("B13 later imports keep existing main photo and append sort order",
		  "[b13][photo-import][ordering]") {
	using shuba::catalog::PhotoImportRequest;
	using shuba::domain::PhotoOwner;
	using shuba::domain::PhotoOwnerType;
	using shuba::platform::make_local_file_source;

	ImportHarness harness;
	const std::filesystem::path source_path =
		harness.temporary.path() / "later.png";
	write_text(source_path, "source-bytes");
	const shuba::persistence::ItemEnvelope item = make_item("item-ordered");
	write_text(
		harness.paths.active_catalog_root / "media/photos/photo-main.jxl",
		"existing");
	const shuba::catalog::CatalogRepositoryState state = state_with_item(
		item, {make_photo("photo-main", item.record.id, 1000, true)},
		{"media/photos/photo-main.jxl"});
	harness.identifiers.script_operation_identifier("operation-b13-002");
	harness.identifiers.script_stable_identifier("photo-later");

	shuba::catalog::PhotoImportUseCase use_case = harness.use_case();
	shuba::catalog::PhotoImportSummary summary	= use_case.import_photos(
		PhotoImportRequest{
			.current_state = state,
			.paths		   = harness.paths,
			.owner =
				PhotoOwner{.type = PhotoOwnerType::Item, .id = item.record.id},
			.sources = {make_local_file_source(source_path, "later.png")},
			.create_previous_copy = false},
		harness.progress, harness.cancellation);

	REQUIRE(summary.succeeded());
	const shuba::persistence::PhotoEnvelope* later =
		shuba::catalog::find_photo_envelope(summary.updated_state,
											make_id("photo-later"));
	REQUIRE(later != nullptr);
	REQUIRE(later->record.sort_order == 2000);
	REQUIRE_FALSE(later->record.is_main);
	const shuba::persistence::PhotoEnvelope* main =
		shuba::catalog::find_photo_envelope(summary.updated_state,
											make_id("photo-main"));
	REQUIRE(main != nullptr);
	REQUIRE(main->record.is_main);
	REQUIRE(summary.updated_state.item_projections.at("item-ordered")
				.representative_photo_id->value()
			== "photo-main");
}

TEST_CASE("B13 duplicate source fingerprint warns without blocking import",
		  "[b13][photo-import][duplicate-warning]") {
	using shuba::catalog::PhotoImportRequest;
	using shuba::domain::PhotoOwner;
	using shuba::domain::PhotoOwnerType;
	using shuba::platform::make_local_file_source;

	ImportHarness harness;
	const std::filesystem::path source_path =
		harness.temporary.path() / "duplicate.jpg";
	write_text(source_path, "duplicate-source");
	const shuba::persistence::ItemEnvelope item = make_item("item-duplicate");
	write_text(harness.paths.active_catalog_root
				   / "media/photos/photo-duplicate-main.jxl",
			   "existing");
	const shuba::catalog::CatalogRepositoryState state = state_with_item(
		item,
		{make_photo("photo-duplicate-main", item.record.id, 1000, true,
					"130ba648e96879a999e2bb3c79c0fa29")},
		{"media/photos/photo-duplicate-main.jxl"});
	harness.identifiers.script_operation_identifier("operation-b13-duplicate");
	harness.identifiers.script_stable_identifier("photo-duplicate-imported");

	shuba::catalog::PhotoImportUseCase use_case = harness.use_case();
	shuba::catalog::PhotoImportSummary summary	= use_case.import_photos(
		PhotoImportRequest{
			.current_state = state,
			.paths		   = harness.paths,
			.owner =
				PhotoOwner{.type = PhotoOwnerType::Item, .id = item.record.id},
			.sources = {make_local_file_source(source_path, "duplicate.jpg")},
			.create_previous_copy = false},
		harness.progress, harness.cancellation);

	REQUIRE(summary.succeeded());
	REQUIRE(summary.success_count == 1U);
	REQUIRE(has_diagnostic_code(summary.diagnostics,
								"photo_import_duplicate_source_warning"));
	const shuba::persistence::PhotoEnvelope* imported =
		shuba::catalog::find_photo_envelope(
			summary.updated_state, make_id("photo-duplicate-imported"));
	REQUIRE(imported != nullptr);
	REQUIRE(imported->record.source_md5 == "130ba648e96879a999e2bb3c79c0fa29");
}

TEST_CASE("B13 metadata commit failure removes newly written media",
		  "[b13][photo-import][metadata-failure]") {
	using shuba::catalog::PhotoImportPhotoStatus;
	using shuba::catalog::PhotoImportRequest;
	using shuba::domain::PhotoOwner;
	using shuba::domain::PhotoOwnerType;
	using shuba::platform::make_local_file_source;

	ImportHarness harness;
	const std::filesystem::path source_path =
		harness.temporary.path() / "photo.jpg";
	write_text(source_path, "source-bytes");
	const shuba::persistence::ItemEnvelope item = make_item("item-failure");
	const shuba::catalog::CatalogRepositoryState state = state_with_item(item);
	harness.identifiers.script_operation_identifier("operation-b13-003");
	harness.identifiers.script_stable_identifier("photo-failure");

	shuba::catalog::PhotoImportUseCase use_case = harness.use_case();
	shuba::catalog::PhotoImportSummary summary	= use_case.import_photos(
		PhotoImportRequest{
			.current_state = state,
			.paths		   = harness.paths,
			.owner =
				PhotoOwner{.type = PhotoOwnerType::Item, .id = item.record.id},
			.sources = {make_local_file_source(source_path, "photo.jpg")},
			.photo_table_validator =
				[](std::string_view) {
		return std::optional{shuba::core::Diagnostic{
			.severity = shuba::core::DiagnosticSeverity::WriteBlockingError,
			.code	  = "scripted_commit_failure",
			.message  = "Scripted commit failure."}};
	},
			.create_previous_copy = false},
		harness.progress, harness.cancellation);

	REQUIRE(summary.failed());
	REQUIRE(summary.failure_count == 1);
	REQUIRE(summary.photos.front().status == PhotoImportPhotoStatus::Failed);
	REQUIRE(summary.photos.front().media_written);
	REQUIRE(summary.photos.front().media_cleanup_attempted);
	REQUIRE_FALSE(summary.photos.front().orphan_media_left);
	REQUIRE_FALSE(std::filesystem::exists(harness.paths.active_catalog_root
										  / "media/photos/photo-failure.jxl"));
	REQUIRE_FALSE(shuba::catalog::find_photo_envelope(
		summary.updated_state, make_id("photo-failure")));
}

TEST_CASE(
	"B13 cancellation before metadata commit removes media without record",
	"[b13][photo-import][cancellation]") {
	using shuba::catalog::PhotoImportPhotoStatus;
	using shuba::catalog::PhotoImportRequest;
	using shuba::domain::PhotoOwner;
	using shuba::domain::PhotoOwnerType;
	using shuba::platform::make_local_file_source;

	ImportHarness harness;
	CancellingInternalPhotoCodec cancelling_codec{harness.cancellation};
	const std::filesystem::path source_path =
		harness.temporary.path() / "photo.jpg";
	write_text(source_path, "source-bytes");
	const shuba::persistence::ItemEnvelope item = make_item("item-cancel");
	const shuba::catalog::CatalogRepositoryState state = state_with_item(item);
	harness.identifiers.script_operation_identifier("operation-b13-004");
	harness.identifiers.script_stable_identifier("photo-cancelled");
	shuba::catalog::PhotoImportUseCase use_case{
		harness.identifiers, harness.clock,			 harness.gate,
		harness.staging,	 harness.fingerprinting, harness.decoder,
		cancelling_codec};

	shuba::catalog::PhotoImportSummary summary = use_case.import_photos(
		PhotoImportRequest{
			.current_state = state,
			.paths		   = harness.paths,
			.owner =
				PhotoOwner{.type = PhotoOwnerType::Item, .id = item.record.id},
			.sources = {make_local_file_source(source_path, "photo.jpg")},
			.create_previous_copy = false},
		harness.progress, harness.cancellation);

	REQUIRE(summary.was_user_cancelled());
	REQUIRE(summary.cancelled_count == 1);
	REQUIRE(summary.photos.front().status == PhotoImportPhotoStatus::Cancelled);
	REQUIRE(summary.photos.front().media_cleanup_attempted);
	REQUIRE_FALSE(
		std::filesystem::exists(harness.paths.active_catalog_root
								/ "media/photos/photo-cancelled.jxl"));
	REQUIRE_FALSE(shuba::catalog::find_photo_envelope(
		summary.updated_state, make_id("photo-cancelled")));
}

TEST_CASE("B13 mixed success import keeps successful photos per source",
		  "[b13][photo-import][mixed]") {
	using shuba::catalog::PhotoImportRequest;
	using shuba::domain::PhotoOwner;
	using shuba::domain::PhotoOwnerType;
	using shuba::platform::make_local_file_source;

	ImportHarness harness;
	const std::filesystem::path missing_source =
		harness.temporary.path() / "missing.jpg";
	const std::filesystem::path valid_source =
		harness.temporary.path() / "valid.jpg";
	write_text(valid_source, "source-bytes");
	const shuba::persistence::ItemEnvelope item = make_item("item-mixed");
	const shuba::catalog::CatalogRepositoryState state = state_with_item(item);
	harness.identifiers.script_operation_identifier("operation-b13-005");
	harness.identifiers.script_stable_identifier("photo-failed-source");
	harness.identifiers.script_stable_identifier("photo-imported-source");

	shuba::catalog::PhotoImportUseCase use_case = harness.use_case();
	shuba::catalog::PhotoImportSummary summary	= use_case.import_photos(
		PhotoImportRequest{
			.current_state = state,
			.paths		   = harness.paths,
			.owner =
				PhotoOwner{.type = PhotoOwnerType::Item, .id = item.record.id},
			.sources = {make_local_file_source(missing_source, "missing.jpg"),
						make_local_file_source(valid_source, "valid.jpg")},
			.create_previous_copy = false},
		harness.progress, harness.cancellation);

	REQUIRE(summary.succeeded());
	REQUIRE(summary.has_partial_failures());
	REQUIRE(summary.success_count == 1);
	REQUIRE(summary.failure_count == 1);
	REQUIRE(summary.photos.size() == 2);
	REQUIRE_FALSE(shuba::catalog::find_photo_envelope(
		summary.updated_state, make_id("photo-failed-source")));
	REQUIRE(shuba::catalog::find_photo_envelope(
				summary.updated_state, make_id("photo-imported-source"))
			!= nullptr);
	REQUIRE(
		std::filesystem::exists(harness.paths.active_catalog_root
								/ "media/photos/photo-imported-source.jxl"));
}
