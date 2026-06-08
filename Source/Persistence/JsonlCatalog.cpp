#include "Persistence/JsonlCatalog.hpp"

#include <glaze/glaze.hpp>

#include <algorithm>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace shuba::persistence::jsonl_detail {
struct DiagnosticDto final {
	std::string severity;
	std::string area;
	std::string code;
	std::string path;
	std::optional<std::uint64_t> line;
	std::string message;
	std::optional<std::string> details;
};

struct QuarantineEntryDto final {
	std::string source;
	std::uint64_t line{};
	std::string code;
	std::string message;
	std::optional<std::string> details;
	std::string raw;
};

struct FileSummaryDto final {
	std::string path;
	std::uint64_t accepted_records{};
	std::uint64_t rejected_lines{};
	std::uint64_t warnings{};
};

struct RecoverySummaryDto final {
	std::uint64_t items_accepted{};
	std::uint64_t storages_accepted{};
	std::uint64_t photos_accepted{};
	std::uint64_t fatal_count{};
	std::uint64_t error_count{};
	std::uint64_t warning_count{};
};

struct RecoveryReportDto final {
	int schema_version{};
	std::int64_t created_at{};
	std::optional<std::string> catalog_id;
	std::string load_status;
	RecoverySummaryDto summary;
	std::vector<FileSummaryDto> files;
	std::vector<DiagnosticDto> diagnostics;
};
}	 // namespace shuba::persistence::jsonl_detail

template<>
struct glz::meta<shuba::persistence::jsonl_detail::DiagnosticDto> {
	using T = shuba::persistence::jsonl_detail::DiagnosticDto;
	static constexpr auto value =
		object("severity", &T::severity, "area", &T::area, "code", &T::code,
			   "path", &T::path, "line", &T::line, "message", &T::message,
			   "details", &T::details);
};

template<>
struct glz::meta<shuba::persistence::jsonl_detail::QuarantineEntryDto> {
	using T = shuba::persistence::jsonl_detail::QuarantineEntryDto;
	static constexpr auto value =
		object("source", &T::source, "line", &T::line, "code", &T::code,
			   "message", &T::message, "details", &T::details, "raw", &T::raw);
};

template<>
struct glz::meta<shuba::persistence::jsonl_detail::FileSummaryDto> {
	using T = shuba::persistence::jsonl_detail::FileSummaryDto;
	static constexpr auto value =
		object("path", &T::path, "acceptedRecords", &T::accepted_records,
			   "rejectedLines", &T::rejected_lines, "warnings", &T::warnings);
};

template<>
struct glz::meta<shuba::persistence::jsonl_detail::RecoverySummaryDto> {
	using T = shuba::persistence::jsonl_detail::RecoverySummaryDto;
	static constexpr auto value =
		object("itemsAccepted", &T::items_accepted, "storagesAccepted",
			   &T::storages_accepted, "photosAccepted", &T::photos_accepted,
			   "fatalCount", &T::fatal_count, "errorCount", &T::error_count,
			   "warningCount", &T::warning_count);
};

template<>
struct glz::meta<shuba::persistence::jsonl_detail::RecoveryReportDto> {
	using T = shuba::persistence::jsonl_detail::RecoveryReportDto;
	static constexpr auto value = object(
		"schemaVersion", &T::schema_version, "createdAt", &T::created_at,
		"catalogId", &T::catalog_id, "loadStatus", &T::load_status, "summary",
		&T::summary, "files", &T::files, "diagnostics", &T::diagnostics);
};

