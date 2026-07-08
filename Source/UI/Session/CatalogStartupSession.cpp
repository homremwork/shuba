#include "UI/Session/CatalogStartupSession.hpp"

#include "UI/Session/AndroidPreviousExitArtifacts.hpp"
#include "UI/Session/StartupRecoverySession.hpp"

#include "Persistence/CatalogStorage.hpp"
#include "Persistence/MetadataSchema.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
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

void append_core_diagnostics(CatalogSessionState& state,
							 const std::vector<Diagnostic>& diagnostics) {
	state.startup_diagnostics.insert(state.startup_diagnostics.end(),
									 diagnostics.begin(), diagnostics.end());
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

[[nodiscard]] std::string exception_message(const std::exception& exception) {
	const char* message = exception.what();
	return message == nullptr ? std::string{"std::exception"}
							  : std::string{message};
}

[[nodiscard]] CatalogSessionState make_path_resolution_exception_session(
	const std::string& exception_kind, const std::string& message,
	const std::string& technical_details) {
	CatalogSessionState state;
	state.source	  = CatalogSessionStartupSource::StartupException;
	state.load_status = CatalogLoadStatus::Fatal;
	state.load_result.load_status = CatalogLoadStatus::Fatal;
	state.startup_diagnostics.push_back(make_diagnostic(
		DiagnosticSeverity::FatalCatalogError,
		"startup_path_resolution_exception",
		"Startup failed while resolving app-private paths. Diagnostic archive "
		"export is unavailable until paths can be resolved.",
		exception_kind + ": " + message
			+ (technical_details.empty()
				   ? std::string{}
				   : std::string{"; "} + technical_details)));
	return state;
}

[[nodiscard]] StartupAttemptMarker make_startup_attempt_marker(
	const GuardedCatalogSessionLoadRequest& request,
	const StartupAttemptMarkerReadResult& stale_marker_result) {
	std::uint64_t previous_attempt_count = 0U;
	if (stale_marker_result.marker.has_value()) {
		previous_attempt_count =
			stale_marker_result.marker->previous_attempt_count + 1U;
	}
	return StartupAttemptMarker{
		.attempt_id	 = request.identifiers.next_operation_identifier().value(),
		.started_at	 = request.clock.now(),
		.app_version = request.app_version,
		.platform	 = request.platform,
		.stage		 = "catalog-load",
		.retry_requested_by_user = request.retry_requested_by_user,
		.previous_attempt_count	 = previous_attempt_count};
}

[[nodiscard]] CatalogSessionState invoke_catalog_session_loader(
	const GuardedCatalogSessionLoadRequest& request,
	const platform::AppPrivatePaths& paths) {
	CatalogSessionLoadRequest load_request{
		.path_provider			 = request.path_provider,
		.identifiers			 = request.identifiers,
		.clock					 = request.clock,
		.debug_demo_seed_enabled = request.debug_demo_seed_enabled,
		.honor_startup_safe_mode = false,
		.resolved_paths			 = paths};
	if (request.loader)
		return request.loader(load_request);
	return load_catalog_session(load_request);
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
		if (manifest.diagnostic) {
			details = manifest.diagnostic->message + ": "
					  + manifest.diagnostic->technical_details;
		}
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
		if (settings.diagnostic) {
			details = settings.diagnostic->message + ": "
					  + settings.diagnostic->technical_details;
		}
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

	if (manifest_json && settings_json) {
		parse_singleton_metadata(state, *manifest_json, *settings_json,
								 singleton_diagnostics);
	}

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
		case CatalogSessionStartupSource::StartupException:
			return "startup exception";
		case CatalogSessionStartupSource::StartupCrashSafeMode:
			return "startup crash safe mode";
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

bool CatalogRecoveryUiSummary::fatal() const noexcept {
	return load_status == persistence::CatalogLoadStatus::Fatal;
}

bool CatalogRecoveryUiSummary::degraded() const noexcept {
	return load_status == persistence::CatalogLoadStatus::Degraded;
}

bool CatalogRecoveryUiSummary::startup_crash_safe_mode() const noexcept {
	return startup_source == CatalogSessionStartupSource::StartupCrashSafeMode;
}

CatalogSessionState load_catalog_session(
	const CatalogSessionLoadRequest& request) {
	CatalogSessionState state;
	if (request.resolved_paths.has_value()) {
		state.paths = request.resolved_paths;
	} else {
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
	}
	if (request.honor_startup_safe_mode) {
		StartupAttemptMarkerReadResult marker_result =
			read_startup_attempt_marker(*state.paths);
		state.startup_diagnostics.insert(state.startup_diagnostics.end(),
										 marker_result.diagnostics.begin(),
										 marker_result.diagnostics.end());
		if (marker_result.safe_mode_required()) {
			marker_result.diagnostics = std::move(state.startup_diagnostics);
			return make_startup_crash_safe_mode_session(
				std::move(*state.paths), std::move(marker_result),
				request.clock.now());
		}
	}

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

	if (!state.demo_catalog_active) {
		state.demo_catalog_active =
			marker_exists(state.paths->active_catalog_root);
	}

	load_canonical_catalog(state);
	return state;
}

CatalogSessionState load_guarded_catalog_session(
	GuardedCatalogSessionLoadRequest request) {
	platform::PlatformValueResult<platform::AppPrivatePaths> paths;
	try {
		paths = request.path_provider.resolve_app_private_paths();
	} catch (const std::exception& exception) {
		return make_path_resolution_exception_session(
			"std::exception", exception_message(exception),
			"App-private path provider threw before startup recovery paths "
			"were "
			"available.");
	} catch (...) {
		return make_path_resolution_exception_session(
			"unknown", "unknown startup path resolution exception",
			"App-private path provider threw a non-standard exception before "
			"startup recovery paths were available.");
	}

	if (!paths.succeeded()) {
		CatalogSessionState state;
		state.source = CatalogSessionStartupSource::PathResolutionFailed;
		state.startup_diagnostics = std::move(paths.diagnostics);
		return state;
	}

	StartupAttemptMarkerReadResult marker_result =
		read_startup_attempt_marker(*paths.value);
	marker_result.diagnostics.insert(marker_result.diagnostics.begin(),
									 paths.diagnostics.begin(),
									 paths.diagnostics.end());
	if (request.android_previous_exit_service != nullptr
		&& (marker_result.safe_mode_required()
			|| request.retry_requested_by_user)) {
		StartupRecoveryFileResult previous_exit_captured =
			capture_android_previous_exit_artifacts(
				AndroidPreviousExitArtifactCaptureRequest{
					.paths	 = *paths.value,
					.service = *request.android_previous_exit_service,
					.clock	 = request.clock});
		marker_result.diagnostics.insert(
			marker_result.diagnostics.end(),
			previous_exit_captured.diagnostics.begin(),
			previous_exit_captured.diagnostics.end());
	}
	if (marker_result.safe_mode_required()
		&& !request.retry_requested_by_user) {
		return make_startup_crash_safe_mode_session(std::move(*paths.value),
													std::move(marker_result),
													request.clock.now());
	}

	std::vector<Diagnostic> guard_diagnostics = marker_result.diagnostics;
	try {
		StartupAttemptMarker marker =
			make_startup_attempt_marker(request, marker_result);
		StartupRecoveryFileResult marker_written =
			write_startup_attempt_marker(*paths.value, marker);
		guard_diagnostics.insert(guard_diagnostics.end(),
								 marker_written.diagnostics.begin(),
								 marker_written.diagnostics.end());

		CatalogSessionState session =
			invoke_catalog_session_loader(request, *paths.value);
		append_core_diagnostics(session, guard_diagnostics);
		return session;
	} catch (const std::exception& exception) {
		return make_startup_exception_session(StartupExceptionSessionRequest{
			.paths			= std::move(*paths.value),
			.captured_at	= request.clock.now(),
			.app_version	= std::move(request.app_version),
			.platform		= std::move(request.platform),
			.fallback_stage = "catalog-load",
			.exception_kind = "std::exception",
			.message		= exception_message(exception),
			.technical_details =
				"Guarded catalog startup caught an ordinary C++ exception.",
			.diagnostics = guard_diagnostics});
	} catch (...) {
		return make_startup_exception_session(StartupExceptionSessionRequest{
			.paths			= std::move(*paths.value),
			.captured_at	= request.clock.now(),
			.app_version	= std::move(request.app_version),
			.platform		= std::move(request.platform),
			.fallback_stage = "catalog-load",
			.exception_kind = "unknown",
			.message		= "unknown startup exception",
			.technical_details =
				"Guarded catalog startup caught a non-standard exception.",
			.diagnostics = guard_diagnostics});
	}
}

CatalogSessionState reload_catalog_session(CatalogSessionState session) {
	session.source = CatalogSessionStartupSource::ExistingCatalog;
	session.existing_canonical_metadata = true;
	session.initialized_empty_catalog	= false;
	session.demo_catalog_seeded			= false;
	load_canonical_catalog(session);
	return session;
}

CatalogRecoveryUiSummary make_recovery_ui_summary(
	const CatalogSessionState& session) {
	CatalogRecoveryUiSummary summary{
		.load_status	= session.load_status,
		.startup_source = session.source,
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
		if (session.source
			== CatalogSessionStartupSource::StartupCrashSafeMode) {
			summary.plain_summary_message =
				"Previous launch stopped before startup completed. Normal "
				"catalog load was skipped so diagnostics can be exported or a "
				"backup can be imported before retrying.";
		} else if (session.source
				   == CatalogSessionStartupSource::StartupException) {
			summary.plain_summary_message =
				"Startup failed before catalog browsing could open. Browsing "
				"and editing stay disabled until diagnostics are reviewed or a "
				"backup is imported.";
		} else {
			summary.plain_summary_message =
				"Catalog cannot safely open. Browsing and editing stay "
				"disabled "
				"until a backup is imported or the catalog is repaired.";
		}
		summary.safe_actions = {"Export diagnostic archive",
								"Import backup ZIP", "Show technical report"};
		if (summary.startup_crash_safe_mode())
			summary.safe_actions.push_back("Retry normal launch");
		summary.safe_actions.push_back("Exit");
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
}	 // namespace shuba::ui
