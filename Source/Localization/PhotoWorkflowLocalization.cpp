#include "Localization/PhotoWorkflowLocalization.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace shuba::localization {
namespace {
struct StaticDefinition final {
	PhotoWorkflowMessageId id;
	std::string_view context;
	std::string_view english;
};

constexpr std::array static_definitions{
	StaticDefinition{PhotoWorkflowMessageId::SelectImport,
					 "workflow.photo.select_import",
					 "Select photos to import."},
	StaticDefinition{PhotoWorkflowMessageId::SelectionCancelled,
					 "workflow.photo.selection_cancelled",
					 "Photo selection cancelled."},
	StaticDefinition{PhotoWorkflowMessageId::SelectionFailed,
					 "workflow.photo.selection_failed",
					 "Photo selection failed."},
	StaticDefinition{PhotoWorkflowMessageId::PickerUnavailable,
					 "workflow.photo.picker_unavailable",
					 "Photo picker could not be opened."},
	StaticDefinition{PhotoWorkflowMessageId::SelectPendingItem,
					 "workflow.photo.select_pending_item",
					 "Select photos to stage before saving the item."},
	StaticDefinition{PhotoWorkflowMessageId::SelectPendingStorage,
					 "workflow.photo.select_pending_storage",
					 "Select photos to stage before saving the storage."},
	StaticDefinition{PhotoWorkflowMessageId::PendingSelectionCancelled,
					 "workflow.photo.pending_selection_cancelled",
					 "Pending photo selection cancelled."},
	StaticDefinition{PhotoWorkflowMessageId::PendingSelectionFailed,
					 "workflow.photo.pending_selection_failed",
					 "Pending photo selection failed."},
	StaticDefinition{PhotoWorkflowMessageId::PendingNoneSelected,
					 "workflow.photo.pending_none_selected",
					 "No photos selected for pending staging."},
	StaticDefinition{PhotoWorkflowMessageId::PendingPickerUnavailable,
					 "workflow.photo.pending_picker_unavailable",
					 "Pending photo picker could not be opened."},
	StaticDefinition{PhotoWorkflowMessageId::JpegDestinationCancelled,
					 "workflow.photo.jpeg_destination_cancelled",
					 "JPEG export destination cancelled."},
	StaticDefinition{PhotoWorkflowMessageId::JpegDestinationFailed,
					 "workflow.photo.jpeg_destination_failed",
					 "JPEG export destination failed."},
	StaticDefinition{PhotoWorkflowMessageId::JpegCancelled,
					 "workflow.photo.jpeg_cancelled", "JPEG export cancelled."},
	StaticDefinition{PhotoWorkflowMessageId::JpegFailed,
					 "workflow.photo.jpeg_failed", "JPEG export failed."},
	StaticDefinition{PhotoWorkflowMessageId::JpegPickerUnavailable,
					 "workflow.photo.jpeg_picker_unavailable",
					 "JPEG export destination picker could not be opened."},
	StaticDefinition{
		PhotoWorkflowMessageId::DeleteInstruction,
		"workflow.photo.delete_instruction",
		"Tap Confirm delete to remove the selected photo metadata first."},
	StaticDefinition{PhotoWorkflowMessageId::DeleteCancelled,
					 "workflow.photo.delete_cancelled",
					 "Photo deletion cancelled."},
	StaticDefinition{PhotoWorkflowMessageId::PendingStagingCancelled,
					 "workflow.photo.pending_staging_cancelled",
					 "Pending photo staging cancelled."},
	StaticDefinition{PhotoWorkflowMessageId::PendingStagingFailed,
					 "workflow.photo.pending_staging_failed",
					 "Pending photo staging failed."},
	StaticDefinition{PhotoWorkflowMessageId::ImportCancelled,
					 "workflow.photo.import_cancelled",
					 "Photo import cancelled."},
	StaticDefinition{PhotoWorkflowMessageId::ImportFailed,
					 "workflow.photo.import_failed", "Photo import failed."},
	StaticDefinition{PhotoWorkflowMessageId::MetadataUpdateFailed,
					 "workflow.photo.metadata_update_failed",
					 "Photo metadata update failed."},
	StaticDefinition{PhotoWorkflowMessageId::MainUpdated,
					 "workflow.photo.main_updated", "Main photo updated."},
	StaticDefinition{PhotoWorkflowMessageId::MainUnchanged,
					 "workflow.photo.main_unchanged", "Main photo unchanged."},
	StaticDefinition{PhotoWorkflowMessageId::DeleteFailed,
					 "workflow.photo.delete_failed", "Photo deletion failed."},
	StaticDefinition{PhotoWorkflowMessageId::PendingCleanupPartial,
					 "workflow.photo.pending_cleanup_partial",
					 "Some pending staged photos could not be cleaned."},
	StaticDefinition{PhotoWorkflowMessageId::PendingCleared,
					 "workflow.photo.pending_cleared",
					 "Pending photos cleared."},
	StaticDefinition{
		PhotoWorkflowMessageId::PendingCleanupRetry,
		"workflow.photo.pending_cleanup_retry",
		"Pending photo cleanup needs attention; source kept for retry."},
	StaticDefinition{PhotoWorkflowMessageId::PendingRemoved,
					 "workflow.photo.pending_removed",
					 "Pending photo removed."},
};

[[nodiscard]] PhotoCountForm photo_count_form(std::uint64_t count) noexcept {
	const std::uint64_t remainder_100 = count % 100U;
	const std::uint64_t remainder_10  = count % 10U;
	if (remainder_10 == 1U && remainder_100 != 11U)
		return PhotoCountForm::One;
	if (remainder_10 >= 2U && remainder_10 <= 4U
		&& (remainder_100 < 10U || remainder_100 >= 20U)) {
		return PhotoCountForm::Few;
	}
	return PhotoCountForm::Many;
}

[[nodiscard]] std::string_view count_form_name(PhotoCountForm form) noexcept {
	switch (form) {
		case PhotoCountForm::One:
			return "one";
		case PhotoCountForm::Few:
			return "few";
		case PhotoCountForm::Many:
			return "many";
	}
	return "many";
}

[[nodiscard]] std::string photo_word(PhotoCountForm form) {
	return form == PhotoCountForm::One ? "photo" : "photos";
}

[[nodiscard]] std::string english_photo_word(std::uint64_t count) {
	return count == 1U ? "photo" : "photos";
}

[[nodiscard]] std::string make_context(std::string_view prefix,
									   std::string_view suffix) {
	std::string result{prefix};
	result.push_back('.');
	result.append(suffix);
	return result;
}

[[nodiscard]] PhotoWorkflowMessage make_matrix_message(
	std::string context, std::string english, std::string english_fallback,
	std::vector<std::pair<std::string, std::string>> replacements) {
	return PhotoWorkflowMessage{.context		  = std::move(context),
								.english		  = std::move(english),
								.english_fallback = std::move(english_fallback),
								.replacements	  = std::move(replacements)};
}

[[nodiscard]] std::string pending_owner_name(PendingSavePhotoOwner owner) {
	return owner == PendingSavePhotoOwner::Item ? "item" : "storage";
}

[[nodiscard]] std::string pending_owner_english(PendingSavePhotoOwner owner) {
	return owner == PendingSavePhotoOwner::Item ? "Item" : "Storage";
}

[[nodiscard]] std::string cleanup_name(PendingSaveCleanupState state) {
	return state == PendingSaveCleanupState::NeedsAttention ? "attention"
															: "clear";
}

[[nodiscard]] std::string main_name(PendingSaveMainPhotoState state) {
	return state == PendingSaveMainPhotoState::NotApplied ? "not_applied"
		   : state == PendingSaveMainPhotoState::Applied  ? "applied"
														  : "not_selected";
}

[[nodiscard]] bool is_valid_pending_save_outcome(
	const PendingSavePhotoOutcome& outcome) noexcept {
	if (outcome.import_state == PendingSavePhotoImportState::NoReadySources) {
		return outcome.cleanup_state == PendingSaveCleanupState::NotApplicable
			   && outcome.main_photo_state
					  == PendingSaveMainPhotoState::NotApplicable;
	}
	if (outcome.import_state == PendingSavePhotoImportState::Completed) {
		return outcome.cleanup_state != PendingSaveCleanupState::NotApplicable
			   && outcome.main_photo_state
					  != PendingSaveMainPhotoState::NotApplicable;
	}
	return outcome.cleanup_state != PendingSaveCleanupState::NotApplicable
		   && outcome.main_photo_state != PendingSaveMainPhotoState::Applied
		   && outcome.main_photo_state
				  != PendingSaveMainPhotoState::NotApplicable;
}
}	 // namespace

