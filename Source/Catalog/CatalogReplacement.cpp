#include "Catalog/CatalogReplacement.hpp"

#include "Persistence/CatalogStorage.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace shuba::catalog {
namespace {
[[nodiscard]] std::string path_text(const std::filesystem::path& path) {
	return path.generic_string();
}

[[nodiscard]] core::Diagnostic make_diagnostic(
	core::DiagnosticSeverity severity, std::string code, std::string message,
	std::string technical_details = {}) {
	return core::Diagnostic{.severity		   = severity,
							.code			   = std::move(code),
							.message		   = std::move(message),
							.technical_details = std::move(technical_details)};
}

[[nodiscard]] core::Diagnostic filesystem_diagnostic(
	core::DiagnosticSeverity severity, std::string code, std::string message,
	const std::filesystem::path& path, const std::error_code& error) {
	std::string details = path_text(path);
	if (error)
		details += ": " + error.message();
	return make_diagnostic(severity, std::move(code), std::move(message),
						   std::move(details));
}

void append_diagnostics(std::vector<core::Diagnostic>& target,
						const std::vector<core::Diagnostic>& source) {
	for (const core::Diagnostic& diagnostic : source)
		target.push_back(diagnostic);
}

[[nodiscard]] bool path_is_same_or_nested_under(
	const std::filesystem::path& candidate,
	const std::filesystem::path& parent) {
	const std::filesystem::path normalized_candidate =
		std::filesystem::absolute(candidate).lexically_normal();
	const std::filesystem::path normalized_parent =
		std::filesystem::absolute(parent).lexically_normal();

	std::filesystem::path::const_iterator parent_iterator =
		normalized_parent.begin();
	std::filesystem::path::const_iterator candidate_iterator =
		normalized_candidate.begin();
	for (; parent_iterator != normalized_parent.end();
		 ++parent_iterator, ++candidate_iterator) {
		if (candidate_iterator == normalized_candidate.end())
			return false;
		if (*candidate_iterator != *parent_iterator)
			return false;
	}

	return true;
}

[[nodiscard]] std::optional<core::Diagnostic> ensure_directory(
	const std::filesystem::path& directory, std::string code,
	std::string message) {
	std::error_code error;
	std::filesystem::create_directories(directory, error);
	if (!error)
		return std::nullopt;

	return filesystem_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
								 std::move(code), std::move(message), directory,
								 error);
}

[[nodiscard]] bool directory_exists(const std::filesystem::path& path,
									core::Diagnostic& diagnostic) {
	std::error_code error;
	const bool exists = std::filesystem::exists(path, error);
	if (error) {
		diagnostic = filesystem_diagnostic(
			core::DiagnosticSeverity::WriteBlockingError,
			"catalog-directory-status-failed",
			"Catalog directory status could not be checked.", path, error);
		return false;
	}
	if (!exists)
		return false;

	error.clear();
	if (std::filesystem::is_directory(path, error) && !error)
		return true;

	diagnostic = filesystem_diagnostic(
		core::DiagnosticSeverity::WriteBlockingError,
		"catalog-directory-not-directory",
		"Catalog path exists but is not an accessible directory.", path, error);
	return false;
}

[[nodiscard]] std::filesystem::path replacement_work_root(
	const persistence::CatalogContainerLayout& layout,
	const core::OperationIdentifier& operation_id) {
	return layout.operation_tmp_root / "catalog-replacement"
		   / operation_id.value();
}

[[nodiscard]] std::filesystem::path parked_active_catalog_path(
	const persistence::CatalogContainerLayout& layout,
	const core::OperationIdentifier& operation_id) {
	return replacement_work_root(layout, operation_id)
		   / "active-before-replacement";
}

[[nodiscard]] std::filesystem::path rollback_copy_path(
	const persistence::CatalogContainerLayout& layout,
	core::EpochMilliseconds timestamp,
	const core::OperationIdentifier& operation_id) {
	return layout.catalog_rollbacks_root
		   / persistence::previous_copy_group_name(timestamp, operation_id);
}

[[nodiscard]] std::optional<core::Diagnostic> copy_directory_tree(
	const std::filesystem::path& source,
	const std::filesystem::path& destination, std::string code,
	std::string message) {
	std::error_code error;
	std::filesystem::create_directories(destination.parent_path(), error);
	if (error) {
		return filesystem_diagnostic(
			core::DiagnosticSeverity::WriteBlockingError, std::move(code),
			std::move(message), destination.parent_path(), error);
	}

	error.clear();
	std::filesystem::copy(source, destination,
						  std::filesystem::copy_options::recursive, error);
	if (!error)
		return std::nullopt;

	std::error_code ignored;
	std::filesystem::remove_all(destination, ignored);
	return filesystem_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
								 std::move(code), std::move(message),
								 destination, error);
}

