#include "UI/Session/EntityEditSession.hpp"

#include "Catalog/CatalogRepository.hpp"
#include "Persistence/CatalogStorage.hpp"
#include "Persistence/MetadataSchema.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
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
							 const std::vector<Diagnostic>& diagnostics) {
	for (const Diagnostic& diagnostic : diagnostics)
		append_edit_diagnostic(result, entity_diagnostic_from_core(diagnostic));
}

void append_edit_diagnostics(EntityEditResult& result,
							 const std::vector<JsonlDiagnostic>& diagnostics) {
	for (const JsonlDiagnostic& diagnostic : diagnostics) {
		append_edit_diagnostic(result,
							   entity_diagnostic_from_jsonl(diagnostic));
	}
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

[[nodiscard]] std::optional<Diagnostic> validate_items_jsonl_text(
	std::string_view text, std::string code, std::string message) {
	persistence::ItemTableLoadResult loaded =
		persistence::load_item_jsonl(text);
	if (loaded.summary.rejected_lines == 0U)
		return std::nullopt;
	return make_diagnostic(
		DiagnosticSeverity::WriteBlockingError, std::move(code),
		std::move(message),
		"quarantineEntries="
			+ std::to_string(loaded.quarantine_entries.size()));
}

[[nodiscard]] std::optional<Diagnostic> validate_storages_jsonl_text(
	std::string_view text, std::string code, std::string message) {
	persistence::StorageTableLoadResult loaded =
		persistence::load_storage_jsonl(text);
	if (loaded.summary.rejected_lines == 0U)
		return std::nullopt;
	return make_diagnostic(
		DiagnosticSeverity::WriteBlockingError, std::move(code),
		std::move(message),
		"quarantineEntries="
			+ std::to_string(loaded.quarantine_entries.size()));
}

[[nodiscard]] std::vector<domain::StorageRecord> storage_records(
	std::span<const persistence::StorageEnvelope> storages) {
	std::vector<domain::StorageRecord> records;
	records.reserve(storages.size());
	for (const persistence::StorageEnvelope& storage : storages)
		records.push_back(storage.record);
	return records;
}

[[nodiscard]] std::filesystem::path active_catalog_root_for_edit(
	const EntityEditRequest& request) {
	if (request.active_catalog_root_override)
		return *request.active_catalog_root_override;
	if (request.current_session.paths)
		return request.current_session.paths->active_catalog_root;
	return {};
}

[[nodiscard]] CatalogSessionState rebuild_edit_session(
	CatalogSessionState session, std::vector<persistence::ItemEnvelope> items,
	std::vector<persistence::StorageEnvelope> storages,
	const std::filesystem::path& active_catalog_root) {
	session.load_result.items = persistence::ItemTableLoadResult{
		.records = items,
		.summary = persistence::JsonlFileSummary{
			.path			  = std::string{persistence::items_data_file_path},
			.accepted_records = static_cast<std::uint64_t>(items.size())}};
	session.load_result.storages = persistence::StorageTableLoadResult{
		.records = storages,
		.summary = persistence::JsonlFileSummary{
			.path = std::string{persistence::storages_data_file_path},
			.accepted_records = static_cast<std::uint64_t>(storages.size())}};
	session.load_result.diagnostics.clear();
	session.load_result.load_status = persistence::CatalogLoadStatus::Normal;
	CatalogMediaSnapshot media =
		scan_photo_media(active_catalog_root, session.startup_diagnostics);
	const bool media_scan_degraded = !media.complete_scan_available;
	session.repository =
		catalog::build_catalog_repository(catalog::CatalogRepositoryInput{
			.items	  = std::move(items),
			.storages = std::move(storages),
			.photos	  = session.load_result.photos.records,
			.media	  = std::move(media)});
	session.search_index = catalog::build_search_index(session.repository);
	session.load_status =
		session.repository.diagnostics.empty() && !media_scan_degraded
			? persistence::CatalogLoadStatus::Normal
			: persistence::CatalogLoadStatus::Degraded;
	session.load_result.load_status = session.load_status;
	return session;
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

[[nodiscard]] std::optional<EntityEditDiagnostic> validate_new_tags(
	std::span<const domain::TagRow> tags) {
	for (const domain::TagRow& tag : tags) {
		domain::TagValidationResult validation =
			domain::validate_tag_for_ui_save(tag);
		if (!validation.accepted) {
			return make_entity_diagnostic(
				core::DiagnosticSeverity::ActionValidationError,
				"blank_tag_key_blocked",
				"Tag rows saved from the UI must have a non-blank key.");
		}
	}
	return std::nullopt;
}

[[nodiscard]] std::optional<EntityEditDiagnostic> validate_item_required_fields(
	const domain::ItemRecord& item) {
	const std::vector<domain::RecordRequiredFieldIssue> issues =
		domain::validate_required_fields(item);
	if (issues.empty())
		return std::nullopt;
	return make_entity_diagnostic(
		core::DiagnosticSeverity::ActionValidationError,
		"item_required_fields_missing",
		"Item display name and category are required before saving.");
}

[[nodiscard]] std::optional<EntityEditDiagnostic>
validate_storage_required_fields(const domain::StorageRecord& storage) {
	const std::vector<domain::RecordRequiredFieldIssue> issues =
		domain::validate_required_fields(storage);
	if (issues.empty())
		return std::nullopt;
	return make_entity_diagnostic(
		core::DiagnosticSeverity::ActionValidationError,
		"storage_required_fields_missing",
		"Storage display name and storage type are required before saving.");
}

void append_item_save_nudges(EntityEditResult& result,
							 const CatalogSessionState& session,
							 const ItemDraft& draft,
							 const core::StableIdentifier& item_id) {
	if (!draft.storage_id) {
		append_edit_diagnostic(
			result,
			make_entity_diagnostic(core::DiagnosticSeverity::RecoverableWarning,
								   "item_saved_without_storage",
								   "Item has no storage. It remains visible "
								   "and can be assigned later."));
	}
	const std::map<std::string, catalog::ItemProjection>::const_iterator found =
		session.repository.item_projections.find(item_id.value());
	const bool item_has_no_photos =
		found == session.repository.item_projections.end()
		|| found->second.photo_presence
			   == catalog::PhotoPresenceState::NoPhotoRecords;
	if (item_has_no_photos && !draft.pending_photo_import_planned) {
		append_edit_diagnostic(
			result,
			make_entity_diagnostic(
				core::DiagnosticSeverity::RecoverableWarning,
				"item_saved_without_photo",
				"Item has no photos yet. Photo import is handled by B19."));
	}
}

[[nodiscard]] std::optional<EntityEditDiagnostic> validate_storage_parent(
	const StorageDraft& draft, const core::StableIdentifier& storage_id,
	std::span<const persistence::StorageEnvelope> candidate_storages) {
	if (!draft.parent_storage_id)
		return std::nullopt;
	const std::vector<domain::StorageRecord> records =
		storage_records(candidate_storages);
	if (!domain::contains_storage_id(records, *draft.parent_storage_id)) {
		return make_entity_diagnostic(
			core::DiagnosticSeverity::ActionValidationError,
			"storage_parent_missing",
			"Parent storage must be empty or point to an accepted storage.",
			draft.parent_storage_id->value());
	}
	domain::StorageCycleCheck cycle = domain::would_create_storage_parent_cycle(
		storage_id, draft.parent_storage_id, records);
	if (!cycle)
		return std::nullopt;
	return make_entity_diagnostic(
		core::DiagnosticSeverity::ActionValidationError, "storage_parent_cycle",
		"Parent storage selection would create a storage cycle.");
}

[[nodiscard]] std::optional<EntityEditDiagnostic> storage_archive_warning(
	const CatalogSessionState& session,
	const core::StableIdentifier& storage_id, const StorageDraft& draft) {
	if (draft.lifecycle_status != domain::StorageLifecycleStatus::Archived
		|| draft.archive_warning_acknowledged) {
		return std::nullopt;
	}
	const std::map<std::string, catalog::StorageProjection>::const_iterator
		found = session.repository.storage_projections.find(storage_id.value());
	if (found == session.repository.storage_projections.end())
		return std::nullopt;
	if (found->second.direct_item_count == 0U
		&& found->second.nested_item_count == 0U
		&& found->second.direct_child_storage_ids.empty()) {
		return std::nullopt;
	}
	return make_entity_diagnostic(
		core::DiagnosticSeverity::RecoverableWarning,
		"archive_storage_with_contents",
		"Archiving a storage keeps child storages and active items visible in "
		"item search. Confirm to save anyway.");
}

[[nodiscard]] std::optional<EntityEditDiagnostic> commit_item_records(
	EntityEditResult& result, const EntityEditRequest& request,
	std::span<const persistence::ItemEnvelope> candidate_items,
	const std::filesystem::path& active_catalog_root) {
	persistence::JsonTextWriteResult item_text =
		persistence::write_item_jsonl(candidate_items);
	if (!item_text.succeeded()) {
		append_edit_diagnostics(result, item_text.diagnostics);
		return make_entity_diagnostic(
			core::DiagnosticSeverity::WriteBlockingError,
			"items_jsonl_write_failed",
			"Item metadata could not be serialized for commit.");
	}
	persistence::CatalogStorageResult committed =
		persistence::commit_metadata_file(
			persistence::CatalogMetadataCommitRequest{
				.active_catalog_root  = active_catalog_root,
				.relative_target_path = std::filesystem::path{std::string{
					persistence::items_data_file_path}},
				.serialized_content	  = std::move(item_text.text),
				.committed_at		  = request.clock.now(),
				.operation_id = request.identifiers.next_operation_identifier(),
				.validator =
					[](std::string_view text) {
		return validate_items_jsonl_text(
			text, "items_jsonl_validation_failed",
			"Item metadata JSONL did not validate before replacement.");
	},
				.create_previous_copy = request.create_previous_copy});
	append_edit_diagnostics(result, committed.diagnostics);
	if (committed.failed()) {
		return make_entity_diagnostic(
			core::DiagnosticSeverity::WriteBlockingError,
			"items_jsonl_commit_failed", "Item metadata replacement failed.");
	}
	return std::nullopt;
}

[[nodiscard]] std::optional<EntityEditDiagnostic> commit_storage_records(
	EntityEditResult& result, const EntityEditRequest& request,
	std::span<const persistence::StorageEnvelope> candidate_storages,
	const std::filesystem::path& active_catalog_root) {
	persistence::JsonTextWriteResult storage_text =
		persistence::write_storage_jsonl(candidate_storages);
	if (!storage_text.succeeded()) {
		append_edit_diagnostics(result, storage_text.diagnostics);
		return make_entity_diagnostic(
			core::DiagnosticSeverity::WriteBlockingError,
			"storages_jsonl_write_failed",
			"Storage metadata could not be serialized for commit.");
	}
	persistence::CatalogStorageResult committed =
		persistence::commit_metadata_file(
			persistence::CatalogMetadataCommitRequest{
				.active_catalog_root  = active_catalog_root,
				.relative_target_path = std::filesystem::path{std::string{
					persistence::storages_data_file_path}},
				.serialized_content	  = std::move(storage_text.text),
				.committed_at		  = request.clock.now(),
				.operation_id = request.identifiers.next_operation_identifier(),
				.validator =
					[](std::string_view text) {
		return validate_storages_jsonl_text(
			text, "storages_jsonl_validation_failed",
			"Storage metadata JSONL did not validate before replacement.");
	},
				.create_previous_copy = request.create_previous_copy});
	append_edit_diagnostics(result, committed.diagnostics);
	if (committed.failed()) {
		return make_entity_diagnostic(
			core::DiagnosticSeverity::WriteBlockingError,
			"storages_jsonl_commit_failed",
			"Storage metadata replacement failed.");
	}
	return std::nullopt;
}
}	 // namespace

EntityEditResult save_item_draft(const EntityEditRequest& request,
								 const ItemDraft& draft) {
	EntityEditResult result{.session = request.current_session};
	if (!request.current_session.ready_for_browsing()) {
		result.category = core::OperationResultCategory::ValidationFailure;
		append_edit_diagnostic(
			result,
			make_entity_diagnostic(
				core::DiagnosticSeverity::ActionValidationError,
				"catalog_not_browsable",
				"Catalog must be loaded before item metadata can be edited."));
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
	if (std::optional<EntityEditDiagnostic> diagnostic =
			validate_new_tags(draft.tags)) {
		result.category = core::OperationResultCategory::ValidationFailure;
		append_edit_diagnostic(result, std::move(*diagnostic));
		return result;
	}
	if (draft.storage_id) {
		const persistence::StorageEnvelope* storage =
			catalog::find_storage_envelope(request.current_session.repository,
										   *draft.storage_id);
		if (storage == nullptr) {
			result.category = core::OperationResultCategory::ValidationFailure;
			append_edit_diagnostic(
				result, make_entity_diagnostic(
							core::DiagnosticSeverity::ActionValidationError,
							"item_storage_missing",
							"Item storage must be unassigned or point to an "
							"accepted storage.",
							draft.storage_id->value()));
			return result;
		}
	}

	std::vector<persistence::ItemEnvelope> candidate_items =
		request.current_session.repository.items;
	const bool draft_is_existing = draft.existing_id.has_value();
	const core::StableIdentifier item_id =
		draft_is_existing		? *draft.existing_id
		: draft.reserved_new_id ? *draft.reserved_new_id
								: request.identifiers.next_stable_identifier();
	if (!draft_is_existing)
		result.saved_record_id = item_id;
	const core::EpochMilliseconds now = request.clock.now();
	domain::ItemRecord item{
		.id			  = item_id,
		.display_name = domain::trim_ascii_copy(draft.display_name),
		.category	  = domain::trim_ascii_copy(draft.category),
		.storage_id	  = draft.storage_id,
		.tags		  = draft.tags,
		.notes		  = draft.notes,
		.status		  = draft.status,
		.listing	  = draft.listing,
		.acquisition  = draft.acquisition,
		.finance	  = draft.finance,
		.timestamps =
			domain::RecordTimestamps{.created_at = now, .updated_at = now}};
	persistence::UnknownFields unknown_fields;
	bool replaced_existing = false;
	for (persistence::ItemEnvelope& existing : candidate_items) {
		if (existing.record.id == item_id) {
			item.timestamps.created_at = existing.record.timestamps.created_at;
			unknown_fields			   = existing.unknown_fields;
			existing				   = persistence::ItemEnvelope{
				.record = item, .unknown_fields = std::move(unknown_fields)};
			replaced_existing = true;
			break;
		}
	}
	if (draft_is_existing && !replaced_existing) {
		result.category = core::OperationResultCategory::ValidationFailure;
		append_edit_diagnostic(
			result, make_entity_diagnostic(
						core::DiagnosticSeverity::ActionValidationError,
						"item_not_found",
						"Existing item cannot be edited because it is missing.",
						item_id.value()));
		return result;
	}
	if (!replaced_existing)
		candidate_items.push_back(persistence::ItemEnvelope{.record = item});

	if (std::optional<EntityEditDiagnostic> diagnostic =
			validate_item_required_fields(item)) {
		result.category = core::OperationResultCategory::ValidationFailure;
		append_edit_diagnostic(result, std::move(*diagnostic));
		return result;
	}

	if (!draft.warning_acknowledged) {
		append_item_save_nudges(result, request.current_session, draft,
								item_id);
		if (!result.diagnostics.empty()) {
			result.warning_acknowledgement_required = true;
			return result;
		}
	}

	if (std::optional<EntityEditDiagnostic> diagnostic = commit_item_records(
			result, request, candidate_items, active_catalog_root)) {
		result.category = core::OperationResultCategory::ReplacementFailure;
		append_edit_diagnostic(result, std::move(*diagnostic));
		return result;
	}
	result.session = rebuild_edit_session(
		request.current_session, std::move(candidate_items),
		request.current_session.repository.storages, active_catalog_root);
	result.saved_record_id	= item_id;
	result.metadata_changed = true;
	return result;
}

EntityEditResult save_storage_draft(const EntityEditRequest& request,
									const StorageDraft& draft) {
	EntityEditResult result{.session = request.current_session};
	if (!request.current_session.ready_for_browsing()) {
		result.category = core::OperationResultCategory::ValidationFailure;
		append_edit_diagnostic(
			result, make_entity_diagnostic(
						core::DiagnosticSeverity::ActionValidationError,
						"catalog_not_browsable",
						"Catalog must be loaded before storage metadata can be "
						"edited."));
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
	if (std::optional<EntityEditDiagnostic> diagnostic =
			validate_new_tags(draft.tags)) {
		result.category = core::OperationResultCategory::ValidationFailure;
		append_edit_diagnostic(result, std::move(*diagnostic));
		return result;
	}

	if (draft.parent_storage_id) {
		const persistence::StorageEnvelope* parent =
			catalog::find_storage_envelope(request.current_session.repository,
										   *draft.parent_storage_id);
		if (parent == nullptr) {
			result.category = core::OperationResultCategory::ValidationFailure;
			append_edit_diagnostic(
				result, make_entity_diagnostic(
							core::DiagnosticSeverity::ActionValidationError,
							"storage_parent_missing",
							"Parent storage must be empty or point to an "
							"accepted storage.",
							draft.parent_storage_id->value()));
			return result;
		}
	}

	std::vector<persistence::StorageEnvelope> candidate_storages =
		request.current_session.repository.storages;
	const bool draft_is_existing = draft.existing_id.has_value();
	const core::StableIdentifier storage_id =
		draft_is_existing		? *draft.existing_id
		: draft.reserved_new_id ? *draft.reserved_new_id
								: request.identifiers.next_stable_identifier();
	if (!draft_is_existing)
		result.saved_record_id = storage_id;
	const core::EpochMilliseconds now = request.clock.now();
	domain::StorageRecord storage{
		.id				   = storage_id,
		.display_name	   = domain::trim_ascii_copy(draft.display_name),
		.storage_type	   = domain::trim_ascii_copy(draft.storage_type),
		.parent_storage_id = draft.parent_storage_id,
		.location		   = draft.location,
		.tags			   = draft.tags,
		.notes			   = draft.notes,
		.lifecycle_status  = draft.lifecycle_status,
		.timestamps =
			domain::RecordTimestamps{.created_at = now, .updated_at = now}};
	persistence::UnknownFields unknown_fields;
	bool replaced_existing = false;
	for (persistence::StorageEnvelope& existing : candidate_storages) {
		if (existing.record.id == storage_id) {
			storage.timestamps.created_at =
				existing.record.timestamps.created_at;
			unknown_fields = existing.unknown_fields;
			existing	   = persistence::StorageEnvelope{
				.record = storage, .unknown_fields = std::move(unknown_fields)};
			replaced_existing = true;
			break;
		}
	}
	if (draft_is_existing && !replaced_existing) {
		result.category = core::OperationResultCategory::ValidationFailure;
		append_edit_diagnostic(
			result,
			make_entity_diagnostic(
				core::DiagnosticSeverity::ActionValidationError,
				"storage_not_found",
				"Existing storage cannot be edited because it is missing.",
				storage_id.value()));
		return result;
	}
	if (!replaced_existing) {
		candidate_storages.push_back(
			persistence::StorageEnvelope{.record = storage});
	}

	if (std::optional<EntityEditDiagnostic> diagnostic =
			validate_storage_required_fields(storage)) {
		result.category = core::OperationResultCategory::ValidationFailure;
		append_edit_diagnostic(result, std::move(*diagnostic));
		return result;
	}
	if (std::optional<EntityEditDiagnostic> diagnostic =
			validate_storage_parent(draft, storage_id, candidate_storages)) {
		result.category = core::OperationResultCategory::ValidationFailure;
		append_edit_diagnostic(result, std::move(*diagnostic));
		return result;
	}
	if (std::optional<EntityEditDiagnostic> diagnostic =
			storage_archive_warning(request.current_session, storage_id,
									draft)) {
		result.warning_acknowledgement_required = true;
		append_edit_diagnostic(result, std::move(*diagnostic));
		return result;
	}

	if (std::optional<EntityEditDiagnostic> diagnostic = commit_storage_records(
			result, request, candidate_storages, active_catalog_root)) {
		result.category = core::OperationResultCategory::ReplacementFailure;
		append_edit_diagnostic(result, std::move(*diagnostic));
		return result;
	}
	result.session = rebuild_edit_session(
		request.current_session, request.current_session.repository.items,
		std::move(candidate_storages), active_catalog_root);
	result.saved_record_id	= storage_id;
	result.metadata_changed = true;
	return result;
}

EntityEditResult archive_item_in_session(
	const EntityEditRequest& request, const core::StableIdentifier& item_id) {
	const persistence::ItemEnvelope* item = catalog::find_item_envelope(
		request.current_session.repository, item_id);
	if (item == nullptr) {
		EntityEditResult result{
			.category = core::OperationResultCategory::ValidationFailure,
			.session  = request.current_session};
		append_edit_diagnostic(
			result, make_entity_diagnostic(
						core::DiagnosticSeverity::ActionValidationError,
						"item_not_found",
						"Item cannot be archived because it is missing.",
						item_id.value()));
		return result;
	}
	ItemDraft draft{.existing_id		  = item_id,
					.display_name		  = item->record.display_name,
					.category			  = item->record.category,
					.storage_id			  = item->record.storage_id,
					.tags				  = item->record.tags,
					.notes				  = item->record.notes,
					.status				  = item->record.status,
					.listing			  = item->record.listing,
					.acquisition		  = item->record.acquisition,
					.finance			  = item->record.finance,
					.warning_acknowledged = true};
	draft.status = domain::ItemStatus::Archived;
	return save_item_draft(request, draft);
}

EntityEditResult archive_storage_in_session(
	const EntityEditRequest& request, const core::StableIdentifier& storage_id,
	bool archive_warning_acknowledged) {
	const persistence::StorageEnvelope* storage =
		catalog::find_storage_envelope(request.current_session.repository,
									   storage_id);
	if (storage == nullptr) {
		EntityEditResult result{
			.category = core::OperationResultCategory::ValidationFailure,
			.session  = request.current_session};
		append_edit_diagnostic(
			result, make_entity_diagnostic(
						core::DiagnosticSeverity::ActionValidationError,
						"storage_not_found",
						"Storage cannot be archived because it is missing.",
						storage_id.value()));
		return result;
	}
	StorageDraft draft{
		.existing_id	   = storage_id,
		.display_name	   = storage->record.display_name,
		.storage_type	   = storage->record.storage_type,
		.parent_storage_id = storage->record.parent_storage_id,
		.location		   = storage->record.location,
		.tags			   = storage->record.tags,
		.notes			   = storage->record.notes,
		.lifecycle_status  = domain::StorageLifecycleStatus::Archived,
		.archive_warning_acknowledged = archive_warning_acknowledged};
	return save_storage_draft(request, draft);
}

bool EntityEditResult::succeeded() const noexcept {
	return category == core::OperationResultCategory::Success
		   && !warning_acknowledgement_required;
}

bool EntityEditResult::failed() const noexcept {
	return category != core::OperationResultCategory::Success;
}

}	 // namespace shuba::ui
