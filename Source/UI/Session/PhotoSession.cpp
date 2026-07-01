#include "UI/Session/PhotoSession.hpp"

#include "Catalog/CatalogRepository.hpp"
#include "Persistence/CatalogStorage.hpp"
#include "Persistence/MetadataSchema.hpp"
#include "UI/Session/EntityEditSession.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace shuba::ui {
namespace {
using shuba::catalog::CatalogMediaSnapshot;
using shuba::core::Diagnostic;
using shuba::core::DiagnosticSeverity;
using shuba::core::OperationResultCategory;
using shuba::persistence::JsonlDiagnostic;

[[nodiscard]] Diagnostic make_diagnostic(DiagnosticSeverity severity,
										 std::string code, std::string message,
										 std::string technical_details = {}) {
	return Diagnostic{.severity			 = severity,
					  .code				 = std::move(code),
					  .message			 = std::move(message),
					  .technical_details = std::move(technical_details)};
}

[[nodiscard]] EntityEditDiagnostic make_entity_diagnostic(
	core::DiagnosticSeverity severity, std::string code, std::string message,
	std::string technical_details = {}) {
	return EntityEditDiagnostic{
		.severity		   = severity,
		.code			   = std::move(code),
		.message		   = std::move(message),
		.technical_details = std::move(technical_details)};
}

[[nodiscard]] EntityEditDiagnostic entity_diagnostic_from_core(
	const Diagnostic& diagnostic) {
	return EntityEditDiagnostic{
		.severity		   = diagnostic.severity,
		.code			   = diagnostic.code,
		.message		   = diagnostic.message,
		.technical_details = diagnostic.technical_details};
}

[[nodiscard]] EntityEditDiagnostic entity_diagnostic_from_jsonl(
	const JsonlDiagnostic& diagnostic) {
	return make_entity_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
								  diagnostic.code, diagnostic.message,
								  diagnostic.details);
}

void append_edit_diagnostic(EntityEditResult& result,
							EntityEditDiagnostic diagnostic) {
	result.diagnostics.push_back(std::move(diagnostic));
}

void append_edit_diagnostics(EntityEditResult& result,
							 const std::vector<JsonlDiagnostic>& diagnostics) {
	for (const JsonlDiagnostic& diagnostic : diagnostics) {
		append_edit_diagnostic(result,
							   entity_diagnostic_from_jsonl(diagnostic));
	}
}

void append_edit_diagnostics(EntityEditResult& result,
							 const std::vector<Diagnostic>& diagnostics) {
	for (const Diagnostic& diagnostic : diagnostics)
		append_edit_diagnostic(result, entity_diagnostic_from_core(diagnostic));
}

[[nodiscard]] CatalogMediaSnapshot scan_photo_media(
	const std::filesystem::path& active_catalog_root,
	std::vector<Diagnostic>& diagnostics) {
	CatalogMediaSnapshot snapshot;
	const std::filesystem::path media_directory =
		active_catalog_root
		/ std::filesystem::path{
			std::string{persistence::photo_media_directory_path}};

	std::error_code error;
	const bool exists = std::filesystem::exists(media_directory, error);
	if (error) {
		snapshot.complete_scan_available = false;
		diagnostics.push_back(make_diagnostic(
			DiagnosticSeverity::DegradedLoad, "media_scan_unavailable",
			"Photo media directory status could not be checked.",
			error.message()));
		return snapshot;
	}
	if (!exists)
		return snapshot;

	std::filesystem::directory_iterator iterator{media_directory, error};
	if (error) {
		snapshot.complete_scan_available = false;
		diagnostics.push_back(make_diagnostic(
			DiagnosticSeverity::DegradedLoad, "media_scan_unavailable",
			"Photo media directory could not be scanned.", error.message()));
		return snapshot;
	}

	for (const std::filesystem::directory_entry& entry : iterator) {
		error.clear();
		if (!entry.is_regular_file(error) || error)
			continue;

		std::filesystem::path relative_path =
			std::filesystem::path{
				std::string{persistence::photo_media_directory_path}}
			/ entry.path().filename();
		snapshot.readable_photo_media_files.push_back(
			relative_path.generic_string());
	}

	std::ranges::sort(snapshot.readable_photo_media_files);
	return snapshot;
}

[[nodiscard]] std::filesystem::path active_catalog_root_for_edit(
	const EntityEditRequest& request) {
	if (request.active_catalog_root_override)
		return *request.active_catalog_root_override;
	if (request.current_session.paths)
		return request.current_session.paths->active_catalog_root;
	return {};
}

[[nodiscard]] std::filesystem::path active_catalog_root_for_photo_import(
	const PhotoImportSessionRequest& request) {
	if (request.active_catalog_root_override)
		return *request.active_catalog_root_override;
	if (request.current_session.paths)
		return request.current_session.paths->active_catalog_root;
	return {};
}

[[nodiscard]] CatalogSessionState rebuild_photo_session(
	CatalogSessionState session, std::vector<persistence::PhotoEnvelope> photos,
	const std::filesystem::path& active_catalog_root) {
	session.load_result.photos = persistence::PhotoTableLoadResult{
		.records = photos,
		.summary = persistence::JsonlFileSummary{
			.path			  = std::string{persistence::photos_data_file_path},
			.accepted_records = static_cast<std::uint64_t>(photos.size())}};
	session.load_result.diagnostics.clear();
	CatalogMediaSnapshot media =
		scan_photo_media(active_catalog_root, session.startup_diagnostics);
	const bool media_scan_degraded = !media.complete_scan_available;
	session.repository =
		catalog::build_catalog_repository(catalog::CatalogRepositoryInput{
			.items	  = session.load_result.items.records,
			.storages = session.load_result.storages.records,
			.photos	  = std::move(photos),
			.media	  = std::move(media)});
	session.search_index = catalog::build_search_index(session.repository);
	session.load_status =
		session.repository.diagnostics.empty() && !media_scan_degraded
			? persistence::CatalogLoadStatus::Normal
			: persistence::CatalogLoadStatus::Degraded;
	session.load_result.load_status = session.load_status;
	return session;
}