[[nodiscard]] std::optional<core::Diagnostic> remove_directory_tree(
	const std::filesystem::path& path, std::string code, std::string message,
	core::DiagnosticSeverity severity =
		core::DiagnosticSeverity::WriteBlockingError) {
	std::error_code error;
	std::filesystem::remove_all(path, error);
	if (!error)
		return std::nullopt;

	return filesystem_diagnostic(severity, std::move(code), std::move(message),
								 path, error);
}

[[nodiscard]] std::optional<core::Diagnostic> rename_directory(
	const std::filesystem::path& source,
	const std::filesystem::path& destination, std::string code,
	std::string message) {
	std::error_code error;
	std::filesystem::create_directories(destination.parent_path(), error);
	if (error) {
		return filesystem_diagnostic(
			core::DiagnosticSeverity::WriteBlockingError, std::move(code),
			std::move(message), destination.parent_path(), error);
	}

	error.clear();
	std::filesystem::rename(source, destination, error);
	if (!error)
		return std::nullopt;

	return filesystem_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
								 std::move(code), std::move(message),
								 destination, error);
}

[[nodiscard]] std::optional<core::Diagnostic> cleanup_rollback_retention(
	const std::filesystem::path& rollbacks_root, std::size_t retention) {
	std::error_code error;
	if (!std::filesystem::exists(rollbacks_root, error))
		return std::nullopt;
	if (error) {
		return filesystem_diagnostic(
			core::DiagnosticSeverity::RecoverableWarning,
			"catalog-rollback-retention-failed",
			"Full-catalog rollback root status could not be checked.",
			rollbacks_root, error);
	}

	std::vector<std::filesystem::path> rollback_directories;
	std::filesystem::directory_iterator iterator{rollbacks_root, error};
	if (error) {
		return filesystem_diagnostic(
			core::DiagnosticSeverity::RecoverableWarning,
			"catalog-rollback-retention-failed",
			"Full-catalog rollback copies could not be listed.", rollbacks_root,
			error);
	}

	for (const std::filesystem::directory_entry& entry : iterator) {
		error.clear();
		if (entry.is_directory(error) && !error)
			rollback_directories.push_back(entry.path());
	}

	if (rollback_directories.size() <= retention)
		return std::nullopt;

	std::ranges::sort(rollback_directories,
					  [](const std::filesystem::path& left,
						 const std::filesystem::path& right) {
		return left.filename().string() < right.filename().string();
	});

	const std::size_t remove_count = rollback_directories.size() - retention;
	for (std::size_t index = 0U; index < remove_count; ++index) {
		error.clear();
		std::filesystem::remove_all(rollback_directories[index], error);
		if (error) {
			return filesystem_diagnostic(
				core::DiagnosticSeverity::RecoverableWarning,
				"catalog-rollback-retention-failed",
				"Old full-catalog rollback copy could not be removed.",
				rollback_directories[index], error);
		}
	}

	return std::nullopt;
}

void mark_cancelled(CatalogReplacementResult& result) {
	result.status	= CatalogReplacementStatus::Cancelled;
	result.category = core::OperationResultCategory::UserCancelled;
}

void mark_rejected(CatalogReplacementResult& result,
				   core::OperationResultCategory category,
				   core::Diagnostic diagnostic) {
	result.status	= CatalogReplacementStatus::Rejected;
	result.category = category;
	result.add_diagnostic(std::move(diagnostic));
}

