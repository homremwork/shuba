#include "Catalog/CatalogReplacement.hpp"
#include "Persistence/CatalogStorage.hpp"
#include "Platform/LinuxFakes.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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

[[nodiscard]] std::string item_row(std::string_view id,
								   std::string_view display_name) {
	std::string text;
	text += R"({"id":")";
	text += id;
	text += R"(","schemaVersion":1,"displayName":")";
	text += display_name;
	text +=
		R"(","category":"other","tags":[],"status":"draft","createdAt":1,"updatedAt":2})";
	return text;
}

[[nodiscard]] std::string photo_row(std::string_view id,
									std::string_view owner_id) {
	std::string text;
	text += R"({"id":")";
	text += id;
	text += R"(","schemaVersion":1,"ownerType":"item","ownerId":")";
	text += owner_id;
	text +=
		R"(","mediaFormat":"jxl","sortOrder":1000,"isMain":true,"createdAt":5,"updatedAt":6})";
	return text;
}

void write_catalog_files(const std::filesystem::path& catalog_root,
						 const shuba::core::StableIdentifier& catalog_id,
						 std::string_view item_id, std::string_view item_name,
						 std::string_view photos_jsonl = {}) {
	const shuba::persistence::EmptyCatalogFixture fixture =
		shuba::persistence::make_empty_catalog_fixture(
			catalog_id, shuba::core::EpochMilliseconds{1000});
	const shuba::persistence::SchemaWriteResult manifest =
		shuba::persistence::serialize_manifest_json(fixture.manifest);
	const shuba::persistence::SchemaWriteResult settings =
		shuba::persistence::serialize_settings_json(fixture.settings);
	REQUIRE(manifest.succeeded());
	REQUIRE(settings.succeeded());

	write_text(catalog_root / "manifest.json", manifest.json);
	write_text(catalog_root / "settings.json", settings.json);
	write_text(catalog_root / "data/items.jsonl",
			   item_row(item_id, item_name) + "\n");
	write_text(catalog_root / "data/storages.jsonl", "");
	write_text(catalog_root / "data/photos.jsonl", photos_jsonl);
	std::filesystem::create_directories(catalog_root / "media/photos");
	std::filesystem::create_directories(catalog_root / "recovery/quarantine");
	std::filesystem::create_directories(catalog_root
										/ "backup/previous-data-copies");
	std::filesystem::create_directories(catalog_root / "tmp");
}

[[nodiscard]] std::size_t count_directories(const std::filesystem::path& path) {
	if (!std::filesystem::exists(path))
		return 0U;

	std::size_t count{};
	for (const std::filesystem::directory_entry& entry :
		 std::filesystem::directory_iterator{path}) {
		if (entry.is_directory())
			++count;
	}
	return count;
}

[[nodiscard]] bool has_diagnostic_code(
	const std::vector<shuba::core::Diagnostic>& diagnostics,
	std::string_view code) {
	return std::ranges::any_of(diagnostics, [code](const auto& diagnostic) {
		return diagnostic.code == code;
	});
}

struct ReplacementHarness final {
	TemporaryDirectory temporary{"shuba-b16-replacement"};
	std::filesystem::path app_private_root{temporary.path() / "app-private"};
	shuba::persistence::CatalogContainerLayout layout{
		shuba::persistence::make_catalog_container_layout(app_private_root)};
	shuba::platform::ScriptedIdentifierSource identifiers;
	shuba::core::ManualClock clock{shuba::core::EpochMilliseconds{2000}};
	shuba::core::OperationGate gate;
	shuba::platform::ProgressCollector progress;
	shuba::platform::ManualCancellationToken cancellation;

	[[nodiscard]] shuba::catalog::CatalogReplacementUseCase use_case() {
		return shuba::catalog::CatalogReplacementUseCase{identifiers, clock,
														 gate};
	}

	[[nodiscard]] std::filesystem::path staged_root(
		std::string_view name) const {
		return layout.operation_tmp_root / "test-staged" / std::string{name};
	}
};
}	 // namespace