PhotoWorkflowMessage photo_workflow_message(PhotoWorkflowMessageId message) {
	for (const StaticDefinition& definition : static_definitions) {
		if (definition.id == message) {
			return PhotoWorkflowMessage{
				.context = std::string{definition.context},
				.english = std::string{definition.english}};
		}
	}
	return PhotoWorkflowMessage{.context = "workflow.photo.import_failed",
								.english = "Photo import failed."};
}

PhotoWorkflowMessage photo_import_completed_message(
	const PhotoImportCompletion& completion) {
	const PhotoCountForm imported_form =
		photo_count_form(completion.imported_count);
	const PhotoCountForm failed_form =
		photo_count_form(completion.failed_count);
	const std::string suffix =
		"imported_" + std::string{count_form_name(imported_form)} + ".failed_"
		+ std::string{count_form_name(failed_form)};
	const std::string english =
		"Photo import completed: {imported} " + photo_word(imported_form)
		+ " imported; {failed} " + photo_word(failed_form) + " failed.";
	return make_matrix_message(
		make_context("workflow.photo.import_completed", suffix), english,
		"Photo import completed: {imported} imported, {failed} failed.",
		{{"{imported}", std::to_string(completion.imported_count)},
		 {"{failed}", std::to_string(completion.failed_count)}});
}