[[nodiscard]] std::filesystem::path internal_photo_media_path(
	const std::filesystem::path& active_catalog_root,
	const core::StableIdentifier& photo_id) {
	return active_catalog_root
		   / std::filesystem::path{
			   catalog::expected_photo_media_relative_path(photo_id)};
}

[[nodiscard]] EntityEditDiagnostic photo_media_cleanup_warning(
	const std::filesystem::path& media_path, std::string details) {
	return make_entity_diagnostic(
		core::DiagnosticSeverity::RecoverableWarning,
		"photo_delete_orphan_media_left",
		"Photo metadata was deleted, but app-private media cleanup failed.",
		media_path.string() + ": " + std::move(details));
}

[[nodiscard]] std::optional<EntityEditDiagnostic> cleanup_deleted_photo_media(
	const std::filesystem::path& media_path) {
	std::error_code status_error;
	const bool path_exists = std::filesystem::exists(media_path, status_error);
	if (status_error) {
		return photo_media_cleanup_warning(
			media_path, "status check failed: " + status_error.message());
	}
	if (!path_exists)
		return std::nullopt;

	status_error.clear();
	const bool regular_file =
		std::filesystem::is_regular_file(media_path, status_error);
	if (status_error) {
		return photo_media_cleanup_warning(
			media_path, "file type check failed: " + status_error.message());
	}
	if (!regular_file) {
		return photo_media_cleanup_warning(
			media_path,
			"expected a regular internal photo media file at cleanup time");
	}

	std::error_code media_error;
	const bool removed = std::filesystem::remove(media_path, media_error);
	if (media_error) {
		return photo_media_cleanup_warning(
			media_path, "remove failed: " + media_error.message());
	}
	if (!removed)
		return photo_media_cleanup_warning(media_path, "file was not removed");
	return std::nullopt;
}

[[nodiscard]] std::optional<EntityEditDiagnostic>
missing_active_root_diagnostic(
	const std::filesystem::path& active_catalog_root) {
	if (!active_catalog_root.empty())
		return std::nullopt;
	return make_entity_diagnostic(
		core::DiagnosticSeverity::WriteBlockingError, "catalog_root_missing",
		"Active catalog root is unavailable for metadata commit.");
}

void append_import_diagnostic(PhotoImportSessionResult& result,
							  EntityEditDiagnostic diagnostic) {
	result.diagnostics.push_back(std::move(diagnostic));
}

void append_import_diagnostics(PhotoImportSessionResult& result,
							   const std::vector<Diagnostic>& diagnostics) {
	for (const Diagnostic& diagnostic : diagnostics) {
		append_import_diagnostic(result,
								 entity_diagnostic_from_core(diagnostic));
	}
}

void append_pending_diagnostic(PendingPhotoStagingResult& result,
							   PendingPhotoSource& pending_source,
							   Diagnostic diagnostic) {
	pending_source.diagnostics.push_back(diagnostic);
	result.diagnostics.push_back(std::move(diagnostic));
}

void append_pending_diagnostics(PendingPhotoStagingResult& result,
								PendingPhotoSource& pending_source,
								const std::vector<Diagnostic>& diagnostics) {
	for (const Diagnostic& diagnostic : diagnostics)
		append_pending_diagnostic(result, pending_source, diagnostic);
}

void append_cleanup_diagnostic(PendingPhotoCleanupResult& result,
							   PendingPhotoSource& pending_source,
							   Diagnostic diagnostic) {
	pending_source.diagnostics.push_back(diagnostic);
	result.diagnostics.push_back(std::move(diagnostic));
}

[[nodiscard]] std::optional<EntityEditDiagnostic> validate_photo_import_owner(
	const CatalogSessionState& session, const domain::PhotoOwner& owner) {
	if (owner.type == domain::PhotoOwnerType::Item) {
		if (catalog::find_item_envelope(session.repository, owner.id)
			!= nullptr) {
			return std::nullopt;
		}
	} else if (catalog::find_storage_envelope(session.repository, owner.id)
			   != nullptr) {
		return std::nullopt;
	}

	return make_entity_diagnostic(
		core::DiagnosticSeverity::ActionValidationError,
		"photo_import_owner_missing",
		"Photo import owner must be an accepted item or storage.",
		owner.id.value());
}

[[nodiscard]] std::optional<EntityEditDiagnostic> validate_photo_sources(
	std::span<const platform::ContentSourceDescriptor> sources) {
	if (!sources.empty())
		return std::nullopt;
	return make_entity_diagnostic(
		core::DiagnosticSeverity::ActionValidationError,
		"photo_import_no_sources",
		"Photo import needs at least one selected source image.");
}

[[nodiscard]] std::optional<Diagnostic> validate_photos_jsonl_text(
	std::string_view text, std::string code, std::string message) {
	persistence::PhotoTableLoadResult loaded =
		persistence::load_photo_jsonl(text);
	if (loaded.summary.rejected_lines == 0U)
		return std::nullopt;
	return make_diagnostic(
		DiagnosticSeverity::WriteBlockingError, std::move(code),
		std::move(message),
		"quarantineEntries="
			+ std::to_string(loaded.quarantine_entries.size()));
}

[[nodiscard]] std::string pending_display_name_for_source(
	const platform::ContentSourceDescriptor& source) {
	if (!source.display_name.empty())
		return source.display_name;
	if (!source.local_path.empty())
		return source.local_path.filename().string();
	if (!source.opaque_handle.empty())
		return source.opaque_handle;
	return "pending photo source";
}

