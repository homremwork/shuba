#include "Persistence/CatalogStorage.hpp"
#include "Platform/JuceHashing.hpp"
#include "Platform/LinuxFakes.hpp"
#include "UI/Session/CatalogStartupSession.hpp"
#include "UI/Session/EntityEditSession.hpp"
#include "UI/Session/PhotoSession.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

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

[[nodiscard]] shuba::core::OperationIdentifier make_operation_id(
	std::string text) {
	std::optional<shuba::core::OperationIdentifier> identifier =
		shuba::core::OperationIdentifier::try_create_file_safe(std::move(text));
	REQUIRE(identifier.has_value());
	return *identifier;
}

[[nodiscard]] bool has_diagnostic_code(
	const std::vector<shuba::ui::EntityEditDiagnostic>& diagnostics,
	std::string_view code) {
	for (const shuba::ui::EntityEditDiagnostic& diagnostic : diagnostics)
		if (diagnostic.code == code)
			return true;
	return false;
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

void import_two_photos_into_owner(
	shuba::ui::CatalogSessionState& session, TemporaryDirectory& temporary,
	shuba::platform::ScriptedIdentifierSource& identifiers,
	shuba::core::ManualClock& clock, const shuba::domain::PhotoOwner& owner,
	std::string operation_id, std::string first_photo_id,
	std::string second_photo_id, const std::string& source_prefix) {
	const std::filesystem::path source_a =
		temporary.path() / (source_prefix + "-a.jpg");
	const std::filesystem::path source_b =
		temporary.path() / (source_prefix + "-b.jpg");
	write_text(source_a, source_prefix + "-a");
	write_text(source_b, source_prefix + "-b");
	shuba::platform::LinuxFakeContentStagingService staging;
	shuba::platform::JuceMd5SourceByteFingerprintService fingerprinting;
	shuba::platform::SyntheticSourceImageDecodeService decoder;
	decoder.set_decoded_pixels(test_pixels());
	shuba::platform::MarkerInternalPhotoCodec codec;
	shuba::core::OperationGate gate;
	shuba::platform::ProgressCollector progress;
	shuba::platform::ManualCancellationToken cancellation;
	identifiers.script_operation_identifier(std::move(operation_id));
	identifiers.script_stable_identifier(std::move(first_photo_id));
	identifiers.script_stable_identifier(std::move(second_photo_id));

	shuba::ui::PhotoImportSessionResult imported =
		shuba::ui::import_photos_into_session(
			shuba::ui::PhotoImportSessionRequest{
				.current_session	 = session,
				.identifiers		 = identifiers,
				.clock				 = clock,
				.operation_gate		 = gate,
				.staging_service	 = staging,
				.fingerprint_service = fingerprinting,
				.decode_service		 = decoder,
				.photo_codec		 = codec,
				.owner				 = owner,
				.sources = {shuba::platform::make_local_file_source(
								source_a, source_prefix + "-a.jpg"),
							shuba::platform::make_local_file_source(
								source_b, source_prefix + "-b.jpg")},
				.create_previous_copy = false},
			progress, cancellation);
	REQUIRE(imported.succeeded());
	REQUIRE(imported.summary.success_count == 2U);
	session = std::move(imported.session);
}
}	 // namespace

