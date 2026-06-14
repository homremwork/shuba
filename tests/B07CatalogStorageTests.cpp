#include "Persistence/CatalogStorage.hpp"
#include "Persistence/JsonlCatalog.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
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
		auto ignored = std::error_code{};
		std::filesystem::remove_all(root_path, ignored);
	}

	[[nodiscard]] const std::filesystem::path& root() const noexcept {
		return root_path;
	}

	[[nodiscard]] std::filesystem::path app_private_root() const {
		return root_path / "app-private";
	}

private:
	void reset() {
		if (!root_path.filename().string().starts_with("shuba-b07-"))
			throw std::logic_error{"unsafe B07 temporary directory name"};

		std::filesystem::remove_all(root_path);
		std::filesystem::create_directories(root_path);
	}

	std::filesystem::path root_path;
};

[[nodiscard]] shuba::core::StableIdentifier make_id(std::string text) {
	auto identifier =
		shuba::core::StableIdentifier::try_create_file_safe(std::move(text));
	REQUIRE(identifier.has_value());
	return *identifier;
}

[[nodiscard]] shuba::core::OperationIdentifier make_op(std::string text) {
	auto identifier =
		shuba::core::OperationIdentifier::try_create_file_safe(std::move(text));
	REQUIRE(identifier.has_value());
	return *identifier;
}

[[nodiscard]] std::string read_text(const std::filesystem::path& path) {
	auto input = std::ifstream{path, std::ios::binary};
	REQUIRE(input.is_open());
	auto buffer = std::ostringstream{};
	buffer << input.rdbuf();
	return buffer.str();
}

void write_text(const std::filesystem::path& path, std::string_view text) {
	std::filesystem::create_directories(path.parent_path());
	auto output = std::ofstream{path, std::ios::binary | std::ios::trunc};
	REQUIRE(output.is_open());
	output << text;
	REQUIRE(output.good());
}

[[nodiscard]] bool contains(std::string_view text, std::string_view fragment) {
	return text.find(fragment) != std::string_view::npos;
}

[[nodiscard]] std::string item_row(std::string_view id,
								   std::string_view display_name) {
	auto text = std::string{};
	text += R"({"id":")";
	text += id;
	text += R"(","schemaVersion":1,"displayName":")";
	text += display_name;
	text +=
		R"(","category":"other","tags":[],"status":"draft","createdAt":1,"updatedAt":2})";
	return text;
}

[[nodiscard]] std::optional<shuba::core::Diagnostic> validate_items_jsonl(
	std::string_view text) {
	const auto loaded = shuba::persistence::load_item_jsonl(text);
	if (loaded.quarantine_entries.empty())
		return std::nullopt;

	return shuba::core::Diagnostic{
		.severity = shuba::core::DiagnosticSeverity::WriteBlockingError,
		.code	  = "items_validation_failed",
		.message  = "Items JSONL did not validate before replacement.",
		.technical_details =
			"quarantineEntries="
			+ std::to_string(loaded.quarantine_entries.size())};
}

[[nodiscard]] std::size_t count_directories(const std::filesystem::path& path) {
	auto count = std::size_t{};
	for (const auto& entry : std::filesystem::directory_iterator{path})
		if (entry.is_directory())
			++count;
	return count;
}

[[nodiscard]] shuba::persistence::CatalogStorageResult initialize_catalog(
	const std::filesystem::path& app_private_root) {
	return shuba::persistence::initialize_empty_catalog(
		shuba::persistence::EmptyCatalogInitializationRequest{
			.app_private_root = app_private_root,
			.catalog_id		  = make_id("catalog-b07"),
			.created_at		  = shuba::core::EpochMilliseconds{1000},
			.operation_id	  = make_op("init-b07")});
}
}	 // namespace