namespace shuba::persistence {
namespace {
using namespace jsonl_detail;

[[nodiscard]] std::string table_path(EntityTableKind kind) {
	switch (kind) {
		case EntityTableKind::Items:
			return std::string{items_data_file_path};
		case EntityTableKind::Storages:
			return std::string{storages_data_file_path};
		case EntityTableKind::Photos:
			return std::string{photos_data_file_path};
	}

	return {};
}

[[nodiscard]] std::string singular_table_name(EntityTableKind kind) {
	switch (kind) {
		case EntityTableKind::Items:
			return "Item";
		case EntityTableKind::Storages:
			return "Storage";
		case EntityTableKind::Photos:
			return "Photo";
	}

	return "Entity";
}

[[nodiscard]] bool is_blank_line(std::string_view line) noexcept {
	return !domain::has_non_whitespace(line);
}

[[nodiscard]] std::optional<std::string> optional_details(
	const std::string& details) {
	if (details.empty())
		return std::nullopt;

	return details;
}

[[nodiscard]] DiagnosticDto diagnostic_dto(const JsonlDiagnostic& diagnostic) {
	return {.severity = std::string{to_string(diagnostic.severity)},
			.area	  = diagnostic.area,
			.code	  = diagnostic.code,
			.path	  = diagnostic.path,
			.line	  = diagnostic.line,
			.message  = diagnostic.message,
			.details  = optional_details(diagnostic.details)};
}

[[nodiscard]] QuarantineEntryDto quarantine_dto(const QuarantineEntry& entry) {
	return {.source	 = entry.source,
			.line	 = entry.line,
			.code	 = entry.code,
			.message = entry.message,
			.details = optional_details(entry.details),
			.raw	 = entry.raw};
}

[[nodiscard]] FileSummaryDto file_summary_dto(const JsonlFileSummary& summary) {
	return {.path			  = summary.path,
			.accepted_records = summary.accepted_records,
			.rejected_lines	  = summary.rejected_lines,
			.warnings		  = summary.warnings};
}

[[nodiscard]] JsonlDiagnostic make_diagnostic(EntityTableKind kind,
											  LoadDiagnosticSeverity severity,
											  std::string code,
											  std::optional<std::uint64_t> line,
											  std::string message,
											  std::string details = {}) {
	return {.severity = severity,
			.area	  = std::string{to_string(kind)},
			.code	  = std::move(code),
			.path	  = table_path(kind),
			.line	  = line,
			.message  = std::move(message),
			.details  = std::move(details)};
}

[[nodiscard]] JsonlDiagnostic make_text_write_diagnostic(
	std::string area, std::string path, std::string message,
	std::string details = {}) {
	return {.severity = LoadDiagnosticSeverity::Error,
			.area	  = std::move(area),
			.code	  = "serialization_failed",
			.path	  = std::move(path),
			.line	  = std::nullopt,
			.message  = std::move(message),
			.details  = std::move(details)};
}

template<class Result>
void append_diagnostic(Result& result, JsonlDiagnostic diagnostic) {
	if (diagnostic.severity == LoadDiagnosticSeverity::Warning)
		++result.summary.warnings;

	result.diagnostics.push_back(std::move(diagnostic));
}

[[nodiscard]] std::string schema_details(const SchemaDiagnostic& diagnostic) {
	auto details = std::string{"field="} + diagnostic.field
				   + "; issue=" + std::string{to_string(diagnostic.issue)};
	if (!diagnostic.technical_details.empty())
		details += "; " + diagnostic.technical_details;
	return details;
}

[[nodiscard]] std::string code_for_schema_diagnostic(
	const SchemaDiagnostic& diagnostic) {
	if (diagnostic.issue == SchemaIssue::JsonParseError)
		return "json_parse_failed";
	if (diagnostic.issue == SchemaIssue::UnsupportedSchemaVersion)
		return "unsupported_record_version";
	if (diagnostic.field == "id"
		&& (diagnostic.issue == SchemaIssue::MissingRequiredField
			|| diagnostic.issue == SchemaIssue::InvalidIdentifier)) {
		return "record_missing_id";
	}
	if (diagnostic.field.starts_with("tags["))
		return "invalid_tag";
	if (diagnostic.issue == SchemaIssue::MalformedKnownField)
		return "invalid_optional_field";

	return "record_invalid";
}

[[nodiscard]] std::string message_for_schema_diagnostic(
	EntityTableKind kind, const SchemaDiagnostic& diagnostic) {
	const auto entity = singular_table_name(kind);
	switch (diagnostic.issue) {
		case SchemaIssue::JsonParseError:
			return entity + " record could not be parsed and was skipped.";
		case SchemaIssue::UnsupportedSchemaVersion:
			return entity
                + " record uses an unsupported schema version and was skipped.";
		case SchemaIssue::InvalidIdentifier:
		case SchemaIssue::MissingRequiredField:
			if (diagnostic.field == "id")
				return entity
					   + " record has no valid identifier and was skipped.";
			return entity
                + " record is missing required first-version data and was skipped.";
		case SchemaIssue::MalformedKnownField:
			return entity
                + " record contains malformed known data and was quarantined to avoid data loss.";
		case SchemaIssue::InvalidEnumValue:
		case SchemaIssue::InvalidKnownValue:
			return entity
                + " record contains unsupported first-version data and was skipped.";
		case SchemaIssue::SerializationFailure:
			return entity + " record could not be serialized.";
		case SchemaIssue::None:
			break;
	}

	return entity + " record could not be loaded and was skipped.";
}

[[nodiscard]] JsonlDiagnostic diagnostic_from_schema(
	EntityTableKind kind, std::uint64_t line,
	const SchemaDiagnostic& diagnostic) {
	return make_diagnostic(kind, LoadDiagnosticSeverity::Error,
						   code_for_schema_diagnostic(diagnostic), line,
						   message_for_schema_diagnostic(kind, diagnostic),
						   schema_details(diagnostic));
}

template<class Result>
void reject_line(Result& result, std::uint64_t line, std::string_view raw,
				 JsonlDiagnostic diagnostic) {
	result.quarantine_entries.push_back(
		QuarantineEntry{.source	 = diagnostic.path,
						.line	 = line,
						.code	 = diagnostic.code,
						.message = diagnostic.message,
						.details = diagnostic.details,
						.raw	 = std::string{raw}});
	++result.summary.rejected_lines;
	result.diagnostics.push_back(std::move(diagnostic));
}

template<class Envelope>
void append_unknown_field_diagnostic(const Envelope& envelope,
									 EntityTableKind kind, std::uint64_t line,
									 EntityTableLoadResult<Envelope>& result) {
	if (envelope.unknown_fields.empty())
		return;

	append_diagnostic(
		result,
		make_diagnostic(kind, LoadDiagnosticSeverity::Info,
						"unknown_field_preserved", line,
						singular_table_name(kind)
							+ " record preserved top-level unknown fields.",
						"unknownFieldCount="
							+ std::to_string(envelope.unknown_fields.size())));
}

template<class Envelope>
void append_tag_warnings(const std::vector<domain::TagRow>& tags,
						 EntityTableKind kind, std::uint64_t line,
						 EntityTableLoadResult<Envelope>& result) {
	for (auto index = std::size_t{0}; index < tags.size(); ++index) {
		const auto validation = domain::validate_existing_tag_row(tags[index]);
		if (!validation.has_warning())
			continue;

		append_diagnostic(
			result,
			make_diagnostic(kind, LoadDiagnosticSeverity::Warning,
							"blank_tag_key", line,
							singular_table_name(kind)
								+ " record kept an existing blank tag key; it "
								  "is preserved but excluded from key hints.",
							"tagIndex=" + std::to_string(index)));
	}
}

template<class Envelope>
void append_accepted_record_diagnostics(
	const Envelope& envelope, EntityTableKind kind, std::uint64_t line,
	EntityTableLoadResult<Envelope>& result) {
	append_unknown_field_diagnostic(envelope, kind, line, result);

	if constexpr (std::is_same_v<Envelope, ItemEnvelope>
				  || std::is_same_v<Envelope, StorageEnvelope>) {
		append_tag_warnings(envelope.record.tags, kind, line, result);
	}
}

template<class Envelope, class Parser>
[[nodiscard]] EntityTableLoadResult<Envelope> load_entity_jsonl(
	std::string_view jsonl, EntityTableKind kind, Parser parser) {
	auto result			= EntityTableLoadResult<Envelope>{};
	result.summary.path = table_path(kind);

	auto accepted_ids = std::unordered_set<std::string>{};
	auto input		  = std::istringstream{std::string{jsonl}};
	auto line		  = std::string{};
	auto line_number  = std::uint64_t{0};

	while (std::getline(input, line)) {
		++line_number;

		if (is_blank_line(line)) {
			append_diagnostic(
				result, make_diagnostic(kind, LoadDiagnosticSeverity::Warning,
										"blank_line", line_number,
										"Blank JSONL line was ignored.",
										"Canonical entity JSONL files should "
										"not contain blank lines."));
			continue;
		}

		auto parsed = parser(line);
		if (!parsed.succeeded()) {
			auto diagnostic = parsed.diagnostic.value_or(SchemaDiagnostic{
				.issue	 = SchemaIssue::JsonParseError,
				.field	 = std::string{to_string(kind)},
				.message = "Entity record could not be parsed."});
			reject_line(result, line_number, line,
						diagnostic_from_schema(kind, line_number, diagnostic));
			continue;
		}

		auto envelope  = std::move(*parsed.value);
		const auto& id = envelope.record.id.value();
		if (!accepted_ids.insert(id).second) {
			reject_line(
				result, line_number, line,
				make_diagnostic(kind, LoadDiagnosticSeverity::Error,
								"duplicate_id", line_number,
								singular_table_name(kind)
									+ " record duplicated an already accepted "
									  "identifier and was skipped.",
								"id=" + id));
			continue;
		}

		append_accepted_record_diagnostics(envelope, kind, line_number, result);
		result.records.push_back(std::move(envelope));
	}

	result.summary.accepted_records = result.records.size();
	return result;
}

template<class Envelope, class Serializer>
[[nodiscard]] JsonTextWriteResult write_entity_jsonl(
	std::span<const Envelope> records, EntityTableKind kind,
	Serializer serializer) {
	auto ordered = std::vector<const Envelope*>{};
	ordered.reserve(records.size());
	for (const auto& record : records)
		ordered.push_back(&record);

	std::ranges::sort(ordered, [](const auto* left, const auto* right) {
		return left->record.id.value() < right->record.id.value();
	});

	auto output = std::string{};
	for (const auto* record : ordered) {
		const auto serialized = serializer(*record);
		if (!serialized.succeeded()) {
			auto details = std::string{};
			if (serialized.diagnostic)
				details = schema_details(*serialized.diagnostic);

			return {.diagnostics = {make_diagnostic(
						kind, LoadDiagnosticSeverity::Error,
						"serialization_failed", std::nullopt,
						singular_table_name(kind)
							+ " table could not be serialized to JSONL.",
						std::move(details))}};
		}

		output += serialized.json;
		output.push_back('\n');
	}

	return {.text = std::move(output)};
}

[[nodiscard]] std::vector<JsonlDiagnostic> collect_diagnostics(
	const CatalogJsonlLoadResult& load_result) {
	auto diagnostics = std::vector<JsonlDiagnostic>{};
	diagnostics.reserve(load_result.items.diagnostics.size()
						+ load_result.storages.diagnostics.size()
						+ load_result.photos.diagnostics.size());
	diagnostics.insert(diagnostics.end(), load_result.items.diagnostics.begin(),
					   load_result.items.diagnostics.end());
	diagnostics.insert(diagnostics.end(),
					   load_result.storages.diagnostics.begin(),
					   load_result.storages.diagnostics.end());
	diagnostics.insert(diagnostics.end(),
					   load_result.photos.diagnostics.begin(),
					   load_result.photos.diagnostics.end());
	return diagnostics;
}

[[nodiscard]] CatalogLoadStatus classify_status(
	std::span<const JsonlDiagnostic> diagnostics) {
	const auto fatal = std::ranges::any_of(diagnostics, [](const auto& entry) {
		return entry.severity == LoadDiagnosticSeverity::Fatal;
	});
	if (fatal)
		return CatalogLoadStatus::Fatal;

	const auto degraded =
		std::ranges::any_of(diagnostics, [](const auto& entry) {
		return entry.severity == LoadDiagnosticSeverity::Error
			   || entry.severity == LoadDiagnosticSeverity::Warning;
	});
	if (degraded)
		return CatalogLoadStatus::Degraded;

	return CatalogLoadStatus::Normal;
}

[[nodiscard]] std::uint64_t count_severity(
	std::span<const JsonlDiagnostic> diagnostics,
	LoadDiagnosticSeverity severity) {
	return static_cast<std::uint64_t>(std::ranges::count_if(
		diagnostics,
		[severity](const auto& entry) { return entry.severity == severity; }));
}

[[nodiscard]] RecoveryReportDto recovery_report_dto(
	const RecoveryReport& report) {
	auto file_dtos = std::vector<FileSummaryDto>{};
	file_dtos.reserve(report.files.size());
	for (const auto& file : report.files)
		file_dtos.push_back(file_summary_dto(file));

	auto diagnostic_dtos = std::vector<DiagnosticDto>{};
	diagnostic_dtos.reserve(report.diagnostics.size());
	for (const auto& diagnostic : report.diagnostics)
		diagnostic_dtos.push_back(diagnostic_dto(diagnostic));

	return {
		.schema_version = report.schema_version,
		.created_at		= report.created_at.count(),
		.catalog_id		= report.catalog_id
							  ? std::optional{report.catalog_id->value()}
							  : std::nullopt,
		.load_status	= std::string{to_string(report.load_status)},
		.summary =
			RecoverySummaryDto{.items_accepted	  = report.items_accepted,
							   .storages_accepted = report.storages_accepted,
							   .photos_accepted	  = report.photos_accepted,
							   .fatal_count		  = report.fatal_count,
							   .error_count		  = report.error_count,
							   .warning_count	  = report.warning_count},
		.files		 = std::move(file_dtos),
		.diagnostics = std::move(diagnostic_dtos)};
}

template<class Dto>
[[nodiscard]] JsonTextWriteResult write_dto_json(const Dto& dto,
												 std::string area,
												 std::string path,
												 std::string failure_message) {
	auto output		 = std::string{};
	const auto error = glz::write_json(dto, output);
	if (!error)
		return {.text = std::move(output)};

	return {.diagnostics = {make_text_write_diagnostic(
				std::move(area), std::move(path), std::move(failure_message),
				glz::format_error(error, output))}};
}
}	 // namespace

std::string_view to_string(EntityTableKind kind) noexcept {
	switch (kind) {
		case EntityTableKind::Items:
			return "items";
		case EntityTableKind::Storages:
			return "storages";
		case EntityTableKind::Photos:
			return "photos";
	}

	return "entities";
}

std::string_view to_string(LoadDiagnosticSeverity severity) noexcept {
	switch (severity) {
		case LoadDiagnosticSeverity::Info:
			return "info";
		case LoadDiagnosticSeverity::Warning:
			return "warning";
		case LoadDiagnosticSeverity::Error:
			return "error";
		case LoadDiagnosticSeverity::Fatal:
			return "fatal";
	}

	return "unknown";
}

std::string_view to_string(CatalogLoadStatus status) noexcept {
	switch (status) {
		case CatalogLoadStatus::Normal:
			return "normal";
		case CatalogLoadStatus::Degraded:
			return "degraded";
		case CatalogLoadStatus::Fatal:
			return "fatal";
	}

	return "unknown";
}

ItemTableLoadResult load_item_jsonl(std::string_view jsonl) {
	return load_entity_jsonl<ItemEnvelope>(jsonl, EntityTableKind::Items,
										   parse_item_record_json);
}

StorageTableLoadResult load_storage_jsonl(std::string_view jsonl) {
	return load_entity_jsonl<StorageEnvelope>(jsonl, EntityTableKind::Storages,
											  parse_storage_record_json);
}

PhotoTableLoadResult load_photo_jsonl(std::string_view jsonl) {
	return load_entity_jsonl<PhotoEnvelope>(jsonl, EntityTableKind::Photos,
											parse_photo_record_json);
}

CatalogJsonlLoadResult load_catalog_jsonl(
	const CatalogJsonlDocuments& documents) {
	auto result = CatalogJsonlLoadResult{
		.items	  = load_item_jsonl(documents.items_jsonl),
		.storages = load_storage_jsonl(documents.storages_jsonl),
		.photos	  = load_photo_jsonl(documents.photos_jsonl)};
	result.diagnostics = collect_diagnostics(result);
	result.load_status = classify_status(result.diagnostics);
	return result;
}

JsonTextWriteResult write_item_jsonl(std::span<const ItemEnvelope> records) {
	return write_entity_jsonl(records, EntityTableKind::Items,
							  serialize_item_record_json);
}

JsonTextWriteResult write_storage_jsonl(
	std::span<const StorageEnvelope> records) {
	return write_entity_jsonl(records, EntityTableKind::Storages,
							  serialize_storage_record_json);
}

JsonTextWriteResult write_photo_jsonl(std::span<const PhotoEnvelope> records) {
	return write_entity_jsonl(records, EntityTableKind::Photos,
							  serialize_photo_record_json);
}

RecoveryReport make_recovery_report(
	const CatalogJsonlLoadResult& load_result,
	core::EpochMilliseconds created_at,
	std::optional<core::StableIdentifier> catalog_id) {
	auto report = RecoveryReport{
		.created_at		   = created_at,
		.catalog_id		   = std::move(catalog_id),
		.load_status	   = load_result.load_status,
		.items_accepted	   = load_result.items.summary.accepted_records,
		.storages_accepted = load_result.storages.summary.accepted_records,
		.photos_accepted   = load_result.photos.summary.accepted_records,
		.files		 = {load_result.items.summary, load_result.storages.summary,
						load_result.photos.summary},
		.diagnostics = load_result.diagnostics};

	report.fatal_count =
		count_severity(report.diagnostics, LoadDiagnosticSeverity::Fatal);
	report.error_count =
		count_severity(report.diagnostics, LoadDiagnosticSeverity::Error);
	report.warning_count =
		count_severity(report.diagnostics, LoadDiagnosticSeverity::Warning);
	report.load_status = classify_status(report.diagnostics);
	return report;
}

JsonTextWriteResult write_recovery_report_json(const RecoveryReport& report) {
	return write_dto_json(recovery_report_dto(report), "recovery",
						  std::string{recovery_report_file_path},
						  "Recovery report could not be serialized.");
}

JsonTextWriteResult write_quarantine_jsonl(
	std::span<const QuarantineEntry> entries) {
	auto output = std::string{};
	for (const auto& entry : entries) {
		const auto written =
			write_dto_json(quarantine_dto(entry), "recovery", entry.source,
						   "Quarantine entry could not be serialized.");
		if (!written.succeeded())
			return written;

		output += written.text;
		output.push_back('\n');
	}

	return {.text = std::move(output)};
}
}	 // namespace shuba::persistence
