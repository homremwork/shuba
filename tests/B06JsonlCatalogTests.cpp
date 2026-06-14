#include "Persistence/JsonlCatalog.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
[[nodiscard]] shuba::core::StableIdentifier make_id(std::string text) {
	auto identifier =
		shuba::core::StableIdentifier::try_create_file_safe(std::move(text));
	REQUIRE(identifier.has_value());
	return *identifier;
}

[[nodiscard]] bool contains(std::string_view text, std::string_view fragment) {
	return text.find(fragment) != std::string_view::npos;
}

[[nodiscard]] bool has_code(
	const std::vector<shuba::persistence::JsonlDiagnostic>& diagnostics,
	std::string_view code) {
	return std::ranges::any_of(diagnostics, [code](const auto& diagnostic) {
		return diagnostic.code == code;
	});
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

[[nodiscard]] std::string storage_row(std::string_view id,
									  std::string_view display_name) {
	auto text = std::string{};
	text += R"({"id":")";
	text += id;
	text += R"(","schemaVersion":1,"displayName":")";
	text += display_name;
	text +=
		R"(","storageType":"box","tags":[],"lifecycleStatus":"active","createdAt":3,"updatedAt":4})";
	return text;
}
}	 // namespace

TEST_CASE("B06 JSONL loader skips corrupted and duplicate item lines",
		  "[b06][jsonl][loader]") {
	using namespace shuba::persistence;

	const auto input = item_row("item-a", "First accepted") + "\n"
					   + "{bad json\n" + item_row("item-a", "Duplicate skipped")
					   + "\n" + "   \n" + item_row("item-b", "Later accepted")
					   + "\n";

	const auto loaded = load_item_jsonl(input);

	REQUIRE(loaded.records.size() == 2);
	REQUIRE(loaded.records[0].record.id.value() == "item-a");
	REQUIRE(loaded.records[0].record.display_name == "First accepted");
	REQUIRE(loaded.records[1].record.id.value() == "item-b");

	REQUIRE(loaded.quarantine_entries.size() == 2);
	REQUIRE(loaded.quarantine_entries[0].code == "json_parse_failed");
	REQUIRE(loaded.quarantine_entries[0].line == 2);
	REQUIRE(loaded.quarantine_entries[0].raw == "{bad json");
	REQUIRE(loaded.quarantine_entries[1].code == "duplicate_id");
	REQUIRE(loaded.quarantine_entries[1].line == 3);
	REQUIRE(contains(loaded.quarantine_entries[1].details, "item-a"));

	REQUIRE(loaded.summary.path == "data/items.jsonl");
	REQUIRE(loaded.summary.accepted_records == 2);
	REQUIRE(loaded.summary.rejected_lines == 2);
	REQUIRE(loaded.summary.warnings == 1);
	REQUIRE(has_code(loaded.diagnostics, "blank_line"));
}

TEST_CASE("B06 JSONL loader accepts CRLF and final records without newline",
		  "[b06][jsonl][loader]") {
	using namespace shuba::persistence;

	const std::string input = item_row("item-crlf", "CRLF accepted") + "\r\n"
							  + "   \r\n"
							  + item_row("item-final", "No trailing newline");

	const ItemTableLoadResult loaded = load_item_jsonl(input);

	REQUIRE(loaded.records.size() == 2);
	REQUIRE(loaded.records[0].record.id.value() == "item-crlf");
	REQUIRE(loaded.records[1].record.id.value() == "item-final");
	REQUIRE(loaded.quarantine_entries.empty());
	REQUIRE(loaded.summary.accepted_records == 2);
	REQUIRE(loaded.summary.rejected_lines == 0);
	REQUIRE(loaded.summary.warnings == 1);
	REQUIRE(has_code(loaded.diagnostics, "blank_line"));
}

TEST_CASE(
	"B06 deterministic JSONL writer sorts records and keeps unknown fields",
	"[b06][jsonl][writer]") {
	using namespace shuba::persistence;

	const auto input =
		item_row("item-z", "Last") + "\n"
		+ R"({"id":"item-a","schemaVersion":1,"displayName":"First","category":"other","tags":[],"status":"draft","createdAt":1,"updatedAt":2,"futureField":{"nested":[1,2]}})"
		+ "\n";
	const auto loaded = load_item_jsonl(input);
	REQUIRE(loaded.records.size() == 2);

	const auto written = write_item_jsonl(loaded.records);
	REQUIRE(written.succeeded());
	REQUIRE(written.text.ends_with('\n'));
	REQUIRE(written.text.find(R"("id":"item-a")")
			< written.text.find(R"("id":"item-z")"));
	REQUIRE(contains(written.text, R"("futureField":{"nested":[1,2]})"));

	const auto reparsed = load_item_jsonl(written.text);
	REQUIRE(reparsed.records.size() == 2);
	REQUIRE(reparsed.records.front().unknown_fields.at("futureField")
			== R"({"nested":[1,2]})");
}

