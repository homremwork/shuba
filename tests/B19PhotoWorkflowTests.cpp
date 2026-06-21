#include "Persistence/CatalogStorage.hpp"
#include "Platform/LinuxFakes.hpp"
#include "UI/Session/CatalogStartupSession.hpp"
#include "UI/Session/EntityEditSession.hpp"
#include "UI/Session/PhotoSession.hpp"

#include <catch2/catch_test_macros.hpp>

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
	explicit TemporaryDirectory(std::string leaf_name)
		: root_path(std::filesystem::temp_directory_path()
					/ std::move(leaf_name)) {
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
		if (!root_path.filename().string().starts_with("shuba-b19-"))
			throw std::logic_error{"unsafe B19 temporary directory name"};

		std::filesystem::remove_all(root_path);
		std::filesystem::create_directories(root_path);
	}

	std::filesystem::path root_path;
};

[[nodiscard]] shuba::core::StableIdentifier make_id(std::string text) {
	std::optional<shuba::core::StableIdentifier> identifier =
		shuba::core::StableIdentifier::try_create_file_safe(std::move(text));
	REQUIRE(identifier.has_value());
	return *identifier;
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
		.bytes				= {8, 16, 24, 255, 32, 40, 48, 255},
		.source_description = "B19 synthetic source"};
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

[[nodiscard]] shuba::ui::EntityEditRequest edit_request(
	shuba::ui::CatalogSessionState session,
	shuba::platform::ScriptedIdentifierSource& identifiers,
	shuba::core::ManualClock& clock) {
	return shuba::ui::EntityEditRequest{.current_session = std::move(session),
										.identifiers	 = identifiers,
										.clock			 = clock,
										.create_previous_copy = false};
}
}	 // namespace

