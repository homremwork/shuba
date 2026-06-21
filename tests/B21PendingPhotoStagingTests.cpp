#include "Persistence/MetadataSchema.hpp"
#include "Platform/LinuxFakes.hpp"
#include "UI/Session/CatalogStartupSession.hpp"
#include "UI/Session/EntityEditSession.hpp"
#include "UI/Session/PhotoSession.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace {
class TemporaryDirectory final {
public:
	explicit TemporaryDirectory(std::string leaf_prefix)
		: root_path(std::filesystem::temp_directory_path()
					/ (std::move(leaf_prefix) + "-"
					   + std::to_string(std::chrono::steady_clock::now()
											.time_since_epoch()
											.count()))) {
		reset();
	}

	TemporaryDirectory(const TemporaryDirectory&)			 = delete;
	TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

	~TemporaryDirectory() {
		std::error_code ignored;
		std::filesystem::remove_all(root_path, ignored);
	}

	[[nodiscard]] const std::filesystem::path& path() const noexcept {
		return root_path;
	}

private:
	void reset() {
		if (!root_path.filename().string().starts_with("shuba-b21-"))
			throw std::logic_error{"unsafe B21 temporary directory name"};

		std::filesystem::remove_all(root_path);
		std::filesystem::create_directories(root_path);
	}

	std::filesystem::path root_path;
};

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

[[nodiscard]] shuba::ui::CatalogSessionState load_session(
	shuba::platform::LinuxFakePathProvider& path_provider,
	shuba::platform::ScriptedIdentifierSource& identifiers,
	shuba::core::ManualClock& clock) {
	return shuba::ui::load_catalog_session(
		shuba::ui::CatalogSessionLoadRequest{.path_provider = path_provider,
											 .identifiers	= identifiers,
											 .clock			= clock,
											 .debug_demo_seed_enabled = false});
}

[[nodiscard]] shuba::ui::PendingPhotoStagingRequest staging_request(
	shuba::ui::CatalogSessionState session,
	shuba::platform::ScriptedIdentifierSource& identifiers,
	shuba::core::OperationGate& gate,
	shuba::platform::LinuxFakeContentStagingService& staging,
	std::vector<shuba::platform::ContentSourceDescriptor> sources) {
	return shuba::ui::PendingPhotoStagingRequest{
		.current_session = std::move(session),
		.identifiers	 = identifiers,
		.operation_gate	 = gate,
		.staging_service = staging,
		.sources		 = std::move(sources)};
}

[[nodiscard]] std::string photos_metadata_text(
	const shuba::ui::CatalogSessionState& session) {
	return read_text(session.paths->active_catalog_root
					 / std::filesystem::path{std::string{
						 shuba::persistence::photos_data_file_path}});
}

[[nodiscard]] shuba::platform::ImagePixels test_pixels() {
	return shuba::platform::ImagePixels{
		.width				= 2,
		.height				= 1,
		.format				= shuba::platform::PixelFormat::Rgba8,
		.bytes				= {8, 16, 24, 255, 32, 40, 48, 255},
		.source_description = "B21 synthetic source"};
}
}	 // namespace