TEST_CASE("B07 initializes an empty catalog inside an app-private container",
		  "[b07][catalog-storage][init]") {
	using namespace shuba::persistence;

	auto temporary = TemporaryDirectory{"shuba-b07-empty-init"};
	const auto layout =
		make_catalog_container_layout(temporary.app_private_root());

	const auto result =
		initialize_empty_catalog(EmptyCatalogInitializationRequest{
			.app_private_root = temporary.app_private_root(),
			.catalog_id		  = make_id("catalog-empty"),
			.created_at		  = shuba::core::EpochMilliseconds{1760000000000},
			.operation_id	  = make_op("init-empty")});

	REQUIRE(result.succeeded());
	REQUIRE(result.changed_canonical_file);
	REQUIRE(
		std::filesystem::exists(layout.active_catalog_root / "manifest.json"));
	REQUIRE(
		std::filesystem::exists(layout.active_catalog_root / "settings.json"));
	REQUIRE(std::filesystem::exists(layout.active_catalog_root
									/ "data/items.jsonl"));
	REQUIRE(std::filesystem::exists(layout.active_catalog_root
									/ "data/storages.jsonl"));
	REQUIRE(std::filesystem::exists(layout.active_catalog_root
									/ "data/photos.jsonl"));
	REQUIRE(
		std::filesystem::exists(layout.active_catalog_root / "media/photos"));
	REQUIRE(std::filesystem::exists(layout.active_catalog_root
									/ "recovery/quarantine"));
	REQUIRE(std::filesystem::exists(layout.active_catalog_root
									/ "backup/previous-data-copies"));
	REQUIRE(std::filesystem::exists(layout.active_catalog_root / "tmp"));
	REQUIRE(std::filesystem::exists(layout.catalog_rollbacks_root));
	REQUIRE(std::filesystem::exists(layout.operation_tmp_root));
	REQUIRE_FALSE(std::filesystem::exists(layout.active_catalog_root
										  / "backup/previous-catalogs"));
	REQUIRE_FALSE(std::filesystem::exists(layout.active_catalog_root
										  / "catalog-rollbacks"));

	const auto manifest = parse_manifest_json(
		read_text(layout.active_catalog_root / "manifest.json"));
	REQUIRE(manifest.succeeded());
	REQUIRE(manifest.value->catalog_id.value() == "catalog-empty");
	REQUIRE(manifest.value->created_at
			== shuba::core::EpochMilliseconds{1760000000000});

	const auto settings = parse_settings_json(
		read_text(layout.active_catalog_root / "settings.json"));
	REQUIRE(settings.succeeded());
	REQUIRE(settings.value->default_currency == "BYN");
	REQUIRE(read_text(layout.active_catalog_root / "data/items.jsonl").empty());
	REQUIRE(
		read_text(layout.active_catalog_root / "data/storages.jsonl").empty());
	REQUIRE(
		read_text(layout.active_catalog_root / "data/photos.jsonl").empty());
}

TEST_CASE(
	"B07 metadata commit validates temp output and creates previous copies",
	"[b07][catalog-storage][commit]") {
	using namespace shuba::persistence;

	auto temporary = TemporaryDirectory{"shuba-b07-commit"};
	const auto layout =
		make_catalog_container_layout(temporary.app_private_root());
	REQUIRE(initialize_catalog(temporary.app_private_root()).succeeded());

	const auto old_items =
		read_text(layout.active_catalog_root / "data/items.jsonl");
	const auto new_items = item_row("item-a", "First committed item") + "\n";

	const auto commit = commit_metadata_file(CatalogMetadataCommitRequest{
		.active_catalog_root = layout.active_catalog_root,
		.relative_target_path =
			std::filesystem::path{std::string{items_data_file_path}},
		.serialized_content = new_items,
		.committed_at		= shuba::core::EpochMilliseconds{2000},
		.operation_id		= make_op("commit-items"),
		.validator			= validate_items_jsonl});

	REQUIRE(commit.succeeded());
	REQUIRE(commit.changed_canonical_file);
	REQUIRE(commit.previous_copy_created);
	REQUIRE(commit.previous_copy_directory.has_value());
	REQUIRE(read_text(layout.active_catalog_root / "data/items.jsonl")
			== new_items);
	REQUIRE(read_text(*commit.previous_copy_directory / "data/items.jsonl")
			== old_items);
	REQUIRE(std::filesystem::exists(*commit.previous_copy_directory
									/ "manifest.json"));
	REQUIRE(std::filesystem::exists(*commit.previous_copy_directory
									/ "settings.json"));
	REQUIRE(std::filesystem::exists(*commit.previous_copy_directory
									/ "data/storages.jsonl"));
	REQUIRE(std::filesystem::exists(*commit.previous_copy_directory
									/ "data/photos.jsonl"));

	auto committed_in_memory_text = new_items;
	const auto failed = commit_metadata_file(CatalogMetadataCommitRequest{
		.active_catalog_root = layout.active_catalog_root,
		.relative_target_path =
			std::filesystem::path{std::string{items_data_file_path}},
		.serialized_content = std::string{"{bad json\n"},
		.committed_at		= shuba::core::EpochMilliseconds{3000},
		.operation_id		= make_op("commit-invalid"),
		.validator			= validate_items_jsonl});

	if (failed.succeeded())
		committed_in_memory_text = "{bad json\n";

	REQUIRE_FALSE(failed.succeeded());
	REQUIRE(failed.category
			== shuba::core::OperationResultCategory::ValidationFailure);
	REQUIRE(read_text(layout.active_catalog_root / "data/items.jsonl")
			== new_items);
	REQUIRE(committed_in_memory_text == new_items);
	REQUIRE(failed.temp_path.has_value());
	REQUIRE_FALSE(std::filesystem::exists(*failed.temp_path));
}

