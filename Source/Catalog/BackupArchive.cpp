#include "Catalog/BackupArchive.hpp"

#include <glaze/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace shuba::catalog::backup_detail {
struct DiagnosticFileIssueDto final {
	std::string path;
	std::string reason;
};

struct DiagnosticArchiveReportDto final {
	int schema_version{};
	std::int64_t created_at{};
	std::string mode;
	bool catalog_root_accessible{};
	std::vector<std::string> included_files;
	std::vector<DiagnosticFileIssueDto> skipped_files;
	std::vector<DiagnosticFileIssueDto> read_errors;
	std::vector<std::string> notes;
};
}	 // namespace shuba::catalog::backup_detail

template<>
struct glz::meta<shuba::catalog::backup_detail::DiagnosticFileIssueDto> {
	using T = shuba::catalog::backup_detail::DiagnosticFileIssueDto;
	static constexpr auto value =
		object("path", &T::path, "reason", &T::reason);
};

template<>
struct glz::meta<shuba::catalog::backup_detail::DiagnosticArchiveReportDto> {
	using T = shuba::catalog::backup_detail::DiagnosticArchiveReportDto;
	static constexpr auto value = object(
		"schemaVersion", &T::schema_version, "createdAt", &T::created_at,
		"mode", &T::mode, "catalogRootAccessible", &T::catalog_root_accessible,
		"includedFiles", &T::included_files, "skippedFiles", &T::skipped_files,
		"readErrors", &T::read_errors, "notes", &T::notes);
};

