#include "Persistence/CatalogStorage.hpp"
#include "Persistence/MetadataSchema.hpp"
#include "Platform/LinuxFakes.hpp"
#include "UI/CatalogSession.hpp"

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
		if (!root_path.filename().string().starts_with("shuba-b18-"))
			throw std::logic_error{"unsafe B18 temporary directory name"};

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

[[nodiscard]] std::string read_text(const std::filesystem::path& path) {
	std::ifstream input{path, std::ios::binary};
	REQUIRE(input.is_open());
	std::ostringstream buffer;
	buffer << input.rdbuf();
	return buffer.str();
}

[[nodiscard]] bool contains(std::string_view text, std::string_view fragment) {
	return text.find(fragment) != std::string_view::npos;
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

[[nodiscard]] const shuba::persistence::ItemEnvelope& only_item(
	const shuba::ui::CatalogSessionState& session) {
	REQUIRE(session.repository.items.size() == 1U);
	return session.repository.items.front();
}

[[nodiscard]] const shuba::persistence::StorageEnvelope& only_storage(
	const shuba::ui::CatalogSessionState& session) {
	REQUIRE(session.repository.storages.size() == 1U);
	return session.repository.storages.front();
}

[[nodiscard]] shuba::domain::TagRow tag(std::string key, std::string value) {
	return shuba::domain::TagRow{.key	= std::move(key),
								 .value = std::move(value)};
}
}	 // namespace

