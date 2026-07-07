#include "UI/AppShell.hpp"
#include "UI/Screens/AppShellScreenRenderer.hpp"
#include "UI/View/AppShellContentComponent.hpp"
#include "UI/View/ScreenText.hpp"

#include "UI/Session/BackupRecoverySession.hpp"
#include "UI/Session/CatalogStartupSession.hpp"

#include <algorithm>
#include <span>
#include <string>
#include <string_view>

namespace shuba::ui {
namespace {
[[nodiscard]] bool contains_action(std::span<const std::string> actions,
								   std::string_view label) {
	return std::ranges::any_of(actions, [label](const std::string& action) {
		return action == label;
	});
}
}	 // namespace

void AppShellComponent::request_export_backup() {
	last_progress_events.clear();
	feedback.backup_diagnostics.clear();
	const std::string suggested_name =
		catalog::suggested_backup_file_name(edit_clock.now());
	core::OperationResult destination_started =
		document_export_service.request_export_destination_selection(
			platform::DocumentExportRequest{
				.suggested_file_name = suggested_name,
				.mime_type = std::string{catalog::backup_zip_mime_type},
				.purpose   = "catalog backup ZIP export"},
			[this](platform::PlatformValueResult<
				   platform::DocumentDestinationDescriptor>
					   result) mutable {
		if (result.was_user_cancelled()) {
			feedback.backup_message = "Backup export destination cancelled.";
			refresh_all();
			return;
		}
		if (result.failed()) {
			feedback.backup_message		= "Backup export destination failed.";
			feedback.backup_diagnostics = std::move(result.diagnostics);
			refresh_all();
			return;
		}
		BackupExportSessionResult exported = export_backup_from_session(
			BackupExportSessionRequest{
				.current_session		 = session,
				.identifiers			 = edit_identifiers,
				.clock					 = edit_clock,
				.operation_gate			 = ui_operation_gate,
				.zip_archive_service	 = zip_archive_service,
				.document_export_service = document_export_service,
				.content_staging_service = content_staging_service,
				.destination			 = std::move(*result.value)},
			last_progress_events, never_cancelled);
		apply_backup_export_result(std::move(exported), false);
	});
	if (destination_started.failed()) {
		feedback.backup_message =
			"Backup export destination picker could not be opened.";
		feedback.backup_diagnostics = destination_started.diagnostics();
		refresh_all();
	}
}

void AppShellComponent::request_export_diagnostic_archive() {
	last_progress_events.clear();
	feedback.backup_diagnostics.clear();
	const std::string suggested_name =
		catalog::suggested_diagnostic_archive_file_name(edit_clock.now());
	core::OperationResult destination_started =
		document_export_service.request_export_destination_selection(
			platform::DocumentExportRequest{
				.suggested_file_name = suggested_name,
				.mime_type = std::string{catalog::backup_zip_mime_type},
				.purpose   = "diagnostic archive ZIP export"},
			[this](platform::PlatformValueResult<
				   platform::DocumentDestinationDescriptor>
					   result) mutable {
		if (result.was_user_cancelled()) {
			feedback.backup_message =
				"Diagnostic archive destination cancelled.";
			refresh_all();
			return;
		}
		if (result.failed()) {
			feedback.backup_message = "Diagnostic archive destination failed.";
			feedback.backup_diagnostics = std::move(result.diagnostics);
			refresh_all();
			return;
		}
		BackupExportSessionResult exported =
			export_diagnostic_archive_from_session(
				BackupExportSessionRequest{
					.current_session		 = session,
					.identifiers			 = edit_identifiers,
					.clock					 = edit_clock,
					.operation_gate			 = ui_operation_gate,
					.zip_archive_service	 = zip_archive_service,
					.document_export_service = document_export_service,
					.content_staging_service = content_staging_service,
					.destination			 = std::move(*result.value)},
				last_progress_events, never_cancelled);
		apply_backup_export_result(std::move(exported), true);
	});
	if (destination_started.failed()) {
		feedback.backup_message =
			"Diagnostic archive destination picker could not be opened.";
		feedback.backup_diagnostics = destination_started.diagnostics();
		refresh_all();
	}
}

void AppShellComponent::request_import_backup() {
	last_progress_events.clear();
	feedback.backup_diagnostics.clear();
	backup.pending_import_staging.reset();
	backup.pending_import_degraded_acknowledged = false;
	core::OperationResult import_started =
		document_import_service.request_import_document_selection(
			platform::DocumentImportRequest{.accepted_mime_types = {std::string{
												catalog::backup_zip_mime_type}},
											.purpose = "backup ZIP import"},
			[this](
				platform::PlatformValueResult<platform::ContentSourceDescriptor>
					result) mutable {
		if (result.was_user_cancelled()) {
			feedback.backup_message =
				"Backup import source selection cancelled.";
			refresh_all();
			return;
		}
		if (result.failed()) {
			feedback.backup_message = "Backup import source selection failed.";
			feedback.backup_diagnostics = std::move(result.diagnostics);
			refresh_all();
			return;
		}
		BackupImportStagingSessionResult staged =
			stage_backup_import_for_session(
				BackupImportStagingSessionRequest{
					.current_session		 = session,
					.identifiers			 = edit_identifiers,
					.clock					 = edit_clock,
					.operation_gate			 = ui_operation_gate,
					.zip_archive_service	 = zip_archive_service,
					.document_export_service = document_export_service,
					.content_staging_service = content_staging_service,
					.source					 = std::move(*result.value)},
				last_progress_events, never_cancelled);
		apply_backup_import_staging_result(std::move(staged));
	});
	if (import_started.failed()) {
		feedback.backup_message = "Backup import picker could not be opened.";
		feedback.backup_diagnostics = import_started.diagnostics();
		refresh_all();
	}
}

void AppShellComponent::retry_normal_startup() {
	if (session.source != CatalogSessionStartupSource::StartupCrashSafeMode) {
		feedback.backup_message =
			"Retry normal launch is available only from startup safe mode.";
		refresh_all();
		return;
	}

	last_progress_events.clear();
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
	route				 = AppShellRouteState{};
	catalog_filter_state = AppShellCatalogFilterState{};
	storage_detail		 = AppShellStorageDetailState{};
	item_form			 = AppShellItemFormState{};
	storage_form		 = AppShellStorageFormState{};
	photo_display		 = AppShellPhotoDisplayState{};
	feedback.photo_message.clear();
	feedback.photo_diagnostics.clear();
	feedback.edit_message.clear();
	feedback.edit_diagnostics.clear();
	feedback.backup_message =
		session.fatal() ? "Normal startup retry reached recovery state."
						: "Normal startup retry completed.";
	select_root(session.fatal() ? RootDestination::BackupRecovery
								: RootDestination::Catalog);
	clear_controlled_startup_attempt_marker();
}

void AppShellComponent::apply_backup_export_result(
	BackupExportSessionResult result, bool diagnostic_archive) {
	feedback.backup_diagnostics = std::move(result.diagnostics);
	if (result.succeeded()) {
		feedback.backup_message = diagnostic_archive
									  ? "Diagnostic archive export completed."
									  : "Backup ZIP export completed.";
		if (result.degraded_backup_warning_required)
			feedback.backup_message +=
				" Degraded catalog state was preserved as raw files.";
	} else if (result.was_user_cancelled()) {
		feedback.backup_message = diagnostic_archive
									  ? "Diagnostic export cancelled."
									  : "Backup export cancelled.";
	} else {
		feedback.backup_message = diagnostic_archive
									  ? "Diagnostic export failed."
									  : "Backup export failed.";
	}
	refresh_all();
}

void AppShellComponent::apply_backup_import_staging_result(
	BackupImportStagingSessionResult result) {
	feedback.backup_diagnostics = std::move(result.diagnostics);
	if (result.succeeded()) {
		backup.pending_import_staging = std::move(result.staging_result);
		feedback.backup_diagnostics =
			backup.pending_import_staging->diagnostics;
		backup.pending_import_degraded_acknowledged = false;
		feedback.backup_message =
			backup.pending_import_staging->validation
					.explicit_warning_required()
				? "Backup ZIP validated as degraded. Review summary and "
				  "confirm degraded import before replacement."
				: "Backup ZIP validated. Confirm replacement to import.";
	} else if (result.was_user_cancelled()) {
		feedback.backup_message = "Backup import staging cancelled.";
	} else {
		feedback.backup_diagnostics = result.staging_result.diagnostics;
		backup.pending_import_staging.reset();
		feedback.backup_message = "Backup import rejected before replacement.";
	}
	select_root(RootDestination::BackupRecovery);
}

void AppShellComponent::confirm_staged_backup_import() {
	if (!backup.pending_import_staging
		|| !backup.pending_import_staging->staging_catalog_root.has_value()) {
		feedback.backup_message =
			"No validated staged backup is ready to import.";
		refresh_all();
		return;
	}
	const bool degraded =
		backup.pending_import_staging->validation.explicit_warning_required();
	if (degraded && !backup.pending_import_degraded_acknowledged) {
		backup.pending_import_degraded_acknowledged = true;
		feedback.backup_message =
			"Degraded import warning acknowledged. Press confirm again to "
			"replace the current catalog.";
		refresh_all();
		return;
	}
	last_progress_events.clear();
	BackupImportReplacementSessionResult replaced =
		replace_session_with_staged_import(
			BackupImportReplacementSessionRequest{
				.current_session = session,
				.identifiers	 = edit_identifiers,
				.clock			 = edit_clock,
				.operation_gate	 = ui_operation_gate,
				.staged_catalog_root =
					*backup.pending_import_staging->staging_catalog_root,
				.replacement_confirmed	   = true,
				.degraded_import_confirmed = degraded},
			last_progress_events, never_cancelled);
	apply_backup_import_replacement_result(std::move(replaced));
}

void AppShellComponent::apply_backup_import_replacement_result(
	BackupImportReplacementSessionResult result) {
	feedback.backup_diagnostics = std::move(result.diagnostics);
	if (result.succeeded()) {
		session = std::move(result.session);
		preview_cache.clear();
		backup.pending_import_staging.reset();
		backup.pending_import_degraded_acknowledged = false;
		feedback.backup_message =
			"Backup import completed and catalog reloaded.";
	} else if (result.was_user_cancelled()) {
		feedback.backup_message = "Backup import replacement cancelled.";
	} else if (result.fatal_recovery_required) {
		session = std::move(result.session);
		preview_cache.clear();
		backup.pending_import_staging.reset();
		feedback.backup_message =
			"Catalog replacement failed and rollback failed. Fatal recovery "
			"actions are required.";
	} else {
		feedback.backup_message =
			"Backup replacement failed; current catalog was not replaced or "
			"was rolled back.";
	}
	select_root(RootDestination::BackupRecovery);
}

void AppShellScreenRenderer::build_add_content() {
	content->add_label(
		"Create metadata first. Photo import and previews are available "
		"from item or storage details. Backup/import is in More.",
		70, panel_colour(), true);
	juce::Button& item	  = content->add_button("Add item", 52);
	item.onClick		  = [this] { open_new_item_form(std::nullopt); };
	juce::Button& storage = content->add_button("Add storage", 52);
	storage.onClick		  = [this] { open_new_storage_form(std::nullopt); };
}

void AppShellScreenRenderer::build_backup_recovery_content() {
	const CatalogRecoveryUiSummary summary = make_recovery_ui_summary(session);
	content->add_label(juce_text(summary.plain_summary_message), 86,
					   summary.fatal() || summary.degraded()
						   ? warning_panel_colour()
						   : panel_colour(),
					   true);
	content->add_label(juce_text(recovery_counts_summary(summary)), 72,
					   surface_colour(), true);
	content->add_label(juce_text(recovery_action_summary(summary.safe_actions)),
					   62, panel_colour());
	content->add_label(
		"ZIP exports are unencrypted and may contain photos, notes, tags, "
		"listing data, and finance values.",
		70, warning_panel_colour(), true);
	if (session.degraded()) {
		content->add_label(
			"Degraded normal backup preserves raw canonical metadata files and "
			"readable media. Export a diagnostic archive as a companion if "
			"manual repair is needed.",
			82, warning_panel_colour(), true);
	}
	if (session.fatal()) {
		content->add_label(
			"Fatal recovery never overwrites data automatically. Import backup "
			"still uses staging, validation, and explicit replacement "
			"confirmation.",
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
	content->add_label(
		juce_text(progress_summary(last_progress_events.events())), 54,
		panel_colour());

	juce::Button& backup_button =
		content->add_button("Export normal backup ZIP", 46);
	backup_button.setEnabled(!session.fatal());
	backup_button.onClick = [this] { request_export_backup(); };
	juce::Button& diagnostic =
		content->add_button("Export diagnostic archive ZIP", 46);
	diagnostic.setEnabled(session.paths.has_value());
	diagnostic.onClick = [this] { request_export_diagnostic_archive(); };
	juce::Button& import =
		content->add_button("Import backup ZIP: select and validate", 46);
	import.setEnabled(session.paths.has_value());
	import.onClick = [this] { request_import_backup(); };
	if (contains_action(summary.safe_actions, "Retry normal launch")) {
		content->add_label(
			"Retry normal launch is explicit and one-shot. Startup cleanup and "
			"normal catalog loading run only after this action.",
			78, warning_panel_colour(), true);
		juce::Button& retry = content->add_button("Retry normal launch", 46);
		retry.onClick		= [this] { retry_normal_startup(); };
	}

	if (backup.pending_import_staging) {
		content->add_label(juce_text(import_validation_summary(
							   backup.pending_import_staging->validation)),
						   76, surface_colour(), true);
		if (backup.pending_import_staging->validation
				.explicit_warning_required()) {
			content->add_label(
				backup.pending_import_degraded_acknowledged
					? "Degraded import warning is acknowledged. Confirming now "
					  "will replace the active catalog."
					: "Degraded staged import can replace current data only "
					  "after explicit warning acknowledgement.",
				78, warning_panel_colour(), true);
		}
		juce::Button& confirm = content->add_button(
			backup.pending_import_staging->validation
						.explicit_warning_required()
					&& !backup.pending_import_degraded_acknowledged
				? "Acknowledge degraded import warning"
				: "Confirm replacement with validated backup",
			48);
		confirm.onClick		= [this] { confirm_staged_backup_import(); };
		juce::Button& clear = content->add_button("Cancel staged import", 42);
		clear.onClick		= [this] {
			backup.pending_import_staging.reset();
			backup.pending_import_degraded_acknowledged = false;
			feedback.backup_message =
				"Validated staged import cleared. Active catalog unchanged.";
			refresh_all();
		};
	}

	if (!summary.technical_details.empty()) {
		content->add_label("Technical report", 38, panel_colour(), true);
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

void AppShellScreenRenderer::build_more_content() {
	content->add_label(
		"Maintenance hub for manual unencrypted ZIP backup, staged import, "
		"diagnostics, and recovery.",
		82, panel_colour(), true);
	juce::Button& maintenance =
		content->add_button("Open backup/import/recovery", 52);
	maintenance.onClick = [this] {
		select_root(RootDestination::BackupRecovery);
	};
	if (session.degraded()) {
		content->add_label(
			"Degraded load: valid records remain usable. Open recovery to "
			"review counts, export backup, or export diagnostic archive.",
			78, warning_panel_colour(), true);
	}
	content->add_label(
		"No cloud, accounts, marketplace automation, SQL, broad "
		"media permissions, or Java/Kotlin business logic is added.",
		82);
}
}	 // namespace shuba::ui