[[nodiscard]] Diagnostic pending_stage_blocking_diagnostic(
	std::string code, std::string message, std::string technical_details = {}) {
	return make_diagnostic(DiagnosticSeverity::ActionValidationError,
						   std::move(code), std::move(message),
						   std::move(technical_details));
}

void append_pending_platform_failure(
	PendingPhotoStagingResult& result, PendingPhotoSource& pending_source,
	const platform::PlatformValueResult<platform::StagedContent>& staged) {
	append_pending_diagnostics(result, pending_source, staged.diagnostics);
	if (pending_source.diagnostics.empty()
		&& staged.category != OperationResultCategory::UserCancelled) {
		append_pending_diagnostic(
			result, pending_source,
			make_diagnostic(DiagnosticSeverity::WriteBlockingError,
							"pending_photo_staging_failed",
							"Pending photo source could not be staged."));
	}
}

void append_pending_fingerprint_failure(
	PendingPhotoStagingResult& result, PendingPhotoSource& pending_source,
	const platform::PlatformValueResult<platform::SourceByteFingerprint>&
		fingerprint) {
	append_pending_diagnostics(result, pending_source, fingerprint.diagnostics);
	if (pending_source.diagnostics.empty()) {
		append_pending_diagnostic(
			result, pending_source,
			make_diagnostic(DiagnosticSeverity::RecoverableWarning,
							"pending_photo_source_fingerprint_failed",
							"Pending photo duplicate fingerprint could not be "
							"computed; staging remains usable for import."));
	}
}

[[nodiscard]] std::optional<Diagnostic> pending_duplicate_warning(
	std::span<const PendingPhotoSource> pending_sources,
	const PendingPhotoSource& candidate) {
	if (candidate.source_md5.empty())
		return std::nullopt;

	for (const PendingPhotoSource& source : pending_sources) {
		if (source.source_md5 == candidate.source_md5) {
			return make_diagnostic(
				DiagnosticSeverity::RecoverableWarning,
				"pending_photo_duplicate_source_warning",
				"Pending photo source appears to duplicate another staged "
				"photo. Save is allowed.",
				candidate.display_name + " matches " + source.display_name);
		}
	}

	return std::nullopt;
}

[[nodiscard]] std::optional<Diagnostic> stored_owner_duplicate_warning(
	const CatalogSessionState& session, const domain::PhotoOwner& owner,
	const PendingPhotoSource& candidate) {
	if (candidate.source_md5.empty())
		return std::nullopt;

	for (const persistence::PhotoEnvelope& photo : session.repository.photos) {
		if (photo.record.owner_type == owner.type
			&& photo.record.owner_id == owner.id
			&& photo.record.source_md5 == candidate.source_md5) {
			return make_diagnostic(
				DiagnosticSeverity::RecoverableWarning,
				"pending_photo_duplicate_existing_source_warning",
				"Pending photo source appears to duplicate an existing owner "
				"photo. Save is allowed.",
				candidate.display_name + " matches photo "
					+ photo.record.id.value());
		}
	}

	return std::nullopt;
}

[[nodiscard]] platform::ContentSourceDescriptor staged_pending_descriptor(
	const platform::StagedContent& staged) {
	platform::ContentSourceDescriptor descriptor =
		platform::make_local_file_source(staged.staged_path,
										 staged.display_name);
	descriptor.byte_count = staged.byte_count;
	descriptor.transient  = true;
	return descriptor;
}

void finalize_pending_staging_result(PendingPhotoStagingResult& result) {
	for (const PendingPhotoSource& source : result.sources)
		if (source.status == PendingPhotoStatus::Staged)
			++result.staged_count;
		else if (source.status == PendingPhotoStatus::Cancelled)
			++result.cancelled_count;
		else if (source.status == PendingPhotoStatus::Failed)
			++result.failure_count;

	if (result.staged_count > 0U) {
		result.category = OperationResultCategory::Success;
		return;
	}
	if (result.cancelled_count > 0U) {
		result.category = OperationResultCategory::UserCancelled;
		return;
	}
	for (const PendingPhotoSource& source : result.sources) {
		if (!source.diagnostics.empty()) {
			result.category = OperationResultCategory::TemporaryStorageFailure;
			return;
		}
	}
	result.category = OperationResultCategory::Success;
}

[[nodiscard]] bool has_ready_pending_photos(
	std::span<const PendingPhotoSource> pending_sources) noexcept {
	return std::ranges::any_of(pending_sources,
							   [](const PendingPhotoSource& source) {
		return source.ready_for_import();
	});
}

struct ReadyPendingPhotoImportSources final {
	std::vector<platform::ContentSourceDescriptor> sources;
	std::vector<std::size_t> pending_source_indexes;
};

struct PendingMainPhotoSelection final {
	std::optional<core::StableIdentifier> imported_photo_id;
	bool selected_ready_source_was_imported{};
};

[[nodiscard]] PendingMainPhotoSelection find_imported_pending_main_photo(
	const catalog::PhotoImportSummary& summary,
	std::span<const std::size_t> pending_source_indexes,
	std::optional<std::size_t> selected_pending_source_index);
[[nodiscard]] EntityEditResult skipped_pending_main_photo_selection(
	CatalogSessionState session, std::size_t pending_source_index);

