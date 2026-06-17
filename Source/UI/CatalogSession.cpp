#include "UI/CatalogSession.hpp"

#include "Persistence/CatalogStorage.hpp"
#include "Persistence/MetadataSchema.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace shuba::ui {
namespace {
using shuba::catalog::CatalogMediaSnapshot;
using shuba::catalog::expected_photo_media_file_name;
using shuba::core::Diagnostic;
using shuba::core::DiagnosticSeverity;
using shuba::core::EpochMilliseconds;
using shuba::core::OperationIdentifier;
using shuba::core::OperationResultCategory;
using shuba::core::StableIdentifier;
using shuba::domain::ItemStatus;
using shuba::domain::PhotoOwnerType;
using shuba::domain::StorageLifecycleStatus;
using shuba::persistence::CatalogJsonlDocuments;
using shuba::persistence::CatalogLoadStatus;
using shuba::persistence::CatalogStorageResult;
using shuba::persistence::JsonlDiagnostic;
using shuba::persistence::LoadDiagnosticSeverity;

[[nodiscard]] std::array<std::string_view, 5> canonical_metadata_paths() {
	return {persistence::manifest_file_path, persistence::settings_file_path,
			persistence::items_data_file_path,
			persistence::storages_data_file_path,
			persistence::photos_data_file_path};
}

[[nodiscard]] Diagnostic make_diagnostic(DiagnosticSeverity severity,
										 std::string code, std::string message,
										 std::string technical_details = {}) {
	return Diagnostic{.severity			 = severity,
					  .code				 = std::move(code),
					  .message			 = std::move(message),
					  .technical_details = std::move(technical_details)};
}

[[nodiscard]] JsonlDiagnostic make_load_diagnostic(
	LoadDiagnosticSeverity severity, std::string code, std::string path,
	std::string message, std::string details = {}) {
	return JsonlDiagnostic{.severity = severity,
						   .area	 = "catalog-session",
						   .code	 = std::move(code),
						   .path	 = std::move(path),
						   .message	 = std::move(message),
						   .details	 = std::move(details)};
}

void append_storage_diagnostics(CatalogSessionState& state,
								const CatalogStorageResult& result) {
	state.startup_diagnostics.insert(state.startup_diagnostics.end(),
									 result.diagnostics.begin(),
									 result.diagnostics.end());
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
	for (const JsonlDiagnostic& diagnostic : diagnostics)
		append_edit_diagnostic(result,
							   entity_diagnostic_from_jsonl(diagnostic));
}

[[nodiscard]] bool has_fatal_diagnostic(
	std::span<const JsonlDiagnostic> diagnostics) {
	return std::ranges::any_of(diagnostics, [](const JsonlDiagnostic& entry) {
		return entry.severity == LoadDiagnosticSeverity::Fatal;
	});
}

[[nodiscard]] bool has_degrading_diagnostic(
	std::span<const JsonlDiagnostic> diagnostics) {
	return std::ranges::any_of(diagnostics, [](const JsonlDiagnostic& entry) {
		return entry.severity == LoadDiagnosticSeverity::Error
			   || entry.severity == LoadDiagnosticSeverity::Warning;
	});
}

[[nodiscard]] CatalogLoadStatus combine_load_status(
	CatalogLoadStatus jsonl_status,
	std::span<const JsonlDiagnostic> diagnostics,
	bool derived_diagnostics_present, bool media_scan_degraded) {
	if (jsonl_status == CatalogLoadStatus::Fatal
		|| has_fatal_diagnostic(diagnostics)) {
		return CatalogLoadStatus::Fatal;
	}
	if (jsonl_status == CatalogLoadStatus::Degraded
		|| has_degrading_diagnostic(diagnostics) || derived_diagnostics_present
		|| media_scan_degraded) {
		return CatalogLoadStatus::Degraded;
	}
	return CatalogLoadStatus::Normal;
}

[[nodiscard]] bool file_exists(const std::filesystem::path& path) {
	std::error_code error;
	const bool exists = std::filesystem::exists(path, error);
	return !error && exists;
}

[[nodiscard]] bool has_any_canonical_metadata(
	const std::filesystem::path& active_catalog_root) {
	for (std::string_view relative_path : canonical_metadata_paths()) {
		if (file_exists(active_catalog_root
						/ std::filesystem::path{std::string{relative_path}})) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] std::filesystem::path marker_path(
	const std::filesystem::path& active_catalog_root) {
	return active_catalog_root
		   / std::filesystem::path{std::string{debug_demo_marker_file_name}};
}

[[nodiscard]] bool marker_exists(
	const std::filesystem::path& active_catalog_root) {
	return file_exists(marker_path(active_catalog_root));
}

[[nodiscard]] std::optional<std::string> read_text_file(
	const std::filesystem::path& active_catalog_root,
	std::string_view relative_path, std::vector<JsonlDiagnostic>& diagnostics) {
	const std::filesystem::path path =
		active_catalog_root / std::filesystem::path{std::string{relative_path}};
	std::ifstream input{path, std::ios::binary};
	if (!input) {
		diagnostics.push_back(make_load_diagnostic(
			LoadDiagnosticSeverity::Fatal, "canonical_file_unavailable",
			std::string{relative_path},
			"Canonical catalog metadata file could not be opened.",
			path.string()));
		return std::nullopt;
	}

	std::ostringstream buffer;
	buffer << input.rdbuf();
	if (input.bad()) {
		diagnostics.push_back(make_load_diagnostic(
			LoadDiagnosticSeverity::Fatal, "canonical_file_read_failed",
			std::string{relative_path},
			"Canonical catalog metadata file could not be read completely.",
			path.string()));
		return std::nullopt;
	}
	return buffer.str();
}

void parse_singleton_metadata(CatalogSessionState& state,
							  std::string_view manifest_json,
							  std::string_view settings_json,
							  std::vector<JsonlDiagnostic>& diagnostics) {
	const persistence::SchemaReadResult<persistence::ManifestRecord> manifest =
		persistence::parse_manifest_json(manifest_json);
	if (manifest.succeeded()) {
		state.catalog_id = manifest.value->catalog_id;
	} else {
		std::string details;
		if (manifest.diagnostic)
			details = manifest.diagnostic->message + ": "
					  + manifest.diagnostic->technical_details;
		diagnostics.push_back(make_load_diagnostic(
			LoadDiagnosticSeverity::Fatal, "manifest_invalid",
			std::string{persistence::manifest_file_path},
			"Catalog manifest could not be parsed as first-version metadata.",
			std::move(details)));
	}

	const persistence::SchemaReadResult<persistence::SettingsRecord> settings =
		persistence::parse_settings_json(settings_json);
	if (!settings.succeeded()) {
		std::string details;
		if (settings.diagnostic)
			details = settings.diagnostic->message + ": "
					  + settings.diagnostic->technical_details;
		diagnostics.push_back(make_load_diagnostic(
			LoadDiagnosticSeverity::Fatal, "settings_invalid",
			std::string{persistence::settings_file_path},
			"Catalog settings could not be parsed as first-version metadata.",
			std::move(details)));
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

[[nodiscard]] StableIdentifier stable_id(std::string text) {
	std::optional<StableIdentifier> identifier =
		StableIdentifier::try_create_file_safe(std::move(text));
	if (!identifier)
		throw std::logic_error("B17 demo identifier literal is invalid");
	return *identifier;
}

[[nodiscard]] domain::RecordTimestamps timestamps(EpochMilliseconds value) {
	return domain::RecordTimestamps{.created_at = value, .updated_at = value};
}

[[nodiscard]] std::optional<domain::MoneyAmount> money(
	std::string_view amount, std::string_view currency) {
	domain::MoneyParseResult parsed =
		domain::parse_money_amount(amount, currency);
	if (!parsed)
		return std::nullopt;
	return parsed.amount;
}

[[nodiscard]] std::vector<persistence::StorageEnvelope> demo_storages(
	EpochMilliseconds now) {
	return {
		persistence::StorageEnvelope{
			.record =
				domain::StorageRecord{
					.id			  = stable_id("storage-home"),
					.display_name = "Home / Дом",
					.storage_type = "home",
					.location	  = "Minsk, apartment",
					.tags  = {domain::TagRow{.key = "zone", .value = "main"}},
					.notes = "Root storage used by the debug demo catalog.",
					.timestamps = timestamps(now)}},
		persistence::StorageEnvelope{
			.record =
				domain::StorageRecord{
					.id				   = stable_id("storage-wardrobe"),
					.display_name	   = "Bedroom wardrobe",
					.storage_type	   = "wardrobe",
					.parent_storage_id = stable_id("storage-home"),
					.location		   = "Bedroom, left side",
					.tags = {domain::TagRow{.key = "room", .value = "bedroom"}},
					.notes		= "Шкаф для одежды и обуви.",
					.timestamps = timestamps(now)}},
		persistence::StorageEnvelope{
			.record =
				domain::StorageRecord{
					.id				   = stable_id("storage-box-scarves"),
					.display_name	   = "Scarf box / Коробка шарфов",
					.storage_type	   = "box",
					.parent_storage_id = stable_id("storage-wardrobe"),
					.location		   = "Top shelf",
					.tags			   = {domain::TagRow{.key	= "season",
														 .value = "winter"}},
					.timestamps		   = timestamps(now)}},
		persistence::StorageEnvelope{
			.record =
				domain::StorageRecord{
					.id				   = stable_id("storage-archive"),
					.display_name	   = "Archived sale box",
					.storage_type	   = "archive box",
					.parent_storage_id = stable_id("storage-home"),
					.location		   = "Closet",
					.notes =
						"Archived storage remains hidden from storage lists by "
						"default.",
					.lifecycle_status = StorageLifecycleStatus::Archived,
					.timestamps		  = timestamps(now)}},
		persistence::StorageEnvelope{
			.record = domain::StorageRecord{
				.id				   = stable_id("storage-broken-parent"),
				.display_name	   = "Legacy bag with missing parent",
				.storage_type	   = "bag",
				.parent_storage_id = stable_id("storage-missing-parent"),
				.location		   = "Unknown shelf",
				.notes			   = "Demo row for broken-parent warnings.",
				.timestamps		   = timestamps(now)}}};
}

[[nodiscard]] std::vector<persistence::ItemEnvelope> demo_items(
	EpochMilliseconds now) {
	domain::ListingData red_listing{.marketplace = "Kufar",
									.price		 = money("45", "BYN"),
									.note		 = "Listed shortcut demo"};
	domain::FinanceData sold_finance{.real_sale_price = money("38", "BYN"),
									 .expenses_total  = money("3", "BYN")};
	return {
		persistence::ItemEnvelope{
			.record =
				domain::ItemRecord{
					.id			  = stable_id("item-red-dress"),
					.display_name = "Red linen dress / Красное платье",
					.category	  = "clothing",
					.storage_id	  = stable_id("storage-wardrobe"),
					.tags = {domain::TagRow{.key = "brand", .value = "Acme"},
							 domain::TagRow{.key = "цвет", .value = "красный"}},
					.notes	= "Representative Russian and English search text.",
					.status = ItemStatus::Listed,
					.listing	= std::move(red_listing),
					.timestamps = timestamps(now)}},
		persistence::ItemEnvelope{
			.record =
				domain::ItemRecord{
					.id			  = stable_id("item-sneakers"),
					.display_name = "White sneakers",
					.category	  = "shoes",
					.storage_id	  = stable_id("storage-box-scarves"),
					.tags	= {domain::TagRow{.key = "size", .value = "39"}},
					.notes	= "Nested storage filter demo.",
					.status = ItemStatus::Planned,
					.timestamps = timestamps(now)}},
		persistence::ItemEnvelope{
			.record =
				domain::ItemRecord{
					.id			  = stable_id("item-sold-coat"),
					.display_name = "Sold wool coat",
					.category	  = "outerwear",
					.storage_id	  = stable_id("storage-archive"),
					.tags		  = {domain::TagRow{.key   = "season",
													.value = "winter"}},
					.notes =
						"Active item in archived storage should show a marker.",
					.status = ItemStatus::Sold,
					.acquisition =
						domain::AcquisitionData{.source = "thrift",
												.cost	= money("15", "BYN")},
					.finance	= std::move(sold_finance),
					.timestamps = timestamps(now)}},
		persistence::ItemEnvelope{
			.record =
				domain::ItemRecord{.id			 = stable_id("item-no-photo"),
								   .display_name = "Silk scarf without photo",
								   .category	 = "accessories",
								   .tags   = {domain::TagRow{.key	= "material",
															 .value = "silk"}},
								   .notes  = "No-photo warning demo.",
								   .status = ItemStatus::Draft,
								   .timestamps = timestamps(now)}},
		persistence::ItemEnvelope{
			.record =
				domain::ItemRecord{
					.id			  = stable_id("item-broken-photo"),
					.display_name = "Blue cardigan with missing media",
					.category	  = "clothing",
					.storage_id	  = stable_id("storage-home"),
					.notes		  = "Broken-photo filter demo.",
					.status		  = ItemStatus::Draft,
					.timestamps	  = timestamps(now)}},
		persistence::ItemEnvelope{
			.record = domain::ItemRecord{
				.id			  = stable_id("item-archived"),
				.display_name = "Archived sandals",
				.category	  = "shoes",
				.storage_id	  = stable_id("storage-wardrobe"),
				.notes		  = "Hidden until include archived is enabled.",
				.status		  = ItemStatus::Archived,
				.timestamps	  = timestamps(now)}}};
}

[[nodiscard]] persistence::PhotoEnvelope demo_photo(
	std::string id, PhotoOwnerType owner_type, std::string owner_id,
	std::int64_t sort_order, bool is_main, EpochMilliseconds now) {
	return persistence::PhotoEnvelope{
		.record =
			domain::PhotoRecord{.id			   = stable_id(std::move(id)),
								.owner_type	   = owner_type,
								.owner_id	   = stable_id(std::move(owner_id)),
								.sort_order	   = sort_order,
								.is_main	   = is_main,
								.width		   = 1200,
								.height		   = 1600,
								.encoded_bytes = std::uint64_t{32},
								.source_mime_type = "image/jpeg",
								.timestamps		  = timestamps(now)}};
}

[[nodiscard]] std::vector<persistence::PhotoEnvelope> demo_photos(
	EpochMilliseconds now) {
	return {demo_photo("photo-red-dress-main", PhotoOwnerType::Item,
					   "item-red-dress", 1000, true, now),
			demo_photo("photo-sneakers-main", PhotoOwnerType::Item,
					   "item-sneakers", 1000, true, now),
			demo_photo("photo-broken-cardigan", PhotoOwnerType::Item,
					   "item-broken-photo", 1000, true, now),
			demo_photo("photo-sold-coat-usable", PhotoOwnerType::Item,
					   "item-sold-coat", 1000, true, now),
			demo_photo("photo-sold-coat-missing", PhotoOwnerType::Item,
					   "item-sold-coat", 2000, false, now),
			demo_photo("photo-wardrobe-main", PhotoOwnerType::Storage,
					   "storage-wardrobe", 1000, true, now)};
}

[[nodiscard]] bool photo_has_demo_media(std::string_view photo_id) {
	return photo_id == "photo-red-dress-main"
		   || photo_id == "photo-sneakers-main"
		   || photo_id == "photo-sold-coat-usable"
		   || photo_id == "photo-wardrobe-main";
}

[[nodiscard]] std::optional<Diagnostic> write_demo_media(
	const std::filesystem::path& active_catalog_root,
	std::span<const persistence::PhotoEnvelope> photos) {
	const std::filesystem::path media_directory =
		active_catalog_root
		/ std::filesystem::path{
			std::string{persistence::photo_media_directory_path}};
	std::error_code error;
	std::filesystem::create_directories(media_directory, error);
	if (error) {
		return make_diagnostic(DiagnosticSeverity::RecoverableWarning,
							   "debug_demo_media_unavailable",
							   "Debug demo photo media directory could not be "
							   "created.",
							   error.message());
	}

	for (const persistence::PhotoEnvelope& photo : photos) {
		if (!photo_has_demo_media(photo.record.id.value()))
			continue;

		const std::filesystem::path path =
			media_directory / expected_photo_media_file_name(photo.record.id);
		std::ofstream output{path, std::ios::binary | std::ios::trunc};
		if (!output) {
			return make_diagnostic(
				DiagnosticSeverity::RecoverableWarning,
				"debug_demo_media_unavailable",
				"Debug demo photo media placeholder could not be created.",
				path.string());
		}
		output << "debug-demo-jxl-placeholder\n";
		if (!output) {
			return make_diagnostic(
				DiagnosticSeverity::RecoverableWarning,
				"debug_demo_media_unavailable",
				"Debug demo photo media placeholder could not be written.",
				path.string());
		}
	}
	return std::nullopt;
}

[[nodiscard]] std::optional<Diagnostic> write_demo_marker(
	const std::filesystem::path& active_catalog_root) {
	std::ofstream output{marker_path(active_catalog_root),
						 std::ios::binary | std::ios::trunc};
	if (!output) {
		return make_diagnostic(DiagnosticSeverity::RecoverableWarning,
							   "debug_demo_marker_unavailable",
							   "Debug demo marker could not be created.",
							   marker_path(active_catalog_root).string());
	}
	output << "B17 debug demo catalog\n";
	return std::nullopt;
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

[[nodiscard]] std::optional<Diagnostic> validate_items(std::string_view text) {
	return validate_items_jsonl_text(
		text, "debug_demo_items_invalid",
		"Debug demo item JSONL failed validation.");
}

[[nodiscard]] std::optional<Diagnostic> validate_storages(
	std::string_view text) {
	return validate_storages_jsonl_text(
		text, "debug_demo_storages_invalid",
		"Debug demo storage JSONL failed validation.");
}

[[nodiscard]] std::optional<Diagnostic> validate_photos(std::string_view text) {
	persistence::PhotoTableLoadResult loaded =
		persistence::load_photo_jsonl(text);
	if (loaded.summary.rejected_lines == 0U)
		return std::nullopt;
	return make_diagnostic(DiagnosticSeverity::WriteBlockingError,
						   "debug_demo_photos_invalid",
						   "Debug demo photo JSONL failed validation.");
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

[[nodiscard]] CatalogStorageResult commit_demo_file(
	const std::filesystem::path& active_catalog_root,
	std::filesystem::path relative_target_path, std::string serialized_content,
	EpochMilliseconds committed_at, OperationIdentifier operation_id,
	persistence::MetadataTextValidator validator) {
	return persistence::commit_metadata_file(
		persistence::CatalogMetadataCommitRequest{
			.active_catalog_root  = active_catalog_root,
			.relative_target_path = std::move(relative_target_path),
			.serialized_content	  = std::move(serialized_content),
			.committed_at		  = committed_at,
			.operation_id		  = std::move(operation_id),
			.validator			  = std::move(validator),
			.create_previous_copy = false});
}

[[nodiscard]] std::vector<domain::StorageRecord> storage_records(
	std::span<const persistence::StorageEnvelope> storages) {
	std::vector<domain::StorageRecord> records;
	records.reserve(storages.size());
	for (const persistence::StorageEnvelope& storage : storages)
		records.push_back(storage.record);
	return records;
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

[[nodiscard]] bool owner_exists_for_photo(const CatalogSessionState& session,
										  const domain::PhotoOwner& owner) {
	if (owner.type == domain::PhotoOwnerType::Item) {
		return catalog::find_item_envelope(session.repository, owner.id)
			   != nullptr;
	}
	return catalog::find_storage_envelope(session.repository, owner.id)
		   != nullptr;
}

[[nodiscard]] std::vector<core::StableIdentifier> imported_photo_ids(
	const catalog::PhotoImportSummary& summary) {
	std::vector<core::StableIdentifier> ids;
	for (const catalog::PhotoImportPhotoResult& photo : summary.photos)
		if (photo.status == catalog::PhotoImportPhotoStatus::Imported
			&& photo.photo_id.has_value()) {
			ids.push_back(*photo.photo_id);
		}
	return ids;
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
	for (const Diagnostic& diagnostic : diagnostics)
		append_import_diagnostic(result,
								 entity_diagnostic_from_core(diagnostic));
}

void append_core_diagnostics(std::vector<Diagnostic>& target,
							 const std::vector<Diagnostic>& source) {
	for (const Diagnostic& diagnostic : source)
		target.push_back(diagnostic);
}

void append_jsonl_technical_details(
	std::vector<std::string>& details,
	std::span<const JsonlDiagnostic> diagnostics) {
	for (const JsonlDiagnostic& diagnostic : diagnostics) {
		std::string line = diagnostic.code + ": " + diagnostic.message;
		if (!diagnostic.path.empty())
			line += " · path=" + diagnostic.path;
		if (diagnostic.line)
			line += " · line=" + std::to_string(*diagnostic.line);
		if (!diagnostic.details.empty())
			line += " · " + diagnostic.details;
		details.push_back(std::move(line));
	}
}

void append_core_technical_details(std::vector<std::string>& details,
								   std::span<const Diagnostic> diagnostics) {
	for (const Diagnostic& diagnostic : diagnostics) {
		std::string line = diagnostic.code + ": " + diagnostic.message;
		if (!diagnostic.technical_details.empty())
			line += " · " + diagnostic.technical_details;
		details.push_back(std::move(line));
	}
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

[[nodiscard]] std::optional<EntityEditDiagnostic> validate_photo_import_owner(
	const CatalogSessionState& session, const domain::PhotoOwner& owner) {
	if (owner_exists_for_photo(session, owner))
		return std::nullopt;
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
	if (found == session.repository.item_projections.end()
		|| found->second.photo_presence
			   == catalog::PhotoPresenceState::NoPhotoRecords) {
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

[[nodiscard]] CatalogStorageResult seed_debug_demo_catalog(
	const platform::AppPrivatePaths& paths, core::IdentifierSource& identifiers,
	core::Clock& clock) {
	if (has_any_canonical_metadata(paths.active_catalog_root)) {
		return CatalogStorageResult{
			.category	 = OperationResultCategory::ValidationFailure,
			.diagnostics = {
				make_diagnostic(DiagnosticSeverity::ActionValidationError,
								"debug_demo_would_overwrite",
								"Debug demo seed was blocked because canonical "
								"metadata exists.",
								paths.active_catalog_root.string())}};
	}

	const EpochMilliseconds now		 = clock.now();
	CatalogStorageResult initialized = persistence::initialize_empty_catalog(
		persistence::EmptyCatalogInitializationRequest{
			.app_private_root = paths.app_private_root,
			.catalog_id		  = identifiers.next_stable_identifier(),
			.created_at		  = now,
			.operation_id	  = identifiers.next_operation_identifier()});
	if (initialized.failed())
		return initialized;

	const std::vector<persistence::StorageEnvelope> storages =
		demo_storages(now);
	const std::vector<persistence::ItemEnvelope> items	 = demo_items(now);
	const std::vector<persistence::PhotoEnvelope> photos = demo_photos(now);

	persistence::JsonTextWriteResult storage_text =
		persistence::write_storage_jsonl(storages);
	persistence::JsonTextWriteResult item_text =
		persistence::write_item_jsonl(items);
	persistence::JsonTextWriteResult photo_text =
		persistence::write_photo_jsonl(photos);
	if (!storage_text.succeeded() || !item_text.succeeded()
		|| !photo_text.succeeded()) {
		return CatalogStorageResult{
			.category	 = OperationResultCategory::InternalError,
			.diagnostics = {make_diagnostic(
				DiagnosticSeverity::WriteBlockingError,
				"debug_demo_serialization_failed",
				"Debug demo canonical JSONL could not be serialized.")}};
	}

	CatalogStorageResult storage_commit = commit_demo_file(
		paths.active_catalog_root,
		std::filesystem::path{
			std::string{persistence::storages_data_file_path}},
		std::move(storage_text.text), now,
		identifiers.next_operation_identifier(), validate_storages);
	if (storage_commit.failed())
		return storage_commit;

	CatalogStorageResult item_commit = commit_demo_file(
		paths.active_catalog_root,
		std::filesystem::path{std::string{persistence::items_data_file_path}},
		std::move(item_text.text), now, identifiers.next_operation_identifier(),
		validate_items);
	if (item_commit.failed())
		return item_commit;

	CatalogStorageResult photo_commit = commit_demo_file(
		paths.active_catalog_root,
		std::filesystem::path{std::string{persistence::photos_data_file_path}},
		std::move(photo_text.text), now,
		identifiers.next_operation_identifier(), validate_photos);
	if (photo_commit.failed())
		return photo_commit;

	CatalogStorageResult result;
	if (std::optional<Diagnostic> media_diagnostic =
			write_demo_media(paths.active_catalog_root, photos)) {
		result.add_diagnostic(std::move(*media_diagnostic));
	}
	if (std::optional<Diagnostic> marker_diagnostic =
			write_demo_marker(paths.active_catalog_root)) {
		result.add_diagnostic(std::move(*marker_diagnostic));
	}
	return result;
}

void load_canonical_catalog(CatalogSessionState& state) {
	if (!state.paths)
		return;

	std::vector<JsonlDiagnostic> singleton_diagnostics;
	const std::optional<std::string> manifest_json =
		read_text_file(state.paths->active_catalog_root,
					   persistence::manifest_file_path, singleton_diagnostics);
	const std::optional<std::string> settings_json =
		read_text_file(state.paths->active_catalog_root,
					   persistence::settings_file_path, singleton_diagnostics);
	const std::optional<std::string> items_jsonl = read_text_file(
		state.paths->active_catalog_root, persistence::items_data_file_path,
		singleton_diagnostics);
	const std::optional<std::string> storages_jsonl = read_text_file(
		state.paths->active_catalog_root, persistence::storages_data_file_path,
		singleton_diagnostics);
	const std::optional<std::string> photos_jsonl = read_text_file(
		state.paths->active_catalog_root, persistence::photos_data_file_path,
		singleton_diagnostics);

	if (manifest_json && settings_json)
		parse_singleton_metadata(state, *manifest_json, *settings_json,
								 singleton_diagnostics);

	CatalogJsonlDocuments documents{
		.items_jsonl	= items_jsonl.value_or(""),
		.storages_jsonl = storages_jsonl.value_or(""),
		.photos_jsonl	= photos_jsonl.value_or("")};
	state.load_result = persistence::load_catalog_jsonl(documents);
	state.load_result.diagnostics.insert(state.load_result.diagnostics.end(),
										 singleton_diagnostics.begin(),
										 singleton_diagnostics.end());

	CatalogMediaSnapshot media = scan_photo_media(
		state.paths->active_catalog_root, state.startup_diagnostics);
	const bool media_scan_degraded = !media.complete_scan_available;
	state.repository			   = catalog::build_catalog_repository(
		catalog::make_catalog_repository_input(state.load_result,
											   std::move(media)));
	state.search_index = catalog::build_search_index(state.repository);
	state.load_status  = combine_load_status(
		state.load_result.load_status, state.load_result.diagnostics,
		!state.repository.diagnostics.empty(), media_scan_degraded);
	state.load_result.load_status = state.load_status;
	if (state.load_status == CatalogLoadStatus::Fatal)
		state.source = CatalogSessionStartupSource::LoadFailed;
}
}	 // namespace

std::string_view to_string(CatalogSessionStartupSource source) noexcept {
	switch (source) {
		case CatalogSessionStartupSource::ExistingCatalog:
			return "existing catalog";
		case CatalogSessionStartupSource::InitializedEmptyCatalog:
			return "initialized empty catalog";
		case CatalogSessionStartupSource::SeededDemoCatalog:
			return "seeded demo catalog";
		case CatalogSessionStartupSource::PathResolutionFailed:
			return "path resolution failed";
		case CatalogSessionStartupSource::InitializationFailed:
			return "initialization failed";
		case CatalogSessionStartupSource::LoadFailed:
			return "load failed";
	}
	return "unknown catalog session source";
}

bool CatalogSessionState::ready_for_browsing() const noexcept {
	return paths.has_value() && load_status != CatalogLoadStatus::Fatal;
}

bool CatalogSessionState::degraded() const noexcept {
	return load_status == CatalogLoadStatus::Degraded;
}

bool CatalogSessionState::fatal() const noexcept {
	return load_status == CatalogLoadStatus::Fatal;
}

bool EntityEditResult::succeeded() const noexcept {
	return category == core::OperationResultCategory::Success
		   && !warning_acknowledgement_required;
}

bool EntityEditResult::failed() const noexcept {
	return category != core::OperationResultCategory::Success;
}

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

bool CatalogRecoveryUiSummary::fatal() const noexcept {
	return load_status == persistence::CatalogLoadStatus::Fatal;
}

bool CatalogRecoveryUiSummary::degraded() const noexcept {
	return load_status == persistence::CatalogLoadStatus::Degraded;
}

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

CatalogSessionState load_catalog_session(
	const CatalogSessionLoadRequest& request) {
	CatalogSessionState state;
	platform::PlatformValueResult<platform::AppPrivatePaths> paths =
		request.path_provider.resolve_app_private_paths();
	if (!paths.succeeded()) {
		state.source = CatalogSessionStartupSource::PathResolutionFailed;
		state.startup_diagnostics = std::move(paths.diagnostics);
		return state;
	}

	state.paths = std::move(*paths.value);
	state.startup_diagnostics.insert(state.startup_diagnostics.end(),
									 paths.diagnostics.begin(),
									 paths.diagnostics.end());

	CatalogStorageResult cleanup = persistence::cleanup_startup_temporary_files(
		state.paths->app_private_root);
	append_storage_diagnostics(state, cleanup);

	const bool had_canonical_metadata =
		has_any_canonical_metadata(state.paths->active_catalog_root);
	state.existing_canonical_metadata = had_canonical_metadata;

	if (!had_canonical_metadata && request.debug_demo_seed_enabled) {
		CatalogStorageResult seed = seed_debug_demo_catalog(
			*state.paths, request.identifiers, request.clock);
		append_storage_diagnostics(state, seed);
		if (seed.failed()) {
			state.source = CatalogSessionStartupSource::InitializationFailed;
			return state;
		}
		state.source = CatalogSessionStartupSource::SeededDemoCatalog;
		state.demo_catalog_seeded = true;
		state.demo_catalog_active = true;
	} else if (!had_canonical_metadata) {
		CatalogStorageResult initialized =
			persistence::initialize_empty_catalog(
				persistence::EmptyCatalogInitializationRequest{
					.app_private_root = state.paths->app_private_root,
					.catalog_id = request.identifiers.next_stable_identifier(),
					.created_at = request.clock.now(),
					.operation_id =
						request.identifiers.next_operation_identifier()});
		append_storage_diagnostics(state, initialized);
		if (initialized.failed()) {
			state.source = CatalogSessionStartupSource::InitializationFailed;
			return state;
		}
		state.source = CatalogSessionStartupSource::InitializedEmptyCatalog;
		state.initialized_empty_catalog = true;
	} else {
		state.source = CatalogSessionStartupSource::ExistingCatalog;
		state.demo_catalog_active =
			marker_exists(state.paths->active_catalog_root);
	}

	if (!state.demo_catalog_active)
		state.demo_catalog_active =
			marker_exists(state.paths->active_catalog_root);

	load_canonical_catalog(state);
	return state;
}

CatalogRecoveryUiSummary make_recovery_ui_summary(
	const CatalogSessionState& session) {
	CatalogRecoveryUiSummary summary{
		.load_status = session.load_status,
		.accepted_item_count =
			static_cast<std::uint64_t>(session.repository.items.size()),
		.accepted_storage_count =
			static_cast<std::uint64_t>(session.repository.storages.size()),
		.accepted_photo_count =
			static_cast<std::uint64_t>(session.repository.photos.size()),
		.skipped_item_count = session.load_result.items.summary.rejected_lines,
		.skipped_storage_count =
			session.load_result.storages.summary.rejected_lines,
		.skipped_photo_count =
			session.load_result.photos.summary.rejected_lines,
		.broken_reference_count =
			session.repository.recovery_summary.broken_reference_count,
		.orphan_media_count =
			session.repository.recovery_summary.orphan_media_count};

	if (session.fatal()) {
		summary.plain_summary_message =
			"Catalog cannot safely open. Browsing and editing stay disabled "
			"until a backup is imported or the catalog is repaired.";
		summary.safe_actions = {"Export diagnostic archive",
								"Import backup ZIP", "Show technical report",
								"Exit"};
	} else if (session.degraded()) {
		summary.plain_summary_message =
			"Some records or media could not be loaded. Accepted records "
			"remain available, and backup/export can preserve the raw "
			"damaged state.";
		summary.safe_actions = {
			"Export normal backup", "Export diagnostic archive",
			"Import backup ZIP", "Continue using accepted records"};
	} else {
		summary.plain_summary_message =
			"Catalog loaded normally. Backup export and staged import are "
			"available from maintenance actions.";
		summary.safe_actions = {"Export normal backup", "Import backup ZIP",
								"Export diagnostic archive"};
	}

	append_jsonl_technical_details(summary.technical_details,
								   session.load_result.diagnostics);
	append_core_technical_details(summary.technical_details,
								  session.startup_diagnostics);
	return summary;
}

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
	if (!replaced_existing)
		candidate_storages.push_back(
			persistence::StorageEnvelope{.record = storage});

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
	for (const persistence::PhotoEnvelope& photo : candidate_photos)
		if (photo.record.owner_type == selected_owner.type
			&& photo.record.owner_id == selected_owner.id) {
			photo_records.push_back(photo.record);
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
		request.identifiers,	 request.clock,			 request.operation_gate,
		request.staging_service, request.decode_service, request.photo_codec};
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
		.session	 = request.current_session,
		.diagnostics = replacement_result.diagnostics,
		.fatal_recovery_required =
			replacement_result.fatal_recovery_required()};
	if (replacement_result.succeeded()) {
		CatalogSessionState reloaded = request.current_session;
		reloaded.source = CatalogSessionStartupSource::ExistingCatalog;
		reloaded.existing_canonical_metadata = true;
		reloaded.initialized_empty_catalog	 = false;
		reloaded.demo_catalog_seeded		 = false;
		load_canonical_catalog(reloaded);
		result.session = std::move(reloaded);
	} else if (replacement_result.fatal_recovery_required()) {
		result.session.load_status = persistence::CatalogLoadStatus::Fatal;
		result.session.source	   = CatalogSessionStartupSource::LoadFailed;
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