[[nodiscard]] bool restore_from_rollback(
	CatalogReplacementResult& result, const CatalogReplacementRequest& request,
	const persistence::CatalogContainerLayout& layout) {
	result.rollback_attempted = true;
	if (!result.rollback_copy_directory.has_value()) {
		result.add_diagnostic(make_diagnostic(
			core::DiagnosticSeverity::FatalCatalogError,
			"catalog-rollback-unavailable",
			"Catalog replacement failed and no rollback copy is available."));
		return false;
	}

	if (request.fault_mode == CatalogReplacementFaultMode::FailRollbackRestore
		|| request.fault_mode
			   == CatalogReplacementFaultMode::
				   ForceImportedLoadFatalAndFailRollbackRestore) {
		result.add_diagnostic(make_diagnostic(
			core::DiagnosticSeverity::FatalCatalogError,
			"catalog-rollback-restore-injected-failure",
			"Fault injection forced catalog rollback restore failure.",
			path_text(*result.rollback_copy_directory)));
		return false;
	}

	if (std::optional<core::Diagnostic> removed = remove_directory_tree(
			layout.active_catalog_root,
			"catalog-active-remove-before-rollback-failed",
			"Failed imported catalog could not be removed before rollback.")) {
		result.add_diagnostic(std::move(*removed));
		return false;
	}

	if (std::optional<core::Diagnostic> restored = copy_directory_tree(
			*result.rollback_copy_directory, layout.active_catalog_root,
			"catalog-rollback-restore-failed",
			"Full-catalog rollback copy could not restore the active "
			"catalog.")) {
		result.add_diagnostic(std::move(*restored));
		return false;
	}

	result.rollback_succeeded = true;
	if (result.parked_catalog_directory.has_value()) {
		result.old_active_cleanup_attempted = true;
		if (std::optional<core::Diagnostic> cleanup = remove_directory_tree(
				*result.parked_catalog_directory,
				"parked-catalog-cleanup-failed",
				"Parked pre-replacement catalog directory could not be "
				"removed.",
				core::DiagnosticSeverity::RecoverableWarning)) {
			result.add_diagnostic(std::move(*cleanup));
		}
	}
	return true;
}

void mark_rollback_outcome(CatalogReplacementResult& result,
						   bool rollback_succeeded) {
	if (rollback_succeeded) {
		result.status	= CatalogReplacementStatus::RolledBack;
		result.category = core::OperationResultCategory::ReplacementFailure;
		return;
	}

	result.status	= CatalogReplacementStatus::FatalRecoveryRequired;
	result.category = core::OperationResultCategory::ReplacementFailure;
}
}	 // namespace

std::string_view to_string(CatalogReplacementStatus status) noexcept {
	switch (status) {
		case CatalogReplacementStatus::Replaced:
			return "replaced";
		case CatalogReplacementStatus::Rejected:
			return "rejected";
		case CatalogReplacementStatus::RolledBack:
			return "rolled back";
		case CatalogReplacementStatus::FatalRecoveryRequired:
			return "fatal recovery required";
		case CatalogReplacementStatus::Cancelled:
			return "cancelled";
	}

	return "unknown catalog replacement status";
}

bool CatalogReplacementResult::succeeded() const noexcept {
	return status == CatalogReplacementStatus::Replaced
		   && category == core::OperationResultCategory::Success;
}

bool CatalogReplacementResult::failed() const noexcept {
	return !succeeded()
		   && category != core::OperationResultCategory::UserCancelled;
}

bool CatalogReplacementResult::fatal_recovery_required() const noexcept {
	return status == CatalogReplacementStatus::FatalRecoveryRequired;
}

void CatalogReplacementResult::add_diagnostic(core::Diagnostic diagnostic) {
	diagnostics.push_back(std::move(diagnostic));
}

CatalogReplacementUseCase::CatalogReplacementUseCase(
	core::IdentifierSource& identifier_source, const core::Clock& clock,
	core::OperationGate& operation_gate)
	: identifiers(identifier_source)
	, replacement_clock(clock)
	, gate(operation_gate) {}