template<class SaveResult>
void apply_pending_main_photo_selection(
	SaveResult& result, const EntityEditRequest& edit_request,
	std::span<const std::size_t> pending_source_indexes,
	std::optional<std::size_t> main_pending_source_index) {
	if (!main_pending_source_index.has_value())
		return;

	result.main_selection_attempted = true;
	const PendingMainPhotoSelection selection =
		find_imported_pending_main_photo(result.import_result.summary,
										 pending_source_indexes,
										 main_pending_source_index);
	if (selection.imported_photo_id.has_value()) {
		result.main_selection_result = set_main_photo_in_session(
			edit_request, *selection.imported_photo_id);
		result.session = result.main_selection_result.session;
		if (result.main_selection_result.succeeded())
			result.main_selected_photo_id = selection.imported_photo_id;
		return;
	}

	result.main_selection_result = skipped_pending_main_photo_selection(
		result.session, *main_pending_source_index);
}

[[nodiscard]] ReadyPendingPhotoImportSources ready_pending_photo_import_sources(
	std::span<const PendingPhotoSource> pending_sources) {
	ReadyPendingPhotoImportSources result;
	for (std::size_t index = 0; index < pending_sources.size(); ++index) {
		const PendingPhotoSource& pending_source = pending_sources[index];
		if (!pending_source.ready_for_import())
			continue;

		result.sources.push_back(*pending_source.staged_source);
		result.pending_source_indexes.push_back(index);
	}
	return result;
}

void mark_pending_photo_sources_consumed(
	std::vector<PendingPhotoSource>& pending_sources,
	std::span<const std::size_t> pending_source_indexes) {
	for (const std::size_t index : pending_source_indexes)
		if (index < pending_sources.size())
			pending_sources[index].status = PendingPhotoStatus::Consumed;
}

[[nodiscard]] std::vector<core::StableIdentifier> imported_photo_ids(
	const catalog::PhotoImportSummary& summary) {
	std::vector<core::StableIdentifier> ids;
	for (const catalog::PhotoImportPhotoResult& photo : summary.photos) {
		if (photo.status == catalog::PhotoImportPhotoStatus::Imported
			&& photo.photo_id.has_value()) {
			ids.push_back(*photo.photo_id);
		}
	}
	return ids;
}

[[nodiscard]] PendingMainPhotoSelection find_imported_pending_main_photo(
	const catalog::PhotoImportSummary& summary,
	std::span<const std::size_t> pending_source_indexes,
	std::optional<std::size_t> selected_pending_source_index) {
	PendingMainPhotoSelection selection;
	if (!selected_pending_source_index.has_value())
		return selection;

	std::span<const std::size_t>::iterator ready_source = std::ranges::find(
		pending_source_indexes, *selected_pending_source_index);
	if (ready_source == pending_source_indexes.end())
		return selection;

	const std::size_t import_source_index = static_cast<std::size_t>(
		std::distance(pending_source_indexes.begin(), ready_source));
	for (const catalog::PhotoImportPhotoResult& photo : summary.photos) {
		if (photo.source_index != import_source_index)
			continue;
		if (photo.status == catalog::PhotoImportPhotoStatus::Imported
			&& photo.photo_id.has_value()) {
			selection.imported_photo_id					 = *photo.photo_id;
			selection.selected_ready_source_was_imported = true;
		}
		break;
	}
	return selection;
}

[[nodiscard]] EntityEditResult skipped_pending_main_photo_selection(
	CatalogSessionState session, std::size_t pending_source_index) {
	EntityEditResult result{
		.category = core::OperationResultCategory::ValidationFailure,
		.session  = std::move(session)};
	append_edit_diagnostic(
		result,
		make_entity_diagnostic(
			core::DiagnosticSeverity::RecoverableWarning,
			"pending_main_photo_not_imported",
			"Selected staged photo could not become main because it was not "
			"imported.",
			std::to_string(pending_source_index)));
	return result;
}

[[nodiscard]] std::optional<EntityEditDiagnostic> commit_photo_records(
	EntityEditResult& result, const EntityEditRequest& request,
	std::span<const persistence::PhotoEnvelope> candidate_photos,
	const std::filesystem::path& active_catalog_root) {
	persistence::JsonTextWriteResult photo_text =
		persistence::write_photo_jsonl(candidate_photos);
	if (!photo_text.succeeded()) {
		append_edit_diagnostics(result, photo_text.diagnostics);
		return make_entity_diagnostic(
			core::DiagnosticSeverity::WriteBlockingError,
			"photos_jsonl_write_failed",
			"Photo metadata could not be serialized for commit.");
	}
	persistence::CatalogStorageResult committed =
		persistence::commit_metadata_file(
			persistence::CatalogMetadataCommitRequest{
				.active_catalog_root  = active_catalog_root,
				.relative_target_path = std::filesystem::path{std::string{
					persistence::photos_data_file_path}},
				.serialized_content	  = std::move(photo_text.text),
				.committed_at		  = request.clock.now(),
				.operation_id = request.identifiers.next_operation_identifier(),
				.validator =
					[](std::string_view text) {
		return validate_photos_jsonl_text(
			text, "photos_jsonl_validation_failed",
			"Photo metadata JSONL did not validate before replacement.");
	},
				.create_previous_copy = request.create_previous_copy});
	append_edit_diagnostics(result, committed.diagnostics);
	if (committed.failed()) {
		return make_entity_diagnostic(
			core::DiagnosticSeverity::WriteBlockingError,
			"photos_jsonl_commit_failed", "Photo metadata replacement failed.");
	}
	return std::nullopt;
}
}	 // namespace

bool PhotoImportSessionResult::succeeded() const noexcept {
	return category == core::OperationResultCategory::Success;
}

bool PhotoImportSessionResult::was_user_cancelled() const noexcept {
	return category == core::OperationResultCategory::UserCancelled;
}

bool PhotoImportSessionResult::failed() const noexcept {
	return !succeeded() && !was_user_cancelled();
}

bool PhotoImportSessionResult::has_partial_failures() const noexcept {
	return summary.has_partial_failures();
}

