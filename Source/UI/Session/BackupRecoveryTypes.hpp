#pragma once

#include "Catalog/BackupArchive.hpp"
#include "Catalog/CatalogReplacement.hpp"
#include "Core/Clock.hpp"
#include "Core/Identifier.hpp"
#include "Core/OperationGate.hpp"
#include "Platform/PlatformServices.hpp"
#include "UI/Session/CatalogSessionState.hpp"

#include <filesystem>
#include <vector>

namespace shuba::ui {
struct BackupExportSessionRequest final {
	CatalogSessionState current_session;
	core::IdentifierSource& identifiers;
	const core::Clock& clock;
	core::OperationGate& operation_gate;
	platform::ZipArchiveService& zip_archive_service;
	platform::DocumentExportService& document_export_service;
	platform::ContentStagingService& content_staging_service;
	platform::DocumentDestinationDescriptor destination;
	bool keep_temp_zip{};
};

struct BackupExportSessionResult final {
	core::OperationResultCategory category{
		core::OperationResultCategory::InternalError};
	catalog::BackupExportResult export_result;
	std::vector<core::Diagnostic> diagnostics;
	bool unencrypted_zip_warning_required{true};
	bool degraded_backup_warning_required{};
	bool diagnostic_companion_recommended{};

	[[nodiscard]] bool succeeded() const noexcept;
	[[nodiscard]] bool was_user_cancelled() const noexcept;
	[[nodiscard]] bool failed() const noexcept;
};

struct BackupImportStagingSessionRequest final {
	CatalogSessionState current_session;
	core::IdentifierSource& identifiers;
	const core::Clock& clock;
	core::OperationGate& operation_gate;
	platform::ZipArchiveService& zip_archive_service;
	platform::DocumentExportService& document_export_service;
	platform::ContentStagingService& content_staging_service;
	platform::ContentSourceDescriptor source;
	bool keep_staged_zip{};
	bool keep_extracted_catalog{true};
};

struct BackupImportStagingSessionResult final {
	core::OperationResultCategory category{
		core::OperationResultCategory::InternalError};
	catalog::BackupImportStagingResult staging_result;
	std::vector<core::Diagnostic> diagnostics;
	bool unencrypted_zip_warning_required{true};
	bool import_validation_ready{};
	bool degraded_import_confirmation_required{};

	[[nodiscard]] bool succeeded() const noexcept;
	[[nodiscard]] bool was_user_cancelled() const noexcept;
	[[nodiscard]] bool failed() const noexcept;
};

struct BackupImportReplacementSessionRequest final {
	CatalogSessionState current_session;
	core::IdentifierSource& identifiers;
	const core::Clock& clock;
	core::OperationGate& operation_gate;
	std::filesystem::path staged_catalog_root;
	bool replacement_confirmed{};
	bool degraded_import_confirmed{};
	catalog::CatalogReplacementFaultMode fault_mode{
		catalog::CatalogReplacementFaultMode::None};
};

struct BackupImportReplacementSessionResult final {
	core::OperationResultCategory category{
		core::OperationResultCategory::InternalError};
	catalog::CatalogReplacementResult replacement_result;
	CatalogSessionState session;
	std::vector<core::Diagnostic> diagnostics;
	bool fatal_recovery_required{};

	[[nodiscard]] bool succeeded() const noexcept;
	[[nodiscard]] bool was_user_cancelled() const noexcept;
	[[nodiscard]] bool failed() const noexcept;
};
}	 // namespace shuba::ui