TEST_CASE("B19 imports photos through session and rebuilds photo projections") {
	TemporaryDirectory temporary{"shuba-b19-import-session"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	shuba::platform::ScriptedIdentifierSource identifiers;
	identifiers.script_stable_identifier("catalog-b19-import");
	identifiers.script_operation_identifier("operation-init-b19-import");
	shuba::core::ManualClock clock{shuba::core::EpochMilliseconds{1000}};
	shuba::ui::CatalogSessionState session =
		load_session(path_provider, identifiers, clock);
	REQUIRE(session.ready_for_browsing());

	identifiers.script_stable_identifier("item-b19-import");
	identifiers.script_operation_identifier("operation-create-b19-item");
	shuba::ui::EntityEditResult item_saved = shuba::ui::save_item_draft(
		edit_request(session, identifiers, clock),
		shuba::ui::ItemDraft{.display_name		   = "Photo owner",
							 .category			   = "Testing",
							 .warning_acknowledged = true});
	REQUIRE(item_saved.succeeded());
	session = item_saved.session;

	const std::filesystem::path source_path = temporary.path() / "source.jpg";
	write_text(source_path, "source-bytes");
	shuba::platform::LinuxFakeContentStagingService staging;
	shuba::platform::SyntheticSourceImageDecodeService decoder;
	decoder.set_decoded_pixels(test_pixels());
	shuba::platform::MarkerInternalPhotoCodec codec;
	shuba::core::OperationGate gate;
	shuba::platform::ProgressCollector progress;
	shuba::platform::ManualCancellationToken cancellation;
	identifiers.script_operation_identifier("operation-import-b19-photo");
	identifiers.script_stable_identifier("photo-b19-imported");

	shuba::ui::PhotoImportSessionResult imported =
		shuba::ui::import_photos_into_session(
			shuba::ui::PhotoImportSessionRequest{
				.current_session = session,
				.identifiers	 = identifiers,
				.clock			 = clock,
				.operation_gate	 = gate,
				.staging_service = staging,
				.decode_service	 = decoder,
				.photo_codec	 = codec,
				.owner =
					shuba::domain::PhotoOwner{
						.type = shuba::domain::PhotoOwnerType::Item,
						.id	  = make_id("item-b19-import")},
				.sources = {shuba::platform::make_local_file_source(
					source_path, "Picked Photo.JPG")},
				.create_previous_copy = false},
			progress, cancellation);

	REQUIRE(imported.succeeded());
	REQUIRE(imported.metadata_changed);
	REQUIRE(imported.summary.success_count == 1U);
	REQUIRE(imported.imported_photo_ids.size() == 1U);
	REQUIRE(imported.imported_photo_ids.front().value()
			== "photo-b19-imported");
	REQUIRE(imported.session.repository.photos.size() == 1U);
	const shuba::persistence::PhotoEnvelope& photo =
		imported.session.repository.photos.front();
	REQUIRE(photo.record.is_main);
	REQUIRE(photo.record.sort_order == 1000);
	REQUIRE(photo.record.source_mime_type == "image/jpeg");
	REQUIRE(imported.session.repository.item_projections.at("item-b19-import")
				.representative_usable_photo_id
			== make_id("photo-b19-imported"));
	REQUIRE(std::filesystem::exists(imported.session.paths->active_catalog_root
									/ "media/photos/photo-b19-imported.jxl"));
	const std::string photos_text = read_text(
		imported.session.paths->active_catalog_root / "data/photos.jsonl");
	REQUIRE(photos_text.find(R"("id":"photo-b19-imported")")
			!= std::string::npos);
	REQUIRE_FALSE(progress.events().empty());
}

TEST_CASE("B19 set-main updates only photo metadata and keeps owner data") {
	TemporaryDirectory temporary{"shuba-b19-set-main"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	shuba::platform::ScriptedIdentifierSource identifiers;
	identifiers.script_stable_identifier("catalog-b19-main");
	identifiers.script_operation_identifier("operation-init-b19-main");
	shuba::core::ManualClock clock{shuba::core::EpochMilliseconds{1000}};
	shuba::ui::CatalogSessionState session =
		load_session(path_provider, identifiers, clock);

	identifiers.script_stable_identifier("item-b19-main");
	identifiers.script_operation_identifier("operation-create-b19-main-item");
	shuba::ui::EntityEditResult item_saved = shuba::ui::save_item_draft(
		edit_request(session, identifiers, clock),
		shuba::ui::ItemDraft{.display_name		   = "Main photo owner",
							 .category			   = "Testing",
							 .warning_acknowledged = true});
	REQUIRE(item_saved.succeeded());
	session = item_saved.session;

	const std::filesystem::path source_a = temporary.path() / "a.jpg";
	const std::filesystem::path source_b = temporary.path() / "b.png";
	write_text(source_a, "a");
	write_text(source_b, "b");
	shuba::platform::LinuxFakeContentStagingService staging;
	shuba::platform::SyntheticSourceImageDecodeService decoder;
	decoder.set_decoded_pixels(test_pixels());
	shuba::platform::MarkerInternalPhotoCodec codec;
	shuba::core::OperationGate gate;
	shuba::platform::ProgressCollector progress;
	shuba::platform::ManualCancellationToken cancellation;
	identifiers.script_operation_identifier("operation-import-b19-main-photos");
	identifiers.script_stable_identifier("photo-b19-main-a");
	identifiers.script_stable_identifier("photo-b19-main-b");

	shuba::ui::PhotoImportSessionResult imported =
		shuba::ui::import_photos_into_session(
			shuba::ui::PhotoImportSessionRequest{
				.current_session = session,
				.identifiers	 = identifiers,
				.clock			 = clock,
				.operation_gate	 = gate,
				.staging_service = staging,
				.decode_service	 = decoder,
				.photo_codec	 = codec,
				.owner =
					shuba::domain::PhotoOwner{
						.type = shuba::domain::PhotoOwnerType::Item,
						.id	  = make_id("item-b19-main")},
				.sources = {shuba::platform::make_local_file_source(source_a,
																	"a.jpg"),
							shuba::platform::make_local_file_source(source_b,
																	"b.png")},
				.create_previous_copy = false},
			progress, cancellation);
	REQUIRE(imported.succeeded());
	session = imported.session;

	REQUIRE(session.repository.photos.size() == 2U);
	REQUIRE(session.repository.photos[0].record.is_main);
	REQUIRE_FALSE(session.repository.photos[1].record.is_main);
	const std::string item_text_before =
		read_text(session.paths->active_catalog_root / "data/items.jsonl");

	identifiers.script_operation_identifier("operation-set-b19-main");
	shuba::ui::EntityEditResult main_set = shuba::ui::set_main_photo_in_session(
		edit_request(session, identifiers, clock), make_id("photo-b19-main-b"));

	REQUIRE(main_set.succeeded());
	REQUIRE(main_set.metadata_changed);
	REQUIRE_FALSE(main_set.session.repository.photos[0].record.is_main);
	REQUIRE(main_set.session.repository.photos[1].record.is_main);
	REQUIRE(main_set.session.repository.item_projections.at("item-b19-main")
				.representative_photo_id
			== make_id("photo-b19-main-b"));
	const std::string item_text_after = read_text(
		main_set.session.paths->active_catalog_root / "data/items.jsonl");
	REQUIRE(item_text_after == item_text_before);
	const std::string photos_text = read_text(
		main_set.session.paths->active_catalog_root / "data/photos.jsonl");
	REQUIRE(photos_text.find(R"("id":"photo-b19-main-b")")
			!= std::string::npos);
	REQUIRE(photos_text.find(R"("isMain":true)") != std::string::npos);
}