TEST_CASE("B16 replaces active catalog after validated staged import",
		  "[b16][catalog-replacement][success]") {
	ReplacementHarness harness;
	harness.identifiers.script_operation_identifier("operation-b16-success");
	write_catalog_files(harness.layout.active_catalog_root,
						make_id("catalog-active"), "item-old", "Old item");
	const std::filesystem::path staged_root = harness.staged_root("success");
	write_catalog_files(staged_root, make_id("catalog-imported"), "item-new",
						"Imported item");
	write_text(harness.layout.catalog_rollbacks_root
				   / "p00000000000000000001-old" / "manifest.json",
			   "old rollback");
	harness.clock.set_now(shuba::core::EpochMilliseconds{3000});

	shuba::catalog::CatalogReplacementUseCase use_case = harness.use_case();
	shuba::catalog::CatalogReplacementResult result =
		use_case.replace_with_staged_import(
			shuba::catalog::CatalogReplacementRequest{
				.app_private_root		   = harness.app_private_root,
				.staged_catalog_root	   = staged_root,
				.replacement_confirmed	   = true,
				.degraded_import_confirmed = false},
			harness.progress, harness.cancellation);

	REQUIRE(result.succeeded());
	REQUIRE(result.rollback_copy_created);
	REQUIRE(result.rollback_copy_directory.has_value());
	REQUIRE(result.active_catalog_parked);
	REQUIRE(result.staged_catalog_moved);
	REQUIRE_FALSE(result.rollback_attempted);
	REQUIRE(result.active_validation.load_status
			== shuba::persistence::CatalogLoadStatus::Normal);
	REQUIRE(read_text(harness.layout.active_catalog_root / "data/items.jsonl")
				.find("Imported item")
			!= std::string::npos);
	REQUIRE_FALSE(std::filesystem::exists(staged_root));
	REQUIRE(std::filesystem::exists(*result.rollback_copy_directory
									/ "data/items.jsonl"));
	REQUIRE(read_text(*result.rollback_copy_directory / "data/items.jsonl")
				.find("Old item")
			!= std::string::npos);
	REQUIRE_FALSE(std::filesystem::exists(harness.layout.active_catalog_root
										  / "catalog-rollbacks"));
	REQUIRE_FALSE(std::filesystem::exists(harness.layout.active_catalog_root
										  / "backup/previous-catalogs"));
	REQUIRE(count_directories(harness.layout.catalog_rollbacks_root) == 1U);
	REQUIRE_FALSE(std::filesystem::exists(harness.layout.catalog_rollbacks_root
										  / "p00000000000000000001-old"));
	REQUIRE(
		std::ranges::all_of(harness.progress.events(), [](const auto& event) {
		if (event.phase.starts_with("catalog-replacement-rollback-copy"))
			return !event.cancellable;
		if (event.phase.starts_with("catalog-replacement-parking-active"))
			return !event.cancellable;
		if (event.phase.starts_with("catalog-replacement-moving-staged"))
			return !event.cancellable;
		if (event.phase.starts_with("catalog-replacement-loading"))
			return !event.cancellable;
		return true;
	}));
}

