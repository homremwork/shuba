#include "Localization/Facade.hpp"
#include "UI/AppShell/Component.hpp"
#include "UI/AppShell/RouteCoordinator.hpp"
#include "UI/AppShell/ScreenRenderer.hpp"
#include "UI/AppShell/ContentComponent.hpp"
#include "UI/View/ScreenText.hpp"

#include "UI/Session/BackupRecoverySession.hpp"
#include "UI/Session/CatalogStartupSession.hpp"

#include <algorithm>
#include <span>
#include <string>
#include <string_view>

namespace shuba::ui {
namespace {
[[nodiscard]] bool contains_action(std::span<const RecoveryAction> actions,
								   RecoveryAction expected) {
	return std::ranges::find(actions, expected) != actions.end();
}
}	 // namespace

void Component::request_export_backup() {
	if (shell_operation.active())
		return;
	feedback.backup_diagnostics.clear();
	const std::string suggested_name =
		catalog::suggested_backup_file_name(edit_clock.now());
	const std::weak_ptr<CallbackLifetimeToken> callback_lifetime =
		document_picker_callback_lifetime;
	core::OperationResult destination_started =
		document_export_service.request_export_destination_selection(
			platform::DocumentExportRequest{
				.suggested_file_name = suggested_name,
				.mime_type = std::string{catalog::backup_zip_mime_type},
				.purpose   = "catalog backup ZIP export"},
			[this, callback_lifetime](platform::PlatformValueResult<
									  platform::DocumentDestinationDescriptor>
										  result) mutable {
		const std::optional<CallbackLifetimeLease> callback_lease =
			CallbackLifetimeLease::try_acquire(callback_lifetime);
		if (!callback_lease.has_value())
			return;
		if (result.was_user_cancelled()) {
			feedback.backup_message = localization.text(
				localization::MessageId::BackupExportDestinationCancelled);
			refresh_all();
			return;
		}
		if (result.failed()) {
			feedback.backup_message = localization.text(
				localization::MessageId::BackupExportDestinationFailed);
			feedback.backup_diagnostics = std::move(result.diagnostics);
			refresh_all();
			return;
		}
		const OperationRunner::Submission submission =
			shell_operation_runner->submit_backup_export(
				BackupExportSessionRequest{
					.current_session		 = session,
					.identifiers			 = edit_identifiers,
					.clock					 = edit_clock,
					.operation_gate			 = ui_operation_gate,
					.zip_archive_service	 = zip_archive_service,
					.document_export_service = document_export_service,
					.content_staging_service = content_staging_service,
					.destination			 = std::move(*result.value)},
				false,
				[this](OperationRunner::CompletionResult completion) {
			if (shell_operation.generation != completion.generation
				|| shell_operation.job_type != completion.job_type) {
				return;
			}
			shell_operation.state = ShellOperationState::Applying;
			apply_backup_export_result(std::get<BackupExportSessionResult>(
										   std::move(completion.value)),
									   false);
			complete_shell_operation();
		});
		if (!submission.accepted) {
			feedback.backup_message =
				localization.text(localization::MessageId::ShellOperationBusy);
			refresh_all();
			return;
		}
		begin_shell_operation(ShellOperationJobType::BackupExport,
							  submission.generation);
	});
	if (destination_started.failed()) {
		feedback.backup_message = localization.text(
			localization::MessageId::BackupExportPickerUnavailable);
		feedback.backup_diagnostics = destination_started.diagnostics();
		refresh_all();
	}
}

void Component::request_export_diagnostic_archive() {
	if (shell_operation.active())
		return;
	feedback.backup_diagnostics.clear();
	const std::string suggested_name =
		catalog::suggested_diagnostic_archive_file_name(edit_clock.now());
	const std::weak_ptr<CallbackLifetimeToken> callback_lifetime =
		document_picker_callback_lifetime;
	core::OperationResult destination_started =
		document_export_service.request_export_destination_selection(
			platform::DocumentExportRequest{
				.suggested_file_name = suggested_name,
				.mime_type = std::string{catalog::backup_zip_mime_type},
				.purpose   = "diagnostic archive ZIP export"},
			[this, callback_lifetime](platform::PlatformValueResult<
									  platform::DocumentDestinationDescriptor>
										  result) mutable {
		const std::optional<CallbackLifetimeLease> callback_lease =
			CallbackLifetimeLease::try_acquire(callback_lifetime);
		if (!callback_lease.has_value())
			return;
		if (result.was_user_cancelled()) {
			feedback.backup_message = localization.text(
				localization::MessageId::DiagnosticExportDestinationCancelled);
			refresh_all();
			return;
		}
		if (result.failed()) {
			feedback.backup_message = localization.text(
				localization::MessageId::DiagnosticExportDestinationFailed);
			feedback.backup_diagnostics = std::move(result.diagnostics);
			refresh_all();
			return;
		}
		const OperationRunner::Submission submission =
			shell_operation_runner->submit_backup_export(
				BackupExportSessionRequest{
					.current_session		 = session,
					.identifiers			 = edit_identifiers,
					.clock					 = edit_clock,
					.operation_gate			 = ui_operation_gate,
					.zip_archive_service	 = zip_archive_service,
					.document_export_service = document_export_service,
					.content_staging_service = content_staging_service,
					.destination			 = std::move(*result.value)},
				true,
				[this](OperationRunner::CompletionResult completion) {
			if (shell_operation.generation != completion.generation
				|| shell_operation.job_type != completion.job_type) {
				return;
			}
			shell_operation.state = ShellOperationState::Applying;
			apply_backup_export_result(std::get<BackupExportSessionResult>(
										   std::move(completion.value)),
									   true);
			complete_shell_operation();
		});
		if (!submission.accepted) {
			feedback.backup_message =
				localization.text(localization::MessageId::ShellOperationBusy);
			refresh_all();
			return;
		}
		begin_shell_operation(ShellOperationJobType::DiagnosticArchiveExport,
							  submission.generation);
	});
	if (destination_started.failed()) {
		feedback.backup_message = localization.text(
			localization::MessageId::DiagnosticExportPickerUnavailable);
		feedback.backup_diagnostics = destination_started.diagnostics();
		refresh_all();
	}
}

void Component::request_import_backup() {
	if (shell_operation.active())
		return;
	feedback.backup_diagnostics.clear();
	backup.pending_import_staging.reset();
	backup.pending_import_degraded_acknowledged = false;
	const std::weak_ptr<CallbackLifetimeToken> callback_lifetime =
		document_picker_callback_lifetime;
	core::OperationResult import_started =
		document_import_service.request_import_document_selection(
			platform::DocumentImportRequest{.accepted_mime_types = {std::string{
												catalog::backup_zip_mime_type}},
											.purpose = "backup ZIP import"},
			[this, callback_lifetime](
				platform::PlatformValueResult<platform::ContentSourceDescriptor>
					result) mutable {
		const std::optional<CallbackLifetimeLease> callback_lease =
			CallbackLifetimeLease::try_acquire(callback_lifetime);
		if (!callback_lease.has_value())
			return;
		if (result.was_user_cancelled()) {
			feedback.backup_message = localization.text(
				localization::MessageId::BackupImportSourceCancelled);
			refresh_all();
			return;
		}
		if (result.failed()) {
			feedback.backup_message = localization.text(
				localization::MessageId::BackupImportSourceFailed);
			feedback.backup_diagnostics = std::move(result.diagnostics);
			refresh_all();
			return;
		}
		const OperationRunner::Submission submission =
			shell_operation_runner->submit_backup_import_staging(
				BackupImportStagingSessionRequest{
					.current_session		 = session,
					.identifiers			 = edit_identifiers,
					.clock					 = edit_clock,
					.operation_gate			 = ui_operation_gate,
					.zip_archive_service	 = zip_archive_service,
					.document_export_service = document_export_service,
					.content_staging_service = content_staging_service,
					.source					 = std::move(*result.value)},
				[this](OperationRunner::CompletionResult completion) {
			if (shell_operation.generation != completion.generation
				|| shell_operation.job_type != completion.job_type) {
				return;
			}
			shell_operation.state = ShellOperationState::Applying;
			apply_backup_import_staging_result(
				std::get<BackupImportStagingSessionResult>(
					std::move(completion.value)));
			complete_shell_operation();
		});
		if (!submission.accepted) {
			feedback.backup_message =
				localization.text(localization::MessageId::ShellOperationBusy);
			refresh_all();
			return;
		}
		begin_shell_operation(ShellOperationJobType::BackupImportStaging,
							  submission.generation);
	});
	if (import_started.failed()) {
		feedback.backup_message = localization.text(
			localization::MessageId::BackupImportPickerUnavailable);
		feedback.backup_diagnostics = import_started.diagnostics();
		refresh_all();
	}
}

void Component::retry_normal_startup() {
	if (shell_operation.active())
		return;
	if (session.source != CatalogSessionStartupSource::StartupCrashSafeMode) {
		feedback.backup_message = localization.text(
			localization::MessageId::RetryNormalStartupUnavailable);
		refresh_all();
		return;
	}

	feedback.backup_diagnostics.clear();
	backup.pending_import_staging.reset();
	backup.pending_import_degraded_acknowledged = false;

	CatalogSessionState retry_session =
		load_guarded_catalog_session(GuardedCatalogSessionLoadRequest{
			.path_provider				   = path_provider,
			.identifiers				   = edit_identifiers,
			.clock						   = edit_clock,
			.app_version				   = app_version,
			.platform					   = platform_name,
			.debug_demo_seed_enabled	   = debug_demo_seed_enabled,
			.retry_requested_by_user	   = true,
			.android_previous_exit_service = &android_previous_exit_service});

	session = std::move(retry_session);
	invalidate_all_previews();
	route				 = RouteState{};
	catalog_filter_state = CatalogFilterState{};
	storage_detail		 = StorageDetailState{};
	item_form			 = ItemFormState{};
	storage_form		 = StorageFormState{};
	photo_display		 = PhotoDisplayState{};
	feedback.photo_message.clear();
	feedback.photo_diagnostics.clear();
	feedback.edit_message.clear();
	feedback.edit_diagnostics.clear();
	feedback.backup_message = localization.text(
		session.fatal()
			? localization::MessageId::RetryNormalStartupReachedRecovery
			: localization::MessageId::RetryNormalStartupCompleted);
	select_root(session.fatal() ? RootDestination::BackupRecovery
								: RootDestination::Catalog);
	clear_controlled_startup_attempt_marker();
}

void Component::apply_backup_export_result(
	BackupExportSessionResult result, bool diagnostic_archive) {
	feedback.backup_diagnostics = std::move(result.diagnostics);
	if (result.succeeded()) {
		feedback.backup_message = localization.text(
			diagnostic_archive
				? localization::MessageId::DiagnosticArchiveExportCompleted
				: localization::MessageId::BackupZipExportCompleted);
		if (result.degraded_backup_warning_required)
			feedback.backup_message +=
				" "
				+ localization.text(
					localization::MessageId::DegradedBackupPreserved);
	} else if (result.was_user_cancelled()) {
		feedback.backup_message = localization.text(
			diagnostic_archive
				? localization::MessageId::DiagnosticExportCancelled
				: localization::MessageId::BackupExportCancelled);
	} else {
		feedback.backup_message = localization.text(
			diagnostic_archive ? localization::MessageId::DiagnosticExportFailed
							   : localization::MessageId::BackupExportFailed);
	}
	refresh_all();
}

void Component::apply_backup_import_staging_result(
	BackupImportStagingSessionResult result) {
	feedback.backup_diagnostics = std::move(result.diagnostics);
	if (result.succeeded()) {
		backup.pending_import_staging = std::move(result.staging_result);
		feedback.backup_diagnostics =
			backup.pending_import_staging->diagnostics;
		backup.pending_import_degraded_acknowledged = false;
		feedback.backup_message						= localization.text(
			backup.pending_import_staging->validation
					.explicit_warning_required()
				? localization::MessageId::BackupImportValidatedDegraded
				: localization::MessageId::BackupImportValidated);
	} else if (result.was_user_cancelled()) {
		feedback.backup_message = localization.text(
			localization::MessageId::BackupImportStagingCancelled);
	} else {
		feedback.backup_diagnostics = result.staging_result.diagnostics;
		backup.pending_import_staging.reset();
		feedback.backup_message =
			localization.text(localization::MessageId::BackupImportRejected);
	}
	if (route_coordinator != nullptr)
		route_coordinator->select_root(RootDestination::BackupRecovery);
}

void Component::confirm_staged_backup_import() {
	if (shell_operation.active())
		return;
	if (!backup.pending_import_staging
		|| !backup.pending_import_staging->staging_catalog_root.has_value()) {
		feedback.backup_message = localization.text(
			localization::MessageId::RecoveryImportNoStagedBackup);
		refresh_all();
		return;
	}
	const bool degraded =
		backup.pending_import_staging->validation.explicit_warning_required();
	if (degraded && !backup.pending_import_degraded_acknowledged) {
		backup.pending_import_degraded_acknowledged = true;
		feedback.backup_message						= localization.text(
			localization::MessageId::DegradedImportWarningAcknowledged);
		refresh_all();
		return;
	}
	const OperationRunner::Submission submission =
		shell_operation_runner->submit_backup_import_replacement(
			BackupImportReplacementSessionRequest{
				.current_session = session,
				.identifiers	 = edit_identifiers,
				.clock			 = edit_clock,
				.operation_gate	 = ui_operation_gate,
				.staged_catalog_root =
					*backup.pending_import_staging->staging_catalog_root,
				.replacement_confirmed	   = true,
				.degraded_import_confirmed = degraded},
			[this](OperationRunner::CompletionResult completion) {
		if (shell_operation.generation != completion.generation
			|| shell_operation.job_type != completion.job_type) {
			return;
		}
		shell_operation.state = ShellOperationState::Applying;
		apply_backup_import_replacement_result(
			std::get<BackupImportReplacementSessionResult>(
				std::move(completion.value)));
		complete_shell_operation();
	});
	if (!submission.accepted) {
		feedback.backup_message =
			localization.text(localization::MessageId::ShellOperationBusy);
		refresh_all();
		return;
	}
	begin_shell_operation(ShellOperationJobType::BackupImportReplacement,
						  submission.generation);
}

void Component::apply_backup_import_replacement_result(
	BackupImportReplacementSessionResult result) {
	feedback.backup_diagnostics = std::move(result.diagnostics);
	if (result.succeeded()) {
		session = std::move(result.session);
		preview_cache.clear();
		backup.pending_import_staging.reset();
		backup.pending_import_degraded_acknowledged = false;
		feedback.backup_message =
			localization.text(localization::MessageId::BackupImportCompleted);
	} else if (result.was_user_cancelled()) {
		feedback.backup_message = localization.text(
			localization::MessageId::BackupImportReplacementCancelled);
	} else if (result.fatal_recovery_required) {
		session = std::move(result.session);
		preview_cache.clear();
		backup.pending_import_staging.reset();
		feedback.backup_message = localization.text(
			localization::MessageId::FatalReplacementRecoveryRequired);
	} else {
		feedback.backup_message =
			localization.text(localization::MessageId::BackupReplacementFailed);
	}
	if (route_coordinator != nullptr)
		route_coordinator->select_root(RootDestination::BackupRecovery);
}

void ScreenRenderer::build_add_content() {
	const bool mutation_allowed = !shell_operation_state.active();
	content->add_label(
		juce_text(localization.text(localization::MessageId::AddDescription)),
		70, panel_colour(), true);
	juce::Button& item = content->add_button(
		localization.text(localization::MessageId::TitleAddItem), 52);
	item.onClick = [this] { open_new_item_form(std::nullopt); };
	item.setEnabled(mutation_allowed);
	juce::Button& storage = content->add_button(
		localization.text(localization::MessageId::TitleAddStorage), 52);
	storage.onClick = [this] { open_new_storage_form(std::nullopt); };
	storage.setEnabled(mutation_allowed);
}

void ScreenRenderer::build_backup_recovery_content() {
	const bool mutation_allowed			   = !shell_operation_state.active();
	const CatalogRecoveryUiSummary summary = make_recovery_ui_summary(session);
	content->add_label(juce_text(recovery_summary(summary, localization)), 86,
					   summary.fatal() || summary.degraded()
						   ? warning_panel_colour()
						   : panel_colour(),
					   true);
	content->add_label(
		juce_text(recovery_counts_summary(summary, localization)), 72,
		surface_colour(), true);
	content->add_label(
		juce_text(recovery_action_summary(summary.safe_actions, localization)),
		62, panel_colour());
	content->add_label(juce_text(localization.text(
						   localization::MessageId::RecoveryZipWarning)),
					   70, warning_panel_colour(), true);
	if (session.degraded()) {
		content->add_label(
			juce_text(localization.text(
				localization::MessageId::RecoveryDegradedBackupGuidance)),
			82, warning_panel_colour(), true);
	}
	if (session.fatal()) {
		content->add_label(juce_text(localization.text(
							   localization::MessageId::RecoveryFatalGuidance)),
						   82, warning_panel_colour(), true);
	}

	if (!feedback.backup_message.empty()) {
		content->add_label(juce_text(feedback.backup_message), 66,
						   accent_colour().withAlpha(0.34f), true);
	}
	if (has_diagnostics(feedback.backup_diagnostics)) {
		content->add_label(
			juce_text(core_diagnostic_summary(feedback.backup_diagnostics)), 86,
			warning_panel_colour(), true);
	}
	juce::Button& backup_button = content->add_button(
		localization.text(localization::MessageId::BackupExportButton), 46);
	backup_button.setEnabled(!session.fatal() && mutation_allowed);
	backup_button.onClick	 = [this] { request_export_backup(); };
	juce::Button& diagnostic = content->add_button(
		localization.text(localization::MessageId::DiagnosticExportButton), 46);
	diagnostic.setEnabled(session.paths.has_value() && mutation_allowed);
	diagnostic.onClick	 = [this] { request_export_diagnostic_archive(); };
	juce::Button& import = content->add_button(
		localization.text(localization::MessageId::ImportBackupButton), 46);
	import.setEnabled(session.paths.has_value() && mutation_allowed);
	import.onClick = [this] { request_import_backup(); };
	if (contains_action(summary.safe_actions,
						RecoveryAction::RetryNormalLaunch)) {
		content->add_label(juce_text(localization.text(
							   localization::MessageId::RecoveryRetryGuidance)),
						   78, warning_panel_colour(), true);
		juce::Button& retry = content->add_button(
			localization.text(localization::MessageId::RetryNormalLaunchButton),
			46);
		retry.onClick = [this] { retry_normal_startup(); };
		retry.setEnabled(mutation_allowed);
	}

	if (backup.pending_import_staging) {
		content->add_label(
			juce_text(import_validation_summary(
				backup.pending_import_staging->validation, localization)),
			76, surface_colour(), true);
		if (backup.pending_import_staging->validation
				.explicit_warning_required()) {
			content->add_label(
				backup.pending_import_degraded_acknowledged
					? localization.text(localization::MessageId::
											RecoveryDegradedImportAcknowledged)
					: localization.text(localization::MessageId::
											RecoveryDegradedImportConfirmation),
				78, warning_panel_colour(), true);
		}
		juce::Button& confirm = content->add_button(
			backup.pending_import_staging->validation
						.explicit_warning_required()
					&& !backup.pending_import_degraded_acknowledged
				? localization.text(
					  localization::MessageId::AcknowledgeDegradedImport)
				: localization.text(localization::MessageId::
										ConfirmValidatedBackupReplacement),
			48);
		confirm.onClick = [this] { confirm_staged_backup_import(); };
		confirm.setEnabled(mutation_allowed);
		juce::Button& clear = content->add_button(
			localization.text(localization::MessageId::CancelStagedImport), 42);
		clear.onClick = [this] {
			backup.pending_import_staging.reset();
			backup.pending_import_degraded_acknowledged = false;
			feedback.backup_message						= localization.text(
				localization::MessageId::ValidatedStagedImportCleared);
			refresh_all();
		};
		clear.setEnabled(mutation_allowed);
	}

	if (!summary.technical_details.empty()) {
		content->add_label(
			juce_text(localization.technical_information_heading()), 38,
			panel_colour(), true);
		std::string details;
		const std::size_t max_entries =
			std::min<std::size_t>(summary.technical_details.size(), 6U);
		for (std::size_t index = 0; index < max_entries; ++index) {
			if (!details.empty())
				details += "\n";
			details += summary.technical_details[index];
		}
		content->add_label(juce_text(details), 124, panel_colour());
	}
}

void ScreenRenderer::build_more_content() {
	const bool mutation_allowed = !shell_operation_state.active();
	content->add_label(
		juce_text(localization.text(localization::MessageId::MoreDescription)),
		82, panel_colour(), true);
	juce::Button& maintenance = content->add_button(
		localization.text(localization::MessageId::MoreOpenRecovery), 52);
	maintenance.onClick = [this] {
		select_root(RootDestination::BackupRecovery);
	};
	maintenance.setEnabled(mutation_allowed);
	if (session.degraded()) {
		content->add_label(juce_text(localization.text(
							   localization::MessageId::MoreDegradedGuidance)),
						   78, warning_panel_colour(), true);
	}
	content->add_label(
		juce_text(localization.text(localization::MessageId::MoreScopeNote)),
		82);
}
}	 // namespace shuba::ui
