#include "UI/Session/BackupRecoverySession.hpp"

#include "UI/Session/CatalogStartupSession.hpp"

#include "Persistence/CatalogStorage.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace shuba::ui {
namespace {
using shuba::core::Diagnostic;
using shuba::core::DiagnosticSeverity;
using shuba::core::OperationResultCategory;

[[nodiscard]] Diagnostic make_diagnostic(DiagnosticSeverity severity,
										 std::string code, std::string message,
										 std::string technical_details = {}) {
	return Diagnostic{.severity			 = severity,
					  .code				 = std::move(code),
					  .message			 = std::move(message),
					  .technical_details = std::move(technical_details)};
}

[[nodiscard]] bool owner_has_photos(const CatalogSessionState& session,
									const core::StableIdentifier& owner_id,
									domain::PhotoOwnerType owner_type) {
	return std::ranges::any_of(session.repository.photos,
							   [&](const persistence::PhotoEnvelope& photo) {
		return photo.record.owner_type == owner_type
			   && photo.record.owner_id == owner_id;
	});
}

[[nodiscard]] BackupExportSessionResult blocked_backup_export_result(
	const CatalogSessionState& session, std::string code, std::string message) {
	BackupExportSessionResult result;
	result.category = OperationResultCategory::ValidationFailure;
	result.diagnostics.push_back(make_diagnostic(
		DiagnosticSeverity::ActionValidationError, std::move(code),
		std::move(message),
		session.paths ? session.paths->active_catalog_root.string()
					  : std::string{}));
	return result;
}

[[nodiscard]] BackupImportStagingSessionResult blocked_import_staging_result(
	const CatalogSessionState& session, std::string code, std::string message) {
	BackupImportStagingSessionResult result;
	result.category = OperationResultCategory::ValidationFailure;
	result.diagnostics.push_back(
		make_diagnostic(DiagnosticSeverity::ActionValidationError,
						std::move(code), std::move(message),
						session.paths ? session.paths->app_private_root.string()
									  : std::string{}));
	return result;
}

[[nodiscard]] BackupImportReplacementSessionResult blocked_replacement_result(
	const CatalogSessionState& session, std::string code, std::string message) {
	BackupImportReplacementSessionResult result{.session = session};
	result.category = OperationResultCategory::ValidationFailure;
	result.diagnostics.push_back(
		make_diagnostic(DiagnosticSeverity::ActionValidationError,
						std::move(code), std::move(message),
						session.paths ? session.paths->app_private_root.string()
									  : std::string{}));
	return result;
}

[[nodiscard]] BackupExportSessionResult export_archive_from_session(
	const BackupExportSessionRequest& request, bool diagnostic_archive,
	platform::ProgressSink& progress_sink,
	platform::CancellationToken& cancellation_token) {
	if (!request.current_session.paths) {
		return blocked_backup_export_result(
			request.current_session, "catalog_paths_missing",
			"App-private catalog paths are unavailable for archive export.");
	}
	if (!diagnostic_archive && !request.current_session.ready_for_browsing()) {
		return blocked_backup_export_result(
			request.current_session, "catalog_not_browsable_for_backup",
			"Normal backup requires a loaded normal or degraded catalog.");
	}

	catalog::BackupArchiveUseCase use_case{request.identifiers,
										   request.clock,
										   request.operation_gate,
										   request.zip_archive_service,
										   request.document_export_service,
										   request.content_staging_service};
	catalog::BackupExportResult archive_result =
		diagnostic_archive
			? use_case.export_diagnostic_archive(
				  catalog::BackupExportRequest{
					  .current_state = request.current_session.repository,
					  .current_load_status =
						  request.current_session.load_status,
					  .paths		 = *request.current_session.paths,
					  .destination	 = request.destination,
					  .keep_temp_zip = request.keep_temp_zip},
				  progress_sink, cancellation_token)
			: use_case.export_normal_backup(
				  catalog::BackupExportRequest{
					  .current_state = request.current_session.repository,
					  .current_load_status =
						  request.current_session.load_status,
					  .paths		 = *request.current_session.paths,
					  .destination	 = request.destination,
					  .keep_temp_zip = request.keep_temp_zip},
				  progress_sink, cancellation_token);

	BackupExportSessionResult result;
	result.category = archive_result.category;
	result.degraded_backup_warning_required =
		archive_result.degraded_warning_required;
	result.diagnostic_companion_recommended =
		archive_result.diagnostic_companion_recommended;
	result.diagnostics	 = archive_result.diagnostics;
	result.export_result = std::move(archive_result);
	return result;
}
}	 // namespace

bool BackupExportSessionResult::succeeded() const noexcept {
	return export_result.succeeded();
}

bool BackupExportSessionResult::was_user_cancelled() const noexcept {
	return category == core::OperationResultCategory::UserCancelled
		   || export_result.was_user_cancelled();
}

bool BackupExportSessionResult::failed() const noexcept {
	return !succeeded() && !was_user_cancelled();
}

bool BackupImportStagingSessionResult::succeeded() const noexcept {
	return staging_result.succeeded();
}

bool BackupImportStagingSessionResult::was_user_cancelled() const noexcept {
	return category == core::OperationResultCategory::UserCancelled
		   || staging_result.was_user_cancelled();
}