TEST_CASE("B16 rejects missing replacement confirmation before rollback copy",
		  "[b16][catalog-replacement][confirmation]") {
	ReplacementHarness harness;
	harness.identifiers.script_operation_identifier(
		"operation-b16-no-replace-confirm");
	write_catalog_files(harness.layout.active_catalog_root,
						make_id("catalog-active"), "item-old", "Old item");
	const std::filesystem::path staged_root =
		harness.staged_root("no-replace-confirm");
	write_catalog_files(staged_root, make_id("catalog-imported"), "item-new",
						"Imported item");

	shuba::catalog::CatalogReplacementUseCase use_case = harness.use_case();
	shuba::catalog::CatalogReplacementResult result =
		use_case.replace_with_staged_import(
			shuba::catalog::CatalogReplacementRequest{
				.app_private_root	   = harness.app_private_root,
				.staged_catalog_root   = staged_root,
				.replacement_confirmed = false},
			harness.progress, harness.cancellation);

	REQUIRE(result.failed());
	REQUIRE(result.status
			== shuba::catalog::CatalogReplacementStatus::Rejected);
	REQUIRE(result.category
			== shuba::core::OperationResultCategory::ValidationFailure);
	REQUIRE_FALSE(result.critical_section_entered);
	REQUIRE_FALSE(result.rollback_copy_created);
	REQUIRE_FALSE(result.active_catalog_parked);
	REQUIRE_FALSE(result.staged_catalog_moved);
	REQUIRE(read_text(harness.layout.active_catalog_root / "data/items.jsonl")
				.find("Old item")
			!= std::string::npos);
	REQUIRE(std::filesystem::exists(staged_root));
	REQUIRE(count_directories(harness.layout.catalog_rollbacks_root) == 0U);
}
TEST_CASE("B16 rejects fatal staged import before changing active catalog",
		  "[b16][catalog-replacement][validation]") {
	ReplacementHarness harness;
	harness.identifiers.script_operation_identifier(
		"operation-b16-fatal-stage");
	write_catalog_files(harness.layout.active_catalog_root,
						make_id("catalog-active"), "item-old", "Old item");
	const std::filesystem::path staged_root = harness.staged_root("fatal");
	write_catalog_files(staged_root, make_id("catalog-imported"), "item-new",
						"Imported item");
	write_text(staged_root / "manifest.json", "{not-valid-json");

	shuba::catalog::CatalogReplacementUseCase use_case = harness.use_case();
	shuba::catalog::CatalogReplacementResult result =
		use_case.replace_with_staged_import(
			shuba::catalog::CatalogReplacementRequest{
				.app_private_root	   = harness.app_private_root,
				.staged_catalog_root   = staged_root,
				.replacement_confirmed = true},
			harness.progress, harness.cancellation);

	REQUIRE(result.failed());
	REQUIRE(result.status
			== shuba::catalog::CatalogReplacementStatus::Rejected);
	REQUIRE(result.category
			== shuba::core::OperationResultCategory::ValidationFailure);
	REQUIRE_FALSE(result.critical_section_entered);
	REQUIRE_FALSE(result.rollback_copy_created);
	REQUIRE_FALSE(result.active_catalog_parked);
	REQUIRE_FALSE(result.staged_catalog_moved);
	REQUIRE(read_text(harness.layout.active_catalog_root / "data/items.jsonl")
				.find("Old item")
			!= std::string::npos);
	REQUIRE(std::filesystem::exists(staged_root));
	REQUIRE(count_directories(harness.layout.catalog_rollbacks_root) == 0U);
}

TEST_CASE("B16 degraded staged import requires explicit degraded confirmation",
		  "[b16][catalog-replacement][confirmation]") {
	ReplacementHarness harness;
	harness.identifiers.script_operation_identifier(
		"operation-b16-degraded-confirm");
	write_catalog_files(harness.layout.active_catalog_root,
						make_id("catalog-active"), "item-old", "Old item");
	const std::string imported_photos =
		photo_row("photo-missing", "item-new") + "\n";
	const std::filesystem::path staged_root = harness.staged_root("degraded");
	write_catalog_files(staged_root, make_id("catalog-imported"), "item-new",
						"Imported item", imported_photos);

	shuba::catalog::CatalogReplacementUseCase use_case = harness.use_case();
	shuba::catalog::CatalogReplacementResult result =
		use_case.replace_with_staged_import(
			shuba::catalog::CatalogReplacementRequest{
				.app_private_root		   = harness.app_private_root,
				.staged_catalog_root	   = staged_root,
				.replacement_confirmed	   = true,
				.degraded_import_confirmed = false},
			harness.progress, harness.cancellation);

	REQUIRE(result.failed());
	REQUIRE(result.status
			== shuba::catalog::CatalogReplacementStatus::Rejected);
	REQUIRE(result.category
			== shuba::core::OperationResultCategory::ValidationFailure);
	REQUIRE(result.staged_validation.load_status
			== shuba::persistence::CatalogLoadStatus::Degraded);
	REQUIRE(result.staged_validation.explicit_warning_required());
	REQUIRE_FALSE(result.critical_section_entered);
	REQUIRE_FALSE(result.rollback_copy_created);
	REQUIRE_FALSE(result.active_catalog_parked);
	REQUIRE_FALSE(result.staged_catalog_moved);
	REQUIRE(read_text(harness.layout.active_catalog_root / "data/items.jsonl")
				.find("Old item")
			!= std::string::npos);
	REQUIRE(std::filesystem::exists(staged_root));
	REQUIRE(count_directories(harness.layout.catalog_rollbacks_root) == 0U);
}