std::string_view to_string(PendingPhotoStatus status) noexcept {
	switch (status) {
		case PendingPhotoStatus::Selected:
			return "selected";
		case PendingPhotoStatus::Staged:
			return "staged";
		case PendingPhotoStatus::Failed:
			return "failed";
		case PendingPhotoStatus::Cancelled:
			return "cancelled";
		case PendingPhotoStatus::Removed:
			return "removed";
		case PendingPhotoStatus::Consumed:
			return "consumed";
	}
	return "unknown pending photo status";
}

bool PendingPhotoSource::ready_for_import() const noexcept {
	return status == PendingPhotoStatus::Staged && staged_source.has_value();
}

bool PendingPhotoStagingResult::succeeded() const noexcept {
	return category == core::OperationResultCategory::Success;
}

bool PendingPhotoStagingResult::was_user_cancelled() const noexcept {
	return category == core::OperationResultCategory::UserCancelled;
}

bool PendingPhotoStagingResult::failed() const noexcept {
	return !succeeded() && !was_user_cancelled();
}

bool PendingPhotoStagingResult::has_partial_failures() const noexcept {
	return staged_count > 0U && (failure_count > 0U || cancelled_count > 0U);
}

bool PendingPhotoCleanupResult::succeeded() const noexcept {
	return category == core::OperationResultCategory::Success;
}

bool PendingPhotoCleanupResult::failed() const noexcept {
	return !succeeded();
}

bool ItemSaveWithPendingPhotosResult::item_saved() const noexcept {
	return save_result.succeeded() && save_result.saved_record_id.has_value();
}

bool ItemSaveWithPendingPhotosResult::warning_acknowledgement_required()
	const noexcept {
	return save_result.warning_acknowledgement_required;
}

bool ItemSaveWithPendingPhotosResult::import_failed() const noexcept {
	return import_attempted && import_result.failed();
}

bool StorageSaveWithPendingPhotosResult::storage_saved() const noexcept {
	return save_result.succeeded() && save_result.saved_record_id.has_value();
}

bool StorageSaveWithPendingPhotosResult::warning_acknowledgement_required()
	const noexcept {
	return save_result.warning_acknowledgement_required;
}

bool StorageSaveWithPendingPhotosResult::import_failed() const noexcept {
	return import_attempted && import_result.failed();
}

EntityEditResult set_main_photo_in_session(
	const EntityEditRequest& request, const core::StableIdentifier& photo_id) {
	EntityEditResult result{.session = request.current_session};
	if (!request.current_session.ready_for_browsing()) {
		result.category = core::OperationResultCategory::ValidationFailure;
		append_edit_diagnostic(
			result,
			make_entity_diagnostic(
				core::DiagnosticSeverity::ActionValidationError,
				"catalog_not_browsable",
				"Catalog must be loaded before photo metadata can be edited."));
		return result;
	}

	const std::filesystem::path active_catalog_root =
		active_catalog_root_for_edit(request);
	if (std::optional<EntityEditDiagnostic> diagnostic =
			missing_active_root_diagnostic(active_catalog_root)) {
		result.category = core::OperationResultCategory::ValidationFailure;
		append_edit_diagnostic(result, std::move(*diagnostic));
		return result;
	}

	const persistence::PhotoEnvelope* selected = catalog::find_photo_envelope(
		request.current_session.repository, photo_id);
	if (selected == nullptr) {
		result.category = core::OperationResultCategory::ValidationFailure;
		append_edit_diagnostic(
			result,
			make_entity_diagnostic(
				core::DiagnosticSeverity::ActionValidationError,
				"photo_not_found",
				"Selected photo cannot become main because it is missing.",
				photo_id.value()));
		return result;
	}
	const domain::PhotoOwner selected_owner =
		domain::owner_of(selected->record);

	std::vector<persistence::PhotoEnvelope> candidate_photos =
		request.current_session.repository.photos;
	std::vector<domain::PhotoRecord> photo_records;
	photo_records.reserve(candidate_photos.size());
	for (const persistence::PhotoEnvelope& photo : candidate_photos) {
		if (photo.record.owner_type == selected_owner.type
			&& photo.record.owner_id == selected_owner.id) {
			photo_records.push_back(photo.record);
		}
	}

	if (!domain::select_main_photo(photo_records, photo_id)) {
		result.category = core::OperationResultCategory::ValidationFailure;
		append_edit_diagnostic(
			result, make_entity_diagnostic(
						core::DiagnosticSeverity::ActionValidationError,
						"photo_main_selection_failed",
						"Selected photo could not be applied as main.",
						photo_id.value()));
		return result;
	}

	bool changed = false;
	for (persistence::PhotoEnvelope& candidate : candidate_photos) {
		if (candidate.record.owner_type != selected_owner.type
			|| candidate.record.owner_id != selected_owner.id) {
			continue;
		}
		const std::vector<domain::PhotoRecord>::const_iterator updated =
			std::ranges::find_if(photo_records,
								 [&](const domain::PhotoRecord& photo) {
			return photo.id == candidate.record.id;
		});
		if (updated == photo_records.end())
			continue;
		if (candidate.record.is_main != updated->is_main)
			changed = true;
		candidate.record.is_main = updated->is_main;
	}
	if (!changed) {
		result.saved_record_id = photo_id;
		return result;
	}

	if (std::optional<EntityEditDiagnostic> diagnostic = commit_photo_records(
			result, request, candidate_photos, active_catalog_root)) {
		result.category = core::OperationResultCategory::ReplacementFailure;
		append_edit_diagnostic(result, std::move(*diagnostic));
		return result;
	}
	result.session =
		rebuild_photo_session(request.current_session,
							  std::move(candidate_photos), active_catalog_root);
	result.saved_record_id	= photo_id;
	result.metadata_changed = true;
	return result;
}