namespace shuba::catalog {
namespace {
using namespace backup_detail;

constexpr std::string_view diagnostic_archive_report_path =
	"diagnostic/archive-report.json";

constexpr std::array<std::string_view, 5> canonical_metadata_paths{
	persistence::manifest_file_path, persistence::settings_file_path,
	persistence::items_data_file_path, persistence::storages_data_file_path,
	persistence::photos_data_file_path};

[[nodiscard]] core::Diagnostic make_diagnostic(
	core::DiagnosticSeverity severity, std::string code, std::string message,
	std::string technical_details = {}) {
	return core::Diagnostic{.severity		   = severity,
							.code			   = std::move(code),
							.message		   = std::move(message),
							.technical_details = std::move(technical_details)};
}

void append_diagnostic(std::vector<core::Diagnostic>& diagnostics,
					   core::Diagnostic diagnostic) {
	diagnostics.push_back(std::move(diagnostic));
}

void append_diagnostics(std::vector<core::Diagnostic>& target,
						const std::vector<core::Diagnostic>& source) {
	for (const core::Diagnostic& diagnostic : source)
		target.push_back(diagnostic);
}

[[nodiscard]] DiagnosticFileIssueDto diagnostic_file_issue_dto(
	const BackupArchiveFileIssue& issue) {
	return DiagnosticFileIssueDto{.path	  = issue.archive_path,
								  .reason = issue.reason};
}

[[nodiscard]] std::vector<DiagnosticFileIssueDto> diagnostic_file_issue_dtos(
	const std::vector<BackupArchiveFileIssue>& issues) {
	std::vector<DiagnosticFileIssueDto> result;
	result.reserve(issues.size());
	for (const BackupArchiveFileIssue& issue : issues)
		result.push_back(diagnostic_file_issue_dto(issue));
	return result;
}

[[nodiscard]] std::optional<std::string> diagnostic_archive_report_json(
	core::EpochMilliseconds created_at, bool catalog_root_accessible,
	const std::vector<std::string>& included_files,
	const std::vector<BackupArchiveFileIssue>& skipped_files,
	const std::vector<std::string>& notes,
	std::vector<core::Diagnostic>& diagnostics) {
	std::vector<DiagnosticFileIssueDto> skipped_file_dtos =
		diagnostic_file_issue_dtos(skipped_files);
	DiagnosticArchiveReportDto report{
		.schema_version			 = 1,
		.created_at				 = created_at.count(),
		.mode					 = "diagnostic",
		.catalog_root_accessible = catalog_root_accessible,
		.included_files			 = included_files,
		.skipped_files			 = skipped_file_dtos,
		.read_errors			 = std::move(skipped_file_dtos),
		.notes					 = notes};

	std::string output;
	const auto error = glz::write_json(report, output);
	if (!error)
		return output;

	append_diagnostic(
		diagnostics,
		make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
						"diagnostic-report-serialization-failed",
						"Diagnostic archive report could not be serialized.",
						glz::format_error(error, output)));
	return std::nullopt;
}

[[nodiscard]] std::string timestamp_file_name_fragment(
	core::EpochMilliseconds timestamp) {
	return std::to_string(timestamp.count());
}

[[nodiscard]] bool file_is_regular(const std::filesystem::path& path) {
	std::error_code error;
	return std::filesystem::is_regular_file(path, error) && !error;
}

[[nodiscard]] bool directory_exists(const std::filesystem::path& path) {
	std::error_code error;
	return std::filesystem::is_directory(path, error) && !error;
}

[[nodiscard]] bool file_is_readable(const std::filesystem::path& path) {
	std::ifstream input{path, std::ios::binary};
	return input.good();
}

[[nodiscard]] std::optional<std::string> read_text_file(
	const std::filesystem::path& path,
	std::vector<core::Diagnostic>& diagnostics,
	core::DiagnosticSeverity severity, std::string code, std::string message) {
	std::ifstream input{path, std::ios::binary};
	if (!input) {
		append_diagnostic(diagnostics,
						  make_diagnostic(severity, std::move(code),
										  std::move(message), path.string()));
		return std::nullopt;
	}

	std::ostringstream buffer;
	buffer << input.rdbuf();
	if (input.bad()) {
		append_diagnostic(diagnostics,
						  make_diagnostic(severity, std::move(code),
										  std::move(message), path.string()));
		return std::nullopt;
	}
	return buffer.str();
}

[[nodiscard]] std::string generic_relative_path(
	const std::filesystem::path& root, const std::filesystem::path& path) {
	std::error_code error;
	std::filesystem::path relative =
		std::filesystem::relative(path, root, error);
	if (error)
		relative = path.filename();
	return relative.generic_string();
}

void add_zip_entry(std::vector<platform::ZipArchiveEntrySource>& entries,
				   std::set<std::string>& entry_names,
				   std::vector<std::string>& included_entries,
				   std::filesystem::path source_path,
				   std::string archive_path) {
	const std::pair<std::set<std::string>::iterator, bool> inserted =
		entry_names.insert(archive_path);
	if (!inserted.second)
		return;

	included_entries.push_back(archive_path);
	entries.push_back(platform::ZipArchiveEntrySource{
		.source_path  = std::move(source_path),
		.archive_path = std::move(archive_path)});
}

[[nodiscard]] bool add_required_file(
	std::vector<platform::ZipArchiveEntrySource>& entries,
	std::set<std::string>& entry_names,
	std::vector<std::string>& included_entries,
	std::vector<BackupArchiveFileIssue>& skipped_files,
	std::vector<core::Diagnostic>& diagnostics,
	const std::filesystem::path& active_catalog_root,
	std::string_view relative_path) {
	const std::filesystem::path source_path =
		active_catalog_root / std::filesystem::path{std::string{relative_path}};
	const std::string archive_path{relative_path};
	if (!file_is_regular(source_path) || !file_is_readable(source_path)) {
		skipped_files.push_back(BackupArchiveFileIssue{
			.archive_path = archive_path,
			.reason = "required canonical file is missing or unreadable"});
		append_diagnostic(
			diagnostics,
			make_diagnostic(
				core::DiagnosticSeverity::WriteBlockingError,
				"backup-required-file-unavailable",
				"Required canonical catalog file is missing or unreadable.",
				source_path.string()));
		return false;
	}

	add_zip_entry(entries, entry_names, included_entries, source_path,
				  archive_path);
	return true;
}

void add_optional_file(std::vector<platform::ZipArchiveEntrySource>& entries,
					   std::set<std::string>& entry_names,
					   std::vector<std::string>& included_entries,
					   std::vector<BackupArchiveFileIssue>& skipped_files,
					   const std::filesystem::path& active_catalog_root,
					   std::string_view relative_path,
					   std::string missing_reason) {
	const std::filesystem::path source_path =
		active_catalog_root / std::filesystem::path{std::string{relative_path}};
	const std::string archive_path{relative_path};
	if (!file_is_regular(source_path) || !file_is_readable(source_path)) {
		skipped_files.push_back(BackupArchiveFileIssue{
			.archive_path = archive_path, .reason = std::move(missing_reason)});
		return;
	}

	add_zip_entry(entries, entry_names, included_entries, source_path,
				  archive_path);
}

[[nodiscard]] bool known_broken_photo_media(
	const CatalogRepositoryState& state,
	const core::StableIdentifier& photo_id) {
	for (const DerivedDiagnostic& diagnostic : state.diagnostics)
		if (diagnostic.code == "missing_photo_media"
			&& diagnostic.subject_id == photo_id.value())
			return true;

	for (const std::pair<const std::string, OwnerPhotoProjection>& entry :
		 state.item_photo_projections) {
		const std::vector<core::StableIdentifier>::const_iterator found =
			std::ranges::find(entry.second.broken_photo_ids, photo_id);
		if (found != entry.second.broken_photo_ids.end())
			return true;
	}
	for (const std::pair<const std::string, OwnerPhotoProjection>& entry :
		 state.storage_photo_projections) {
		const std::vector<core::StableIdentifier>::const_iterator found =
			std::ranges::find(entry.second.broken_photo_ids, photo_id);
		if (found != entry.second.broken_photo_ids.end())
			return true;
	}

	return false;
}

[[nodiscard]] bool collect_normal_backup_entries(
	const BackupExportRequest& request,
	std::vector<platform::ZipArchiveEntrySource>& entries,
	std::vector<std::string>& included_entries,
	std::vector<BackupArchiveFileIssue>& skipped_files,
	std::vector<core::Diagnostic>& diagnostics) {
	std::set<std::string> entry_names;
	bool required_files_ok = true;
	for (std::string_view relative_path : canonical_metadata_paths)
		if (!add_required_file(
				entries, entry_names, included_entries, skipped_files,
				diagnostics, request.paths.active_catalog_root, relative_path))
			required_files_ok = false;

	for (const persistence::PhotoEnvelope& photo :
		 request.current_state.photos) {
		const std::string relative_path =
			expected_photo_media_relative_path(photo.record.id);
		const std::filesystem::path source_path =
			request.paths.active_catalog_root
			/ std::filesystem::path{relative_path};
		if (file_is_regular(source_path) && file_is_readable(source_path)) {
			add_zip_entry(entries, entry_names, included_entries, source_path,
						  relative_path);
			continue;
		}

		const bool known_broken =
			known_broken_photo_media(request.current_state, photo.record.id);
		skipped_files.push_back(BackupArchiveFileIssue{
			.archive_path = relative_path,
			.reason		  = known_broken
								? "known broken accepted photo media"
								: "accepted photo media missing unexpectedly"});
		if (!known_broken) {
			append_diagnostic(
				diagnostics,
				make_diagnostic(
					core::DiagnosticSeverity::WriteBlockingError,
					"backup-photo-media-missing",
					"Accepted photo media is missing from disk before backup.",
					relative_path));
			required_files_ok = false;
		}
	}

	return required_files_ok;
}

void collect_recursive_files(
	std::vector<platform::ZipArchiveEntrySource>& entries,
	std::set<std::string>& entry_names,
	std::vector<std::string>& included_entries,
	std::vector<BackupArchiveFileIssue>& skipped_files,
	const std::filesystem::path& active_catalog_root,
	const std::filesystem::path& directory) {
	if (!directory_exists(directory))
		return;

	std::error_code error;
	std::filesystem::recursive_directory_iterator iterator{
		directory, std::filesystem::directory_options::skip_permission_denied,
		error};
	if (error) {
		skipped_files.push_back(BackupArchiveFileIssue{
			.archive_path =
				generic_relative_path(active_catalog_root, directory),
			.reason = error.message()});
		return;
	}

	const std::filesystem::recursive_directory_iterator end;
	for (; iterator != end; iterator.increment(error)) {
		if (error) {
			skipped_files.push_back(BackupArchiveFileIssue{
				.archive_path = generic_relative_path(active_catalog_root,
													  iterator->path()),
				.reason		  = error.message()});
			error.clear();
			continue;
		}

		const std::filesystem::directory_entry& entry = *iterator;
		std::error_code status_error;
		if (!entry.is_regular_file(status_error) || status_error)
			continue;

		const std::string archive_path =
			generic_relative_path(active_catalog_root, entry.path());
		if (!file_is_readable(entry.path())) {
			skipped_files.push_back(
				BackupArchiveFileIssue{.archive_path = archive_path,
									   .reason		 = "file is not readable"});
			continue;
		}
		add_zip_entry(entries, entry_names, included_entries, entry.path(),
					  archive_path);
	}
}

[[nodiscard]] std::optional<std::filesystem::path> write_temp_text_file(
	const std::filesystem::path& directory, std::string file_name,
	std::string_view text, std::vector<core::Diagnostic>& diagnostics) {
	std::error_code error;
	std::filesystem::create_directories(directory, error);
	if (error) {
		append_diagnostic(
			diagnostics,
			make_diagnostic(
				core::DiagnosticSeverity::WriteBlockingError,
				"diagnostic-report-directory-unavailable",
				"Diagnostic archive report directory could not be created.",
				error.message()));
		return std::nullopt;
	}

	std::filesystem::path file_path = directory / std::move(file_name);
	std::ofstream output{file_path, std::ios::binary | std::ios::trunc};
	if (!output) {
		append_diagnostic(
			diagnostics,
			make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
							"diagnostic-report-write-failed",
							"Diagnostic archive report could not be opened.",
							file_path.string()));
		return std::nullopt;
	}
	output << text;
	output.flush();
	if (!output) {
		append_diagnostic(
			diagnostics,
			make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
							"diagnostic-report-write-failed",
							"Diagnostic archive report could not be written.",
							file_path.string()));
		return std::nullopt;
	}
	return file_path;
}

