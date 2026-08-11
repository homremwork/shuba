#include "Localization/MessageCatalog.hpp"

#include "UI/Session/CatalogSessionState.hpp"
#include "UI/Session/PhotoSessionTypes.hpp"

#include <array>

namespace shuba::localization::detail {
namespace {
struct RecoveryActionMessage final {
	ui::RecoveryAction action;
	MessageId message;
};

constexpr std::array recovery_action_messages{
	RecoveryActionMessage{ui::RecoveryAction::ExportNormalBackup,
						  MessageId::RecoveryActionExportNormalBackup},
	RecoveryActionMessage{ui::RecoveryAction::ExportDiagnosticArchive,
						  MessageId::RecoveryActionExportDiagnosticArchive},
	RecoveryActionMessage{ui::RecoveryAction::ImportBackup,
						  MessageId::RecoveryActionImportBackup},
	RecoveryActionMessage{ui::RecoveryAction::ShowTechnicalReport,
						  MessageId::RecoveryActionShowTechnicalReport},
	RecoveryActionMessage{
		ui::RecoveryAction::ContinueUsingAcceptedRecords,
		MessageId::RecoveryActionContinueUsingAcceptedRecords},
	RecoveryActionMessage{ui::RecoveryAction::RetryNormalLaunch,
						  MessageId::RecoveryActionRetryNormalLaunch},
	RecoveryActionMessage{ui::RecoveryAction::Exit,
						  MessageId::RecoveryActionExit},
};

constexpr std::array static_message_table{
	StaticMessage{MessageId::Save, "common.action.save", "Save"},
	StaticMessage{MessageId::Cancel, "common.action.cancel", "Cancel"},
	StaticMessage{MessageId::Close, "common.action.close", "Close"},
	StaticMessage{MessageId::Filters, "common.action.filters", "Filters"},
	StaticMessage{MessageId::ClearFilters, "catalog.clear_filters",
				  "Clear filters"},
	StaticMessage{MessageId::SearchCatalog, "chrome.search.catalog",
				  "Search catalog"},
	StaticMessage{MessageId::SearchStorages, "chrome.search.storages",
				  "Search storages"},
	StaticMessage{MessageId::Clear, "common.action.clear", "Clear"},
	StaticMessage{MessageId::ApplyFilters, "catalog.filters.apply", "Apply"},
	StaticMessage{MessageId::StorageClear, "storage.action.clear", "Clear"},
	StaticMessage{MessageId::Back, "common.action.back", "Back"},
	StaticMessage{MessageId::NavigationCatalog, "shell.navigation.catalog",
				  "Catalog"},
	StaticMessage{MessageId::NavigationStorages, "shell.navigation.storages",
				  "Storages"},
	StaticMessage{MessageId::NavigationAdd, "shell.navigation.add", "Add"},
	StaticMessage{MessageId::NavigationMore, "shell.navigation.more", "More"},
	StaticMessage{MessageId::TitleItemDetail, "shell.title.item_detail",
				  "Item detail"},
	StaticMessage{MessageId::TitleStorageDetail, "shell.title.storage_detail",
				  "Storage detail"},
	StaticMessage{MessageId::TitleAddItem, "shell.title.add_item", "Add item"},
	StaticMessage{MessageId::TitleEditItem, "shell.title.edit_item",
				  "Edit item"},
	StaticMessage{MessageId::TitleAddStorage, "shell.title.add_storage",
				  "Add storage"},
	StaticMessage{MessageId::TitleEditStorage, "shell.title.edit_storage",
				  "Edit storage"},
	StaticMessage{MessageId::TitlePhotoViewer, "shell.title.photo_viewer",
				  "Photo viewer"},
	StaticMessage{MessageId::TitleFatalRecovery, "shell.title.fatal_recovery",
				  "Fatal recovery"},
	StaticMessage{MessageId::TitleBackupRecovery, "shell.title.backup_recovery",
				  "Backup and recovery"},
	StaticMessage{MessageId::RecoverySummaryFatal, "recovery.summary.fatal",
				  "Startup recovery is required. Export diagnostics or import "
				  "a backup before retrying normal browsing."},
	StaticMessage{
		MessageId::RecoverySummaryDegraded, "recovery.summary.degraded",
		"Some records or media could not be loaded. Accepted records remain "
		"available, and backup/export can preserve the raw damaged state."},
	StaticMessage{MessageId::RecoverySummaryNormal, "recovery.summary.normal",
				  "Catalog loaded normally. Backup export and staged import "
				  "are available from maintenance actions."},
	StaticMessage{MessageId::RecoveryActions, "recovery.actions",
				  "Safe recovery actions: {actions}"},
	StaticMessage{MessageId::RecoveryActionExportNormalBackup,
				  "recovery.action.export_normal", "Export normal backup"},
	StaticMessage{MessageId::RecoveryActionExportDiagnosticArchive,
				  "recovery.action.export_diagnostic",
				  "Export diagnostic archive"},
	StaticMessage{MessageId::RecoveryActionImportBackup,
				  "recovery.action.import_backup", "Import backup ZIP"},
	StaticMessage{MessageId::RecoveryActionShowTechnicalReport,
				  "recovery.action.show_technical", "Show technical report"},
	StaticMessage{MessageId::RecoveryActionContinueUsingAcceptedRecords,
				  "recovery.action.continue_accepted",
				  "Continue using accepted records"},
	StaticMessage{MessageId::RecoveryActionRetryNormalLaunch,
				  "recovery.action.retry_normal", "Retry normal launch"},
	StaticMessage{MessageId::RecoveryActionExit, "recovery.action.exit",
				  "Exit"},
	StaticMessage{
		MessageId::RecoveryCounts, "recovery.counts",
		"Accepted: items={accepted_items} · storages={accepted_storages} · "
		"photos={accepted_photos} · skipped lines: items={skipped_items} "
		"storages={skipped_storages} photos={skipped_photos} · broken "
		"refs={broken_references} · orphan media={orphan_media}"},
	StaticMessage{
		MessageId::ImportValidationSummary, "backup.import.validation",
		"Staged import: {load_status} · items={accepted_items} · "
		"storages={accepted_storages} · photos={accepted_photos} · broken "
		"refs={broken_references} · orphan media={orphan_media}"},
	StaticMessage{
		MessageId::AddDescription, "shell.add.description",
		"Create metadata first. Photo import and previews are available from "
		"item or storage details. Backup/import is in More."},
	StaticMessage{MessageId::MoreDescription, "shell.more.description",
				  "Maintenance hub for manual unencrypted ZIP backup, staged "
				  "import, diagnostics, and recovery."},
	StaticMessage{MessageId::MoreOpenRecovery, "shell.more.open_recovery",
				  "Open backup/import/recovery"},
	StaticMessage{
		MessageId::MoreDegradedGuidance, "shell.more.degraded_guidance",
		"Degraded load: valid records remain usable. Open recovery to review "
		"counts, export backup, or export diagnostic archive."},
	StaticMessage{MessageId::MoreScopeNote, "shell.more.scope_note",
				  "No cloud, accounts, marketplace automation, SQL, broad "
				  "media permissions, or Java/Kotlin business logic is added."},
	StaticMessage{MessageId::BackupExportButton, "backup.action.export_normal",
				  "Export normal backup ZIP"},
	StaticMessage{MessageId::DiagnosticExportButton,
				  "backup.action.export_diagnostic",
				  "Export diagnostic archive ZIP"},
	StaticMessage{MessageId::ImportBackupButton, "backup.action.import",
				  "Import backup ZIP: select and validate"},
	StaticMessage{MessageId::RetryNormalLaunchButton,
				  "recovery.action.retry_launch", "Retry normal launch"},
	StaticMessage{MessageId::AcknowledgeDegradedImport,
				  "backup.action.ack_degraded",
				  "Acknowledge degraded import warning"},
	StaticMessage{MessageId::ConfirmValidatedBackupReplacement,
				  "backup.action.confirm_replacement",
				  "Confirm replacement with validated backup"},
	StaticMessage{MessageId::CancelStagedImport, "backup.action.cancel_staged",
				  "Cancel staged import"},
	StaticMessage{MessageId::RecoveryZipWarning, "backup.warning.unencrypted",
				  "ZIP exports are unencrypted and may contain photos, notes, "
				  "tags, listing data, and finance values."},
	StaticMessage{MessageId::RecoveryDegradedBackupGuidance,
				  "backup.warning.degraded",
				  "Degraded normal backup preserves raw canonical metadata "
				  "files and readable media. Export a diagnostic archive as a "
				  "companion if manual repair is needed."},
	StaticMessage{MessageId::RecoveryFatalGuidance, "recovery.warning.fatal",
				  "Fatal recovery never overwrites data automatically. Import "
				  "backup still uses staging, validation, and explicit "
				  "replacement confirmation."},
	StaticMessage{
		MessageId::RecoveryRetryGuidance, "recovery.warning.retry",
		"Retry normal launch is explicit and one-shot. Startup cleanup and "
		"normal catalog loading run only after this action."},
	StaticMessage{MessageId::RecoveryDegradedImportAcknowledged,
				  "backup.warning.degraded_acknowledged",
				  "Degraded import warning is acknowledged. Confirming now "
				  "will replace the active catalog."},
	StaticMessage{MessageId::RecoveryDegradedImportConfirmation,
				  "backup.warning.degraded_confirmation",
				  "Degraded staged import can replace current data only after "
				  "explicit warning acknowledgement."},
	StaticMessage{MessageId::RecoveryImportNoStagedBackup,
				  "backup.feedback.no_staged",
				  "No validated staged backup is ready to import."},
	StaticMessage{MessageId::BackupExportDestinationCancelled,
				  "backup.feedback.export_destination_cancelled",
				  "Backup export destination cancelled."},
	StaticMessage{MessageId::BackupExportDestinationFailed,
				  "backup.feedback.export_destination_failed",
				  "Backup export destination failed."},
	StaticMessage{MessageId::BackupExportPickerUnavailable,
				  "backup.feedback.export_picker_unavailable",
				  "Backup export destination picker could not be opened."},
	StaticMessage{MessageId::DiagnosticExportDestinationCancelled,
				  "backup.feedback.diagnostic_destination_cancelled",
				  "Diagnostic archive destination cancelled."},
	StaticMessage{MessageId::DiagnosticExportDestinationFailed,
				  "backup.feedback.diagnostic_destination_failed",
				  "Diagnostic archive destination failed."},
	StaticMessage{MessageId::DiagnosticExportPickerUnavailable,
				  "backup.feedback.diagnostic_picker_unavailable",
				  "Diagnostic archive destination picker could not be opened."},
	StaticMessage{MessageId::BackupImportSourceCancelled,
				  "backup.feedback.import_source_cancelled",
				  "Backup import source selection cancelled."},
	StaticMessage{MessageId::BackupImportSourceFailed,
				  "backup.feedback.import_source_failed",
				  "Backup import source selection failed."},
	StaticMessage{MessageId::BackupImportPickerUnavailable,
				  "backup.feedback.import_picker_unavailable",
				  "Backup import picker could not be opened."},
	StaticMessage{
		MessageId::RetryNormalStartupUnavailable,
		"recovery.feedback.retry_unavailable",
		"Retry normal launch is available only from startup safe mode."},
	StaticMessage{MessageId::RetryNormalStartupReachedRecovery,
				  "recovery.feedback.retry_recovery",
				  "Normal startup retry reached recovery state."},
	StaticMessage{MessageId::RetryNormalStartupCompleted,
				  "recovery.feedback.retry_completed",
				  "Normal startup retry completed."},
	StaticMessage{MessageId::DiagnosticArchiveExportCompleted,
				  "backup.feedback.diagnostic_completed",
				  "Diagnostic archive export completed."},
	StaticMessage{MessageId::BackupZipExportCompleted,
				  "backup.feedback.export_completed",
				  "Backup ZIP export completed."},
	StaticMessage{MessageId::DegradedBackupPreserved,
				  "backup.feedback.degraded_preserved",
				  "Degraded catalog state was preserved as raw files."},
	StaticMessage{MessageId::DiagnosticExportCancelled,
				  "backup.feedback.diagnostic_cancelled",
				  "Diagnostic export cancelled."},
	StaticMessage{MessageId::BackupExportCancelled,
				  "backup.feedback.export_cancelled",
				  "Backup export cancelled."},
	StaticMessage{MessageId::DiagnosticExportFailed,
				  "backup.feedback.diagnostic_failed",
				  "Diagnostic export failed."},
	StaticMessage{MessageId::BackupExportFailed,
				  "backup.feedback.export_failed", "Backup export failed."},
	StaticMessage{MessageId::BackupImportValidatedDegraded,
				  "backup.feedback.validated_degraded",
				  "Backup ZIP validated as degraded. Review summary and "
				  "confirm degraded import before replacement."},
	StaticMessage{MessageId::BackupImportValidated, "backup.feedback.validated",
				  "Backup ZIP validated. Confirm replacement to import."},
	StaticMessage{MessageId::BackupImportStagingCancelled,
				  "backup.feedback.staging_cancelled",
				  "Backup import staging cancelled."},
	StaticMessage{MessageId::BackupImportRejected, "backup.feedback.rejected",
				  "Backup import rejected before replacement."},
	StaticMessage{MessageId::BackupImportCompleted, "backup.feedback.completed",
				  "Backup import completed and catalog reloaded."},
	StaticMessage{MessageId::BackupImportReplacementCancelled,
				  "backup.feedback.replacement_cancelled",
				  "Backup import replacement cancelled."},
	StaticMessage{MessageId::FatalReplacementRecoveryRequired,
				  "backup.feedback.fatal_recovery",
				  "Catalog replacement failed and rollback failed. Fatal "
				  "recovery actions are required."},
	StaticMessage{MessageId::BackupReplacementFailed,
				  "backup.feedback.replacement_failed",
				  "Backup replacement failed; current catalog was not replaced "
				  "or was rolled back."},
	StaticMessage{MessageId::DegradedImportWarningAcknowledged,
				  "backup.feedback.warning_acknowledged",
				  "Degraded import warning acknowledged. Press confirm again "
				  "to replace the current catalog."},
	StaticMessage{MessageId::ValidatedStagedImportCleared,
				  "backup.feedback.staged_cleared",
				  "Validated staged import cleared. Active catalog unchanged."},
	StaticMessage{MessageId::SaveItem, "common.action.save_item", "Save item"},
	StaticMessage{MessageId::SaveStorage, "common.action.save_storage",
				  "Save storage"},
	StaticMessage{MessageId::TagKeyPlaceholder, "forms.tag.key_placeholder",
				  "Key"},
	StaticMessage{MessageId::TagValuePlaceholder, "forms.tag.value_placeholder",
				  "Value"},
	StaticMessage{MessageId::TagRemove, "forms.tag.remove", "Remove"},
	StaticMessage{MessageId::PhotoAdd, "photo.action.add", "Add photos"},
	StaticMessage{MessageId::PhotoClearStaged, "photo.action.clear_staged",
				  "Clear"},
	StaticMessage{MessageId::Previous, "common.navigation.previous",
				  "Previous"},
	StaticMessage{MessageId::Next, "common.navigation.next", "Next"},
	StaticMessage{MessageId::SetMain, "photo.action.set_main", "Set as main"},
	StaticMessage{MessageId::PhotoMain, "photo.state.main", "Main"},
	StaticMessage{MessageId::ConfirmDelete, "photo.action.confirm_delete",
				  "Confirm delete"},
	StaticMessage{MessageId::Delete, "photo.action.delete", "Delete"},
	StaticMessage{MessageId::RemoveStaged, "photo.action.remove_staged",
				  "Remove staged"},
	StaticMessage{MessageId::MainAfterSave, "photo.state.main_after_save",
				  "Main after save"},
	StaticMessage{MessageId::NoStagedPhotosTitle, "photo.deck.no_staged_title",
				  "No staged photos"},
	StaticMessage{MessageId::NoStagedPhotosCaption,
				  "photo.deck.no_staged_caption",
				  "Use Add photos to stage images before saving."},
	StaticMessage{MessageId::NoStagedPhotosPlaceholder,
				  "photo.deck.no_staged_placeholder", "No staged photos yet."},
	StaticMessage{MessageId::NoCurrentPhotosTitle,
				  "photo.deck.no_current_title", "No current photos"},
	StaticMessage{MessageId::NoCurrentPhotosCaption,
				  "photo.deck.no_current_caption",
				  "Stored owner photos will appear here."},
	StaticMessage{MessageId::NoCurrentPhotosPlaceholder,
				  "photo.deck.no_current_placeholder",
				  "No current owner photos yet."},
	StaticMessage{MessageId::PreviewStateEmpty, "preview.state.empty",
				  "No photos"},
	StaticMessage{MessageId::PreviewStateLoading, "preview.state.loading",
				  "Loading"},
	StaticMessage{MessageId::PreviewStateLoaded, "preview.state.loaded",
				  "Preview"},
	StaticMessage{MessageId::PreviewStateBroken, "preview.state.broken",
				  "Broken"},
	StaticMessage{MessageId::PreviewStateStaged, "preview.state.staged",
				  "Staged"},
	StaticMessage{MessageId::PreviewPlaceholderEmpty,
				  "preview.placeholder.empty", "No photos yet."},
	StaticMessage{MessageId::PreviewPlaceholderLoading,
				  "preview.placeholder.loading",
				  "Preview will load when requested."},
	StaticMessage{MessageId::PreviewPlaceholderLoaded,
				  "preview.placeholder.loaded",
				  "Decoded image cannot be displayed."},
	StaticMessage{MessageId::PreviewPlaceholderBroken,
				  "preview.placeholder.broken",
				  "Photo preview is unavailable."},
	StaticMessage{MessageId::PreviewPlaceholderStaged,
				  "preview.placeholder.staged",
				  "Staged photo preview is unavailable."},
	StaticMessage{MessageId::PreviewCarouselNoSlides,
				  "preview.carousel.no_slides", "No photos / Photo preview"},
	StaticMessage{MessageId::PreviewViewerTitle, "preview.viewer.title",
				  "Photo viewer"},
	StaticMessage{MessageId::PreviewViewerGestureHint,
				  "preview.viewer.gesture_hint",
				  "Double-tap to zoom; drag when zoomed; swipe when fitted."},
	StaticMessage{MessageId::PreviewViewerLoading, "preview.viewer.loading",
				  "Loading full photo in background."},
	StaticMessage{MessageId::PreviewViewerDecodeUnavailable,
				  "preview.viewer.decode_unavailable",
				  "Decoded image cannot be displayed."},
	StaticMessage{MessageId::PreviewViewerCachedUnavailable,
				  "preview.viewer.cached_unavailable",
				  "Cached preview cannot be displayed."},
	StaticMessage{MessageId::PreviewViewerCancelled, "preview.viewer.cancelled",
				  "Photo display was cancelled."},
	StaticMessage{MessageId::PreviewViewerPlaceholder,
				  "preview.viewer.placeholder",
				  "Photo preview placeholder. Full decode is only attempted "
				  "for this viewer, not for result lists."},
	StaticMessage{MessageId::PreviewViewerRotateLeft,
				  "preview.viewer.rotate_left", "Rotate left"},
	StaticMessage{MessageId::PreviewViewerRotateRight,
				  "preview.viewer.rotate_right", "Rotate right"},
	StaticMessage{MessageId::PreviewViewerControls, "preview.viewer.controls",
				  "Viewer controls"},
	StaticMessage{MessageId::PhotoAlreadyMain, "photo.state.already_main",
				  "Already main photo"},
	StaticMessage{MessageId::PhotoDeletePrompt, "photo.delete.prompt",
				  "Delete selected photo? Metadata is removed first."},
	StaticMessage{MessageId::PhotoDeleteCurrent, "photo.action.delete_current",
				  "Delete current photo"},
	StaticMessage{MessageId::PhotoAddMore, "photo.action.add_more",
				  "Add more photos"},
	StaticMessage{MessageId::PhotoExportJpeg, "photo.action.export_jpeg",
				  "Export selected photo as JPEG"},
	StaticMessage{MessageId::CatalogBannerDegraded,
				  "catalog.banner.degraded_load",
				  "Degraded load: some records or media need attention. "
				  "Accepted records remain browsable."},
	StaticMessage{MessageId::CatalogBannerDemo, "catalog.banner.demo_active",
				  "Demo catalog is active. Existing canonical catalogs are "
				  "never overwritten by debug seeding."},
	StaticMessage{MessageId::CatalogEmptyGuidance, "catalog.empty_guidance",
				  "Empty catalog. Add item and Add storage are available from "
				  "Add; backup/import/recovery are available from More."},
	StaticMessage{MessageId::CatalogNoResults, "catalog.no_results",
				  "No results. Try clearing the query or filters, or include "
				  "archived records."},
	StaticMessage{MessageId::CatalogHeadingStorages, "catalog.heading.storages",
				  "Storages"},
	StaticMessage{MessageId::CatalogHeadingItems, "catalog.heading.items",
				  "Items"},
	StaticMessage{MessageId::CatalogCategoriesEmpty, "catalog.categories.empty",
				  "Categories: no values yet."},
	StaticMessage{MessageId::CatalogCategories, "catalog.filters.categories",
				  "Categories"},
	StaticMessage{MessageId::CatalogStatus, "catalog.filters.status", "Status"},
	StaticMessage{MessageId::CatalogStorageAny, "catalog.filters.storage_any",
				  "Any"},
	StaticMessage{MessageId::CatalogStorageUnassigned,
				  "catalog.filters.storage_unassigned", "Unassigned"},
	StaticMessage{MessageId::CatalogStorage, "catalog.filters.storage",
				  "Storage"},
	StaticMessage{MessageId::CatalogStorageScope,
				  "catalog.filters.storage_scope", "Storage scope"},
	StaticMessage{MessageId::CatalogNestedContents,
				  "catalog.filters.nested_contents", "Nested contents"},
	StaticMessage{MessageId::CatalogPhotoPresence,
				  "catalog.filters.photo_presence", "Photo presence"},
	StaticMessage{MessageId::CatalogShortcuts, "catalog.filters.shortcuts",
				  "Shortcuts"},
	StaticMessage{MessageId::CatalogShortcutListed,
				  "catalog.filters.shortcut_listed", "Listed"},
	StaticMessage{MessageId::CatalogShortcutSold,
				  "catalog.filters.shortcut_sold", "Sold"},
	StaticMessage{MessageId::CatalogShortcutArchived,
				  "catalog.filters.shortcut_archived", "Archived"},
	StaticMessage{MessageId::CatalogPreviewUntitledItem,
				  "catalog.preview.untitled_item", "Untitled item"},
	StaticMessage{MessageId::CatalogPreviewDefaultStorage,
				  "catalog.preview.default_storage", "Storage"},
	StaticMessage{MessageId::CatalogPreviewArchivedStorage,
				  "catalog.preview.archived_storage", "archived storage"},
	StaticMessage{MessageId::CatalogPreviewBrokenStorage,
				  "catalog.preview.broken_storage", "broken storage"},
	StaticMessage{MessageId::CatalogPreviewBrokenParent,
				  "catalog.preview.broken_parent", "broken parent"},
	StaticMessage{MessageId::CatalogItemNoSelection,
				  "catalog.item.no_selection", "No item selected."},
	StaticMessage{MessageId::CatalogItemMissing, "catalog.item.missing",
				  "Selected item is missing from the accepted catalog."},
	StaticMessage{MessageId::CatalogItemNoPhotosTitle,
				  "catalog.item.no_photos_title", "No item photos"},
	StaticMessage{MessageId::CatalogItemNoPhotosCaption,
				  "catalog.item.no_photos_caption",
				  "Add photos to make this item image-centric."},
	StaticMessage{MessageId::EntityActionEditItem, "entity.action.edit_item",
				  "Edit item"},
	StaticMessage{MessageId::PhotoActionOpenViewer, "photo.action.open_viewer",
				  "Open photo viewer"},
	StaticMessage{MessageId::CatalogItemPhotoWarning,
				  "catalog.item.photo_warning",
				  "Recovery/photo warning: photo state needs attention or "
				  "photo import is still pending."},
	StaticMessage{MessageId::CatalogStorageNoSelection,
				  "catalog.storage.no_selection", "No storage selected."},
	StaticMessage{MessageId::CatalogStorageMissing, "catalog.storage.missing",
				  "Selected storage is missing from the accepted catalog."},
	StaticMessage{MessageId::CatalogStorageNoPhotosTitle,
				  "catalog.storage.no_photos_title", "No storage photos"},
	StaticMessage{MessageId::CatalogStorageNoPhotosCaption,
				  "catalog.storage.no_photos_caption",
				  "Add photos to make this storage image-centric."},
	StaticMessage{MessageId::CatalogStorageIncludeNested,
				  "catalog.storage.include_nested", "Include nested contents"},
	StaticMessage{MessageId::CatalogStorageNestedDefault,
				  "catalog.storage.nested_default",
				  "Showing nested contents by default."},
	StaticMessage{MessageId::CatalogStorageDirectOnly,
				  "catalog.storage.direct_only", "Direct contents only mode."},
	StaticMessage{MessageId::CatalogStorageChildStorages,
				  "catalog.storage.child_storages", "Child storages"},
	StaticMessage{MessageId::CatalogStorageNoChildren,
				  "catalog.storage.no_children", "No child storages."},
	StaticMessage{MessageId::CatalogStorageItemsInside,
				  "catalog.storage.items_inside", "Items inside"},
	StaticMessage{MessageId::CatalogStorageNoItems, "catalog.storage.no_items",
				  "No items in this mode."},
	StaticMessage{MessageId::CatalogStorageActions, "catalog.storage.actions",
				  "Actions"},
	StaticMessage{MessageId::CatalogStorageAddItem, "catalog.storage.add_item",
				  "Add item here"},
	StaticMessage{MessageId::CatalogStorageAddNestedStorage,
				  "catalog.storage.add_nested_storage", "Add nested storage"},
	StaticMessage{MessageId::PhotoActionOpenStorageViewer,
				  "photo.action.open_storage_viewer",
				  "Open storage photo viewer"},
	StaticMessage{MessageId::PhotoActionExportStorageJpeg,
				  "photo.action.export_storage_jpeg",
				  "Export selected storage photo as JPEG"},
	StaticMessage{MessageId::EntityActionEditStorage,
				  "entity.action.edit_storage", "Edit storage"},
	StaticMessage{MessageId::StorageLabelUnassigned, "storage.label.unassigned",
				  "Unassigned storage"},
	StaticMessage{MessageId::FormsItemStatus, "forms.item_status", "Status"},
	StaticMessage{MessageId::FormsItemStatusDraft, "forms.item_status.draft",
				  "Draft"},
	StaticMessage{MessageId::FormsItemStatusPlannedShort,
				  "forms.item_status.planned_short", "Plan"},
	StaticMessage{MessageId::FormsItemStatusListedShort,
				  "forms.item_status.listed_short", "List"},
	StaticMessage{MessageId::FormsItemStatusSold, "forms.item_status.sold",
				  "Sold"},
	StaticMessage{MessageId::FormsItemStatusArchivedShort,
				  "forms.item_status.archived_short", "Arch"},
	StaticMessage{MessageId::FormsTagsEmpty, "forms.tags.empty",
				  "No tags yet. Add a row for brand, size, condition, or any "
				  "custom fact."},
	StaticMessage{MessageId::FormsTagsTitle, "forms.tags.title", "Tags"},
	StaticMessage{MessageId::FormsTagsAddRow, "forms.tags.add_row", "Add row"},
	StaticMessage{MessageId::FormsTagsClearAll, "forms.tags.clear_all",
				  "Clear all"},
	StaticMessage{MessageId::FormsItemDisplayName, "forms.item.display_name",
				  "Display name (required)"},
	StaticMessage{MessageId::FormsItemCategory, "forms.item.category",
				  "Category (required)"},
	StaticMessage{MessageId::FormsChoiceHide, "forms.choice.hide",
				  "Hide choices"},
	StaticMessage{MessageId::FormsChoiceChange, "forms.choice.change",
				  "Change"},
	StaticMessage{MessageId::FormsChoiceChoose, "forms.choice.choose",
				  "Choose"},
	StaticMessage{MessageId::FormsStorageUnassigned, "forms.storage.unassigned",
				  "Unassigned storage"},
	StaticMessage{MessageId::FormsStorageChoices, "forms.storage.choices",
				  "Storage choices"},
	StaticMessage{MessageId::FormsTagsKeyHints, "forms.tags.key_hints",
				  "Key hints"},
	StaticMessage{MessageId::FormsTagsReusableKeysEmpty,
				  "forms.tags.reusable_keys_empty",
				  "No reusable tag keys in this catalog yet."},
	StaticMessage{MessageId::FormsTagsItemKeys, "forms.tags.item_keys",
				  "Item tag keys"},
	StaticMessage{MessageId::FormsTagsStorageKeys, "forms.tags.storage_keys",
				  "Storage tag keys"},
	StaticMessage{MessageId::FormsTagsEmptyHint, "forms.tags.empty_hint",
				  "No tags yet. Use Key hints or Add row."},
	StaticMessage{MessageId::FormsNotes, "forms.notes", "Notes"},
	StaticMessage{MessageId::FormsListingCollapse, "forms.listing.collapse",
				  "Collapse listing details"},
	StaticMessage{MessageId::FormsListingExpand, "forms.listing.expand",
				  "Add listing details"},
	StaticMessage{MessageId::FormsListingMarketplace,
				  "forms.listing.marketplace", "Marketplace"},
	StaticMessage{MessageId::FormsListingUrl, "forms.listing.url",
				  "Listing URL"},
	StaticMessage{MessageId::FormsListingNote, "forms.listing.note",
				  "Listing note"},
	StaticMessage{MessageId::FormsFinanceCollapse, "forms.finance.collapse",
				  "Collapse finance details"},
	StaticMessage{MessageId::FormsFinanceExpand, "forms.finance.expand",
				  "Add finance details"},
	StaticMessage{MessageId::FormsFinanceAcquisitionSource,
				  "forms.finance.acquisition_source", "Acquisition source"},
	StaticMessage{MessageId::FormsFinanceMoneyNote, "forms.finance.money_note",
				  "Money fields stay omitted from this compact JUCE form pass; "
				  "existing values are preserved when editing."},
	StaticMessage{MessageId::FormsEditOnlyActions, "forms.edit_only_actions",
				  "Edit-only actions"},
	StaticMessage{MessageId::FormsArchiveItem, "forms.archive_item",
				  "Archive item"},
	StaticMessage{MessageId::FormsHardDeleteDisabled,
				  "forms.hard_delete.disabled",
				  "Hard delete disabled until deletion sequence tests pass"},
	StaticMessage{MessageId::FormsStorageDisplayName,
				  "forms.storage.display_name", "Display name (required)"},
	StaticMessage{MessageId::FormsStorageType, "forms.storage.type",
				  "Storage type (required)"},
	StaticMessage{MessageId::FormsStorageNoParent, "forms.storage.no_parent",
				  "No parent storage"},
	StaticMessage{MessageId::FormsStorageParentChoices,
				  "forms.storage.parent_choices", "Parent choices"},
	StaticMessage{MessageId::FormsStorageLocation, "forms.storage.location",
				  "Physical location"},
	StaticMessage{MessageId::FormsStorageArchived, "forms.storage.archived",
				  "Storage archived"},
	StaticMessage{MessageId::FormsStorageArchive, "forms.storage.archive",
				  "Archive storage"},
	StaticMessage{MessageId::FormsStorageArchiveContentsAck,
				  "forms.storage.archive_contents_ack",
				  "Confirm archive-with-contents warning if shown"},
	StaticMessage{MessageId::FormsPhotoOwnerNone, "forms.photo.owner_none",
				  "No photo owner selected."},
	StaticMessage{MessageId::FormsPhotoOwnerEmpty, "forms.photo.owner_empty",
				  "No photo records for this owner yet. Add photos to import "
				  "into app-private JPEG XL storage."},
	StaticMessage{MessageId::FormsPhotoNoneSelected,
				  "forms.photo.none_selected", "No selected photo."},
	StaticMessage{
		MessageId::FormsPhotoMissing, "forms.photo.missing",
		"Selected photo record is missing from the accepted catalog."},
	StaticMessage{MessageId::ScreenFieldValue, "screen.field_value",
				  "{label}: {value}"},
	StaticMessage{MessageId::ScreenTagsSummary, "screen.tags.summary",
				  "Tags: {tag_list}"},
	StaticMessage{MessageId::ScreenListingEmpty, "screen.listing.empty",
				  "Listing: no values yet"},
	StaticMessage{MessageId::ScreenFinanceEmpty, "screen.finance.empty",
				  "Finance: no values yet"},
	StaticMessage{MessageId::WorkflowPreviewUnavailable,
				  "workflow.preview.scheduler_unavailable",
				  "Photo preview is unavailable."},
	StaticMessage{MessageId::PhotoOperationBusy, "photo.operation.busy",
				  "A photo operation is already in progress."},
	StaticMessage{MessageId::PhotoOperationHeading,
				  "photo.operation.heading", "Photo operation in progress"},
	StaticMessage{MessageId::PhotoOperationCancel, "photo.operation.cancel",
				  "Cancel photo operation"},
	StaticMessage{MessageId::PhotoOperationCancelling,
				  "photo.operation.cancelling",
				  "Photo operation cancellation requested."},
	StaticMessage{MessageId::PhotoOperationFailed, "photo.operation.failed",
				  "Photo operation failed."},
};

constexpr std::array presentation_message_table{
	PresentationMessage{"catalog.item_status.draft", "Draft"},
	PresentationMessage{"catalog.item_status.planned", "Planned"},
	PresentationMessage{"catalog.item_status.listed", "Listed"},
	PresentationMessage{"catalog.item_status.sold", "Sold"},
	PresentationMessage{"catalog.item_status.archived", "Archived"},
	PresentationMessage{"storage.lifecycle.active", "Active"},
	PresentationMessage{"storage.lifecycle.archived", "Archived"},
	PresentationMessage{"catalog.photo_presence.usable", "Photos available"},
	PresentationMessage{"catalog.photo_presence.none", "No photos"},
	PresentationMessage{"catalog.photo_presence.broken",
						"Photos need attention"},
	PresentationMessage{"catalog.photo_presence.mixed",
						"Some photos need attention"},
	PresentationMessage{"catalog.photo_filter.any", "Any photo state"},
	PresentationMessage{"catalog.photo_filter.has", "Has photos"},
	PresentationMessage{"catalog.photo_filter.none", "No photos"},
	PresentationMessage{"catalog.photo_filter.broken", "Broken photos"},
	PresentationMessage{"photo.pending_status.selected", "Selected"},
	PresentationMessage{"photo.pending_status.staged", "Staged"},
	PresentationMessage{"photo.pending_status.failed", "Failed"},
	PresentationMessage{"photo.pending_status.cancelled", "Cancelled"},
	PresentationMessage{"photo.pending_status.removed", "Removed"},
	PresentationMessage{"photo.pending_status.consumed", "Imported"},
	PresentationMessage{"catalog.load_status.normal", "Loaded normally"},
	PresentationMessage{"catalog.load_status.degraded", "Loaded with warnings"},
	PresentationMessage{"catalog.load_status.fatal", "Could not load"},
	PresentationMessage{"startup_source.existing", "Existing catalog"},
	PresentationMessage{"startup_source.empty", "New empty catalog"},
	PresentationMessage{"startup_source.demo", "Demo catalog"},
	PresentationMessage{"startup_source.path_failed",
						"Storage path unavailable"},
	PresentationMessage{"startup_source.initialization_failed",
						"Catalog initialization failed"},
	PresentationMessage{"startup_source.load_failed", "Catalog loading failed"},
	PresentationMessage{"startup_source.exception", "Startup exception"},
	PresentationMessage{"startup_source.safe_mode", "Startup safe mode"},
};

constexpr std::array warning_message_table{
	PresentationMessage{"catalog.warning.no_photo", "no photo"},
	PresentationMessage{"catalog.warning.broken_photos", "broken photos"},
	PresentationMessage{"catalog.warning.broken_storage", "broken storage"},
	PresentationMessage{"catalog.warning.broken_parent", "broken parent"},
	PresentationMessage{"catalog.warning.archived", "archived"},
	PresentationMessage{"catalog.warning.archived_storage", "archived storage"},
};

constexpr std::array template_message_table{
	PresentationMessage{"recovery.technical_information",
						"Technical information"},
	PresentationMessage{"catalog.filters.summary.applied",
						"Active filters: {filters}"},
	PresentationMessage{"catalog.filters.draft_summary",
						"Draft filters: {filters}"},
	PresentationMessage{"catalog.filters.summary.none", "No active filters"},
	PresentationMessage{"catalog.filters.summary.categories",
						"Categories: {categories}"},
	PresentationMessage{"catalog.filters.summary.statuses",
						"Statuses: {statuses}"},
	PresentationMessage{"catalog.filters.summary.storage_unassigned",
						"Storage: unassigned"},
	PresentationMessage{"catalog.filters.summary.storage",
						"Storage: {storage}"},
	PresentationMessage{"catalog.filters.summary.nested",
						"Including nested contents"},
	PresentationMessage{"catalog.filters.summary.photos",
						"Photos: {photo_presence}"},
	PresentationMessage{"catalog.filters.summary.listed_shortcut",
						"Listed shortcut"},
	PresentationMessage{"catalog.filters.summary.sold_shortcut",
						"Sold shortcut"},
	PresentationMessage{"catalog.filters.summary.include_archived",
						"Including archived records"},
	PresentationMessage{"forms.item.storage", "Storage: {storage}"},
	PresentationMessage{"forms.storage.parent", "Parent storage: {storage}"},
	PresentationMessage{"storage.label.missing", "Storage not found: {id}"},
	PresentationMessage{"screen.listing.summary",
						"Listing: marketplace {marketplace}; URL {url}; price "
						"{price}; note {note}"},
	PresentationMessage{
		"screen.finance.summary",
		"Finance: source {source}; acquisition cost {acquisition_cost}; sale "
		"price {sale_price}; expenses {expenses}; profit {profit}"},
	PresentationMessage{
		"catalog.storage.choice",
		"Storage: {display_name}; type: {type}; location: {path_or_location}"},
	PresentationMessage{
		"shell.status",
		"Load: {load_status} · {source} · items={items} · storages={storages}"},
	PresentationMessage{"shell.status.demo",
						"Load: {load_status} · {source} · items={items} · "
						"storages={storages} · demo catalog"},
	PresentationMessage{
		"catalog.item.header",
		"Item: {name}; photo state: {photo_state}; category: {category}; "
		"status: {status}; storage: {storage_path}; warnings: {warnings}"},
	PresentationMessage{
		"catalog.storage.header",
		"Storage: {name}; type: {type}; path: {path}; location: {location}; "
		"notes: {notes}; warnings: {warnings}"},
	PresentationMessage{"catalog.result.item_card",
						"Item: {title}; photo state: {photo_state}; category: "
						"{category}; status: {status}; location: {location}; "
						"details: {details}; warnings: {warnings}"},
	PresentationMessage{
		"catalog.result.storage_card",
		"Storage: {title}; type: {type}; lifecycle: {lifecycle}; location: "
		"{location}; child storages: {direct_children}; items: {direct_items} "
		"direct, {nested_items} including nested contents; details: {details}; "
		"warnings: {warnings}"},
	PresentationMessage{
		"photo.deck.selection_summary",
		"{scope} photo {position} of {count}; total photos: {total}"},
	PresentationMessage{"photo.deck.stored_caption",
						"Stored photo {position} of {total}"},
	PresentationMessage{"photo.deck.staged_caption",
						"Staged photo {position} of {total}"},
	PresentationMessage{"preview.viewer.zoom_hint",
						"Zoom {scale}x; drag to pan; double-tap to fit."},
	PresentationMessage{
		"common.progress.summary",
		"Progress: {phase} · {message}{units} · {cancellability}"},
	PresentationMessage{"common.progress.no_events",
						"Progress: no events reported yet."},
	PresentationMessage{"common.progress.units", " · {current}/{total}"},
	PresentationMessage{"common.progress.current_units", " · {current}"},
	PresentationMessage{"common.progress.cancellable", "cancellable"},
	PresentationMessage{"common.progress.not_cancellable", "not cancellable"},
};

constexpr std::array plural_message_table{
	FormatterMessage{"catalog.result_count", "{count} result",
					 "{count} results"},
	FormatterMessage{"catalog.item_count", "{count} item", "{count} items"},
	FormatterMessage{"photo.staged_count", "{count} photo staged",
					 "{count} photos staged"},
	FormatterMessage{"catalog.results_count", "Results: {count} result",
					 "Results: {count} results"},
	FormatterMessage{"catalog.draft_results_count",
					 "Draft results: {count} result",
					 "Draft results: {count} results"},
	FormatterMessage{"photo.deck.current", "Current: {count} photo",
					 "Current: {count} photos"},
	FormatterMessage{"photo.deck.staged", "Staged: {count} photo",
					 "Staged: {count} photos"},
};

constexpr std::array progress_message_definition_table{
	ProgressMessageDefinition{platform::ProgressMessageId::CopyStarted,
							  "copy-started", "Copy", "Copy started."},
	ProgressMessageDefinition{platform::ProgressMessageId::Copying, "copying",
							  "Copy", "Copying content."},
	ProgressMessageDefinition{platform::ProgressMessageId::CopyCompleted,
							  "copy-completed", "Copy", "Copy completed."},
	ProgressMessageDefinition{
		platform::ProgressMessageId::SourceFingerprintStarted,
		"source-fingerprint-started", "Source fingerprint",
		"Source-byte fingerprint started."},
	ProgressMessageDefinition{
		platform::ProgressMessageId::SourceFingerprintCompleted,
		"source-fingerprint-completed", "Source fingerprint",
		"Source-byte fingerprint completed."},
	ProgressMessageDefinition{platform::ProgressMessageId::SourceDecodeStarted,
							  "source-decode-started", "Source image decode",
							  "Source image decode started."},
	ProgressMessageDefinition{
		platform::ProgressMessageId::SourceDecodeCompleted,
		"source-decode-completed", "Source image decode",
		"Source image decode completed."},
	ProgressMessageDefinition{
		platform::ProgressMessageId::SyntheticSourceDecodeStarted,
		"decode-started", "Source image decode",
		"Source image decode started."},
	ProgressMessageDefinition{
		platform::ProgressMessageId::SyntheticSourceDecodeCompleted,
		"decode-completed", "Source image decode",
		"Source image decode completed."},
	ProgressMessageDefinition{
		platform::ProgressMessageId::InternalPhotoDecodeStarted,
		"internal-decode-started", "Internal photo decode",
		"Internal photo decode started."},
	ProgressMessageDefinition{
		platform::ProgressMessageId::InternalPhotoDecodeCompleted,
		"internal-decode-completed", "Internal photo decode",
		"Internal photo decode completed."},
	ProgressMessageDefinition{platform::ProgressMessageId::JpegWriteStarted,
							  "jpeg-write-started", "JPEG write",
							  "JPEG write started."},
	ProgressMessageDefinition{platform::ProgressMessageId::JpegWriteCompleted,
							  "jpeg-write-completed", "JPEG write",
							  "JPEG write completed."},
	ProgressMessageDefinition{platform::ProgressMessageId::JpegXlEncodeStarted,
							  "jxl-encode-started", "JPEG XL encode",
							  "JPEG XL encode started."},
	ProgressMessageDefinition{
		platform::ProgressMessageId::JpegXlEncodeCompleted,
		"jxl-encode-completed", "JPEG XL encode", "JPEG XL encode completed."},
	ProgressMessageDefinition{platform::ProgressMessageId::JpegXlDecodeStarted,
							  "jxl-decode-started", "JPEG XL decode",
							  "JPEG XL decode started."},
	ProgressMessageDefinition{
		platform::ProgressMessageId::JpegXlDecodeCompleted,
		"jxl-decode-completed", "JPEG XL decode", "JPEG XL decode completed."},
	ProgressMessageDefinition{platform::ProgressMessageId::ZipBuildStarted,
							  "zip-build-started", "ZIP archive",
							  "ZIP archive build started."},
	ProgressMessageDefinition{platform::ProgressMessageId::ZipBuildWriting,
							  "zip-build-writing", "ZIP archive",
							  "Writing ZIP archive."},
	ProgressMessageDefinition{platform::ProgressMessageId::ZipBuildValidating,
							  "zip-build-validating", "ZIP archive",
							  "Validating ZIP archive."},
	ProgressMessageDefinition{platform::ProgressMessageId::ZipInspecting,
							  "zip-inspecting", "ZIP archive",
							  "Inspecting ZIP archive entries."},
	ProgressMessageDefinition{platform::ProgressMessageId::ZipExtracting,
							  "zip-extracting", "ZIP archive",
							  "Extracting ZIP archive."},
	ProgressMessageDefinition{platform::ProgressMessageId::ZipExtractCompleted,
							  "zip-extract-completed", "ZIP archive",
							  "ZIP archive extraction completed."},
	ProgressMessageDefinition{platform::ProgressMessageId::MediaWriteStarted,
							  "media-write-started", "Media write",
							  "Media write started."},
	ProgressMessageDefinition{platform::ProgressMessageId::MediaWriteCompleted,
							  "media-write-completed", "Media write",
							  "Media write completed."},
	ProgressMessageDefinition{platform::ProgressMessageId::PhotoImportStarted,
							  "photo-import-started", "Photo import",
							  "Photo import started."},
	ProgressMessageDefinition{
		platform::ProgressMessageId::PhotoImportSourceStarted,
		"photo-import-photo-started", "Photo import",
		"Photo import source started."},
	ProgressMessageDefinition{
		platform::ProgressMessageId::PhotoImportCommitting,
		"photo-import-committing", "Photo import",
		"Photo metadata commit started."},
	ProgressMessageDefinition{platform::ProgressMessageId::PhotoImportCompleted,
							  "photo-import-completed", "Photo import",
							  "Photo import completed."},
	ProgressMessageDefinition{
		platform::ProgressMessageId::PendingPhotoStagingStarted,
		"pending-photo-staging-started", "Pending photo staging",
		"Pending photo staging started."},
	ProgressMessageDefinition{
		platform::ProgressMessageId::PendingPhotoSourceStarted,
		"pending-photo-source-started", "Pending photo staging",
		"Pending photo source staging started."},
	ProgressMessageDefinition{
		platform::ProgressMessageId::PendingPhotoStagingCompleted,
		"pending-photo-staging-completed", "Pending photo staging",
		"Pending photo staging completed."},
	ProgressMessageDefinition{platform::ProgressMessageId::JpegExportStarted,
							  "jpeg-export-started", "JPEG export",
							  "JPEG export started."},
	ProgressMessageDefinition{platform::ProgressMessageId::JpegExportDecoding,
							  "jpeg-export-decoding", "JPEG export",
							  "Decoding internal JPEG XL photo."},
	ProgressMessageDefinition{
		platform::ProgressMessageId::JpegExportWritingTemporary,
		"jpeg-export-writing-temp", "JPEG export",
		"Writing temporary JPEG export."},
	ProgressMessageDefinition{platform::ProgressMessageId::JpegExportCopying,
							  "jpeg-export-copying", "JPEG export",
							  "Copying JPEG to selected destination."},
	ProgressMessageDefinition{platform::ProgressMessageId::JpegExportCompleted,
							  "jpeg-export-completed", "JPEG export",
							  "JPEG export completed."},
	ProgressMessageDefinition{platform::ProgressMessageId::BackupPreparing,
							  "backup-preparing", "Backup export",
							  "Preparing normal backup."},
	ProgressMessageDefinition{platform::ProgressMessageId::BackupCopying,
							  "backup-copying", "Backup export",
							  "Copying backup ZIP to destination."},
	ProgressMessageDefinition{platform::ProgressMessageId::BackupCompleted,
							  "backup-done", "Backup export",
							  "Normal backup export completed."},
	ProgressMessageDefinition{platform::ProgressMessageId::DiagnosticPreparing,
							  "diagnostic-preparing", "Diagnostic archive",
							  "Preparing diagnostic archive."},
	ProgressMessageDefinition{platform::ProgressMessageId::DiagnosticCompleted,
							  "diagnostic-done", "Diagnostic archive",
							  "Diagnostic archive export completed."},
	ProgressMessageDefinition{platform::ProgressMessageId::BackupImportStaging,
							  "backup-import-staging", "Backup import",
							  "Staging backup ZIP for import."},
	ProgressMessageDefinition{
		platform::ProgressMessageId::BackupImportValidated,
		"backup-import-validated", "Backup import",
		"Backup ZIP staging validation completed."},
	ProgressMessageDefinition{
		platform::ProgressMessageId::CatalogReplacementValidating,
		"catalog-replacement-validating", "Catalog replacement",
		"Validating staged catalog before replacement."},
	ProgressMessageDefinition{
		platform::ProgressMessageId::CatalogReplacementRollbackCopy,
		"catalog-replacement-rollback-copy", "Catalog replacement",
		"Creating full-catalog rollback copy."},
	ProgressMessageDefinition{
		platform::ProgressMessageId::CatalogReplacementParkingActive,
		"catalog-replacement-parking-active", "Catalog replacement",
		"Moving current active catalog aside."},
	ProgressMessageDefinition{
		platform::ProgressMessageId::CatalogReplacementMovingStaged,
		"catalog-replacement-moving-staged", "Catalog replacement",
		"Moving staged catalog into the active catalog root."},
	ProgressMessageDefinition{
		platform::ProgressMessageId::CatalogReplacementLoading,
		"catalog-replacement-loading", "Catalog replacement",
		"Loading replaced active catalog."},
	ProgressMessageDefinition{
		platform::ProgressMessageId::CatalogReplacementCompleted,
		"catalog-replacement-done", "Catalog replacement",
		"Catalog replacement completed."},
};

static_assert(static_message_table.size() == 238U);
static_assert(
	presentation_message_table.size()
	== static_cast<std::size_t>(PresentationMessageIndex::StartupSourceSafeMode)
		   + 1U);
static_assert(
	template_message_table.size()
	== static_cast<std::size_t>(TemplateMessageIndex::ProgressNotCancellable)
		   + 1U);
static_assert(plural_message_table.size()
			  == static_cast<std::size_t>(PluralMessageIndex::PhotoDeckStaged)
					 + 1U);
static_assert(progress_message_definition_table.size()
			  == static_cast<std::size_t>(platform::ProgressMessageId::Count));

template<typename Message, std::size_t Size>
[[nodiscard]] std::span<const Message> as_span(
	const std::array<Message, Size>& messages) noexcept {
	return {messages.data(), messages.size()};
}
}	 // namespace

std::span<const StaticMessage> static_messages() noexcept {
	return as_span(static_message_table);
}

std::span<const PresentationMessage> presentation_messages() noexcept {
	return as_span(presentation_message_table);
}

std::span<const PresentationMessage> catalog_warning_messages() noexcept {
	return as_span(warning_message_table);
}

std::span<const PresentationMessage> template_messages() noexcept {
	return as_span(template_message_table);
}

std::span<const FormatterMessage> plural_messages() noexcept {
	return as_span(plural_message_table);
}

std::span<const ProgressMessageDefinition>
progress_message_definitions() noexcept {
	return as_span(progress_message_definition_table);
}

MessageId recovery_action_message(ui::RecoveryAction action) noexcept {
	for (const RecoveryActionMessage& message : recovery_action_messages)
		if (message.action == action)
			return message.message;
	return MessageId::RecoveryActionExit;
}

const StaticMessage& static_message(MessageId id) noexcept {
	for (const StaticMessage& message : static_message_table)
		if (message.id == id)
			return message;
	return static_message_table.front();
}

const StaticMessage& static_template_message(MessageId id) noexcept {
	return static_message(id);
}

const PresentationMessage& presentation_message(
	PresentationMessageIndex index) noexcept {
	return presentation_message_table[static_cast<std::size_t>(index)];
}

const PresentationMessage& template_message(
	TemplateMessageIndex index) noexcept {
	return template_message_table[static_cast<std::size_t>(index)];
}

const FormatterMessage& plural_message(PluralMessageIndex index) noexcept {
	return plural_message_table[static_cast<std::size_t>(index)];
}

PresentationMessageIndex presentation_message_index(
	domain::ItemStatus status) noexcept {
	switch (status) {
		case domain::ItemStatus::Draft:
			return PresentationMessageIndex::ItemStatusDraft;
		case domain::ItemStatus::Planned:
			return PresentationMessageIndex::ItemStatusPlanned;
		case domain::ItemStatus::Listed:
			return PresentationMessageIndex::ItemStatusListed;
		case domain::ItemStatus::Sold:
			return PresentationMessageIndex::ItemStatusSold;
		case domain::ItemStatus::Archived:
			return PresentationMessageIndex::ItemStatusArchived;
	}
	return PresentationMessageIndex::ItemStatusDraft;
}

PresentationMessageIndex presentation_message_index(
	domain::StorageLifecycleStatus status) noexcept {
	switch (status) {
		case domain::StorageLifecycleStatus::Active:
			return PresentationMessageIndex::StorageLifecycleActive;
		case domain::StorageLifecycleStatus::Archived:
			return PresentationMessageIndex::StorageLifecycleArchived;
	}
	return PresentationMessageIndex::StorageLifecycleActive;
}

PresentationMessageIndex presentation_message_index(
	catalog::PhotoPresenceState state) noexcept {
	switch (state) {
		case catalog::PhotoPresenceState::HasUsablePhotos:
			return PresentationMessageIndex::PhotoPresenceUsable;
		case catalog::PhotoPresenceState::NoPhotoRecords:
			return PresentationMessageIndex::PhotoPresenceNone;
		case catalog::PhotoPresenceState::OnlyBrokenPhotos:
			return PresentationMessageIndex::PhotoPresenceBroken;
		case catalog::PhotoPresenceState::MixedUsableAndBrokenPhotos:
			return PresentationMessageIndex::PhotoPresenceMixed;
	}
	return PresentationMessageIndex::PhotoPresenceNone;
}

PresentationMessageIndex presentation_message_index(
	catalog::SearchPhotoPresenceFilter filter) noexcept {
	switch (filter) {
		case catalog::SearchPhotoPresenceFilter::Any:
			return PresentationMessageIndex::PhotoFilterAny;
		case catalog::SearchPhotoPresenceFilter::HasPhotos:
			return PresentationMessageIndex::PhotoFilterHas;
		case catalog::SearchPhotoPresenceFilter::NoPhotos:
			return PresentationMessageIndex::PhotoFilterNone;
		case catalog::SearchPhotoPresenceFilter::BrokenPhotos:
			return PresentationMessageIndex::PhotoFilterBroken;
	}
	return PresentationMessageIndex::PhotoFilterAny;
}

PresentationMessageIndex presentation_message_index(
	ui::PendingPhotoStatus status) noexcept {
	switch (status) {
		case ui::PendingPhotoStatus::Selected:
			return PresentationMessageIndex::PendingPhotoSelected;
		case ui::PendingPhotoStatus::Staged:
			return PresentationMessageIndex::PendingPhotoStaged;
		case ui::PendingPhotoStatus::Failed:
			return PresentationMessageIndex::PendingPhotoFailed;
		case ui::PendingPhotoStatus::Cancelled:
			return PresentationMessageIndex::PendingPhotoCancelled;
		case ui::PendingPhotoStatus::Removed:
			return PresentationMessageIndex::PendingPhotoRemoved;
		case ui::PendingPhotoStatus::Consumed:
			return PresentationMessageIndex::PendingPhotoConsumed;
	}
	return PresentationMessageIndex::PendingPhotoSelected;
}

PresentationMessageIndex presentation_message_index(
	persistence::CatalogLoadStatus status) noexcept {
	switch (status) {
		case persistence::CatalogLoadStatus::Normal:
			return PresentationMessageIndex::CatalogLoadNormal;
		case persistence::CatalogLoadStatus::Degraded:
			return PresentationMessageIndex::CatalogLoadDegraded;
		case persistence::CatalogLoadStatus::Fatal:
			return PresentationMessageIndex::CatalogLoadFatal;
	}
	return PresentationMessageIndex::CatalogLoadFatal;
}

PresentationMessageIndex presentation_message_index(
	ui::CatalogSessionStartupSource source) noexcept {
	switch (source) {
		case ui::CatalogSessionStartupSource::ExistingCatalog:
			return PresentationMessageIndex::StartupSourceExisting;
		case ui::CatalogSessionStartupSource::InitializedEmptyCatalog:
			return PresentationMessageIndex::StartupSourceEmpty;
		case ui::CatalogSessionStartupSource::SeededDemoCatalog:
			return PresentationMessageIndex::StartupSourceDemo;
		case ui::CatalogSessionStartupSource::PathResolutionFailed:
			return PresentationMessageIndex::StartupSourcePathFailed;
		case ui::CatalogSessionStartupSource::InitializationFailed:
			return PresentationMessageIndex::StartupSourceInitializationFailed;
		case ui::CatalogSessionStartupSource::LoadFailed:
			return PresentationMessageIndex::StartupSourceLoadFailed;
		case ui::CatalogSessionStartupSource::StartupException:
			return PresentationMessageIndex::StartupSourceException;
		case ui::CatalogSessionStartupSource::StartupCrashSafeMode:
			return PresentationMessageIndex::StartupSourceSafeMode;
	}
	return PresentationMessageIndex::StartupSourceLoadFailed;
}
}	 // namespace shuba::localization::detail