TEST_CASE("B16 confirmed degraded staged import can replace active catalog",
		  "[b16][catalog-replacement][degraded]") {
	ReplacementHarness harness;
	harness.identifiers.script_operation_identifier(
		"operation-b16-degraded-replace");
	write_catalog_files(harness.layout.active_catalog_root,
						make_id("catalog-active"), "item-old", "Old item");
	const std::string imported_photos =
		photo_row("photo-missing", "item-new") + "\n";
	const std::filesystem::path staged_root =
		harness.staged_root("degraded-replace");
	write_catalog_files(staged_root, make_id("catalog-imported"), "item-new",
						"Imported item", imported_photos);

	shuba::catalog::CatalogReplacementUseCase use_case = harness.use_case();
	shuba::catalog::CatalogReplacementResult result =
		use_case.replace_with_staged_import(
			shuba::catalog::CatalogReplacementRequest{
				.app_private_root		   = harness.app_private_root,
				.staged_catalog_root	   = staged_root,
				.replacement_confirmed	   = true,
				.degraded_import_confirmed = true},
			harness.progress, harness.cancellation);

	REQUIRE(result.succeeded());
	REQUIRE(result.status
			== shuba::catalog::CatalogReplacementStatus::Replaced);
	REQUIRE(result.rollback_copy_created);
	REQUIRE(result.staged_catalog_moved);
	REQUIRE_FALSE(result.rollback_attempted);
	REQUIRE(result.staged_validation.load_status
			== shuba::persistence::CatalogLoadStatus::Degraded);
	REQUIRE(result.active_validation.load_status
			== shuba::persistence::CatalogLoadStatus::Degraded);
	REQUIRE(read_text(harness.layout.active_catalog_root / "data/items.jsonl")
				.find("Imported item")
			!= std::string::npos);
	REQUIRE(read_text(harness.layout.active_catalog_root / "data/photos.jsonl")
				.find("photo-missing")
			!= std::string::npos);
	REQUIRE_FALSE(std::filesystem::exists(staged_root));
}

TEST_CASE("B16 simulated replacement move failure restores rollback copy",
		  "[b16][catalog-replacement][rollback]") {
	ReplacementHarness harness;
	harness.identifiers.script_operation_identifier("operation-b16-rollback");
	write_catalog_files(harness.layout.active_catalog_root,
						make_id("catalog-active"), "item-old", "Old item");
	const std::filesystem::path staged_root = harness.staged_root("rollback");
	write_catalog_files(staged_root, make_id("catalog-imported"), "item-new",
						"Imported item");

	shuba::catalog::CatalogReplacementUseCase use_case = harness.use_case();
	shuba::catalog::CatalogReplacementResult result =
		use_case.replace_with_staged_import(
			shuba::catalog::CatalogReplacementRequest{
				.app_private_root	   = harness.app_private_root,
				.staged_catalog_root   = staged_root,
				.replacement_confirmed = true,
				.fault_mode = shuba::catalog::CatalogReplacementFaultMode::
					FailAfterActiveCatalogParked},
			harness.progress, harness.cancellation);

	REQUIRE(result.failed());
	REQUIRE(result.status
			== shuba::catalog::CatalogReplacementStatus::RolledBack);
	REQUIRE(result.rollback_copy_created);
	REQUIRE(result.active_catalog_parked);
	REQUIRE_FALSE(result.staged_catalog_moved);
	REQUIRE(result.rollback_attempted);
	REQUIRE(result.rollback_succeeded);
	REQUIRE(read_text(harness.layout.active_catalog_root / "data/items.jsonl")
				.find("Old item")
			!= std::string::npos);
	REQUIRE_FALSE(
		std::filesystem::exists(result.parked_catalog_directory.value()));
	REQUIRE(std::filesystem::exists(staged_root));
	REQUIRE(result.rollback_copy_directory.has_value());
	REQUIRE(std::filesystem::exists(*result.rollback_copy_directory));
}