[[nodiscard]] bool collect_diagnostic_archive_entries(
	const BackupExportRequest& request, core::EpochMilliseconds created_at,
	const core::OperationIdentifier& operation_id,
	std::vector<platform::ZipArchiveEntrySource>& entries,
	std::vector<std::string>& included_entries,
	std::vector<BackupArchiveFileIssue>& skipped_files,
	std::vector<core::Diagnostic>& diagnostics) {
	std::set<std::string> entry_names;
	const bool catalog_root_accessible =
		directory_exists(request.paths.active_catalog_root);

	for (std::string_view relative_path : canonical_metadata_paths) {
		add_optional_file(entries, entry_names, included_entries, skipped_files,
						  request.paths.active_catalog_root, relative_path,
						  "canonical file is not available");
	}
	collect_recursive_files(entries, entry_names, included_entries,
							skipped_files, request.paths.active_catalog_root,
							request.paths.active_catalog_root
								/ std::filesystem::path{std::string{
									persistence::photo_media_directory_path}});
	collect_recursive_files(entries, entry_names, included_entries,
							skipped_files, request.paths.active_catalog_root,
							request.paths.active_catalog_root
								/ std::filesystem::path{std::string{
									persistence::recovery_directory_path}});

	std::vector<std::string> notes{
		"Diagnostic archive preserves raw readable files and does not repair "
		"catalog metadata.",
		"All readable files under media/photos are included, including orphan "
		"media."};
	std::vector<std::string> report_included_entries = included_entries;
	report_included_entries.push_back(
		std::string{diagnostic_archive_report_path});
	std::optional<std::string> report_json = diagnostic_archive_report_json(
		created_at, catalog_root_accessible, report_included_entries,
		skipped_files, notes, diagnostics);
	if (!report_json.has_value())
		return false;
	const std::string report_file_name =
		"diagnostic-archive-report-" + operation_id.value() + ".json";
	std::optional<std::filesystem::path> report_path =
		write_temp_text_file(request.paths.export_tmp_root, report_file_name,
							 *report_json, diagnostics);
	if (!report_path.has_value())
		return false;

	add_zip_entry(entries, entry_names, included_entries, *report_path,
				  std::string{diagnostic_archive_report_path});
	return true;
}

[[nodiscard]] core::Diagnostic schema_diagnostic(
	core::DiagnosticSeverity severity, std::string code, std::string_view path,
	const persistence::SchemaDiagnostic& diagnostic) {
	std::string details =
		"field=" + diagnostic.field
		+ "; issue=" + std::string{persistence::to_string(diagnostic.issue)};
	if (!diagnostic.technical_details.empty())
		details += "; " + diagnostic.technical_details;
	if (!path.empty())
		details += "; path=";
	details += path;
	return make_diagnostic(severity, std::move(code), diagnostic.message,
						   details);
}

[[nodiscard]] core::Diagnostic jsonl_diagnostic(
	const persistence::JsonlDiagnostic& diagnostic) {
	core::DiagnosticSeverity severity = core::DiagnosticSeverity::DegradedLoad;
	if (diagnostic.severity == persistence::LoadDiagnosticSeverity::Fatal)
		severity = core::DiagnosticSeverity::FatalCatalogError;
	else if (diagnostic.severity == persistence::LoadDiagnosticSeverity::Info)
		severity = core::DiagnosticSeverity::RecoverableWarning;

	std::string details = diagnostic.details;
	if (diagnostic.line.has_value())
		details += "; line=" + std::to_string(*diagnostic.line);
	if (!diagnostic.path.empty())
		details += "; path=" + diagnostic.path;

	return make_diagnostic(severity, diagnostic.code, diagnostic.message,
						   details);
}