bool BackupImportStagingSessionResult::failed() const noexcept {
	return !succeeded() && !was_user_cancelled();
}

bool BackupImportReplacementSessionResult::succeeded() const noexcept {
	return replacement_result.succeeded();
}

bool BackupImportReplacementSessionResult::was_user_cancelled() const noexcept {
	return category == core::OperationResultCategory::UserCancelled
		   || replacement_result.status
				  == catalog::CatalogReplacementStatus::Cancelled;
}

bool BackupImportReplacementSessionResult::failed() const noexcept {
	return !succeeded() && !was_user_cancelled();
}

BackupExportSessionResult export_backup_from_session(
	const BackupExportSessionRequest& request,
	platform::ProgressSink& progress_sink,
	platform::CancellationToken& cancellation_token) {
	return export_archive_from_session(request, false, progress_sink,
									   cancellation_token);
}

BackupExportSessionResult export_diagnostic_archive_from_session(
	const BackupExportSessionRequest& request,
	platform::ProgressSink& progress_sink,
	platform::CancellationToken& cancellation_token) {
	return export_archive_from_session(request, true, progress_sink,
									   cancellation_token);
}

BackupImportStagingSessionResult stage_backup_import_for_session(
	const BackupImportStagingSessionRequest& request,
	platform::ProgressSink& progress_sink,
	platform::CancellationToken& cancellation_token) {
	if (!request.current_session.paths) {
		return blocked_import_staging_result(
			request.current_session, "catalog_paths_missing",
			"App-private catalog paths are unavailable for backup import.");
	}

	catalog::BackupArchiveUseCase use_case{request.identifiers,
										   request.clock,
										   request.operation_gate,
										   request.zip_archive_service,
										   request.document_export_service,
										   request.content_staging_service};
	catalog::BackupImportStagingResult staging_result =
		use_case.stage_and_validate_import(
			catalog::BackupImportStagingRequest{
				.source					= request.source,
				.paths					= *request.current_session.paths,
				.keep_staged_zip		= request.keep_staged_zip,
				.keep_extracted_catalog = request.keep_extracted_catalog},
			progress_sink, cancellation_token);

	BackupImportStagingSessionResult result;
	result.category				   = staging_result.category;
	result.import_validation_ready = staging_result.succeeded();
	result.degraded_import_confirmation_required =
		staging_result.validation.explicit_warning_required();
	result.diagnostics	  = staging_result.diagnostics;
	result.staging_result = std::move(staging_result);
	return result;
}

BackupImportReplacementSessionResult replace_session_with_staged_import(
	const BackupImportReplacementSessionRequest& request,
	platform::ProgressSink& progress_sink,
	platform::CancellationToken& cancellation_token) {
	if (!request.current_session.paths) {
		return blocked_replacement_result(
			request.current_session, "catalog_paths_missing",
			"App-private catalog paths are unavailable for replacement.");
	}
	if (request.staged_catalog_root.empty()) {
		return blocked_replacement_result(
			request.current_session, "staged_catalog_missing",
			"Validated staged catalog root is required before replacement.");
	}

	CatalogSessionState current_session = request.current_session;
	if (current_session.source
		== CatalogSessionStartupSource::StartupCrashSafeMode) {
		persistence::CatalogStorageResult cleanup =
			persistence::cleanup_startup_temporary_files(
				persistence::CatalogStartupCleanupRequest{
					.app_private_root = current_session.paths->app_private_root,
					.protected_paths  = {request.staged_catalog_root}});
		current_session.startup_diagnostics.insert(
			current_session.startup_diagnostics.end(),
			cleanup.diagnostics.begin(), cleanup.diagnostics.end());
	}

	catalog::CatalogReplacementUseCase use_case{
		request.identifiers, request.clock, request.operation_gate};
	catalog::CatalogReplacementResult replacement_result =
		use_case.replace_with_staged_import(
			catalog::CatalogReplacementRequest{
				.app_private_root =
					request.current_session.paths->app_private_root,
				.staged_catalog_root	   = request.staged_catalog_root,
				.replacement_confirmed	   = request.replacement_confirmed,
				.degraded_import_confirmed = request.degraded_import_confirmed,
				.fault_mode				   = request.fault_mode},
			progress_sink, cancellation_token);

	BackupImportReplacementSessionResult result{
		.category	 = replacement_result.category,
		.session	 = current_session,
		.diagnostics = replacement_result.diagnostics,
		.fatal_recovery_required =
			replacement_result.fatal_recovery_required()};
	if (replacement_result.succeeded()) {
		result.session = reload_catalog_session(std::move(current_session));
	} else if (replacement_result.fatal_recovery_required()) {
		result.session.load_status = persistence::CatalogLoadStatus::Fatal;
		result.session.source	   = CatalogSessionStartupSource::LoadFailed;
	} else {
		result.session = std::move(current_session);
	}
	result.replacement_result = std::move(replacement_result);
	return result;
}

bool hard_delete_enabled_for_owner(
	const CatalogSessionState& session, const core::StableIdentifier& owner_id,
	domain::PhotoOwnerType owner_type,
	bool multi_file_deletion_sequence_tests_proven) {
	if (!domain::owner_hard_delete_visible(
			multi_file_deletion_sequence_tests_proven)) {
		return false;
	}
	return !owner_has_photos(session, owner_id, owner_type);
}
}	 // namespace shuba::ui