TEST_CASE("B16 fatal imported active load restores previous catalog",
		  "[b16][catalog-replacement][rollback]") {
	ReplacementHarness harness;
	harness.identifiers.script_operation_identifier(
		"operation-b16-imported-load-rollback");
	write_catalog_files(harness.layout.active_catalog_root,
						make_id("catalog-active"), "item-old", "Old item");
	const std::filesystem::path staged_root =
		harness.staged_root("imported-load-rollback");
	write_catalog_files(staged_root, make_id("catalog-imported"), "item-new",
						"Imported item");

	shuba::catalog::CatalogReplacementUseCase use_case = harness.use_case();
	shuba::catalog::CatalogReplacementResult result =
		use_case.replace_with_staged_import(
			shuba::catalog::CatalogReplacementRequest{
				.app_private_root	   = harness.app_private_root,
				.staged_catalog_root   = staged_root,
				.replacement_confirmed = true,
				.fault_mode = shuba::catalog::CatalogReplacementFaultMode::
					ForceImportedLoadFatal},
			harness.progress, harness.cancellation);

	REQUIRE(result.failed());
	REQUIRE(result.status
			== shuba::catalog::CatalogReplacementStatus::RolledBack);
	REQUIRE(result.rollback_copy_created);
	REQUIRE(result.active_catalog_parked);
	REQUIRE(result.staged_catalog_moved);
	REQUIRE(result.rollback_attempted);
	REQUIRE(result.rollback_succeeded);
	REQUIRE(read_text(harness.layout.active_catalog_root / "data/items.jsonl")
				.find("Old item")
			!= std::string::npos);
	REQUIRE(read_text(harness.layout.active_catalog_root / "data/items.jsonl")
				.find("Imported item")
			== std::string::npos);
	REQUIRE_FALSE(std::filesystem::exists(staged_root));
	REQUIRE(result.rollback_copy_directory.has_value());
	REQUIRE(std::filesystem::exists(*result.rollback_copy_directory));
}

TEST_CASE("B16 rollback restore failure enters fatal recovery result",
		  "[b16][catalog-replacement][fatal-recovery]") {
	ReplacementHarness harness;
	harness.identifiers.script_operation_identifier(
		"operation-b16-fatal-recovery");
	write_catalog_files(harness.layout.active_catalog_root,
						make_id("catalog-active"), "item-old", "Old item");
	const std::filesystem::path staged_root =
		harness.staged_root("fatal-recovery");
	write_catalog_files(staged_root, make_id("catalog-imported"), "item-new",
						"Imported item");

	shuba::catalog::CatalogReplacementUseCase use_case = harness.use_case();
	shuba::catalog::CatalogReplacementResult result =
		use_case.replace_with_staged_import(
			shuba::catalog::CatalogReplacementRequest{
				.app_private_root	   = harness.app_private_root,
				.staged_catalog_root   = staged_root,
				.replacement_confirmed = true,
				.fault_mode = shuba::catalog::CatalogReplacementFaultMode::
					ForceImportedLoadFatalAndFailRollbackRestore},
			harness.progress, harness.cancellation);

	REQUIRE(result.failed());
	REQUIRE(result.fatal_recovery_required());
	REQUIRE(result.status
			== shuba::catalog::CatalogReplacementStatus::FatalRecoveryRequired);
	REQUIRE(result.rollback_copy_created);
	REQUIRE(result.staged_catalog_moved);
	REQUIRE(result.rollback_attempted);
	REQUIRE_FALSE(result.rollback_succeeded);
	REQUIRE(read_text(harness.layout.active_catalog_root / "data/items.jsonl")
				.find("Imported item")
			!= std::string::npos);
	REQUIRE(result.rollback_copy_directory.has_value());
	REQUIRE(std::filesystem::exists(*result.rollback_copy_directory));
}