PhotoWorkflowMessage pending_photo_staging_completed_message(
	const PendingPhotoStagingCompletion& completion) {
	const PhotoCountForm staged_form =
		photo_count_form(completion.staged_count);
	const PhotoCountForm failed_form =
		photo_count_form(completion.failed_count);
	const std::string suffix =
		"staged_" + std::string{count_form_name(staged_form)} + ".failed_"
		+ std::string{count_form_name(failed_form)};
	const std::string english = "Pending photo staging completed: {staged} "
								+ photo_word(staged_form) + " staged; {failed} "
								+ photo_word(failed_form) + " failed.";
	return make_matrix_message(
		make_context("workflow.photo.pending_staging_completed", suffix),
		english,
		"Pending photo staging completed: {staged} staged, {failed} failed.",
		{{"{staged}", std::to_string(completion.staged_count)},
		 {"{failed}", std::to_string(completion.failed_count)}});
}

PhotoWorkflowMessage jpeg_export_completed_message(
	std::uint64_t bytes_written) {
	const PhotoCountForm form = photo_count_form(bytes_written);
	const std::string english =
		"JPEG export completed: {bytes} "
		+ std::string{form == PhotoCountForm::One ? "byte." : "bytes."};
	return make_matrix_message(
		make_context("workflow.photo.jpeg_completed",
					 "bytes_" + std::string{count_form_name(form)}),
		english, "JPEG export completed: {bytes} bytes.",
		{{"{bytes}", std::to_string(bytes_written)}});
}