TEST_CASE(
	"B07 temp write failure keeps canonical metadata and skips previous copy",
	"[b07][catalog-storage][fault-injection]") {
	using namespace shuba::persistence;

	auto temporary = TemporaryDirectory{"shuba-b07-temp-write-failure"};
	const auto layout =
		make_catalog_container_layout(temporary.app_private_root());
	REQUIRE(initialize_catalog(temporary.app_private_root()).succeeded());

	const auto old_items =
		read_text(layout.active_catalog_root / "data/items.jsonl");
	const auto operation_id = make_op("commit-temp-fail");
	const auto group_name	= previous_copy_group_name(
		shuba::core::EpochMilliseconds{4000}, operation_id);
	const auto colliding_temp = metadata_temp_file_path(
		layout.active_catalog_root,
		std::filesystem::path{std::string{items_data_file_path}}, group_name);
	std::filesystem::create_directories(colliding_temp);

	const auto failed = commit_metadata_file(CatalogMetadataCommitRequest{
		.active_catalog_root = layout.active_catalog_root,
		.relative_target_path =
			std::filesystem::path{std::string{items_data_file_path}},
		.serialized_content = item_row("item-b", "Blocked item") + "\n",
		.committed_at		= shuba::core::EpochMilliseconds{4000},
		.operation_id		= operation_id,
		.validator			= validate_items_jsonl});

	REQUIRE_FALSE(failed.succeeded());
	REQUIRE(failed.category
			== shuba::core::OperationResultCategory::TemporaryStorageFailure);
	REQUIRE(read_text(layout.active_catalog_root / "data/items.jsonl")
			== old_items);
	REQUIRE_FALSE(std::filesystem::exists(previous_metadata_copy_directory(
		layout.active_catalog_root, group_name)));
}

TEST_CASE("B07 previous copy failure warns without blocking replacement",
		  "[b07][catalog-storage][previous-copy]") {
	using namespace shuba::persistence;

	auto temporary = TemporaryDirectory{"shuba-b07-previous-copy-failure"};
	const auto layout =
		make_catalog_container_layout(temporary.app_private_root());
	REQUIRE(initialize_catalog(temporary.app_private_root()).succeeded());

	const auto operation_id = make_op("commit-copy-fail");
	const auto group_name	= previous_copy_group_name(
		shuba::core::EpochMilliseconds{5000}, operation_id);
	write_text(previous_metadata_copy_directory(layout.active_catalog_root,
												group_name),
			   "not a directory");

	const auto new_items =
		item_row("item-copy-warning", "Still committed") + "\n";
	const auto commit = commit_metadata_file(CatalogMetadataCommitRequest{
		.active_catalog_root = layout.active_catalog_root,
		.relative_target_path =
			std::filesystem::path{std::string{items_data_file_path}},
		.serialized_content = new_items,
		.committed_at		= shuba::core::EpochMilliseconds{5000},
		.operation_id		= operation_id,
		.validator			= validate_items_jsonl});

	REQUIRE(commit.succeeded());
	REQUIRE(commit.has_warnings());
	REQUIRE_FALSE(commit.previous_copy_created);
	REQUIRE(read_text(layout.active_catalog_root / "data/items.jsonl")
			== new_items);
	REQUIRE(std::ranges::any_of(commit.diagnostics, [](const auto& diagnostic) {
		return diagnostic.code == "previous_copy_failed";
	}));
}