TEST_CASE("R13 no-photo warning contains durable user language") {
	TemporaryDirectory temporary{"shuba-b19-no-photo-warning"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	shuba::platform::ScriptedIdentifierSource identifiers;
	identifiers.script_stable_identifier("catalog-r13-warning");
	identifiers.script_operation_identifier("operation-r13-warning-init");
	shuba::core::ManualClock clock{shuba::core::EpochMilliseconds{1000}};
	shuba::ui::CatalogSessionState session =
		load_session(path_provider, identifiers, clock);
	identifiers.script_stable_identifier("item-r13-warning");
	identifiers.script_operation_identifier("operation-r13-warning-save");

	const shuba::ui::EntityEditResult saved = shuba::ui::save_item_draft(
		edit_request(session, identifiers, clock),
		shuba::ui::ItemDraft{.display_name = "No photo item",
							 .category = "Testing"});

	REQUIRE(saved.warning_acknowledgement_required);
	const std::vector<shuba::ui::EntityEditDiagnostic>::const_iterator warning =
		std::ranges::find_if(saved.diagnostics,
			[](const shuba::ui::EntityEditDiagnostic& diagnostic) {
				return diagnostic.code == "item_saved_without_photo";
			});
	REQUIRE(warning != saved.diagnostics.end());
	REQUIRE(warning->message
			== "Item has no photos yet. Add photos now or later.");
	REQUIRE(warning->message.find("B19") == std::string::npos);
}

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
	shuba::platform::JuceMd5SourceByteFingerprintService fingerprinting;
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
				.current_session	 = session,
				.identifiers		 = identifiers,
				.clock				 = clock,
				.operation_gate		 = gate,
				.staging_service	 = staging,
				.fingerprint_service = fingerprinting,
				.decode_service		 = decoder,
				.photo_codec		 = codec,
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
	shuba::platform::JuceMd5SourceByteFingerprintService fingerprinting;
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
				.current_session	 = session,
				.identifiers		 = identifiers,
				.clock				 = clock,
				.operation_gate		 = gate,
				.staging_service	 = staging,
				.fingerprint_service = fingerprinting,
				.decode_service		 = decoder,
				.photo_codec		 = codec,
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

TEST_CASE("B19 deletes one photo metadata-first and preserves owner data") {
	TemporaryDirectory temporary{"shuba-b19-delete-photo"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	shuba::platform::ScriptedIdentifierSource identifiers;
	identifiers.script_stable_identifier("catalog-b19-delete");
	identifiers.script_operation_identifier("operation-init-b19-delete");
	shuba::core::ManualClock clock{shuba::core::EpochMilliseconds{1000}};
	shuba::ui::CatalogSessionState session =
		load_session(path_provider, identifiers, clock);

	identifiers.script_stable_identifier("item-b19-delete");
	identifiers.script_operation_identifier("operation-create-b19-delete-item");
	shuba::ui::EntityEditResult item_saved = shuba::ui::save_item_draft(
		edit_request(session, identifiers, clock),
		shuba::ui::ItemDraft{.display_name		   = "Delete photo owner",
							 .category			   = "Testing",
							 .warning_acknowledged = true});
	REQUIRE(item_saved.succeeded());
	session = item_saved.session;

	const std::filesystem::path source_a = temporary.path() / "delete-a.jpg";
	const std::filesystem::path source_b = temporary.path() / "delete-b.jpg";
	write_text(source_a, "delete-a");
	write_text(source_b, "delete-b");
	shuba::platform::LinuxFakeContentStagingService staging;
	shuba::platform::JuceMd5SourceByteFingerprintService fingerprinting;
	shuba::platform::SyntheticSourceImageDecodeService decoder;
	decoder.set_decoded_pixels(test_pixels());
	shuba::platform::MarkerInternalPhotoCodec codec;
	shuba::core::OperationGate gate;
	shuba::platform::ProgressCollector progress;
	shuba::platform::ManualCancellationToken cancellation;
	identifiers.script_operation_identifier("operation-import-b19-delete");
	identifiers.script_stable_identifier("photo-b19-delete-a");
	identifiers.script_stable_identifier("photo-b19-delete-b");

	shuba::ui::PhotoImportSessionResult imported =
		shuba::ui::import_photos_into_session(
			shuba::ui::PhotoImportSessionRequest{
				.current_session	 = session,
				.identifiers		 = identifiers,
				.clock				 = clock,
				.operation_gate		 = gate,
				.staging_service	 = staging,
				.fingerprint_service = fingerprinting,
				.decode_service		 = decoder,
				.photo_codec		 = codec,
				.owner =
					shuba::domain::PhotoOwner{
						.type = shuba::domain::PhotoOwnerType::Item,
						.id	  = make_id("item-b19-delete")},
				.sources = {shuba::platform::make_local_file_source(
								source_a, "delete-a.jpg"),
							shuba::platform::make_local_file_source(
								source_b, "delete-b.jpg")},
				.create_previous_copy = false},
			progress, cancellation);
	REQUIRE(imported.succeeded());
	session = imported.session;

	const std::string item_text_before =
		read_text(session.paths->active_catalog_root / "data/items.jsonl");
	const std::filesystem::path deleted_media_path =
		session.paths->active_catalog_root
		/ "media/photos/photo-b19-delete-a.jxl";
	const std::filesystem::path remaining_media_path =
		session.paths->active_catalog_root
		/ "media/photos/photo-b19-delete-b.jxl";
	REQUIRE(std::filesystem::exists(deleted_media_path));
	REQUIRE(std::filesystem::exists(remaining_media_path));

	identifiers.script_operation_identifier("operation-delete-b19-photo");
	shuba::ui::EntityEditResult deleted = shuba::ui::delete_photo_in_session(
		edit_request(session, identifiers, clock),
		make_id("photo-b19-delete-a"));

	REQUIRE(deleted.succeeded());
	REQUIRE(deleted.metadata_changed);
	REQUIRE(deleted.session.repository.photos.size() == 1U);
	REQUIRE(deleted.session.repository.photos.front().record.id
			== make_id("photo-b19-delete-b"));
	REQUIRE(deleted.session.repository.item_projections.at("item-b19-delete")
				.representative_photo_id
			== make_id("photo-b19-delete-b"));
	REQUIRE_FALSE(std::filesystem::exists(deleted_media_path));
	REQUIRE(std::filesystem::exists(remaining_media_path));
	const std::string item_text_after = read_text(
		deleted.session.paths->active_catalog_root / "data/items.jsonl");
	REQUIRE(item_text_after == item_text_before);
	const std::string photos_text = read_text(
		deleted.session.paths->active_catalog_root / "data/photos.jsonl");
	REQUIRE(photos_text.find(R"("id":"photo-b19-delete-a")")
			== std::string::npos);
	REQUIRE(photos_text.find(R"("id":"photo-b19-delete-b")")
			!= std::string::npos);
}

TEST_CASE("B19 deletes one storage photo and preserves storage data",
		  "[b19][photo-delete][storage]") {
	TemporaryDirectory temporary{"shuba-b19-delete-storage-photo"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	shuba::platform::ScriptedIdentifierSource identifiers;
	identifiers.script_stable_identifier("catalog-b19-delete-storage");
	identifiers.script_operation_identifier(
		"operation-init-b19-delete-storage");
	shuba::core::ManualClock clock{shuba::core::EpochMilliseconds{1000}};
	shuba::ui::CatalogSessionState session =
		load_session(path_provider, identifiers, clock);

	identifiers.script_stable_identifier("storage-b19-delete");
	identifiers.script_operation_identifier(
		"operation-create-b19-delete-storage");
	shuba::ui::EntityEditResult storage_saved = shuba::ui::save_storage_draft(
		edit_request(session, identifiers, clock),
		shuba::ui::StorageDraft{.display_name = "Delete photo storage",
								.storage_type = "Box"});
	REQUIRE(storage_saved.succeeded());
	session = storage_saved.session;

	import_two_photos_into_owner(
		session, temporary, identifiers, clock,
		shuba::domain::PhotoOwner{
			.type = shuba::domain::PhotoOwnerType::Storage,
			.id	  = make_id("storage-b19-delete")},
		"operation-import-b19-delete-storage", "photo-b19-delete-storage-a",
		"photo-b19-delete-storage-b", "delete-storage");

	const std::string storage_text_before =
		read_text(session.paths->active_catalog_root / "data/storages.jsonl");
	const std::filesystem::path deleted_media_path =
		session.paths->active_catalog_root
		/ "media/photos/photo-b19-delete-storage-a.jxl";
	const std::filesystem::path remaining_media_path =
		session.paths->active_catalog_root
		/ "media/photos/photo-b19-delete-storage-b.jxl";
	REQUIRE(std::filesystem::exists(deleted_media_path));
	REQUIRE(std::filesystem::exists(remaining_media_path));

	identifiers.script_operation_identifier(
		"operation-delete-b19-storage-photo");
	shuba::ui::EntityEditResult deleted = shuba::ui::delete_photo_in_session(
		edit_request(session, identifiers, clock),
		make_id("photo-b19-delete-storage-a"));

	REQUIRE(deleted.succeeded());
	REQUIRE(deleted.metadata_changed);
	REQUIRE(deleted.session.repository.photos.size() == 1U);
	REQUIRE(deleted.session.repository.photos.front().record.id
			== make_id("photo-b19-delete-storage-b"));
	REQUIRE(
		deleted.session.repository.storage_projections.at("storage-b19-delete")
			.representative_photo_id
		== make_id("photo-b19-delete-storage-b"));
	REQUIRE(deleted.session.repository.storage_photo_projections
				.at("storage-b19-delete")
				.representative_photo_id
			== make_id("photo-b19-delete-storage-b"));
	REQUIRE_FALSE(std::filesystem::exists(deleted_media_path));
	REQUIRE(std::filesystem::exists(remaining_media_path));
	REQUIRE(read_text(deleted.session.paths->active_catalog_root
					  / "data/storages.jsonl")
			== storage_text_before);
	const std::string photos_text = read_text(
		deleted.session.paths->active_catalog_root / "data/photos.jsonl");
	REQUIRE(photos_text.find(R"("id":"photo-b19-delete-storage-a")")
			== std::string::npos);
	REQUIRE(photos_text.find(R"("id":"photo-b19-delete-storage-b")")
			!= std::string::npos);
}

TEST_CASE("B19 delete photo metadata failure preserves metadata and media",
		  "[b19][photo-delete][metadata-failure]") {
	TemporaryDirectory temporary{"shuba-b19-delete-metadata-failure"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	shuba::platform::ScriptedIdentifierSource identifiers;
	identifiers.script_stable_identifier("catalog-b19-delete-metadata-failure");
	identifiers.script_operation_identifier(
		"operation-init-b19-delete-metadata-failure");
	shuba::core::ManualClock clock{shuba::core::EpochMilliseconds{1000}};
	shuba::ui::CatalogSessionState session =
		load_session(path_provider, identifiers, clock);

	identifiers.script_stable_identifier("item-b19-delete-metadata-failure");
	identifiers.script_operation_identifier(
		"operation-create-b19-delete-metadata-failure-item");
	shuba::ui::EntityEditResult item_saved = shuba::ui::save_item_draft(
		edit_request(session, identifiers, clock),
		shuba::ui::ItemDraft{.display_name = "Delete metadata failure owner",
							 .category	   = "Testing",
							 .warning_acknowledged = true});
	REQUIRE(item_saved.succeeded());
	session = item_saved.session;

	import_two_photos_into_owner(
		session, temporary, identifiers, clock,
		shuba::domain::PhotoOwner{
			.type = shuba::domain::PhotoOwnerType::Item,
			.id	  = make_id("item-b19-delete-metadata-failure")},
		"operation-import-b19-delete-metadata-failure",
		"photo-b19-delete-metadata-failure-a",
		"photo-b19-delete-metadata-failure-b", "delete-metadata-failure");

	const std::string item_text_before =
		read_text(session.paths->active_catalog_root / "data/items.jsonl");
	const std::string photos_text_before =
		read_text(session.paths->active_catalog_root / "data/photos.jsonl");
	const std::filesystem::path deleted_media_path =
		session.paths->active_catalog_root
		/ "media/photos/photo-b19-delete-metadata-failure-a.jxl";
	const std::filesystem::path remaining_media_path =
		session.paths->active_catalog_root
		/ "media/photos/photo-b19-delete-metadata-failure-b.jxl";
	REQUIRE(std::filesystem::exists(deleted_media_path));
	REQUIRE(std::filesystem::exists(remaining_media_path));

	const shuba::core::OperationIdentifier delete_operation_id =
		make_operation_id("operation-delete-b19-metadata-failure");
	const std::string group_name = shuba::persistence::previous_copy_group_name(
		clock.now(), delete_operation_id);
	const std::filesystem::path colliding_temp_path =
		shuba::persistence::metadata_temp_file_path(
			session.paths->active_catalog_root,
			std::filesystem::path{
				std::string{shuba::persistence::photos_data_file_path}},
			group_name);
	std::filesystem::create_directories(colliding_temp_path);

	identifiers.script_operation_identifier(delete_operation_id.value());
	shuba::ui::EntityEditResult deleted = shuba::ui::delete_photo_in_session(
		edit_request(session, identifiers, clock),
		make_id("photo-b19-delete-metadata-failure-a"));

	REQUIRE(deleted.failed());
	REQUIRE(deleted.category
			== shuba::core::OperationResultCategory::ReplacementFailure);
	REQUIRE_FALSE(deleted.metadata_changed);
	REQUIRE(deleted.session.repository.photos.size() == 2U);
	REQUIRE(has_diagnostic_code(deleted.diagnostics, "temp_write_failed"));
	REQUIRE(
		has_diagnostic_code(deleted.diagnostics, "photos_jsonl_commit_failed"));
	REQUIRE(read_text(session.paths->active_catalog_root / "data/items.jsonl")
			== item_text_before);
	REQUIRE(read_text(session.paths->active_catalog_root / "data/photos.jsonl")
			== photos_text_before);
	REQUIRE(std::filesystem::exists(deleted_media_path));
	REQUIRE(std::filesystem::exists(remaining_media_path));
}

TEST_CASE("B19 delete photo reports orphan media warning after cleanup failure",
		  "[b19][photo-delete][media-cleanup-failure]") {
	TemporaryDirectory temporary{"shuba-b19-delete-media-cleanup-failure"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	shuba::platform::ScriptedIdentifierSource identifiers;
	identifiers.script_stable_identifier("catalog-b19-delete-media-failure");
	identifiers.script_operation_identifier(
		"operation-init-b19-delete-media-failure");
	shuba::core::ManualClock clock{shuba::core::EpochMilliseconds{1000}};
	shuba::ui::CatalogSessionState session =
		load_session(path_provider, identifiers, clock);

	identifiers.script_stable_identifier("item-b19-delete-media-failure");
	identifiers.script_operation_identifier(
		"operation-create-b19-delete-media-failure-item");
	shuba::ui::EntityEditResult item_saved = shuba::ui::save_item_draft(
		edit_request(session, identifiers, clock),
		shuba::ui::ItemDraft{.display_name = "Delete media failure owner",
							 .category	   = "Testing",
							 .warning_acknowledged = true});
	REQUIRE(item_saved.succeeded());
	session = item_saved.session;

	import_two_photos_into_owner(
		session, temporary, identifiers, clock,
		shuba::domain::PhotoOwner{
			.type = shuba::domain::PhotoOwnerType::Item,
			.id	  = make_id("item-b19-delete-media-failure")},
		"operation-import-b19-delete-media-failure",
		"photo-b19-delete-media-failure-a", "photo-b19-delete-media-failure-b",
		"delete-media-failure");

	const std::string item_text_before =
		read_text(session.paths->active_catalog_root / "data/items.jsonl");
	const std::filesystem::path deleted_media_path =
		session.paths->active_catalog_root
		/ "media/photos/photo-b19-delete-media-failure-a.jxl";
	const std::filesystem::path remaining_media_path =
		session.paths->active_catalog_root
		/ "media/photos/photo-b19-delete-media-failure-b.jxl";
	REQUIRE(std::filesystem::exists(deleted_media_path));
	REQUIRE(std::filesystem::exists(remaining_media_path));
	std::filesystem::remove(deleted_media_path);
	std::filesystem::create_directory(deleted_media_path);
	REQUIRE(std::filesystem::is_directory(deleted_media_path));

	identifiers.script_operation_identifier(
		"operation-delete-b19-media-cleanup-failure");
	shuba::ui::EntityEditResult deleted = shuba::ui::delete_photo_in_session(
		edit_request(session, identifiers, clock),
		make_id("photo-b19-delete-media-failure-a"));

	REQUIRE(deleted.succeeded());
	REQUIRE(deleted.metadata_changed);
	REQUIRE(has_diagnostic_code(deleted.diagnostics,
								"photo_delete_orphan_media_left"));
	REQUIRE(deleted.session.repository.photos.size() == 1U);
	REQUIRE(deleted.session.repository.photos.front().record.id
			== make_id("photo-b19-delete-media-failure-b"));
	REQUIRE(deleted.session.repository.item_projections
				.at("item-b19-delete-media-failure")
				.representative_photo_id
			== make_id("photo-b19-delete-media-failure-b"));
	REQUIRE(std::filesystem::is_directory(deleted_media_path));
	REQUIRE(std::filesystem::exists(remaining_media_path));
	REQUIRE(read_text(deleted.session.paths->active_catalog_root
					  / "data/items.jsonl")
			== item_text_before);
	const std::string photos_text = read_text(
		deleted.session.paths->active_catalog_root / "data/photos.jsonl");
	REQUIRE(photos_text.find(R"("id":"photo-b19-delete-media-failure-a")")
			== std::string::npos);
	REQUIRE(photos_text.find(R"("id":"photo-b19-delete-media-failure-b")")
			!= std::string::npos);
}