[[nodiscard]] core::Diagnostic derived_diagnostic(
	const DerivedDiagnostic& diagnostic) {
	core::DiagnosticSeverity severity = core::DiagnosticSeverity::DegradedLoad;
	if (diagnostic.severity == DerivedDiagnosticSeverity::Info)
		severity = core::DiagnosticSeverity::RecoverableWarning;
	return make_diagnostic(severity, diagnostic.code, diagnostic.message,
						   diagnostic.details);
}

[[nodiscard]] bool has_fatal_diagnostic(
	const std::vector<core::Diagnostic>& diagnostics) {
	return std::ranges::any_of(diagnostics, [](const core::Diagnostic& entry) {
		return entry.severity == core::DiagnosticSeverity::FatalCatalogError;
	});
}

[[nodiscard]] bool has_degraded_diagnostic(
	const std::vector<core::Diagnostic>& diagnostics) {
	return std::ranges::any_of(diagnostics, [](const core::Diagnostic& entry) {
		return entry.severity == core::DiagnosticSeverity::DegradedLoad
			   || entry.severity
					  == core::DiagnosticSeverity::RecoverableWarning;
	});
}

[[nodiscard]] CatalogMediaSnapshot scan_staged_photo_media(
	const std::filesystem::path& staged_catalog_root,
	std::vector<core::Diagnostic>& diagnostics) {
	CatalogMediaSnapshot snapshot;
	const std::filesystem::path media_directory =
		staged_catalog_root
		/ std::filesystem::path{
			std::string{persistence::photo_media_directory_path}};
	std::error_code error;
	if (!std::filesystem::exists(media_directory, error))
		return snapshot;
	if (error) {
		snapshot.complete_scan_available = false;
		append_diagnostic(
			diagnostics,
			make_diagnostic(core::DiagnosticSeverity::DegradedLoad,
							"staged-media-scan-unavailable",
							"Staged media directory could not be inspected.",
							error.message()));
		return snapshot;
	}
	if (!directory_exists(media_directory)) {
		append_diagnostic(
			diagnostics,
			make_diagnostic(core::DiagnosticSeverity::FatalCatalogError,
							"staged-media-path-not-directory",
							"Staged media path is not a directory.",
							media_directory.string()));
		return snapshot;
	}

	std::filesystem::directory_iterator iterator{media_directory, error};
	if (error) {
		snapshot.complete_scan_available = false;
		append_diagnostic(
			diagnostics,
			make_diagnostic(core::DiagnosticSeverity::DegradedLoad,
							"staged-media-scan-unavailable",
							"Staged media directory could not be scanned.",
							error.message()));
		return snapshot;
	}

	for (const std::filesystem::directory_entry& entry : iterator) {
		error.clear();
		if (!entry.is_regular_file(error) || error)
			continue;
		snapshot.readable_photo_media_files.push_back(
			generic_relative_path(staged_catalog_root, entry.path()));
	}
	std::ranges::sort(snapshot.readable_photo_media_files);
	return snapshot;
}

void validate_required_path_not_directory(
	const std::filesystem::path& staged_catalog_root,
	std::string_view relative_path,
	std::vector<core::Diagnostic>& diagnostics) {
	const std::filesystem::path path =
		staged_catalog_root / std::filesystem::path{std::string{relative_path}};
	std::error_code error;
	if (std::filesystem::is_directory(path, error) && !error) {
		append_diagnostic(
			diagnostics,
			make_diagnostic(core::DiagnosticSeverity::FatalCatalogError,
							"staged-required-path-is-directory",
							"Required staged catalog file path is a directory.",
							std::string{relative_path}));
	}
}

[[nodiscard]] std::string load_entity_text_or_empty(
	const std::filesystem::path& staged_catalog_root,
	std::string_view relative_path,
	std::vector<core::Diagnostic>& diagnostics) {
	const std::filesystem::path path =
		staged_catalog_root / std::filesystem::path{std::string{relative_path}};
	std::error_code error;
	if (!std::filesystem::exists(path, error)) {
		append_diagnostic(
			diagnostics, make_diagnostic(core::DiagnosticSeverity::DegradedLoad,
										 "staged-entity-file-missing",
										 "Staged entity JSONL file is missing "
										 "and will be treated as empty.",
										 std::string{relative_path}));
		return {};
	}
	if (error) {
		append_diagnostic(
			diagnostics,
			make_diagnostic(
				core::DiagnosticSeverity::FatalCatalogError,
				"staged-entity-file-status-failed",
				"Staged entity JSONL file status could not be read.",
				error.message()));
		return {};
	}

	std::optional<std::string> text = read_text_file(
		path, diagnostics, core::DiagnosticSeverity::FatalCatalogError,
		"staged-entity-file-unreadable",
		"Staged entity JSONL file could not be read.");
	return text.value_or(std::string{});
}

void copy_zip_metrics(BackupExportResult& result,
					  const platform::ZipArchiveInspection& inspection) {
	result.archive_byte_count		   = inspection.archive_byte_count;
	result.largest_entry_byte_count	   = inspection.largest_entry_byte_count;
	result.classic_zip64_risk_observed = inspection.classic_zip64_risk_observed;
}

void copy_zip_metrics(BackupImportStagingResult& result,
					  const platform::ZipArchiveInspection& inspection) {
	result.archive_byte_count		   = inspection.archive_byte_count;
	result.largest_entry_byte_count	   = inspection.largest_entry_byte_count;
	result.classic_zip64_risk_observed = inspection.classic_zip64_risk_observed;
}

