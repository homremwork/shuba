#pragma once

#include "Catalog/CatalogRepository.hpp"
#include "Core/Clock.hpp"
#include "Core/Identifier.hpp"
#include "Core/OperationGate.hpp"
#include "Core/Result.hpp"
#include "Persistence/JsonlCatalog.hpp"
#include "Persistence/MetadataSchema.hpp"
#include "Platform/PlatformServices.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace shuba::catalog {
inline constexpr auto backup_zip_mime_type =
	std::string_view{"application/zip"};

enum class BackupArchiveKind : std::uint8_t {
	NormalBackup,
	DiagnosticArchive,
};

[[nodiscard]] std::string_view to_string(BackupArchiveKind kind) noexcept;

enum class BackupExportStatus : std::uint8_t {
	Exported,
	Failed,
	Cancelled,
};

[[nodiscard]] std::string_view to_string(BackupExportStatus status) noexcept;

struct BackupArchiveFileIssue final {
	std::string archive_path;
	std::string reason;

	friend bool operator==(const BackupArchiveFileIssue&,
						   const BackupArchiveFileIssue&) = default;
};

struct BackupExportRequest final {
	CatalogRepositoryState current_state;
	persistence::CatalogLoadStatus current_load_status{
		persistence::CatalogLoadStatus::Normal};
	platform::AppPrivatePaths paths;
	platform::DocumentDestinationDescriptor destination;
	bool keep_temp_zip{};
};

struct BackupExportResult final {
	BackupArchiveKind archive_kind{BackupArchiveKind::NormalBackup};
	BackupExportStatus status{BackupExportStatus::Failed};
	core::OperationResultCategory category{
		core::OperationResultCategory::InternalError};
	std::vector<core::Diagnostic> diagnostics;
	std::optional<std::filesystem::path> temp_zip_path;
	std::optional<platform::DocumentDestinationDescriptor> destination;
	std::vector<std::string> included_entries;
	std::vector<BackupArchiveFileIssue> skipped_files;
	std::uint64_t archive_byte_count{};
	std::uint64_t largest_entry_byte_count{};
	bool temp_zip_built{};
	bool temp_zip_validated{};
	bool destination_copied{};
	bool temp_cleanup_attempted{};
	bool degraded_warning_required{};
	bool diagnostic_companion_recommended{};
	bool classic_zip64_risk_observed{};

	[[nodiscard]] bool succeeded() const noexcept;
	[[nodiscard]] bool was_user_cancelled() const noexcept;
	[[nodiscard]] bool failed() const noexcept;
};

struct StagedCatalogValidationResult final {
	persistence::CatalogLoadStatus load_status{
		persistence::CatalogLoadStatus::Fatal};
	std::vector<core::Diagnostic> diagnostics;
	std::optional<core::StableIdentifier> catalog_id;
	std::uint64_t items_accepted{};
	std::uint64_t storages_accepted{};
	std::uint64_t photos_accepted{};
	DerivedRecoverySummary derived_recovery_summary;

	[[nodiscard]] bool import_allowed() const noexcept;
	[[nodiscard]] bool explicit_warning_required() const noexcept;
};

struct BackupImportStagingRequest final {
	platform::ContentSourceDescriptor source;
	platform::AppPrivatePaths paths;
	bool keep_staged_zip{};
	bool keep_extracted_catalog{true};

	friend bool operator==(const BackupImportStagingRequest&,
						   const BackupImportStagingRequest&) = default;
};

struct BackupImportStagingResult final {
	core::OperationResultCategory category{
		core::OperationResultCategory::InternalError};
	std::vector<core::Diagnostic> diagnostics;
	std::optional<std::filesystem::path> staged_zip_path;
	std::optional<std::filesystem::path> staging_catalog_root;
	StagedCatalogValidationResult validation;
	std::uint64_t archive_byte_count{};
	std::uint64_t largest_entry_byte_count{};
	bool staged_zip_copied{};
	bool zip_extracted{};
	bool staged_zip_cleanup_attempted{};
	bool extracted_catalog_cleanup_attempted{};
	bool classic_zip64_risk_observed{};

	[[nodiscard]] bool succeeded() const noexcept;
	[[nodiscard]] bool was_user_cancelled() const noexcept;
	[[nodiscard]] bool failed() const noexcept;
};

[[nodiscard]] std::string suggested_backup_file_name(
	core::EpochMilliseconds created_at);
[[nodiscard]] std::string suggested_diagnostic_archive_file_name(
	core::EpochMilliseconds created_at);

[[nodiscard]] StagedCatalogValidationResult validate_staged_catalog(
	const std::filesystem::path& staged_catalog_root,
	core::EpochMilliseconds validated_at);

class BackupArchiveUseCase final {
public:
	BackupArchiveUseCase(
		core::IdentifierSource& identifier_source, const core::Clock& clock,
		core::OperationGate& operation_gate,
		platform::ZipArchiveService& zip_archive_service,
		platform::DocumentExportService& document_export_service,
		platform::ContentStagingService& content_staging_service);

	[[nodiscard]] BackupExportResult export_normal_backup(
		const BackupExportRequest& request,
		platform::ProgressSink& progress_sink,
		platform::CancellationToken& cancellation_token);
	[[nodiscard]] BackupExportResult export_diagnostic_archive(
		const BackupExportRequest& request,
		platform::ProgressSink& progress_sink,
		platform::CancellationToken& cancellation_token);
	[[nodiscard]] BackupImportStagingResult stage_and_validate_import(
		const BackupImportStagingRequest& request,
		platform::ProgressSink& progress_sink,
		platform::CancellationToken& cancellation_token);

private:
	core::IdentifierSource& identifiers;
	const core::Clock& backup_clock;
	core::OperationGate& gate;
	platform::ZipArchiveService& zip_archives;
	platform::DocumentExportService& document_exporter;
	platform::ContentStagingService& staging;
};
}	 // namespace shuba::catalog