TEST_CASE("B21 pending photo staging copies local sources without metadata") {
	TemporaryDirectory temporary{"shuba-b21-stage-success"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	shuba::platform::ScriptedIdentifierSource identifiers;
	identifiers.script_stable_identifier("catalog-b21-stage-success");
	identifiers.script_operation_identifier("operation-b21-init-success");
	shuba::core::ManualClock clock{shuba::core::EpochMilliseconds{1000}};
	shuba::ui::CatalogSessionState session =
		load_session(path_provider, identifiers, clock);
	REQUIRE(session.ready_for_browsing());
	const std::string photos_before = photos_metadata_text(session);

	const std::filesystem::path source_path = temporary.path() / "camera.jpg";
	write_text(source_path, "source-bytes");
	shuba::platform::LinuxFakeContentStagingService staging;
	shuba::core::OperationGate gate;
	shuba::platform::ProgressCollector progress;
	shuba::platform::ManualCancellationToken cancellation;
	identifiers.script_operation_identifier("operation-b21-pending-stage");

	shuba::ui::PendingPhotoStagingResult result =
		shuba::ui::stage_pending_photos_for_session(
			staging_request(session, identifiers, gate, staging,
							{shuba::platform::make_local_file_source(
								source_path, "Camera Photo.JPG")}),
			progress, cancellation);

	REQUIRE(result.succeeded());
	REQUIRE(result.staged_count == 1U);
	REQUIRE(result.failure_count == 0U);
	REQUIRE(result.sources.size() == 1U);
	const shuba::ui::PendingPhotoSource& pending = result.sources.front();
	REQUIRE(pending.status == shuba::ui::PendingPhotoStatus::Staged);
	REQUIRE(pending.ready_for_import());
	REQUIRE(pending.staged_source.has_value());
	REQUIRE(pending.staged_source->kind
			== shuba::platform::PlatformContentHandleKind::LocalFile);
	REQUIRE(pending.staged_path.has_value());
	REQUIRE(pending.staged_source->local_path == *pending.staged_path);
	REQUIRE(pending.staged_path->parent_path()
			== session.paths->staged_content_root);
	REQUIRE(pending.byte_count == 12U);
	REQUIRE(read_text(*pending.staged_path) == "source-bytes");
	REQUIRE(photos_metadata_text(session) == photos_before);
	REQUIRE(session.repository.photos.empty());
	REQUIRE_FALSE(progress.events().empty());
}

TEST_CASE("B21 pending photo staging keeps successes when one source fails") {
	TemporaryDirectory temporary{"shuba-b21-stage-mixed"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	shuba::platform::ScriptedIdentifierSource identifiers;
	identifiers.script_stable_identifier("catalog-b21-stage-mixed");
	identifiers.script_operation_identifier("operation-b21-init-mixed");
	shuba::core::ManualClock clock{shuba::core::EpochMilliseconds{1000}};
	shuba::ui::CatalogSessionState session =
		load_session(path_provider, identifiers, clock);
	const std::string photos_before = photos_metadata_text(session);

	const std::filesystem::path good_source = temporary.path() / "good.png";
	const std::filesystem::path missing_source =
		temporary.path() / "missing.png";
	write_text(good_source, "good-bytes");
	shuba::platform::LinuxFakeContentStagingService staging;
	shuba::core::OperationGate gate;
	shuba::platform::ProgressCollector progress;
	shuba::platform::ManualCancellationToken cancellation;
	identifiers.script_operation_identifier("operation-b21-pending-mixed");

	shuba::ui::PendingPhotoStagingResult result =
		shuba::ui::stage_pending_photos_for_session(
			staging_request(session, identifiers, gate, staging,
							{shuba::platform::make_local_file_source(
								 good_source, "good.png"),
							 shuba::platform::make_local_file_source(
								 missing_source, "missing.png")}),
			progress, cancellation);

	REQUIRE(result.succeeded());
	REQUIRE(result.has_partial_failures());
	REQUIRE(result.staged_count == 1U);
	REQUIRE(result.failure_count == 1U);
	REQUIRE(result.sources.size() == 2U);
	REQUIRE(result.sources[0].status == shuba::ui::PendingPhotoStatus::Staged);
	REQUIRE(result.sources[0].staged_path.has_value());
	REQUIRE(std::filesystem::exists(*result.sources[0].staged_path));
	REQUIRE(result.sources[1].status == shuba::ui::PendingPhotoStatus::Failed);
	REQUIRE_FALSE(result.sources[1].ready_for_import());
	REQUIRE_FALSE(result.sources[1].diagnostics.empty());
	REQUIRE_FALSE(result.diagnostics.empty());
	REQUIRE(photos_metadata_text(session) == photos_before);
}

TEST_CASE("B21 pending photo cancellation leaves metadata unchanged") {
	TemporaryDirectory temporary{"shuba-b21-stage-cancel"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	shuba::platform::ScriptedIdentifierSource identifiers;
	identifiers.script_stable_identifier("catalog-b21-stage-cancel");
	identifiers.script_operation_identifier("operation-b21-init-cancel");
	shuba::core::ManualClock clock{shuba::core::EpochMilliseconds{1000}};
	shuba::ui::CatalogSessionState session =
		load_session(path_provider, identifiers, clock);
	const std::string photos_before = photos_metadata_text(session);

	const std::filesystem::path source_path = temporary.path() / "cancel.jpg";
	write_text(source_path, "cancel-bytes");
	shuba::platform::LinuxFakeContentStagingService staging;
	shuba::core::OperationGate gate;
	shuba::platform::ProgressCollector progress;
	shuba::platform::ManualCancellationToken cancellation;
	cancellation.request_cancellation();
	identifiers.script_operation_identifier("operation-b21-pending-cancel");

	shuba::ui::PendingPhotoStagingResult result =
		shuba::ui::stage_pending_photos_for_session(
			staging_request(session, identifiers, gate, staging,
							{shuba::platform::make_local_file_source(
								source_path, "Cancel.JPG")}),
			progress, cancellation);

	REQUIRE(result.was_user_cancelled());
	REQUIRE(result.cancelled_count == 1U);
	REQUIRE(result.sources.size() == 1U);
	REQUIRE(result.sources.front().status
			== shuba::ui::PendingPhotoStatus::Cancelled);
	REQUIRE_FALSE(result.sources.front().staged_path.has_value());
	REQUIRE(photos_metadata_text(session) == photos_before);
}

TEST_CASE(
	"B21 pending photo cleanup removes staged files and reports failures") {
	TemporaryDirectory temporary{"shuba-b21-cleanup"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	shuba::platform::ScriptedIdentifierSource identifiers;
	identifiers.script_stable_identifier("catalog-b21-cleanup");
	identifiers.script_operation_identifier("operation-b21-init-cleanup");
	shuba::core::ManualClock clock{shuba::core::EpochMilliseconds{1000}};
	shuba::ui::CatalogSessionState session =
		load_session(path_provider, identifiers, clock);

	const std::filesystem::path first_source  = temporary.path() / "first.jpg";
	const std::filesystem::path second_source = temporary.path() / "second.jpg";
	write_text(first_source, "first");
	write_text(second_source, "second");
	shuba::platform::LinuxFakeContentStagingService staging;
	shuba::core::OperationGate gate;
	shuba::platform::ProgressCollector progress;
	shuba::platform::ManualCancellationToken cancellation;
	identifiers.script_operation_identifier("operation-b21-pending-cleanup");
	shuba::ui::PendingPhotoStagingResult staged =
		shuba::ui::stage_pending_photos_for_session(
			staging_request(session, identifiers, gate, staging,
							{shuba::platform::make_local_file_source(
								 first_source, "first.jpg"),
							 shuba::platform::make_local_file_source(
								 second_source, "second.jpg")}),
			progress, cancellation);
	REQUIRE(staged.succeeded());
	REQUIRE(staged.sources.size() == 2U);
	const std::filesystem::path removable_path = *staged.sources[0].staged_path;
	const std::filesystem::path blocked_path   = *staged.sources[1].staged_path;
	std::filesystem::remove(blocked_path);
	std::filesystem::create_directories(blocked_path / "child");
	write_text(blocked_path / "child" / "marker.txt", "blocked");

	shuba::ui::PendingPhotoCleanupResult cleanup =
		shuba::ui::cleanup_pending_photo_sources(staged.sources);

	REQUIRE(cleanup.failed());
	REQUIRE(cleanup.cleanup_attempt_count == 2U);
	REQUIRE(cleanup.removed_count == 1U);
	REQUIRE(cleanup.failure_count == 1U);
	REQUIRE_FALSE(std::filesystem::exists(removable_path));
	REQUIRE(std::filesystem::exists(blocked_path));
	REQUIRE(staged.sources[0].status == shuba::ui::PendingPhotoStatus::Removed);
	REQUIRE_FALSE(staged.sources[0].staged_path.has_value());
	REQUIRE(staged.sources[1].status == shuba::ui::PendingPhotoStatus::Staged);
	REQUIRE(staged.sources[1].staged_path.has_value());
	REQUIRE_FALSE(staged.sources[1].diagnostics.empty());
	REQUIRE_FALSE(cleanup.diagnostics.empty());
}

TEST_CASE(
	"B21 pending item save keeps item when photo import fails after save") {
	TemporaryDirectory temporary{"shuba-b21-save-import-failure"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	shuba::platform::ScriptedIdentifierSource identifiers;
	identifiers.script_stable_identifier("catalog-b21-save-import-failure");
	identifiers.script_operation_identifier("operation-b21-init-save-failure");
	shuba::core::ManualClock clock{shuba::core::EpochMilliseconds{1000}};
	shuba::ui::CatalogSessionState session =
		load_session(path_provider, identifiers, clock);

	const std::filesystem::path source_path = temporary.path() / "photo.jpg";
	write_text(source_path, "pending-source");
	shuba::platform::LinuxFakeContentStagingService staging;
	shuba::core::OperationGate gate;
	shuba::platform::ProgressCollector progress;
	shuba::platform::ManualCancellationToken cancellation;
	identifiers.script_operation_identifier("operation-b21-stage-save-failure");
	shuba::ui::PendingPhotoStagingResult staged =
		shuba::ui::stage_pending_photos_for_session(
			staging_request(session, identifiers, gate, staging,
							{shuba::platform::make_local_file_source(
								source_path, "photo.jpg")}),
			progress, cancellation);
	REQUIRE(staged.succeeded());
	REQUIRE(staged.sources.size() == 1U);
	const std::filesystem::path pending_path =
		*staged.sources.front().staged_path;

	shuba::platform::SyntheticSourceImageDecodeService decoder;
	shuba::platform::MarkerInternalPhotoCodec codec;
	identifiers.script_stable_identifier("item-b21-save-import-failure");
	identifiers.script_stable_identifier("photo-b21-import-failure");
	identifiers.script_operation_identifier(
		"operation-b21-save-item-before-import");
	identifiers.script_operation_identifier(
		"operation-b21-import-expected-failure");
	clock.set_now(shuba::core::EpochMilliseconds{2000});
	shuba::ui::ItemSaveWithPendingPhotosResult result =
		shuba::ui::save_item_draft_and_import_pending_photos(
			shuba::ui::ItemSaveWithPendingPhotosRequest{
				.current_session = session,
				.identifiers	 = identifiers,
				.clock			 = clock,
				.operation_gate	 = gate,
				.staging_service = staging,
				.decode_service	 = decoder,
				.photo_codec	 = codec,
				.draft = shuba::ui::ItemDraft{.display_name = "Photo item",
											  .category		= "Testing",
											  .warning_acknowledged = true},
				.pending_sources	  = staged.sources,
				.create_previous_copy = false},
			progress, cancellation);

	REQUIRE(result.item_saved());
	REQUIRE(result.import_attempted);
	REQUIRE(result.import_failed());
	REQUIRE(result.import_result.summary.failure_count == 1U);
	REQUIRE(result.session.repository.items.size() == 1U);
	REQUIRE(result.session.repository.items.front().record.id.value()
			== "item-b21-save-import-failure");
	REQUIRE(result.session.repository.photos.empty());
	REQUIRE(result.cleanup_attempted);
	REQUIRE(result.cleanup_result.succeeded());
	REQUIRE(result.pending_sources.size() == 1U);
	REQUIRE(result.pending_sources.front().status
			== shuba::ui::PendingPhotoStatus::Consumed);
	REQUIRE_FALSE(result.pending_sources.front().staged_path.has_value());
	REQUIRE_FALSE(std::filesystem::exists(pending_path));
	REQUIRE(
		photos_metadata_text(result.session).find("photo-b21-import-failure")
		== std::string::npos);
}

TEST_CASE("B21 pending item photos survive item save validation failure") {
	TemporaryDirectory temporary{"shuba-b21-save-validation-failure"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	shuba::platform::ScriptedIdentifierSource identifiers;
	identifiers.script_stable_identifier("catalog-b21-save-validation-failure");
	identifiers.script_operation_identifier(
		"operation-b21-init-validation-failure");
	shuba::core::ManualClock clock{shuba::core::EpochMilliseconds{1000}};
	shuba::ui::CatalogSessionState session =
		load_session(path_provider, identifiers, clock);

	const std::filesystem::path source_path = temporary.path() / "kept.jpg";
	write_text(source_path, "kept-source");
	shuba::platform::LinuxFakeContentStagingService staging;
	shuba::core::OperationGate gate;
	shuba::platform::ProgressCollector progress;
	shuba::platform::ManualCancellationToken cancellation;
	identifiers.script_operation_identifier(
		"operation-b21-stage-validation-failure");
	shuba::ui::PendingPhotoStagingResult staged =
		shuba::ui::stage_pending_photos_for_session(
			staging_request(session, identifiers, gate, staging,
							{shuba::platform::make_local_file_source(
								source_path, "kept.jpg")}),
			progress, cancellation);
	REQUIRE(staged.succeeded());
	const std::filesystem::path pending_path =
		*staged.sources.front().staged_path;

	shuba::platform::SyntheticSourceImageDecodeService decoder;
	decoder.set_decoded_pixels(test_pixels());
	shuba::platform::MarkerInternalPhotoCodec codec;
	identifiers.script_stable_identifier("item-b21-validation-reserved");
	shuba::ui::ItemSaveWithPendingPhotosResult result =
		shuba::ui::save_item_draft_and_import_pending_photos(
			shuba::ui::ItemSaveWithPendingPhotosRequest{
				.current_session = session,
				.identifiers	 = identifiers,
				.clock			 = clock,
				.operation_gate	 = gate,
				.staging_service = staging,
				.decode_service	 = decoder,
				.photo_codec	 = codec,
				.draft = shuba::ui::ItemDraft{.display_name			= "",
											  .category				= "Testing",
											  .warning_acknowledged = true},
				.pending_sources	  = staged.sources,
				.create_previous_copy = false},
			progress, cancellation);

	REQUIRE(result.save_result.failed());
	REQUIRE_FALSE(result.import_attempted);
	REQUIRE_FALSE(result.cleanup_attempted);
	REQUIRE(result.pending_sources.size() == 1U);
	REQUIRE(result.pending_sources.front().ready_for_import());
	REQUIRE(result.pending_sources.front().staged_path == pending_path);
	REQUIRE(std::filesystem::exists(pending_path));
	REQUIRE(session.repository.items.empty());
}