TEST_CASE(
	"B06 loader quarantines future versions malformed optional data and "
	"malformed tags",
	"[b06][jsonl][quarantine]") {
	using namespace shuba::persistence;

	const auto input =
		std::string{
			R"({"id":"item-future","schemaVersion":2,"displayName":"Future","category":"other","tags":[],"status":"draft","createdAt":1,"updatedAt":2})"}
		+ "\n"
		+ R"({"id":"item-bad-money","schemaVersion":1,"displayName":"Bad money","category":"other","tags":[],"status":"draft","finance":{"originalPrice":{"amount":"12,34","currency":"BYN"}},"createdAt":1,"updatedAt":2})"
		+ "\n"
		+ R"({"id":"item-bad-tag","schemaVersion":1,"displayName":"Bad tag","category":"other","tags":[{"value":"missing key"}],"status":"draft","createdAt":1,"updatedAt":2})"
		+ "\n" + item_row("item-valid", "Valid") + "\n";

	const auto loaded = load_item_jsonl(input);

	REQUIRE(loaded.records.size() == 1);
	REQUIRE(loaded.records.front().record.id.value() == "item-valid");
	REQUIRE(loaded.quarantine_entries.size() == 3);
	REQUIRE(loaded.quarantine_entries[0].code == "unsupported_record_version");
	REQUIRE(loaded.quarantine_entries[1].code == "invalid_optional_field");
	REQUIRE(loaded.quarantine_entries[2].code == "invalid_tag");
	REQUIRE(contains(loaded.quarantine_entries[1].raw, "12,34"));
	REQUIRE(loaded.summary.accepted_records == 1);
	REQUIRE(loaded.summary.rejected_lines == 3);
}

TEST_CASE("B06 existing blank tag keys are accepted with warnings",
		  "[b06][jsonl][tags]") {
	using namespace shuba::persistence;

	const auto input =
		std::string{
			R"({"id":"storage-blank-tag","schemaVersion":1,"displayName":"Legacy box","storageType":"box","tags":[{"key":"","value":"legacy"}],"lifecycleStatus":"active","createdAt":3,"updatedAt":4})"}
		+ "\n"
		+ R"({"id":"storage-malformed-tag","schemaVersion":1,"displayName":"Bad tag","storageType":"box","tags":[{"value":"missing key"}],"lifecycleStatus":"active","createdAt":3,"updatedAt":4})"
		+ "\n";

	const auto loaded = load_storage_jsonl(input);

	REQUIRE(loaded.records.size() == 1);
	REQUIRE(loaded.records.front().record.tags.size() == 1);
	REQUIRE(loaded.records.front().record.tags.front().key.empty());
	REQUIRE(loaded.summary.warnings == 1);
	REQUIRE(has_code(loaded.diagnostics, "blank_tag_key"));
	REQUIRE(loaded.quarantine_entries.size() == 1);
	REQUIRE(loaded.quarantine_entries.front().code == "invalid_tag");
}

TEST_CASE(
	"B06 recovery report and quarantine writers summarize accepted and skipped "
	"records",
	"[b06][jsonl][recovery]") {
	using namespace shuba::persistence;

	const auto loaded = load_catalog_jsonl(CatalogJsonlDocuments{
		.items_jsonl = item_row("item-report", "Report item") + "\n{bad json\n",
		.storages_jsonl =
			storage_row("storage-report", "Report storage") + "\n",
		.photos_jsonl =
			R"({"id":"photo-future","schemaVersion":2,"ownerType":"item","ownerId":"item-report","mediaFormat":"jxl","sortOrder":1000,"createdAt":5,"updatedAt":6})"
			+ std::string{"\n"}});

	REQUIRE(loaded.load_status == CatalogLoadStatus::Degraded);
	REQUIRE(loaded.items.records.size() == 1);
	REQUIRE(loaded.photos.quarantine_entries.size() == 1);

	const auto report = make_recovery_report(
		loaded, shuba::core::EpochMilliseconds{1760000300000},
		make_id("catalog-001"));
	REQUIRE(report.items_accepted == 1);
	REQUIRE(report.storages_accepted == 1);
	REQUIRE(report.photos_accepted == 0);
	REQUIRE(report.error_count == 2);

	const auto report_json = write_recovery_report_json(report);
	REQUIRE(report_json.succeeded());
	REQUIRE(contains(report_json.text, R"("loadStatus":"degraded")"));
	REQUIRE(contains(report_json.text, R"("itemsAccepted":1)"));
	REQUIRE(contains(report_json.text, R"("photosAccepted":0)"));
	REQUIRE(contains(report_json.text, R"("errorCount":2)"));
	REQUIRE(contains(report_json.text, R"("catalogId":"catalog-001")"));

	const auto quarantine_jsonl =
		write_quarantine_jsonl(loaded.items.quarantine_entries);
	REQUIRE(quarantine_jsonl.succeeded());
	REQUIRE(contains(quarantine_jsonl.text, R"("code":"json_parse_failed")"));
	REQUIRE(contains(quarantine_jsonl.text, R"("raw":"{bad json")"));

	const auto empty_quarantine = write_quarantine_jsonl({});
	REQUIRE(empty_quarantine.succeeded());
	REQUIRE(empty_quarantine.text.empty());
}