EntityEditResult delete_photo_in_session(
	const EntityEditRequest& request, const core::StableIdentifier& photo_id) {
	EntityEditResult result{.session = request.current_session};
	if (!request.current_session.ready_for_browsing()) {
		result.category = core::OperationResultCategory::ValidationFailure;
		append_edit_diagnostic(
			result,
			make_entity_diagnostic(
				core::DiagnosticSeverity::ActionValidationError,
				"catalog_not_browsable",
				"Catalog must be loaded before photo metadata can be edited."));
		return result;
	}

	const std::filesystem::path active_catalog_root =
		active_catalog_root_for_edit(request);
	if (std::optional<EntityEditDiagnostic> diagnostic =
			missing_active_root_diagnostic(active_catalog_root)) {
		result.category = core::OperationResultCategory::ValidationFailure;
		append_edit_diagnostic(result, std::move(*diagnostic));
		return result;
	}

	const persistence::PhotoEnvelope* selected = catalog::find_photo_envelope(
		request.current_session.repository, photo_id);
	if (selected == nullptr) {
		result.category = core::OperationResultCategory::ValidationFailure;
		append_edit_diagnostic(
			result,
			make_entity_diagnostic(
				core::DiagnosticSeverity::ActionValidationError,
				"photo_not_found",
				"Selected photo cannot be deleted because it is missing.",
				photo_id.value()));
		return result;
	}

	std::vector<persistence::PhotoEnvelope> candidate_photos;
	candidate_photos.reserve(request.current_session.repository.photos.size());
	for (const persistence::PhotoEnvelope& photo :
		 request.current_session.repository.photos) {
		if (photo.record.id != photo_id)
			candidate_photos.push_back(photo);
	}

	if (std::optional<EntityEditDiagnostic> diagnostic = commit_photo_records(
			result, request, candidate_photos, active_catalog_root)) {
		result.category = core::OperationResultCategory::ReplacementFailure;
		append_edit_diagnostic(result, std::move(*diagnostic));
		return result;
	}

	const std::filesystem::path media_path =
		internal_photo_media_path(active_catalog_root, photo_id);
	if (std::optional<EntityEditDiagnostic> diagnostic =
			cleanup_deleted_photo_media(media_path)) {
		append_edit_diagnostic(result, std::move(*diagnostic));
	}

	result.session =
		rebuild_photo_session(request.current_session,
							  std::move(candidate_photos), active_catalog_root);
	result.saved_record_id	= photo_id;
	result.metadata_changed = true;
	return result;
}

PhotoImportSessionResult import_photos_into_session(
	const PhotoImportSessionRequest& request,
	platform::ProgressSink& progress_sink,
	platform::CancellationToken& cancellation_token) {
	PhotoImportSessionResult result{.session = request.current_session};
	if (!request.current_session.ready_for_browsing()) {
		result.category = core::OperationResultCategory::ValidationFailure;
		append_import_diagnostic(
			result,
			make_entity_diagnostic(
				core::DiagnosticSeverity::ActionValidationError,
				"catalog_not_browsable",
				"Catalog must be loaded before photos can be imported."));
		return result;
	}

	const std::filesystem::path active_catalog_root =
		active_catalog_root_for_photo_import(request);
	if (std::optional<EntityEditDiagnostic> diagnostic =
			missing_active_root_diagnostic(active_catalog_root)) {
		result.category = core::OperationResultCategory::ValidationFailure;
		append_import_diagnostic(result, std::move(*diagnostic));
		return result;
	}
	if (std::optional<EntityEditDiagnostic> diagnostic =
			validate_photo_import_owner(request.current_session,
										request.owner)) {
		result.category = core::OperationResultCategory::ValidationFailure;
		append_import_diagnostic(result, std::move(*diagnostic));
		return result;
	}
	if (std::optional<EntityEditDiagnostic> diagnostic =
			validate_photo_sources(request.sources)) {
		result.category = core::OperationResultCategory::ValidationFailure;
		append_import_diagnostic(result, std::move(*diagnostic));
		return result;
	}

	platform::AppPrivatePaths paths = *request.current_session.paths;
	paths.active_catalog_root		= active_catalog_root;
	paths.media_root = active_catalog_root
					   / std::filesystem::path{std::string{
						   persistence::photo_media_directory_path}};
	catalog::PhotoImportUseCase use_case{
		request.identifiers,		 request.clock,
		request.operation_gate,		 request.staging_service,
		request.fingerprint_service, request.decode_service,
		request.photo_codec};
	result.summary = use_case.import_photos(
		catalog::PhotoImportRequest{
			.current_state = request.current_session.repository,
			.paths		   = std::move(paths),
			.owner		   = request.owner,
			.sources	   = request.sources,
			.photo_table_validator =
				[](std::string_view text) {
		return validate_photos_jsonl_text(
			text, "photos_jsonl_validation_failed",
			"Photo metadata JSONL did not validate before replacement.");
	},
			.create_previous_copy = request.create_previous_copy},
		progress_sink, cancellation_token);
	result.category			  = result.summary.category;
	result.metadata_changed	  = result.summary.metadata_changed;
	result.imported_photo_ids = imported_photo_ids(result.summary);
	append_import_diagnostics(result, result.summary.diagnostics);
	if (result.summary.metadata_changed) {
		result.session = rebuild_photo_session(
			request.current_session, result.summary.updated_state.photos,
			active_catalog_root);
	}
	return result;
}