TEST_CASE("B07 previous copy retention keeps the latest ten copy groups",
		  "[b07][catalog-storage][retention]") {
	using namespace shuba::persistence;

	auto temporary = TemporaryDirectory{"shuba-b07-retention"};
	const auto layout =
		make_catalog_container_layout(temporary.app_private_root());
	REQUIRE(initialize_catalog(temporary.app_private_root()).succeeded());

	std::vector<std::string> group_names;
	for (auto index = 1; index <= 12; ++index) {
		const auto operation_id =
			make_op("commit-retention-" + std::to_string(index));
		group_names.push_back(previous_copy_group_name(
			shuba::core::EpochMilliseconds{index}, operation_id));
		const auto commit = commit_metadata_file(CatalogMetadataCommitRequest{
			.active_catalog_root = layout.active_catalog_root,
			.relative_target_path =
				std::filesystem::path{std::string{items_data_file_path}},
			.serialized_content =
				item_row("item-retention-" + std::to_string(index),
						 "Retention item")
				+ "\n",
			.committed_at = shuba::core::EpochMilliseconds{index},
			.operation_id = operation_id,
			.validator	  = validate_items_jsonl});
		REQUIRE(commit.succeeded());
	}

	const auto copies_root = layout.active_catalog_root
							 / std::filesystem::path{std::string{
								 previous_data_copies_directory_path}};
	REQUIRE(count_directories(copies_root) == 10);
	REQUIRE_FALSE(std::filesystem::exists(copies_root / group_names[0]));
	REQUIRE_FALSE(std::filesystem::exists(copies_root / group_names[1]));
	REQUIRE(std::filesystem::exists(copies_root / group_names[2]));
	REQUIRE(std::filesystem::exists(copies_root / group_names[11]));
}

TEST_CASE("B07 startup cleanup removes safe temp leftovers only",
		  "[b07][catalog-storage][cleanup]") {
	using namespace shuba::persistence;

	auto temporary = TemporaryDirectory{"shuba-b07-cleanup"};
	const auto layout =
		make_catalog_container_layout(temporary.app_private_root());
	REQUIRE(initialize_catalog(temporary.app_private_root()).succeeded());

	const auto active_tmp_file = layout.active_catalog_root / "tmp/stale.tmp";
	const auto active_tmp_dir_file =
		layout.active_catalog_root / "tmp/stale-dir/file.txt";
	const auto operation_tmp_file =
		layout.operation_tmp_root / "operation-leftover.bin";
	const auto data_temp =
		layout.active_catalog_root
		/ "data/.items.jsonl.p00000000000000000001-cleanup.tmp";
	const auto root_temp = layout.active_catalog_root
						   / ".manifest.json.p00000000000000000001-cleanup.tmp";
	const auto keep_file = layout.active_catalog_root / "data/keep.tmp";
	write_text(active_tmp_file, "old temp");
	write_text(active_tmp_dir_file, "old nested temp");
	write_text(operation_tmp_file, "operation temp");
	write_text(data_temp, "metadata temp");
	write_text(root_temp, "manifest temp");
	write_text(keep_file, "not ours");

	const auto cleanup =
		cleanup_startup_temporary_files(temporary.app_private_root());

	REQUIRE(cleanup.succeeded());
	REQUIRE_FALSE(std::filesystem::exists(active_tmp_file));
	REQUIRE_FALSE(std::filesystem::exists(active_tmp_dir_file));
	REQUIRE_FALSE(std::filesystem::exists(operation_tmp_file));
	REQUIRE_FALSE(std::filesystem::exists(data_temp));
	REQUIRE_FALSE(std::filesystem::exists(root_temp));
	REQUIRE(std::filesystem::exists(layout.active_catalog_root / "tmp"));
	REQUIRE(std::filesystem::exists(layout.operation_tmp_root));
	REQUIRE(read_text(keep_file) == "not ours");
}