PhotoWorkflowMessage pending_save_photo_outcome_message(
	const PendingSavePhotoOutcome& outcome) {
	PendingSavePhotoOutcome normalized = outcome;
	if (!is_valid_pending_save_outcome(normalized)) {
		normalized.import_state		= PendingSavePhotoImportState::Failed;
		normalized.cleanup_state	= PendingSaveCleanupState::NeedsAttention;
		normalized.main_photo_state = PendingSaveMainPhotoState::NotApplied;
	}

	const std::string owner			= pending_owner_name(normalized.owner);
	const std::string owner_english = pending_owner_english(normalized.owner);
	if (normalized.import_state
		== PendingSavePhotoImportState::NoReadySources) {
		const std::string english =
			owner_english + " saved, but no staged pending photos were ready.";
		return make_matrix_message(
			make_context("workflow.edit.pending_" + owner, "no_ready"), english,
			english, {});
	}

	const std::string cleanup = cleanup_name(normalized.cleanup_state);
	const std::string main	  = main_name(normalized.main_photo_state);
	if (normalized.import_state == PendingSavePhotoImportState::Completed) {
		const PhotoCountForm imported_form =
			photo_count_form(normalized.imported_count);
		const PhotoCountForm failed_form =
			photo_count_form(normalized.failed_count);
		const std::string suffix =
			"completed.imported_" + std::string{count_form_name(imported_form)}
			+ ".failed_" + std::string{count_form_name(failed_form)}
			+ ".cleanup_" + cleanup + ".main_" + main;
		const std::string catalog_english =
			owner_english
			+ " saved. Pending photo import completed: {imported} "
			+ photo_word(imported_form) + " imported; {failed} "
			+ photo_word(failed_form) + " failed."
			+ (normalized.cleanup_state
					   == PendingSaveCleanupState::NeedsAttention
				   ? " Pending source cleanup needs attention."
				   : "")
			+ (normalized.main_photo_state == PendingSaveMainPhotoState::Applied
				   ? " Selected staged photo is now main."
			   : normalized.main_photo_state
					   == PendingSaveMainPhotoState::NotApplied
				   ? " Selected staged photo did not become main."
				   : "");
		std::string english =
			owner_english + " saved. Pending photo import completed: "
			+ "{imported} " + english_photo_word(normalized.imported_count)
			+ " imported; {failed} "
			+ english_photo_word(normalized.failed_count) + " failed.";
		if (normalized.cleanup_state == PendingSaveCleanupState::NeedsAttention)
			english += " Pending source cleanup needs attention.";
		if (normalized.main_photo_state == PendingSaveMainPhotoState::Applied)
			english += " Selected staged photo is now main.";
		else if (normalized.main_photo_state
				 == PendingSaveMainPhotoState::NotApplied)
			english += " Selected staged photo did not become main.";
		return make_matrix_message(
			make_context("workflow.edit.pending_" + owner, suffix),
			catalog_english, english,
			{{"{imported}", std::to_string(normalized.imported_count)},
			 {"{failed}", std::to_string(normalized.failed_count)}});
	}

	const std::string terminal =
		normalized.import_state == PendingSavePhotoImportState::Cancelled
			? "cancelled"
			: "failed";
	std::string english =
		owner_english + " saved, but pending photo import "
		+ (terminal == "cancelled" ? "was cancelled." : "failed.");
	if (normalized.cleanup_state == PendingSaveCleanupState::NeedsAttention)
		english += " Pending source cleanup needs attention.";
	if (normalized.main_photo_state == PendingSaveMainPhotoState::NotApplied)
		english += " Selected staged photo did not become main.";
	return make_matrix_message(
		make_context("workflow.edit.pending_" + owner,
					 terminal + ".cleanup_" + cleanup + ".main_" + main),
		std::move(english),
		owner_english + " saved, but pending photo import "
			+ (terminal == "cancelled" ? "was cancelled." : "failed.")
			+ (normalized.cleanup_state
					   == PendingSaveCleanupState::NeedsAttention
				   ? " Pending source cleanup needs attention."
				   : "")
			+ (normalized.main_photo_state
					   == PendingSaveMainPhotoState::NotApplied
				   ? " Selected staged photo did not become main."
				   : ""),
		{});
}

PhotoWorkflowMessage photo_deletion_outcome_message(
	const PhotoDeletionOutcome& outcome) {
	const std::string deletion =
		outcome.state == PhotoDeletionState::Deleted ? "deleted" : "no_changes";
	const std::string cleanup =
		outcome.media_cleanup_needs_attention ? "attention" : "clear";
	std::string english = outcome.state == PhotoDeletionState::Deleted
							  ? "Photo deleted."
							  : "Photo deletion made no changes.";
	if (outcome.media_cleanup_needs_attention)
		english += " Pending source cleanup needs attention.";
	return make_matrix_message(
		make_context("workflow.photo.delete", deletion + ".cleanup_" + cleanup),
		english, english, {});
}