ItemSaveWithPendingPhotosResult save_item_draft_and_import_pending_photos(
	const ItemSaveWithPendingPhotosRequest& request,
	platform::ProgressSink& progress_sink,
	platform::CancellationToken& cancellation_token) {
	ItemSaveWithPendingPhotosResult result{
		.session		 = request.current_session,
		.pending_sources = request.pending_sources};
	result.import_result.session = request.current_session;

	ItemDraft draft = request.draft;
	draft.pending_photo_import_planned =
		has_ready_pending_photos(result.pending_sources);
	result.save_result = save_item_draft(
		EntityEditRequest{.current_session = request.current_session,
						  .identifiers	   = request.identifiers,
						  .clock		   = request.clock,
						  .active_catalog_root_override =
							  request.active_catalog_root_override,
						  .create_previous_copy = request.create_previous_copy},
		draft);
	result.session				 = result.save_result.session;
	result.import_result.session = result.session;
	if (result.save_result.warning_acknowledgement_required
		|| result.save_result.failed()
		|| !result.save_result.saved_record_id.has_value()) {
		return result;
	}

	ReadyPendingPhotoImportSources pending_import_sources =
		ready_pending_photo_import_sources(result.pending_sources);
	if (pending_import_sources.sources.empty())
		return result;

	result.import_attempted = true;
	result.import_result	= import_photos_into_session(
		PhotoImportSessionRequest{
			.current_session	 = result.save_result.session,
			.identifiers		 = request.identifiers,
			.clock				 = request.clock,
			.operation_gate		 = request.operation_gate,
			.staging_service	 = request.staging_service,
			.fingerprint_service = request.fingerprint_service,
			.decode_service		 = request.decode_service,
			.photo_codec		 = request.photo_codec,
			.owner =
				domain::PhotoOwner{.type = domain::PhotoOwnerType::Item,
								   .id	 = *result.save_result.saved_record_id},
			.sources = std::move(pending_import_sources.sources),
			.active_catalog_root_override =
				request.active_catalog_root_override,
			.create_previous_copy = request.create_previous_copy},
		progress_sink, cancellation_token);
	result.session = result.import_result.session;
	apply_pending_main_photo_selection(
		result,
		EntityEditRequest{.current_session = result.session,
						  .identifiers	   = request.identifiers,
						  .clock		   = request.clock,
						  .active_catalog_root_override =
							  request.active_catalog_root_override,
						  .create_previous_copy = request.create_previous_copy},
		pending_import_sources.pending_source_indexes,
		request.main_pending_source_index);

	mark_pending_photo_sources_consumed(
		result.pending_sources, pending_import_sources.pending_source_indexes);
	result.cleanup_attempted = true;
	result.cleanup_result =
		cleanup_pending_photo_sources(result.pending_sources);
	return result;
}

StorageSaveWithPendingPhotosResult save_storage_draft_and_import_pending_photos(
	const StorageSaveWithPendingPhotosRequest& request,
	platform::ProgressSink& progress_sink,
	platform::CancellationToken& cancellation_token) {
	StorageSaveWithPendingPhotosResult result{
		.session		 = request.current_session,
		.pending_sources = request.pending_sources};
	result.import_result.session = request.current_session;

	result.save_result = save_storage_draft(
		EntityEditRequest{.current_session = request.current_session,
						  .identifiers	   = request.identifiers,
						  .clock		   = request.clock,
						  .active_catalog_root_override =
							  request.active_catalog_root_override,
						  .create_previous_copy = request.create_previous_copy},
		request.draft);
	result.session				 = result.save_result.session;
	result.import_result.session = result.session;
	if (result.save_result.warning_acknowledgement_required
		|| result.save_result.failed()
		|| !result.save_result.saved_record_id.has_value()) {
		return result;
	}

	ReadyPendingPhotoImportSources pending_import_sources =
		ready_pending_photo_import_sources(result.pending_sources);
	if (pending_import_sources.sources.empty())
		return result;

	result.import_attempted = true;
	result.import_result	= import_photos_into_session(
		PhotoImportSessionRequest{
			.current_session	 = result.save_result.session,
			.identifiers		 = request.identifiers,
			.clock				 = request.clock,
			.operation_gate		 = request.operation_gate,
			.staging_service	 = request.staging_service,
			.fingerprint_service = request.fingerprint_service,
			.decode_service		 = request.decode_service,
			.photo_codec		 = request.photo_codec,
			.owner =
				domain::PhotoOwner{.type = domain::PhotoOwnerType::Storage,
								   .id	 = *result.save_result.saved_record_id},
			.sources = std::move(pending_import_sources.sources),
			.active_catalog_root_override =
				request.active_catalog_root_override,
			.create_previous_copy = request.create_previous_copy},
		progress_sink, cancellation_token);
	result.session = result.import_result.session;
	apply_pending_main_photo_selection(
		result,
		EntityEditRequest{.current_session = result.session,
						  .identifiers	   = request.identifiers,
						  .clock		   = request.clock,
						  .active_catalog_root_override =
							  request.active_catalog_root_override,
						  .create_previous_copy = request.create_previous_copy},
		pending_import_sources.pending_source_indexes,
		request.main_pending_source_index);

	mark_pending_photo_sources_consumed(
		result.pending_sources, pending_import_sources.pending_source_indexes);
	result.cleanup_attempted = true;
	result.cleanup_result =
		cleanup_pending_photo_sources(result.pending_sources);
	return result;
}