TEST_CASE("B16 operation gate serializes replacement against other writes",
		  "[b16][catalog-replacement][operation-gate]") {
	ReplacementHarness harness;
	harness.identifiers.script_operation_identifier("operation-b16-busy");
	std::optional<shuba::core::OperationGate::Lease> lease =
		harness.gate.try_acquire(shuba::core::OperationKind::MetadataWrite,
								 make_operation_id("operation-existing-write"));
	REQUIRE(lease.has_value());
	write_catalog_files(harness.layout.active_catalog_root,
						make_id("catalog-active"), "item-old", "Old item");
	const std::filesystem::path staged_root = harness.staged_root("busy");
	write_catalog_files(staged_root, make_id("catalog-imported"), "item-new",
						"Imported item");

	shuba::catalog::CatalogReplacementUseCase use_case = harness.use_case();
	shuba::catalog::CatalogReplacementResult result =
		use_case.replace_with_staged_import(
			shuba::catalog::CatalogReplacementRequest{
				.app_private_root	   = harness.app_private_root,
				.staged_catalog_root   = staged_root,
				.replacement_confirmed = true},
			harness.progress, harness.cancellation);

	REQUIRE(result.failed());
	REQUIRE(result.category
			== shuba::core::OperationResultCategory::ValidationFailure);
	REQUIRE_FALSE(result.critical_section_entered);
	REQUIRE_FALSE(result.rollback_copy_created);
	REQUIRE(read_text(harness.layout.active_catalog_root / "data/items.jsonl")
				.find("Old item")
			!= std::string::npos);
}

TEST_CASE(
	"B16 cancellation before critical replacement leaves catalog unchanged",
	"[b16][catalog-replacement][cancellation]") {
	ReplacementHarness harness;
	harness.identifiers.script_operation_identifier("operation-b16-cancelled");
	write_catalog_files(harness.layout.active_catalog_root,
						make_id("catalog-active"), "item-old", "Old item");
	const std::filesystem::path staged_root = harness.staged_root("cancelled");
	write_catalog_files(staged_root, make_id("catalog-imported"), "item-new",
						"Imported item");
	harness.cancellation.request_cancellation();

	shuba::catalog::CatalogReplacementUseCase use_case = harness.use_case();
	shuba::catalog::CatalogReplacementResult result =
		use_case.replace_with_staged_import(
			shuba::catalog::CatalogReplacementRequest{
				.app_private_root	   = harness.app_private_root,
				.staged_catalog_root   = staged_root,
				.replacement_confirmed = true},
			harness.progress, harness.cancellation);

	REQUIRE_FALSE(result.succeeded());
	REQUIRE(result.status
			== shuba::catalog::CatalogReplacementStatus::Cancelled);
	REQUIRE(result.category
			== shuba::core::OperationResultCategory::UserCancelled);
	REQUIRE_FALSE(result.critical_section_entered);
	REQUIRE_FALSE(result.rollback_copy_created);
	REQUIRE_FALSE(result.active_catalog_parked);
	REQUIRE_FALSE(result.staged_catalog_moved);
	REQUIRE_FALSE(result.rollback_attempted);
	REQUIRE(read_text(harness.layout.active_catalog_root / "data/items.jsonl")
				.find("Old item")
			!= std::string::npos);
	REQUIRE(std::filesystem::exists(staged_root));
	REQUIRE(count_directories(harness.layout.catalog_rollbacks_root) == 0U);
}

TEST_CASE(
	"B16 cancellation during critical replacement is ignored until finish",
	"[b16][catalog-replacement][cancellation]") {
	ReplacementHarness harness;
	harness.identifiers.script_operation_identifier("operation-b16-critical");
	write_catalog_files(harness.layout.active_catalog_root,
						make_id("catalog-active"), "item-old", "Old item");
	const std::filesystem::path staged_root = harness.staged_root("critical");
	write_catalog_files(staged_root, make_id("catalog-imported"), "item-new",
						"Imported item");

	class CancellingProgressSink final : public shuba::platform::ProgressSink {
	public:
		explicit CancellingProgressSink(
			shuba::platform::ManualCancellationToken& cancellation_token)
			: cancellation{cancellation_token} {}

		void publish_progress(shuba::platform::ProgressEvent event) override {
			if (event.phase.starts_with("catalog-replacement-rollback-copy"))
				cancellation.request_cancellation();
			events.push_back(std::move(event));
		}

		std::vector<shuba::platform::ProgressEvent> events;

	private:
		shuba::platform::ManualCancellationToken& cancellation;
	};
	CancellingProgressSink progress{harness.cancellation};

	shuba::catalog::CatalogReplacementUseCase use_case = harness.use_case();
	shuba::catalog::CatalogReplacementResult result =
		use_case.replace_with_staged_import(
			shuba::catalog::CatalogReplacementRequest{
				.app_private_root	   = harness.app_private_root,
				.staged_catalog_root   = staged_root,
				.replacement_confirmed = true},
			progress, harness.cancellation);

	REQUIRE(result.succeeded());
	REQUIRE(result.critical_section_entered);
	REQUIRE(result.rollback_copy_created);
	REQUIRE(result.staged_catalog_moved);
	REQUIRE_FALSE(result.rollback_attempted);
	REQUIRE(harness.cancellation.cancellation_requested());
	REQUIRE(read_text(harness.layout.active_catalog_root / "data/items.jsonl")
				.find("Imported item")
			!= std::string::npos);
	REQUIRE_FALSE(std::filesystem::exists(staged_root));
	REQUIRE(std::ranges::any_of(progress.events, [](const auto& event) {
		return event.phase.starts_with("catalog-replacement-rollback-copy")
			   && !event.cancellable;
	}));
}