CatalogReplacementResult CatalogReplacementUseCase::replace_with_staged_import(
	const CatalogReplacementRequest& request,
	platform::ProgressSink& progress_sink,
	platform::CancellationToken& cancellation_token) {
	CatalogReplacementResult result;
	const core::OperationIdentifier operation_id =
		identifiers.next_operation_identifier();
	platform::PlatformOperationStartResult operation_start =
		platform::try_start_platform_operation(
			gate,
			platform::PlatformOperationStartRequest{
				.operation_kind = core::OperationKind::CatalogReplacement,
				.operation_id	= operation_id,
				.operation_type =
					platform::ProgressOperationType::CatalogReplacement},
			progress_sink, cancellation_token);
	if (!operation_start.succeeded()) {
		result.category	   = operation_start.category;
		result.diagnostics = std::move(operation_start.diagnostics);
		return result;
	}

	platform::ScopedPlatformOperation& operation = *operation_start.operation;
	if (operation.cancellation_requested()) {
		mark_cancelled(result);
		return result;
	}

	operation.publish_progress(
		"catalog-replacement-validating",
		platform::ProgressMessageId::CatalogReplacementValidating,
		std::uint64_t{0}, std::nullopt,
		"Validating staged catalog before replacement.", true);
	result.staged_validation = validate_staged_catalog(
		request.staged_catalog_root, replacement_clock.now());
	append_diagnostics(result.diagnostics,
					   result.staged_validation.diagnostics);
	if (!result.staged_validation.import_allowed()) {
		result.category = core::OperationResultCategory::ValidationFailure;
		return result;
	}
	if (result.staged_validation.explicit_warning_required()
		&& !request.degraded_import_confirmed) {
		mark_rejected(
			result, core::OperationResultCategory::ValidationFailure,
			make_diagnostic(core::DiagnosticSeverity::ActionValidationError,
							"degraded-import-not-confirmed",
							"Degraded staged catalog import requires explicit "
							"warning confirmation."));
		return result;
	}
	if (!request.replacement_confirmed) {
		mark_rejected(
			result, core::OperationResultCategory::ValidationFailure,
			make_diagnostic(core::DiagnosticSeverity::ActionValidationError,
							"catalog-replacement-not-confirmed",
							"Catalog replacement requires explicit user "
							"confirmation after staged validation."));
		return result;
	}
	if (operation.cancellation_requested()) {
		mark_cancelled(result);
		return result;
	}

	const persistence::CatalogContainerLayout layout =
		persistence::make_catalog_container_layout(request.app_private_root);
	if (path_is_same_or_nested_under(layout.catalog_rollbacks_root,
									 layout.active_catalog_root)) {
		mark_rejected(
			result, core::OperationResultCategory::ValidationFailure,
			make_diagnostic(core::DiagnosticSeverity::ActionValidationError,
							"catalog-rollback-root-inside-active-root",
							"Full-catalog rollback root must live outside the "
							"active catalog root.",
							path_text(layout.catalog_rollbacks_root)));
		return result;
	}
	if (path_is_same_or_nested_under(request.staged_catalog_root,
									 layout.active_catalog_root)) {
		mark_rejected(
			result, core::OperationResultCategory::ValidationFailure,
			make_diagnostic(core::DiagnosticSeverity::ActionValidationError,
							"staged-catalog-inside-active-root",
							"Staged catalog root must not live inside the "
							"active catalog root.",
							path_text(request.staged_catalog_root)));
		return result;
	}

	if (std::optional<core::Diagnostic> rollback_root = ensure_directory(
			layout.catalog_rollbacks_root, "rollback-root-unavailable",
			"Full-catalog rollback root directory could not be created.")) {
		mark_rejected(result,
					  core::OperationResultCategory::DestinationUnavailable,
					  std::move(*rollback_root));
		return result;
	}
	if (std::optional<core::Diagnostic> operation_tmp = ensure_directory(
			layout.operation_tmp_root, "operation-tmp-unavailable",
			"Operation temporary directory could not be created.")) {
		mark_rejected(result,
					  core::OperationResultCategory::DestinationUnavailable,
					  std::move(*operation_tmp));
		return result;
	}

	result.critical_section_entered = true;
	operation.publish_progress(
		"catalog-replacement-rollback-copy",
		platform::ProgressMessageId::CatalogReplacementRollbackCopy,
		std::uint64_t{1}, std::uint64_t{5},
		"Creating full-catalog rollback copy.", false);

	core::Diagnostic active_status_diagnostic;
	const bool active_exists =
		directory_exists(layout.active_catalog_root, active_status_diagnostic);
	if (!active_status_diagnostic.code.empty()) {
		mark_rejected(result,
					  core::OperationResultCategory::DestinationUnavailable,
					  std::move(active_status_diagnostic));
		return result;
	}

	if (active_exists) {
		result.rollback_copy_directory = rollback_copy_path(
			layout, replacement_clock.now(), operation.context().operation_id);
		if (std::optional<core::Diagnostic> copied = copy_directory_tree(
				layout.active_catalog_root, *result.rollback_copy_directory,
				"catalog-rollback-copy-failed",
				"Current catalog could not be copied to the rollback root.")) {
			mark_rejected(
				result, core::OperationResultCategory::TemporaryStorageFailure,
				std::move(*copied));
			return result;
		}
		result.rollback_copy_created = true;
	} else {
		result.add_diagnostic(
			make_diagnostic(core::DiagnosticSeverity::RecoverableWarning,
							"active-catalog-missing-before-replacement",
							"No active catalog directory existed, so no "
							"rollback copy was created.",
							path_text(layout.active_catalog_root)));
	}

	const std::filesystem::path work_root =
		replacement_work_root(layout, operation.context().operation_id);
	if (std::optional<core::Diagnostic> removed_work = remove_directory_tree(
			work_root, "replacement-work-cleanup-failed",
			"Replacement work directory could not be prepared.")) {
		mark_rejected(result,
					  core::OperationResultCategory::TemporaryStorageFailure,
					  std::move(*removed_work));
		return result;
	}

	if (active_exists) {
		operation.publish_progress(
			"catalog-replacement-parking-active",
			platform::ProgressMessageId::CatalogReplacementParkingActive,
			std::uint64_t{2}, std::uint64_t{5},
			"Moving current active catalog aside.", false);
		result.parked_catalog_directory = parked_active_catalog_path(
			layout, operation.context().operation_id);
		if (std::optional<core::Diagnostic> parked = rename_directory(
				layout.active_catalog_root, *result.parked_catalog_directory,
				"catalog-active-park-failed",
				"Current active catalog could not be moved aside.")) {
			result.category = core::OperationResultCategory::ReplacementFailure;
			result.add_diagnostic(std::move(*parked));
			return result;
		}
		result.active_catalog_parked = true;
	}

	if (request.fault_mode
		== CatalogReplacementFaultMode::FailAfterActiveCatalogParked) {
		result.add_diagnostic(
			make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
							"catalog-replacement-injected-move-failure",
							"Fault injection stopped replacement after the "
							"active catalog was parked."));
		const bool rollback_ok = restore_from_rollback(result, request, layout);
		mark_rollback_outcome(result, rollback_ok);
		return result;
	}

	operation.publish_progress(
		"catalog-replacement-moving-staged",
		platform::ProgressMessageId::CatalogReplacementMovingStaged,
		std::uint64_t{3}, std::uint64_t{5},
		"Moving staged catalog into the active catalog root.", false);
	if (std::optional<core::Diagnostic> moved = rename_directory(
			request.staged_catalog_root, layout.active_catalog_root,
			"catalog-staged-move-failed",
			"Staged catalog could not replace the active catalog root.")) {
		result.add_diagnostic(std::move(*moved));
		const bool rollback_ok = restore_from_rollback(result, request, layout);
		mark_rollback_outcome(result, rollback_ok);
		return result;
	}
	result.staged_catalog_moved = true;

	operation.publish_progress(
		"catalog-replacement-loading",
		platform::ProgressMessageId::CatalogReplacementLoading,
		std::uint64_t{4}, std::uint64_t{5}, "Loading replaced active catalog.",
		false);
	if (request.fault_mode
			== CatalogReplacementFaultMode::ForceImportedLoadFatal
		|| request.fault_mode
			   == CatalogReplacementFaultMode::
				   ForceImportedLoadFatalAndFailRollbackRestore) {
		result.active_validation.load_status =
			persistence::CatalogLoadStatus::Fatal;
		result.active_validation.diagnostics.push_back(make_diagnostic(
			core::DiagnosticSeverity::FatalCatalogError,
			"catalog-imported-load-injected-failure",
			"Fault injection forced imported catalog load failure."));
	} else {
		result.active_validation = validate_staged_catalog(
			layout.active_catalog_root, replacement_clock.now());
	}
	append_diagnostics(result.diagnostics,
					   result.active_validation.diagnostics);
	if (!result.active_validation.import_allowed()) {
		const bool rollback_ok = restore_from_rollback(result, request, layout);
		mark_rollback_outcome(result, rollback_ok);
		return result;
	}

	if (result.parked_catalog_directory.has_value()) {
		result.old_active_cleanup_attempted = true;
		if (std::optional<core::Diagnostic> cleanup = remove_directory_tree(
				*result.parked_catalog_directory,
				"parked-catalog-cleanup-failed",
				"Parked pre-replacement catalog directory could not be "
				"removed.",
				core::DiagnosticSeverity::RecoverableWarning)) {
			result.add_diagnostic(std::move(*cleanup));
		}
	}

	result.rollback_retention_cleanup_attempted = true;
	if (std::optional<core::Diagnostic> retention = cleanup_rollback_retention(
			layout.catalog_rollbacks_root, request.rollback_retention)) {
		result.add_diagnostic(std::move(*retention));
	}

	result.status	= CatalogReplacementStatus::Replaced;
	result.category = core::OperationResultCategory::Success;
	operation.publish_progress(
		"catalog-replacement-done",
		platform::ProgressMessageId::CatalogReplacementCompleted,
		std::uint64_t{5}, std::uint64_t{5}, "Catalog replacement completed.",
		false);
	return result;
}
}	 // namespace shuba::catalog