PendingPhotoStagingResult stage_pending_photos_for_session(
	const PendingPhotoStagingRequest& request,
	platform::ProgressSink& progress_sink,
	platform::CancellationToken& cancellation_token) {
	PendingPhotoStagingResult result;
	if (!request.current_session.paths) {
		result.category = OperationResultCategory::ValidationFailure;
		result.diagnostics.push_back(pending_stage_blocking_diagnostic(
			"catalog_paths_missing",
			"App-private catalog paths are unavailable for pending photo "
			"staging."));
		return result;
	}
	if (request.sources.empty()) {
		result.category = OperationResultCategory::ValidationFailure;
		result.diagnostics.push_back(pending_stage_blocking_diagnostic(
			"pending_photo_staging_no_sources",
			"Pending photo staging needs at least one selected source image."));
		return result;
	}

	const core::OperationIdentifier operation_id =
		request.identifiers.next_operation_identifier();
	platform::PlatformOperationStartResult operation_start =
		platform::try_start_platform_operation(
			request.operation_gate,
			platform::PlatformOperationStartRequest{
				.operation_kind = core::OperationKind::PhotoImport,
				.operation_id	= operation_id,
				.operation_type = platform::ProgressOperationType::PhotoImport},
			progress_sink, cancellation_token);
	if (!operation_start.succeeded()) {
		result.category	   = operation_start.category;
		result.diagnostics = std::move(operation_start.diagnostics);
		return result;
	}

	platform::ScopedPlatformOperation& operation = *operation_start.operation;
	const std::uint64_t total_sources =
		static_cast<std::uint64_t>(request.sources.size());
	operation.publish_progress("pending-photo-staging-started",
							   std::uint64_t{0}, total_sources,
							   "Pending photo staging started.", true);

	for (std::size_t index = 0; index < request.sources.size(); ++index) {
		const platform::ContentSourceDescriptor& source =
			request.sources[index];
		PendingPhotoSource pending_source{
			.source_index = index,
			.display_name = pending_display_name_for_source(source),
			.byte_count	  = source.byte_count,
			.status		  = PendingPhotoStatus::Selected};

		if (operation.cancellation_requested()) {
			pending_source.status = PendingPhotoStatus::Cancelled;
			result.sources.push_back(std::move(pending_source));
			break;
		}

		operation.publish_progress(
			"pending-photo-source-started",
			static_cast<std::uint64_t>(index + 1U), total_sources,
			"Pending photo source staging started.", true);
		const std::string staged_name = platform::make_staged_content_file_name(
			"pending-photo-source", operation.context().operation_id,
			index + 1U, pending_source.display_name);
		platform::PlatformValueResult<platform::StagedContent> staged =
			request.staging_service.stage_content(
				platform::ContentStagingRequest{
					.source = source,
					.target_directory =
						request.current_session.paths->staged_content_root,
					.target_file_name			= staged_name,
					.allow_no_copy_optimization = false},
				operation.context(), progress_sink, cancellation_token);
		if (!staged.succeeded()) {
			if (staged.was_user_cancelled()) {
				pending_source.status = PendingPhotoStatus::Cancelled;
			} else {
				pending_source.status = PendingPhotoStatus::Failed;
				append_pending_platform_failure(result, pending_source, staged);
			}
			result.sources.push_back(std::move(pending_source));
			if (staged.was_user_cancelled())
				break;
			continue;
		}

		pending_source.status		 = PendingPhotoStatus::Staged;
		pending_source.display_name	 = staged.value->display_name.empty()
										   ? pending_source.display_name
										   : staged.value->display_name;
		pending_source.byte_count	 = staged.value->byte_count;
		pending_source.staged_path	 = staged.value->staged_path;
		pending_source.staged_source = staged_pending_descriptor(*staged.value);
		platform::PlatformValueResult<platform::SourceByteFingerprint>
			fingerprint = request.fingerprint_service.fingerprint_source_bytes(
				platform::SourceByteFingerprintRequest{
					.source_path = staged.value->staged_path},
				operation.context(), progress_sink, cancellation_token);
		if (fingerprint.was_user_cancelled()) {
			pending_source.status = PendingPhotoStatus::Cancelled;
			result.sources.push_back(std::move(pending_source));
			break;
		}
		if (fingerprint.succeeded()) {
			pending_source.source_md5 =
				std::move(fingerprint.value->source_md5);
			if (std::optional<Diagnostic> warning = pending_duplicate_warning(
					request.existing_pending_sources, pending_source)) {
				append_pending_diagnostic(result, pending_source,
										  std::move(*warning));
			}
			if (std::optional<Diagnostic> warning =
					pending_duplicate_warning(result.sources, pending_source)) {
				append_pending_diagnostic(result, pending_source,
										  std::move(*warning));
			}
			if (request.existing_owner.has_value()) {
				if (std::optional<Diagnostic> warning =
						stored_owner_duplicate_warning(request.current_session,
													   *request.existing_owner,
													   pending_source)) {
					append_pending_diagnostic(result, pending_source,
											  std::move(*warning));
				}
			}
		} else {
			append_pending_fingerprint_failure(result, pending_source,
											   fingerprint);
		}
		result.sources.push_back(std::move(pending_source));
	}

	operation.publish_progress("pending-photo-staging-completed", total_sources,
							   total_sources,
							   "Pending photo staging completed.", false);
	finalize_pending_staging_result(result);
	return result;
}

PendingPhotoCleanupResult cleanup_pending_photo_sources(
	std::vector<PendingPhotoSource>& pending_sources) {
	PendingPhotoCleanupResult result;
	for (PendingPhotoSource& pending_source : pending_sources) {
		if (!pending_source.staged_path.has_value())
			continue;

		++result.cleanup_attempt_count;
		std::error_code error;
		std::filesystem::remove(*pending_source.staged_path, error);
		if (error) {
			++result.failure_count;
			append_cleanup_diagnostic(
				result, pending_source,
				make_diagnostic(
					DiagnosticSeverity::RecoverableWarning,
					"pending_photo_cleanup_failed",
					"Pending staged photo source could not be removed.",
					pending_source.staged_path->string() + ": "
						+ error.message()));
			continue;
		}

		++result.removed_count;
		pending_source.staged_source.reset();
		pending_source.staged_path.reset();
		if (pending_source.status != PendingPhotoStatus::Consumed)
			pending_source.status = PendingPhotoStatus::Removed;
	}

	if (result.failure_count > 0U)
		result.category = OperationResultCategory::TemporaryStorageFailure;
	return result;
}
}	 // namespace shuba::ui