TEST_CASE(
	"B18 item creation requires acknowledgement for missing storage and "
	"photo") {
	TemporaryDirectory temporary{"shuba-b18-item-create"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	shuba::platform::ScriptedIdentifierSource identifiers;
	identifiers.script_stable_identifier("catalog-b18-item-create");
	identifiers.script_operation_identifier("operation-init-item-create");
	shuba::core::ManualClock clock{shuba::core::EpochMilliseconds{1000}};
	shuba::ui::CatalogSessionState session =
		load_session(path_provider, identifiers, clock);
	REQUIRE(session.ready_for_browsing());

	identifiers.script_stable_identifier("item-created");
	clock.set_now(shuba::core::EpochMilliseconds{2000});
	shuba::ui::ItemDraft draft{.display_name = " Boots ",
							   .category	 = " Shoes ",
							   .tags		 = {tag("brand", "Acme")},
							   .notes		 = "Needs cleaning"};
	shuba::ui::EntityEditResult warning = shuba::ui::save_item_draft(
		edit_request(session, identifiers, clock), draft);

	REQUIRE_FALSE(warning.failed());
	REQUIRE_FALSE(warning.metadata_changed);
	REQUIRE(warning.saved_record_id.has_value());
	REQUIRE(warning.saved_record_id->value() == "item-created");
	REQUIRE(warning.warning_acknowledgement_required);
	REQUIRE(warning.diagnostics.size() == 2U);
	REQUIRE(warning.diagnostics[0].code == "item_saved_without_storage");
	REQUIRE(warning.diagnostics[1].code == "item_saved_without_photo");
	REQUIRE(session.repository.items.empty());

	draft.reserved_new_id	   = warning.saved_record_id;
	draft.warning_acknowledged = true;
	identifiers.script_operation_identifier("operation-save-item-created");
	shuba::ui::EntityEditResult saved = shuba::ui::save_item_draft(
		edit_request(session, identifiers, clock), draft);

	REQUIRE(saved.succeeded());
	REQUIRE(saved.metadata_changed);
	REQUIRE(saved.saved_record_id.has_value());
	REQUIRE(saved.saved_record_id->value() == "item-created");
	REQUIRE(saved.session.repository.items.size() == 1U);
	const shuba::persistence::ItemEnvelope& item = only_item(saved.session);
	REQUIRE(item.record.display_name == "Boots");
	REQUIRE(item.record.category == "Shoes");
	REQUIRE(item.record.notes == "Needs cleaning");
	REQUIRE(item.record.tags
			== std::vector<shuba::domain::TagRow>{tag("brand", "Acme")});
	REQUIRE_FALSE(item.record.storage_id.has_value());
	REQUIRE(saved.session.search_index.items.size() == 1U);

	const std::string items_text = read_text(
		saved.session.paths->active_catalog_root / "data/items.jsonl");
	REQUIRE(contains(items_text, "item-created"));
	REQUIRE(contains(items_text, "Boots"));
}

TEST_CASE("B18 item editing validates tags and preserves unknown fields") {
	TemporaryDirectory temporary{"shuba-b18-item-edit"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	shuba::platform::ScriptedIdentifierSource identifiers;
	identifiers.script_stable_identifier("catalog-b18-item-edit");
	identifiers.script_operation_identifier("operation-init-item-edit");
	shuba::core::ManualClock clock{shuba::core::EpochMilliseconds{1000}};
	shuba::ui::CatalogSessionState session =
		load_session(path_provider, identifiers, clock);

	identifiers.script_stable_identifier("item-edited");
	identifiers.script_operation_identifier("operation-create-item-edited");
	shuba::ui::ItemDraft initial{.display_name		   = "Camera",
								 .category			   = "Electronics",
								 .notes				   = "Original note",
								 .warning_acknowledged = true};
	shuba::ui::EntityEditResult created = shuba::ui::save_item_draft(
		edit_request(session, identifiers, clock), initial);
	REQUIRE(created.succeeded());
	session = created.session;
	session.repository.items.front().unknown_fields.emplace(
		"futureField", R"({"nested":[1,2]})");

	shuba::ui::ItemDraft invalid{.existing_id		   = make_id("item-edited"),
								 .display_name		   = "Camera",
								 .category			   = "Electronics",
								 .tags				   = {tag("", "bad")},
								 .warning_acknowledged = true};
	shuba::ui::EntityEditResult blocked = shuba::ui::save_item_draft(
		edit_request(session, identifiers, clock), invalid);
	REQUIRE(blocked.failed());
	REQUIRE_FALSE(blocked.metadata_changed);
	REQUIRE(blocked.diagnostics.size() == 1U);
	REQUIRE(blocked.diagnostics.front().code == "blank_tag_key_blocked");

	identifiers.script_operation_identifier("operation-edit-item-edited");
	clock.set_now(shuba::core::EpochMilliseconds{3000});
	shuba::ui::ItemDraft edited{.existing_id		  = make_id("item-edited"),
								.display_name		  = "Camera kit",
								.category			  = "Electronics",
								.tags				  = {tag("brand", "Canon")},
								.notes				  = "Updated note",
								.warning_acknowledged = true};
	shuba::ui::EntityEditResult saved = shuba::ui::save_item_draft(
		edit_request(session, identifiers, clock), edited);

	REQUIRE(saved.succeeded());
	REQUIRE(saved.metadata_changed);
	const shuba::persistence::ItemEnvelope& item = only_item(saved.session);
	REQUIRE(item.record.display_name == "Camera kit");
	REQUIRE(item.record.timestamps.created_at
			== shuba::core::EpochMilliseconds{1000});
	REQUIRE(item.record.timestamps.updated_at
			== shuba::core::EpochMilliseconds{3000});
	REQUIRE(item.unknown_fields.contains("futureField"));
	const std::string items_text = read_text(
		saved.session.paths->active_catalog_root / "data/items.jsonl");
	REQUIRE(contains(items_text, "futureField"));
	REQUIRE(contains(items_text, "Camera kit"));
}

TEST_CASE(
	"B18 storage creation and parent validation cover nested storage rules") {
	TemporaryDirectory temporary{"shuba-b18-storage-parent"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	shuba::platform::ScriptedIdentifierSource identifiers;
	identifiers.script_stable_identifier("catalog-b18-storage-parent");
	identifiers.script_operation_identifier("operation-init-storage-parent");
	shuba::core::ManualClock clock{shuba::core::EpochMilliseconds{1000}};
	shuba::ui::CatalogSessionState session =
		load_session(path_provider, identifiers, clock);

	identifiers.script_stable_identifier("storage-root");
	identifiers.script_operation_identifier("operation-create-root");
	shuba::ui::StorageDraft root_draft{.display_name = "Main closet",
									   .storage_type = "Closet",
									   .location	 = "Hall"};
	shuba::ui::EntityEditResult root_saved = shuba::ui::save_storage_draft(
		edit_request(session, identifiers, clock), root_draft);
	REQUIRE(root_saved.succeeded());
	REQUIRE(root_saved.saved_record_id->value() == "storage-root");
	session = root_saved.session;

	identifiers.script_stable_identifier("storage-child");
	identifiers.script_operation_identifier("operation-create-child");
	shuba::ui::StorageDraft child_draft{
		.display_name	   = "Shoe box",
		.storage_type	   = "Box",
		.parent_storage_id = make_id("storage-root"),
		.tags			   = {tag("zone", "top")}};
	shuba::ui::EntityEditResult child_saved = shuba::ui::save_storage_draft(
		edit_request(session, identifiers, clock), child_draft);
	REQUIRE(child_saved.succeeded());
	REQUIRE(child_saved.session.repository.storages.size() == 2U);
	REQUIRE(
		child_saved.session.repository.storage_projections.at("storage-child")
			.parent_reference_state
		== shuba::domain::ReferenceState::Resolved);
	session = child_saved.session;

	shuba::ui::StorageDraft cycle_draft{
		.existing_id	   = make_id("storage-root"),
		.display_name	   = "Main closet",
		.storage_type	   = "Closet",
		.parent_storage_id = make_id("storage-child")};
	shuba::ui::EntityEditResult cycle_blocked = shuba::ui::save_storage_draft(
		edit_request(session, identifiers, clock), cycle_draft);
	REQUIRE(cycle_blocked.failed());
	REQUIRE(cycle_blocked.diagnostics.size() == 1U);
	REQUIRE(cycle_blocked.diagnostics.front().code == "storage_parent_cycle");
	REQUIRE_FALSE(cycle_blocked.metadata_changed);
}

TEST_CASE(
	"B18 archive actions warn for storage contents and keep hard delete "
	"gated") {
	TemporaryDirectory temporary{"shuba-b18-archive"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	shuba::platform::ScriptedIdentifierSource identifiers;
	identifiers.script_stable_identifier("catalog-b18-archive");
	identifiers.script_operation_identifier("operation-init-archive");
	shuba::core::ManualClock clock{shuba::core::EpochMilliseconds{1000}};
	shuba::ui::CatalogSessionState session =
		load_session(path_provider, identifiers, clock);

	identifiers.script_stable_identifier("storage-archive");
	identifiers.script_operation_identifier("operation-create-storage-archive");
	shuba::ui::EntityEditResult storage_saved = shuba::ui::save_storage_draft(
		edit_request(session, identifiers, clock),
		shuba::ui::StorageDraft{.display_name = "Archive shelf",
								.storage_type = "Shelf"});
	REQUIRE(storage_saved.succeeded());
	session = storage_saved.session;

	identifiers.script_stable_identifier("item-archive");
	identifiers.script_operation_identifier("operation-create-item-archive");
	shuba::ui::EntityEditResult item_saved = shuba::ui::save_item_draft(
		edit_request(session, identifiers, clock),
		shuba::ui::ItemDraft{.display_name		   = "Archived book",
							 .category			   = "Books",
							 .storage_id		   = make_id("storage-archive"),
							 .warning_acknowledged = true});
	REQUIRE(item_saved.succeeded());
	session = item_saved.session;

	identifiers.script_operation_identifier("operation-archive-item");
	shuba::ui::EntityEditResult item_archived =
		shuba::ui::archive_item_in_session(
			edit_request(session, identifiers, clock), make_id("item-archive"));
	REQUIRE(item_archived.succeeded());
	REQUIRE(item_archived.session.repository.items.front().record.status
			== shuba::domain::ItemStatus::Archived);
	REQUIRE_FALSE(shuba::ui::hard_delete_enabled_for_owner(
		item_archived.session, make_id("item-archive"),
		shuba::domain::PhotoOwnerType::Item));
	session = item_archived.session;

	shuba::ui::EntityEditResult storage_warning =
		shuba::ui::archive_storage_in_session(
			edit_request(session, identifiers, clock),
			make_id("storage-archive"), false);
	REQUIRE_FALSE(storage_warning.failed());
	REQUIRE_FALSE(storage_warning.metadata_changed);
	REQUIRE(storage_warning.warning_acknowledgement_required);
	REQUIRE(storage_warning.diagnostics.size() == 1U);
	REQUIRE(storage_warning.diagnostics.front().code
			== "archive_storage_with_contents");
	REQUIRE(session.repository.storages.front().record.lifecycle_status
			== shuba::domain::StorageLifecycleStatus::Active);

	identifiers.script_operation_identifier("operation-archive-storage");
	shuba::ui::EntityEditResult storage_archived =
		shuba::ui::archive_storage_in_session(
			edit_request(session, identifiers, clock),
			make_id("storage-archive"), true);
	REQUIRE(storage_archived.succeeded());
	REQUIRE(storage_archived.metadata_changed);
	REQUIRE(storage_archived.session.repository.storages.front()
				.record.lifecycle_status
			== shuba::domain::StorageLifecycleStatus::Archived);
	REQUIRE(
		storage_archived.session.repository.item_projections.at("item-archive")
			.storage_archived);
	REQUIRE_FALSE(shuba::ui::hard_delete_enabled_for_owner(
		storage_archived.session, make_id("storage-archive"),
		shuba::domain::PhotoOwnerType::Storage));
}