TEST_CASE("B16 rollback retention zero removes successful rollback copy",
		  "[b16][catalog-replacement][retention]") {
	ReplacementHarness harness;
	harness.identifiers.script_operation_identifier(
		"operation-b16-retention-zero");
	write_catalog_files(harness.layout.active_catalog_root,
						make_id("catalog-active"), "item-old", "Old item");
	const std::filesystem::path staged_root =
		harness.staged_root("retention-zero");
	write_catalog_files(staged_root, make_id("catalog-imported"), "item-new",
						"Imported item");

	shuba::catalog::CatalogReplacementUseCase use_case = harness.use_case();
	shuba::catalog::CatalogReplacementResult result =
		use_case.replace_with_staged_import(
			shuba::catalog::CatalogReplacementRequest{
				.app_private_root	   = harness.app_private_root,
				.staged_catalog_root   = staged_root,
				.replacement_confirmed = true,
				.rollback_retention	   = 0U},
			harness.progress, harness.cancellation);

	REQUIRE(result.succeeded());
	REQUIRE(result.rollback_copy_created);
	REQUIRE(result.rollback_copy_directory.has_value());
	REQUIRE(result.rollback_retention_cleanup_attempted);
	REQUIRE_FALSE(std::filesystem::exists(*result.rollback_copy_directory));
	REQUIRE(count_directories(harness.layout.catalog_rollbacks_root) == 0U);
	REQUIRE(read_text(harness.layout.active_catalog_root / "data/items.jsonl")
				.find("Imported item")
			!= std::string::npos);
}

TEST_CASE("B16 skips rollback retention cleanup when rollback is used",
		  "[b16][catalog-replacement][retention]") {
	ReplacementHarness harness;
	harness.identifiers.script_operation_identifier(
		"operation-b16-retention-after-rollback");
	write_catalog_files(harness.layout.active_catalog_root,
						make_id("catalog-active"), "item-old", "Old item");
	const std::filesystem::path staged_root =
		harness.staged_root("retention-after-rollback");
	write_catalog_files(staged_root, make_id("catalog-imported"), "item-new",
						"Imported item");
	write_text(harness.layout.catalog_rollbacks_root
				   / "p00000000000000000001-old" / "manifest.json",
			   "old rollback");

	shuba::catalog::CatalogReplacementUseCase use_case = harness.use_case();
	shuba::catalog::CatalogReplacementResult result =
		use_case.replace_with_staged_import(
			shuba::catalog::CatalogReplacementRequest{
				.app_private_root	   = harness.app_private_root,
				.staged_catalog_root   = staged_root,
				.replacement_confirmed = true,
				.rollback_retention	   = 1U,
				.fault_mode = shuba::catalog::CatalogReplacementFaultMode::
					FailAfterActiveCatalogParked},
			harness.progress, harness.cancellation);

	REQUIRE(result.failed());
	REQUIRE(result.status
			== shuba::catalog::CatalogReplacementStatus::RolledBack);
	REQUIRE(result.rollback_succeeded);
	REQUIRE_FALSE(result.rollback_retention_cleanup_attempted);
	REQUIRE(std::filesystem::exists(*result.rollback_copy_directory));
	REQUIRE(std::filesystem::exists(harness.layout.catalog_rollbacks_root
									/ "p00000000000000000001-old"));
	REQUIRE(count_directories(harness.layout.catalog_rollbacks_root) == 2U);
}