std::vector<PhotoWorkflowMessage> photo_workflow_catalog_messages() {
	std::vector<PhotoWorkflowMessage> messages;
	messages.reserve(static_definitions.size());
	for (const StaticDefinition& definition : static_definitions) {
		messages.push_back(
			PhotoWorkflowMessage{.context = std::string{definition.context},
								 .english = std::string{definition.english}});
	}

	for (const PhotoCountForm imported_form :
		 {PhotoCountForm::One, PhotoCountForm::Few, PhotoCountForm::Many}) {
		for (const PhotoCountForm failed_form :
			 {PhotoCountForm::One, PhotoCountForm::Few, PhotoCountForm::Many}) {
			const std::uint64_t imported =
				imported_form == PhotoCountForm::One   ? 1U
				: imported_form == PhotoCountForm::Few ? 2U
													   : 5U;
			const std::uint64_t failed = failed_form == PhotoCountForm::One ? 1U
										 : failed_form == PhotoCountForm::Few
											 ? 2U
											 : 5U;
			messages.push_back(
				photo_import_completed_message(PhotoImportCompletion{
					.imported_count = imported, .failed_count = failed}));
			messages.push_back(pending_photo_staging_completed_message(
				PendingPhotoStagingCompletion{.staged_count = imported,
											  .failed_count = failed}));
		}
	}
	for (const std::uint64_t bytes : {1U, 2U, 5U})
		messages.push_back(jpeg_export_completed_message(bytes));

	for (const PendingSavePhotoOwner owner :
		 {PendingSavePhotoOwner::Item, PendingSavePhotoOwner::Storage}) {
		for (const PhotoCountForm imported_form :
			 {PhotoCountForm::One, PhotoCountForm::Few, PhotoCountForm::Many}) {
			for (const PhotoCountForm failed_form :
				 {PhotoCountForm::One, PhotoCountForm::Few,
				  PhotoCountForm::Many}) {
				const std::uint64_t imported =
					imported_form == PhotoCountForm::One   ? 1U
					: imported_form == PhotoCountForm::Few ? 2U
														   : 5U;
				const std::uint64_t failed =
					failed_form == PhotoCountForm::One	 ? 1U
					: failed_form == PhotoCountForm::Few ? 2U
														 : 5U;
				for (const PendingSaveCleanupState cleanup :
					 {PendingSaveCleanupState::Clear,
					  PendingSaveCleanupState::NeedsAttention}) {
					for (const PendingSaveMainPhotoState main :
						 {PendingSaveMainPhotoState::NotSelected,
						  PendingSaveMainPhotoState::Applied,
						  PendingSaveMainPhotoState::NotApplied}) {
						messages.push_back(pending_save_photo_outcome_message(
							PendingSavePhotoOutcome{
								.owner = owner,
								.import_state =
									PendingSavePhotoImportState::Completed,
								.imported_count	  = imported,
								.failed_count	  = failed,
								.cleanup_state	  = cleanup,
								.main_photo_state = main}));
					}
				}
			}
		}
		for (const PendingSavePhotoImportState state :
			 {PendingSavePhotoImportState::Cancelled,
			  PendingSavePhotoImportState::Failed}) {
			for (const PendingSaveCleanupState cleanup :
				 {PendingSaveCleanupState::Clear,
				  PendingSaveCleanupState::NeedsAttention}) {
				for (const PendingSaveMainPhotoState main :
					 {PendingSaveMainPhotoState::NotSelected,
					  PendingSaveMainPhotoState::NotApplied}) {
					messages.push_back(pending_save_photo_outcome_message(
						PendingSavePhotoOutcome{.owner			  = owner,
												.import_state	  = state,
												.cleanup_state	  = cleanup,
												.main_photo_state = main}));
				}
			}
		}
		messages.push_back(pending_save_photo_outcome_message(
			PendingSavePhotoOutcome{.owner = owner}));
	}
	for (const PhotoDeletionState state :
		 {PhotoDeletionState::Deleted, PhotoDeletionState::NoChanges}) {
		for (const bool cleanup : {false, true}) {
			messages.push_back(
				photo_deletion_outcome_message(PhotoDeletionOutcome{
					.state = state, .media_cleanup_needs_attention = cleanup}));
		}
	}
	return messages;
}

}	 // namespace shuba::localization
