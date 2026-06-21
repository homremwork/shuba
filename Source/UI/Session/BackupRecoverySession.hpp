#pragma once

#include "Domain/Domain.hpp"
#include "UI/Session/BackupRecoveryTypes.hpp"

namespace shuba::ui {
[[nodiscard]] BackupExportSessionResult export_backup_from_session(
	const BackupExportSessionRequest& request,
	platform::ProgressSink& progress_sink,
	platform::CancellationToken& cancellation_token);
[[nodiscard]] BackupExportSessionResult export_diagnostic_archive_from_session(
	const BackupExportSessionRequest& request,
	platform::ProgressSink& progress_sink,
	platform::CancellationToken& cancellation_token);
[[nodiscard]] BackupImportStagingSessionResult stage_backup_import_for_session(
	const BackupImportStagingSessionRequest& request,
	platform::ProgressSink& progress_sink,
	platform::CancellationToken& cancellation_token);
[[nodiscard]] BackupImportReplacementSessionResult
replace_session_with_staged_import(
	const BackupImportReplacementSessionRequest& request,
	platform::ProgressSink& progress_sink,
	platform::CancellationToken& cancellation_token);
[[nodiscard]] bool hard_delete_enabled_for_owner(
	const CatalogSessionState& session, const core::StableIdentifier& owner_id,
	domain::PhotoOwnerType owner_type,
	bool multi_file_deletion_sequence_tests_proven = false);
}	 // namespace shuba::ui