[[nodiscard]] std::set<std::string> archive_entry_set(
	const platform::ZipArchiveInspection& inspection) {
	std::set<std::string> entries;
	for (const platform::ZipArchiveEntryInfo& entry : inspection.entries)
		if (!entry.directory)
			entries.insert(entry.archive_path);
	return entries;
}

[[nodiscard]] bool validate_normal_backup_inspection(
	BackupExportResult& result, const BackupExportRequest& request,
	const platform::ZipArchiveInspection& inspection) {
	const std::set<std::string> entries = archive_entry_set(inspection);
	bool valid							= true;
	for (std::string_view relative_path : canonical_metadata_paths) {
		if (!entries.contains(std::string{relative_path})) {
			append_diagnostic(
				result.diagnostics,
				make_diagnostic(
					core::DiagnosticSeverity::WriteBlockingError,
					"backup-required-entry-missing",
					"Backup ZIP is missing a required catalog entry.",
					std::string{relative_path}));
			valid = false;
		}
	}

	for (const persistence::PhotoEnvelope& photo :
		 request.current_state.photos) {
		const std::string relative_path =
			expected_photo_media_relative_path(photo.record.id);
		if (entries.contains(relative_path))
			continue;
		if (known_broken_photo_media(request.current_state, photo.record.id))
			continue;

		append_diagnostic(
			result.diagnostics,
			make_diagnostic(
				core::DiagnosticSeverity::WriteBlockingError,
				"backup-photo-entry-missing",
				"Backup ZIP is missing media for an accepted usable photo.",
				relative_path));
		valid = false;
	}

	return valid;
}

void cleanup_path(std::optional<std::filesystem::path> path,
				  bool& cleanup_attempted) {
	if (!path.has_value())
		return;
	cleanup_attempted = true;
	std::error_code ignored;
	std::filesystem::remove_all(*path, ignored);
}

[[nodiscard]] std::filesystem::path export_temp_zip_path(
	const platform::AppPrivatePaths& paths,
	const core::OperationIdentifier& operation_id,
	BackupArchiveKind archive_kind) {
	const std::string prefix = archive_kind == BackupArchiveKind::NormalBackup
								   ? "backup-"
								   : "diagnostic-";
	return paths.export_tmp_root / (prefix + operation_id.value() + ".zip");
}

[[nodiscard]] std::filesystem::path validation_temp_root(
	const platform::AppPrivatePaths& paths,
	const core::OperationIdentifier& operation_id) {
	return paths.operation_tmp_root / "zip-validation" / operation_id.value();
}
}	 // namespace

std::string_view to_string(BackupArchiveKind kind) noexcept {
	switch (kind) {
		case BackupArchiveKind::NormalBackup:
			return "normal backup";
		case BackupArchiveKind::DiagnosticArchive:
			return "diagnostic archive";
	}

	return "unknown backup archive kind";
}

std::string_view to_string(BackupExportStatus status) noexcept {
	switch (status) {
		case BackupExportStatus::Exported:
			return "exported";
		case BackupExportStatus::Failed:
			return "failed";
		case BackupExportStatus::Cancelled:
			return "cancelled";
	}

	return "unknown backup export status";
}

bool BackupExportResult::succeeded() const noexcept {
	return status == BackupExportStatus::Exported
		   && category == core::OperationResultCategory::Success;
}

bool BackupExportResult::was_user_cancelled() const noexcept {
	return status == BackupExportStatus::Cancelled
		   || category == core::OperationResultCategory::UserCancelled;
}

bool BackupExportResult::failed() const noexcept {
	return !succeeded() && !was_user_cancelled();
}

bool StagedCatalogValidationResult::import_allowed() const noexcept {
	return load_status == persistence::CatalogLoadStatus::Normal
		   || load_status == persistence::CatalogLoadStatus::Degraded;
}

bool StagedCatalogValidationResult::explicit_warning_required() const noexcept {
	return load_status == persistence::CatalogLoadStatus::Degraded;
}

bool BackupImportStagingResult::succeeded() const noexcept {
	return category == core::OperationResultCategory::Success
		   && validation.import_allowed();
}

bool BackupImportStagingResult::was_user_cancelled() const noexcept {
	return category == core::OperationResultCategory::UserCancelled;
}

bool BackupImportStagingResult::failed() const noexcept {
	return !succeeded() && !was_user_cancelled();
}

std::string suggested_backup_file_name(core::EpochMilliseconds created_at) {
	return "shuba-backup-" + timestamp_file_name_fragment(created_at) + ".zip";
}

std::string suggested_diagnostic_archive_file_name(
	core::EpochMilliseconds created_at) {
	return "shuba-diagnostic-" + timestamp_file_name_fragment(created_at)
		   + ".zip";
}

