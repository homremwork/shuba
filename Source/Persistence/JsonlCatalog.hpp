#pragma once

#include "Core/Clock.hpp"
#include "Core/Identifier.hpp"
#include "Persistence/MetadataSchema.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace shuba::persistence {
inline constexpr auto recovery_directory_path = std::string_view{"recovery"};
inline constexpr auto recovery_report_file_path =
	std::string_view{"recovery/last-load-report.json"};
inline constexpr auto quarantine_directory_path =
	std::string_view{"recovery/quarantine"};
inline constexpr auto items_quarantine_file_path =
	std::string_view{"recovery/quarantine/items.invalid.jsonl"};
inline constexpr auto storages_quarantine_file_path =
	std::string_view{"recovery/quarantine/storages.invalid.jsonl"};
inline constexpr auto photos_quarantine_file_path =
	std::string_view{"recovery/quarantine/photos.invalid.jsonl"};

enum class EntityTableKind {
	Items,
	Storages,
	Photos,
};

[[nodiscard]] std::string_view to_string(EntityTableKind kind) noexcept;

enum class LoadDiagnosticSeverity {
	Info,
	Warning,
	Error,
	Fatal,
};

[[nodiscard]] std::string_view to_string(
	LoadDiagnosticSeverity severity) noexcept;

enum class CatalogLoadStatus {
	Normal,
	Degraded,
	Fatal,
};

[[nodiscard]] std::string_view to_string(CatalogLoadStatus status) noexcept;

struct JsonlDiagnostic final {
	LoadDiagnosticSeverity severity{LoadDiagnosticSeverity::Info};
	std::string area;
	std::string code;
	std::string path;
	std::optional<std::uint64_t> line;
	std::string message;
	std::string details;

	friend bool operator==(const JsonlDiagnostic&,
						   const JsonlDiagnostic&) = default;
};

struct JsonlFileSummary final {
	std::string path;
	std::uint64_t accepted_records{};
	std::uint64_t rejected_lines{};
	std::uint64_t warnings{};

	friend bool operator==(const JsonlFileSummary&,
						   const JsonlFileSummary&) = default;
};

struct QuarantineEntry final {
	std::string source;
	std::uint64_t line{};
	std::string code;
	std::string message;
	std::string details;
	std::string raw;

	friend bool operator==(const QuarantineEntry&,
						   const QuarantineEntry&) = default;
};

template<class Envelope>
struct EntityTableLoadResult final {
	std::vector<Envelope> records;
	std::vector<JsonlDiagnostic> diagnostics;
	std::vector<QuarantineEntry> quarantine_entries;
	JsonlFileSummary summary;

	friend bool operator==(const EntityTableLoadResult&,
						   const EntityTableLoadResult&) = default;
};

using ItemTableLoadResult	 = EntityTableLoadResult<ItemEnvelope>;
using StorageTableLoadResult = EntityTableLoadResult<StorageEnvelope>;
using PhotoTableLoadResult	 = EntityTableLoadResult<PhotoEnvelope>;

struct CatalogJsonlDocuments final {
	std::string items_jsonl;
	std::string storages_jsonl;
	std::string photos_jsonl;

	friend bool operator==(const CatalogJsonlDocuments&,
						   const CatalogJsonlDocuments&) = default;
};

struct CatalogJsonlLoadResult final {
	ItemTableLoadResult items;
	StorageTableLoadResult storages;
	PhotoTableLoadResult photos;
	std::vector<JsonlDiagnostic> diagnostics;
	CatalogLoadStatus load_status{CatalogLoadStatus::Normal};

	friend bool operator==(const CatalogJsonlLoadResult&,
						   const CatalogJsonlLoadResult&) = default;
};

struct RecoveryReport final {
	int schema_version{first_catalog_schema_version};
	core::EpochMilliseconds created_at{};
	std::optional<core::StableIdentifier> catalog_id;
	CatalogLoadStatus load_status{CatalogLoadStatus::Normal};
	std::uint64_t items_accepted{};
	std::uint64_t storages_accepted{};
	std::uint64_t photos_accepted{};
	std::uint64_t fatal_count{};
	std::uint64_t error_count{};
	std::uint64_t warning_count{};
	std::vector<JsonlFileSummary> files;
	std::vector<JsonlDiagnostic> diagnostics;

	friend bool operator==(const RecoveryReport&,
						   const RecoveryReport&) = default;
};

struct JsonTextWriteResult final {
	std::string text;
	std::vector<JsonlDiagnostic> diagnostics;

	[[nodiscard]] bool succeeded() const noexcept {
		return diagnostics.empty();
	}

	[[nodiscard]] explicit operator bool() const noexcept {
		return succeeded();
	}

	friend bool operator==(const JsonTextWriteResult&,
						   const JsonTextWriteResult&) = default;
};

[[nodiscard]] ItemTableLoadResult load_item_jsonl(std::string_view jsonl);
[[nodiscard]] StorageTableLoadResult load_storage_jsonl(std::string_view jsonl);
[[nodiscard]] PhotoTableLoadResult load_photo_jsonl(std::string_view jsonl);
[[nodiscard]] CatalogJsonlLoadResult load_catalog_jsonl(
	const CatalogJsonlDocuments& documents);

[[nodiscard]] JsonTextWriteResult write_item_jsonl(
	std::span<const ItemEnvelope> records);
[[nodiscard]] JsonTextWriteResult write_storage_jsonl(
	std::span<const StorageEnvelope> records);
[[nodiscard]] JsonTextWriteResult write_photo_jsonl(
	std::span<const PhotoEnvelope> records);

[[nodiscard]] RecoveryReport make_recovery_report(
	const CatalogJsonlLoadResult& load_result,
	core::EpochMilliseconds created_at,
	std::optional<core::StableIdentifier> catalog_id = std::nullopt);
[[nodiscard]] JsonTextWriteResult write_recovery_report_json(
	const RecoveryReport& report);
[[nodiscard]] JsonTextWriteResult write_quarantine_jsonl(
	std::span<const QuarantineEntry> entries);
}	 // namespace shuba::persistence
