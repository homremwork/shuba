#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace shuba::localization {
enum class PhotoWorkflowMessageId : std::uint8_t {
	SelectImport,
	SelectionCancelled,
	SelectionFailed,
	PickerUnavailable,
	SelectPendingItem,
	SelectPendingStorage,
	PendingSelectionCancelled,
	PendingSelectionFailed,
	PendingNoneSelected,
	PendingPickerUnavailable,
	JpegDestinationCancelled,
	JpegDestinationFailed,
	JpegCancelled,
	JpegFailed,
	JpegPickerUnavailable,
	DeleteInstruction,
	DeleteCancelled,
	PendingStagingCancelled,
	PendingStagingFailed,
	ImportCancelled,
	ImportFailed,
	MetadataUpdateFailed,
	MainUpdated,
	MainUnchanged,
	DeleteFailed,
	PendingCleanupPartial,
	PendingCleared,
	PendingCleanupRetry,
	PendingRemoved,
};

enum class PhotoCountForm : std::uint8_t {
	One,
	Few,
	Many,
};

struct PhotoImportCompletion final {
	std::uint64_t imported_count{};
	std::uint64_t failed_count{};
};

struct PendingPhotoStagingCompletion final {
	std::uint64_t staged_count{};
	std::uint64_t failed_count{};
};

enum class PendingSavePhotoOwner : std::uint8_t {
	Item,
	Storage,
};

enum class PendingSavePhotoImportState : std::uint8_t {
	Completed,
	Cancelled,
	Failed,
	NoReadySources,
};

enum class PendingSaveCleanupState : std::uint8_t {
	Clear,
	NeedsAttention,
	NotApplicable,
};

enum class PendingSaveMainPhotoState : std::uint8_t {
	NotSelected,
	Applied,
	NotApplied,
	NotApplicable,
};

struct PendingSavePhotoOutcome final {
	PendingSavePhotoOwner owner{PendingSavePhotoOwner::Item};
	PendingSavePhotoImportState import_state{
		PendingSavePhotoImportState::NoReadySources};
	std::uint64_t imported_count{};
	std::uint64_t failed_count{};
	PendingSaveCleanupState cleanup_state{
		PendingSaveCleanupState::NotApplicable};
	PendingSaveMainPhotoState main_photo_state{
		PendingSaveMainPhotoState::NotApplicable};
};

enum class PhotoDeletionState : std::uint8_t {
	Deleted,
	NoChanges,
};

struct PhotoDeletionOutcome final {
	PhotoDeletionState state{PhotoDeletionState::NoChanges};
	bool media_cleanup_needs_attention{};
};

struct PhotoWorkflowMessage final {
	std::string context;
	std::string english;
	std::string english_fallback;
	std::vector<std::pair<std::string, std::string>> replacements;
};

[[nodiscard]] PhotoWorkflowMessage photo_workflow_message(
	PhotoWorkflowMessageId message);
[[nodiscard]] PhotoWorkflowMessage photo_import_completed_message(
	const PhotoImportCompletion& completion);
[[nodiscard]] PhotoWorkflowMessage pending_photo_staging_completed_message(
	const PendingPhotoStagingCompletion& completion);
[[nodiscard]] PhotoWorkflowMessage jpeg_export_completed_message(
	std::uint64_t bytes_written);
[[nodiscard]] PhotoWorkflowMessage pending_save_photo_outcome_message(
	const PendingSavePhotoOutcome& outcome);
[[nodiscard]] PhotoWorkflowMessage photo_deletion_outcome_message(
	const PhotoDeletionOutcome& outcome);
[[nodiscard]] std::vector<PhotoWorkflowMessage>
photo_workflow_catalog_messages();
}	 // namespace shuba::localization