StagedCatalogValidationResult validate_staged_catalog(
	const std::filesystem::path& staged_catalog_root,
	core::EpochMilliseconds validated_at) {
	(void)validated_at;
	StagedCatalogValidationResult result;
	std::error_code error;
	if (!std::filesystem::is_directory(staged_catalog_root, error) || error) {
		append_diagnostic(
			result.diagnostics,
			make_diagnostic(
				core::DiagnosticSeverity::FatalCatalogError,
				"staged-root-unavailable",
				"Staged catalog root is not accessible as a directory.",
				staged_catalog_root.string()));
		return result;
	}

	for (std::string_view relative_path : canonical_metadata_paths)
		validate_required_path_not_directory(staged_catalog_root, relative_path,
											 result.diagnostics);
	if (has_fatal_diagnostic(result.diagnostics))
		return result;

	const std::filesystem::path manifest_path =
		staged_catalog_root
		/ std::filesystem::path{std::string{persistence::manifest_file_path}};
	if (!file_is_regular(manifest_path)) {
		append_diagnostic(
			result.diagnostics,
			make_diagnostic(core::DiagnosticSeverity::FatalCatalogError,
							"staged-manifest-missing",
							"Staged catalog manifest is missing.",
							manifest_path.string()));
		return result;
	}

	std::optional<std::string> manifest_text =
		read_text_file(manifest_path, result.diagnostics,
					   core::DiagnosticSeverity::FatalCatalogError,
					   "staged-manifest-unreadable",
					   "Staged catalog manifest could not be read.");
	if (!manifest_text.has_value())
		return result;
	persistence::SchemaReadResult<persistence::ManifestRecord> manifest =
		persistence::parse_manifest_json(*manifest_text);
	if (!manifest.succeeded()) {
		append_diagnostic(
			result.diagnostics,
			schema_diagnostic(core::DiagnosticSeverity::FatalCatalogError,
							  "staged-manifest-invalid",
							  std::string{persistence::manifest_file_path},
							  *manifest.diagnostic));
		return result;
	}
	result.catalog_id = manifest.value->catalog_id;

	const std::filesystem::path settings_path =
		staged_catalog_root
		/ std::filesystem::path{std::string{persistence::settings_file_path}};
	if (file_is_regular(settings_path)) {
		std::optional<std::string> settings_text =
			read_text_file(settings_path, result.diagnostics,
						   core::DiagnosticSeverity::DegradedLoad,
						   "staged-settings-unreadable",
						   "Staged settings file could not be read and "
						   "defaults will be used.");
		if (settings_text.has_value()) {
			persistence::SchemaReadResult<persistence::SettingsRecord>
				settings = persistence::parse_settings_json(*settings_text);
			if (!settings.succeeded()) {
				append_diagnostic(
					result.diagnostics,
					schema_diagnostic(
						core::DiagnosticSeverity::DegradedLoad,
						"staged-settings-invalid",
						std::string{persistence::settings_file_path},
						*settings.diagnostic));
			}
		}
	} else {
		append_diagnostic(
			result.diagnostics,
			make_diagnostic(
				core::DiagnosticSeverity::DegradedLoad,
				"staged-settings-missing",
				"Staged settings file is missing and defaults will be used.",
				settings_path.string()));
	}

	persistence::CatalogJsonlDocuments documents{
		.items_jsonl = load_entity_text_or_empty(
			staged_catalog_root, persistence::items_data_file_path,
			result.diagnostics),
		.storages_jsonl = load_entity_text_or_empty(
			staged_catalog_root, persistence::storages_data_file_path,
			result.diagnostics),
		.photos_jsonl = load_entity_text_or_empty(
			staged_catalog_root, persistence::photos_data_file_path,
			result.diagnostics)};
	if (has_fatal_diagnostic(result.diagnostics))
		return result;

	persistence::CatalogJsonlLoadResult load_result =
		persistence::load_catalog_jsonl(documents);
	for (const persistence::JsonlDiagnostic& diagnostic :
		 load_result.diagnostics)
		append_diagnostic(result.diagnostics, jsonl_diagnostic(diagnostic));
	result.items_accepted	 = load_result.items.summary.accepted_records;
	result.storages_accepted = load_result.storages.summary.accepted_records;
	result.photos_accepted	 = load_result.photos.summary.accepted_records;

	CatalogMediaSnapshot media =
		scan_staged_photo_media(staged_catalog_root, result.diagnostics);
	CatalogRepositoryState state = build_catalog_repository(
		make_catalog_repository_input(load_result, std::move(media)));
	for (const DerivedDiagnostic& diagnostic : state.diagnostics)
		append_diagnostic(result.diagnostics, derived_diagnostic(diagnostic));
	result.derived_recovery_summary = state.recovery_summary;

	if (has_fatal_diagnostic(result.diagnostics))
		result.load_status = persistence::CatalogLoadStatus::Fatal;
	else if (load_result.load_status == persistence::CatalogLoadStatus::Degraded
			 || has_degraded_diagnostic(result.diagnostics))
		result.load_status = persistence::CatalogLoadStatus::Degraded;
	else
		result.load_status = persistence::CatalogLoadStatus::Normal;
	return result;
}

BackupArchiveUseCase::BackupArchiveUseCase(
	core::IdentifierSource& identifier_source, const core::Clock& clock,
	core::OperationGate& operation_gate,
	platform::ZipArchiveService& zip_archive_service,
	platform::DocumentExportService& document_export_service,
	platform::ContentStagingService& content_staging_service)
	: identifiers(identifier_source)
	, backup_clock(clock)
	, gate(operation_gate)
	, zip_archives(zip_archive_service)
	, document_exporter(document_export_service)
	, staging(content_staging_service) {}

BackupExportResult BackupArchiveUseCase::export_normal_backup(
	const BackupExportRequest& request, platform::ProgressSink& progress_sink,
	platform::CancellationToken& cancellation_token) {
	BackupExportResult result{.archive_kind = BackupArchiveKind::NormalBackup,
							  .destination	= request.destination};
	const core::OperationIdentifier operation_id =
		identifiers.next_operation_identifier();
	platform::PlatformOperationStartResult operation_start =
		platform::try_start_platform_operation(
			gate,
			platform::PlatformOperationStartRequest{
				.operation_kind = core::OperationKind::BackupExport,
				.operation_id	= operation_id,
				.operation_type =
					platform::ProgressOperationType::BackupExport},
			progress_sink, cancellation_token);
	if (!operation_start.succeeded()) {
		result.category	   = operation_start.category;
		result.diagnostics = std::move(operation_start.diagnostics);
		return result;
	}
	platform::ScopedPlatformOperation& operation = *operation_start.operation;
	operation.publish_progress("backup-preparing", std::uint64_t{0},
							   std::nullopt, "Preparing normal backup.", true);

	if (request.current_load_status
		== persistence::CatalogLoadStatus::Degraded) {
		result.degraded_warning_required		= true;
		result.diagnostic_companion_recommended = true;
		append_diagnostic(
			result.diagnostics,
			make_diagnostic(
				core::DiagnosticSeverity::DegradedLoad,
				"backup-preserves-degraded-catalog",
				"Normal backup will preserve the current damaged catalog state "
				"as raw files.",
				"Offer a diagnostic archive as a companion action."));
	}

	std::vector<platform::ZipArchiveEntrySource> entries;
	if (!collect_normal_backup_entries(
			request, entries, result.included_entries, result.skipped_files,
			result.diagnostics)) {
		result.category = core::OperationResultCategory::SourceUnavailable;
		return result;
	}

	result.temp_zip_path =
		export_temp_zip_path(request.paths, operation.context().operation_id,
							 BackupArchiveKind::NormalBackup);
	platform::PlatformValueResult<platform::ZipArchiveInspection> built =
		zip_archives.build_zip_archive(
			platform::ZipArchiveBuildRequest{
				.output_path = *result.temp_zip_path, .entries = entries},
			operation.context(), progress_sink, cancellation_token);
	if (!built.succeeded()) {
		result.category = built.category;
		if (built.was_user_cancelled())
			result.status = BackupExportStatus::Cancelled;
		append_diagnostics(result.diagnostics, built.diagnostics);
		cleanup_path(result.temp_zip_path, result.temp_cleanup_attempted);
		return result;
	}
	result.temp_zip_built = true;
	copy_zip_metrics(result, *built.value);
	if (!validate_normal_backup_inspection(result, request, *built.value)) {
		result.category = core::OperationResultCategory::ValidationFailure;
		cleanup_path(result.temp_zip_path, result.temp_cleanup_attempted);
		return result;
	}

	const std::filesystem::path validation_root =
		validation_temp_root(request.paths, operation.context().operation_id);
	std::error_code ignored;
	std::filesystem::remove_all(validation_root, ignored);
	platform::PlatformValueResult<platform::ZipArchiveInspection> extracted =
		zip_archives.extract_zip_archive(
			platform::ZipArchiveExtractRequest{
				.archive_path	  = *result.temp_zip_path,
				.target_directory = validation_root},
			operation.context(), progress_sink, cancellation_token);
	if (!extracted.succeeded()) {
		result.category = extracted.category;
		if (extracted.was_user_cancelled())
			result.status = BackupExportStatus::Cancelled;
		append_diagnostics(result.diagnostics, extracted.diagnostics);
		std::filesystem::remove_all(validation_root, ignored);
		cleanup_path(result.temp_zip_path, result.temp_cleanup_attempted);
		return result;
	}
	StagedCatalogValidationResult staged_validation =
		validate_staged_catalog(validation_root, backup_clock.now());
	std::filesystem::remove_all(validation_root, ignored);
	if (staged_validation.load_status
		== persistence::CatalogLoadStatus::Fatal) {
		result.category = core::OperationResultCategory::ValidationFailure;
		append_diagnostics(result.diagnostics, staged_validation.diagnostics);
		cleanup_path(result.temp_zip_path, result.temp_cleanup_attempted);
		return result;
	}
	result.temp_zip_validated = true;

	operation.publish_progress("backup-copying", std::nullopt, std::nullopt,
							   "Copying backup ZIP to destination.", true);
	core::OperationResult copied = document_exporter.copy_file_to_destination(
		platform::DocumentCopyRequest{.temp_source_path = *result.temp_zip_path,
									  .destination		= request.destination},
		operation.context(), progress_sink, cancellation_token);
	if (copied.was_user_cancelled()) {
		result.status	= BackupExportStatus::Cancelled;
		result.category = core::OperationResultCategory::UserCancelled;
		cleanup_path(result.temp_zip_path, result.temp_cleanup_attempted);
		return result;
	}
	if (copied.failed()) {
		result.category = copied.category();
		append_diagnostics(result.diagnostics, copied.diagnostics());
		cleanup_path(result.temp_zip_path, result.temp_cleanup_attempted);
		return result;
	}

	result.destination_copied = true;
	result.status			  = BackupExportStatus::Exported;
	result.category			  = core::OperationResultCategory::Success;
	if (!request.keep_temp_zip)
		cleanup_path(result.temp_zip_path, result.temp_cleanup_attempted);
	operation.publish_progress("backup-done", std::nullopt, std::nullopt,
							   "Normal backup export completed.", false);
	return result;
}

BackupExportResult BackupArchiveUseCase::export_diagnostic_archive(
	const BackupExportRequest& request, platform::ProgressSink& progress_sink,
	platform::CancellationToken& cancellation_token) {
	BackupExportResult result{
		.archive_kind = BackupArchiveKind::DiagnosticArchive,
		.destination  = request.destination};
	const core::OperationIdentifier operation_id =
		identifiers.next_operation_identifier();
	platform::PlatformOperationStartResult operation_start =
		platform::try_start_platform_operation(
			gate,
			platform::PlatformOperationStartRequest{
				.operation_kind = core::OperationKind::BackupExport,
				.operation_id	= operation_id,
				.operation_type =
					platform::ProgressOperationType::DiagnosticExport},
			progress_sink, cancellation_token);
	if (!operation_start.succeeded()) {
		result.category	   = operation_start.category;
		result.diagnostics = std::move(operation_start.diagnostics);
		return result;
	}
	platform::ScopedPlatformOperation& operation = *operation_start.operation;
	operation.publish_progress("diagnostic-preparing", std::uint64_t{0},
							   std::nullopt, "Preparing diagnostic archive.",
							   true);

	std::vector<platform::ZipArchiveEntrySource> entries;
	if (!collect_diagnostic_archive_entries(
			request, backup_clock.now(), operation.context().operation_id,
			entries, result.included_entries, result.skipped_files,
			result.diagnostics)) {
		result.category =
			core::OperationResultCategory::TemporaryStorageFailure;
		return result;
	}

	result.temp_zip_path =
		export_temp_zip_path(request.paths, operation.context().operation_id,
							 BackupArchiveKind::DiagnosticArchive);
	platform::PlatformValueResult<platform::ZipArchiveInspection> built =
		zip_archives.build_zip_archive(
			platform::ZipArchiveBuildRequest{
				.output_path = *result.temp_zip_path, .entries = entries},
			operation.context(), progress_sink, cancellation_token);
	if (!built.succeeded()) {
		result.category = built.category;
		if (built.was_user_cancelled())
			result.status = BackupExportStatus::Cancelled;
		append_diagnostics(result.diagnostics, built.diagnostics);
		cleanup_path(result.temp_zip_path, result.temp_cleanup_attempted);
		return result;
	}
	result.temp_zip_built	  = true;
	result.temp_zip_validated = true;
	copy_zip_metrics(result, *built.value);

	core::OperationResult copied = document_exporter.copy_file_to_destination(
		platform::DocumentCopyRequest{.temp_source_path = *result.temp_zip_path,
									  .destination		= request.destination},
		operation.context(), progress_sink, cancellation_token);
	if (copied.was_user_cancelled()) {
		result.status	= BackupExportStatus::Cancelled;
		result.category = core::OperationResultCategory::UserCancelled;
		cleanup_path(result.temp_zip_path, result.temp_cleanup_attempted);
		return result;
	}
	if (copied.failed()) {
		result.category = copied.category();
		append_diagnostics(result.diagnostics, copied.diagnostics());
		cleanup_path(result.temp_zip_path, result.temp_cleanup_attempted);
		return result;
	}

	result.destination_copied = true;
	result.status			  = BackupExportStatus::Exported;
	result.category			  = core::OperationResultCategory::Success;
	if (!request.keep_temp_zip)
		cleanup_path(result.temp_zip_path, result.temp_cleanup_attempted);
	operation.publish_progress("diagnostic-done", std::nullopt, std::nullopt,
							   "Diagnostic archive export completed.", false);
	return result;
}

BackupImportStagingResult BackupArchiveUseCase::stage_and_validate_import(
	const BackupImportStagingRequest& request,
	platform::ProgressSink& progress_sink,
	platform::CancellationToken& cancellation_token) {
	BackupImportStagingResult result;
	const core::OperationIdentifier operation_id =
		identifiers.next_operation_identifier();
	platform::PlatformOperationStartResult operation_start =
		platform::try_start_platform_operation(
			gate,
			platform::PlatformOperationStartRequest{
				.operation_kind = core::OperationKind::BackupImport,
				.operation_id	= operation_id,
				.operation_type =
					platform::ProgressOperationType::BackupImport},
			progress_sink, cancellation_token);
	if (!operation_start.succeeded()) {
		result.category	   = operation_start.category;
		result.diagnostics = std::move(operation_start.diagnostics);
		return result;
	}
	platform::ScopedPlatformOperation& operation = *operation_start.operation;
	operation.publish_progress("backup-import-staging", std::uint64_t{0},
							   std::nullopt, "Staging backup ZIP for import.",
							   true);

	const std::filesystem::path staged_zip_directory =
		request.paths.operation_tmp_root / "imports";
	const std::string display_name = request.source.display_name.empty()
										 ? "backup.zip"
										 : request.source.display_name;
	const std::string staged_file_name =
		platform::make_staged_content_file_name(
			"backup-import", operation.context().operation_id, 1U,
			display_name);
	platform::PlatformValueResult<platform::StagedContent> staged =
		staging.stage_content(
			platform::ContentStagingRequest{
				.source						= request.source,
				.target_directory			= staged_zip_directory,
				.target_file_name			= staged_file_name,
				.allow_no_copy_optimization = false},
			operation.context(), progress_sink, cancellation_token);
	if (!staged.succeeded()) {
		result.category = staged.was_user_cancelled()
							  ? core::OperationResultCategory::UserCancelled
							  : staged.category;
		append_diagnostics(result.diagnostics, staged.diagnostics);
		return result;
	}
	result.staged_zip_copied	= true;
	result.staged_zip_path		= staged.value->staged_path;
	result.staging_catalog_root = request.paths.operation_tmp_root
								  / "import-staging"
								  / operation.context().operation_id.value();
	std::error_code ignored;
	std::filesystem::remove_all(*result.staging_catalog_root, ignored);

	platform::PlatformValueResult<platform::ZipArchiveInspection> extracted =
		zip_archives.extract_zip_archive(
			platform::ZipArchiveExtractRequest{
				.archive_path	  = *result.staged_zip_path,
				.target_directory = *result.staging_catalog_root},
			operation.context(), progress_sink, cancellation_token);
	if (!extracted.succeeded()) {
		result.category = extracted.was_user_cancelled()
							  ? core::OperationResultCategory::UserCancelled
							  : extracted.category;
		append_diagnostics(result.diagnostics, extracted.diagnostics);
		if (!request.keep_staged_zip)
			cleanup_path(result.staged_zip_path,
						 result.staged_zip_cleanup_attempted);
		if (!request.keep_extracted_catalog)
			cleanup_path(result.staging_catalog_root,
						 result.extracted_catalog_cleanup_attempted);
		return result;
	}
	result.zip_extracted = true;
	copy_zip_metrics(result, *extracted.value);
	result.validation = validate_staged_catalog(*result.staging_catalog_root,
												backup_clock.now());
	append_diagnostics(result.diagnostics, result.validation.diagnostics);
	result.category = result.validation.import_allowed()
						  ? core::OperationResultCategory::Success
						  : core::OperationResultCategory::ValidationFailure;
	if (!request.keep_staged_zip)
		cleanup_path(result.staged_zip_path,
					 result.staged_zip_cleanup_attempted);
	if (!request.keep_extracted_catalog)
		cleanup_path(result.staging_catalog_root,
					 result.extracted_catalog_cleanup_attempted);
	operation.publish_progress(
		"backup-import-validated", std::nullopt, std::nullopt,
		"Backup ZIP staging validation completed.", false);
	return result;
}
}	 // namespace shuba::catalog
