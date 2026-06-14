#include "Persistence/CatalogStorage.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

namespace shuba::persistence {
namespace {
constexpr auto recovery_directory_path = std::string_view{"recovery"};
constexpr auto quarantine_directory_path =
	std::string_view{"recovery/quarantine"};

[[nodiscard]] std::array<std::string_view, 5> canonical_metadata_paths() {
	return {manifest_file_path, settings_file_path, items_data_file_path,
			storages_data_file_path, photos_data_file_path};
}

[[nodiscard]] std::array<std::string_view, 8> empty_catalog_directories() {
	return {data_directory_path,
			std::string_view{"media"},
			photo_media_directory_path,
			recovery_directory_path,
			quarantine_directory_path,
			backup_directory_path,
			previous_data_copies_directory_path,
			active_catalog_tmp_directory_path};
}

[[nodiscard]] std::string path_text(const std::filesystem::path& path) {
	return path.generic_string();
}

[[nodiscard]] bool is_supported_metadata_relative_path(
	const std::filesystem::path& path) {
	if (path.empty() || path.is_absolute())
		return false;

	for (const auto& part : path)
		if (part == "..")
			return false;

	const auto text			   = path.lexically_normal().generic_string();
	const auto supported_paths = canonical_metadata_paths();
	return std::ranges::any_of(
		supported_paths,
		[&](std::string_view candidate) { return text == candidate; });
}

[[nodiscard]] bool is_metadata_temp_name(
	const std::filesystem::path& filename) {
	const auto text = filename.string();
	return text.starts_with('.') && text.ends_with(".tmp");
}

[[nodiscard]] std::uint64_t absolute_milliseconds(std::int64_t value) noexcept {
	if (value >= 0)
		return static_cast<std::uint64_t>(value);

	return static_cast<std::uint64_t>(-(value + 1)) + 1U;
}

[[nodiscard]] core::Diagnostic make_diagnostic(
	core::DiagnosticSeverity severity, std::string code, std::string message,
	std::string technical_details = {}) {
	return {.severity		   = severity,
			.code			   = std::move(code),
			.message		   = std::move(message),
			.technical_details = std::move(technical_details)};
}

[[nodiscard]] CatalogStorageResult make_failure(
	core::OperationResultCategory category, core::Diagnostic diagnostic,
	std::optional<std::filesystem::path> temp_path = std::nullopt) {
	return {.category	 = category,
			.diagnostics = {std::move(diagnostic)},
			.temp_path	 = std::move(temp_path)};
}

[[nodiscard]] core::Diagnostic filesystem_diagnostic(
	core::DiagnosticSeverity severity, std::string code, std::string message,
	const std::filesystem::path& path, const std::error_code& error) {
	auto details = path_text(path);
	if (error)
		details += ": " + error.message();
	return make_diagnostic(severity, std::move(code), std::move(message),
						   std::move(details));
}

[[nodiscard]] CatalogStorageResult validate_commit_target(
	const std::filesystem::path& relative_target_path,
	const core::OperationIdentifier& operation_id) {
	if (!is_supported_metadata_relative_path(relative_target_path)) {
		return make_failure(
			core::OperationResultCategory::ValidationFailure,
			make_diagnostic(core::DiagnosticSeverity::ActionValidationError,
							"unsupported_metadata_path",
							"Metadata commit target is not a first-version "
							"canonical metadata file.",
							path_text(relative_target_path)));
	}

	if (!core::validate_file_safe_identifier_text(operation_id.value())) {
		return make_failure(
			core::OperationResultCategory::ValidationFailure,
			make_diagnostic(
				core::DiagnosticSeverity::ActionValidationError,
				"unsafe_operation_id",
				"Metadata commit operation identifier is not file-safe.",
				operation_id.value()));
	}

	return {};
}

[[nodiscard]] std::optional<core::Diagnostic> create_directories_or_diagnostic(
	const std::filesystem::path& path, std::string code, std::string message) {
	auto error = std::error_code{};
	std::filesystem::create_directories(path, error);
	if (!error)
		return std::nullopt;

	return filesystem_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
								 std::move(code), std::move(message), path,
								 error);
}

[[nodiscard]] std::optional<core::Diagnostic> write_text_file(
	const std::filesystem::path& path, std::string_view text) {
	auto output = std::ofstream{path, std::ios::binary | std::ios::trunc};
	if (!output.is_open()) {
		return make_diagnostic(
			core::DiagnosticSeverity::WriteBlockingError, "temp_write_failed",
			"Temporary metadata file could not be opened.", path_text(path));
	}

	output.write(text.data(), static_cast<std::streamsize>(text.size()));
	if (!output) {
		return make_diagnostic(
			core::DiagnosticSeverity::WriteBlockingError, "temp_write_failed",
			"Temporary metadata file write failed.", path_text(path));
	}

	output.flush();
	if (!output) {
		return make_diagnostic(
			core::DiagnosticSeverity::WriteBlockingError, "temp_flush_failed",
			"Temporary metadata file flush failed.", path_text(path));
	}

	output.close();
	if (!output) {
		return make_diagnostic(
			core::DiagnosticSeverity::WriteBlockingError, "temp_close_failed",
			"Temporary metadata file close failed.", path_text(path));
	}

	return std::nullopt;
}

[[nodiscard]] std::optional<std::string> read_text_file(
	const std::filesystem::path& path, core::Diagnostic& diagnostic) {
	auto input = std::ifstream{path, std::ios::binary};
	if (!input.is_open()) {
		diagnostic = make_diagnostic(
			core::DiagnosticSeverity::WriteBlockingError,
			"temp_validation_read_failed",
			"Temporary metadata file could not be reopened for validation.",
			path_text(path));
		return std::nullopt;
	}

	auto buffer = std::ostringstream{};
	buffer << input.rdbuf();
	if (input.bad()) {
		diagnostic = make_diagnostic(
			core::DiagnosticSeverity::WriteBlockingError,
			"temp_validation_read_failed",
			"Temporary metadata file could not be read for validation.",
			path_text(path));
		return std::nullopt;
	}

	return buffer.str();
}

void remove_temp_if_owned(const std::filesystem::path& temp_path) noexcept {
	auto ignored = std::error_code{};
	std::filesystem::remove(temp_path, ignored);
}

[[nodiscard]] CatalogStorageResult prepare_validated_temp_file(
	const std::filesystem::path& active_catalog_root,
	const std::filesystem::path& relative_target_path, std::string_view content,
	std::string_view group_name, const MetadataTextValidator& validator,
	bool allow_replace) {
	if (!is_supported_metadata_relative_path(relative_target_path)) {
		return make_failure(
			core::OperationResultCategory::ValidationFailure,
			make_diagnostic(core::DiagnosticSeverity::ActionValidationError,
							"unsupported_metadata_path",
							"Metadata commit target is not a first-version "
							"canonical metadata file.",
							path_text(relative_target_path)));
	}

	const auto target_path = active_catalog_root / relative_target_path;
	const auto temp_path   = metadata_temp_file_path(
		active_catalog_root, relative_target_path, group_name);

	auto result = CatalogStorageResult{.temp_path = temp_path};

	if (auto diagnostic = create_directories_or_diagnostic(
			target_path.parent_path(), "metadata_parent_unavailable",
			"Canonical metadata parent directory could not be created.")) {
		return make_failure(
			core::OperationResultCategory::DestinationUnavailable,
			std::move(*diagnostic), temp_path);
	}

	auto error				 = std::error_code{};
	const auto target_exists = std::filesystem::exists(target_path, error);
	if (error) {
		return make_failure(
			core::OperationResultCategory::DestinationUnavailable,
			filesystem_diagnostic(
				core::DiagnosticSeverity::WriteBlockingError,
				"metadata_target_status_failed",
				"Canonical metadata target status could not be checked.",
				target_path, error),
			temp_path);
	}

	if (!allow_replace && target_exists) {
		return make_failure(
			core::OperationResultCategory::ValidationFailure,
			make_diagnostic(core::DiagnosticSeverity::ActionValidationError,
							"metadata_target_exists",
							"Empty catalog initialization would overwrite "
							"existing canonical metadata.",
							path_text(target_path)),
			temp_path);
	}

	if (auto diagnostic = write_text_file(temp_path, content)) {
		return make_failure(
			core::OperationResultCategory::TemporaryStorageFailure,
			std::move(*diagnostic), temp_path);
	}

	if (validator) {
		auto read_diagnostic = core::Diagnostic{};
		const auto temp_text = read_text_file(temp_path, read_diagnostic);
		if (!temp_text) {
			remove_temp_if_owned(temp_path);
			return make_failure(
				core::OperationResultCategory::TemporaryStorageFailure,
				std::move(read_diagnostic), temp_path);
		}

		if (auto validation_diagnostic = validator(*temp_text)) {
			remove_temp_if_owned(temp_path);
			return make_failure(
				core::OperationResultCategory::ValidationFailure,
				std::move(*validation_diagnostic), temp_path);
		}
	}

	if (!allow_replace) {
		error.clear();
		if (std::filesystem::exists(target_path, error) || error) {
			remove_temp_if_owned(temp_path);
			return make_failure(
				core::OperationResultCategory::ValidationFailure,
				make_diagnostic(core::DiagnosticSeverity::ActionValidationError,
								"metadata_target_exists",
								"Empty catalog initialization target appeared "
								"before replacement.",
								path_text(target_path)),
				temp_path);
		}
	}

	return result;
}

[[nodiscard]] std::optional<core::Diagnostic> replace_temp_file(
	const std::filesystem::path& temp_path,
	const std::filesystem::path& target_path) {
	auto error = std::error_code{};
	std::filesystem::rename(temp_path, target_path, error);
	if (!error)
		return std::nullopt;

	remove_temp_if_owned(temp_path);
	return filesystem_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
								 "metadata_replace_failed",
								 "Validated temporary metadata file could not "
								 "replace the canonical file.",
								 target_path, error);
}

struct PreviousCopyAttempt final {
	bool created{};
	std::filesystem::path directory;
	std::vector<core::Diagnostic> diagnostics;
};

[[nodiscard]] PreviousCopyAttempt create_previous_metadata_copy_group(
	const std::filesystem::path& active_catalog_root,
	std::string_view group_name) {
	auto attempt =
		PreviousCopyAttempt{.directory = previous_metadata_copy_directory(
								active_catalog_root, group_name)};

	auto error = std::error_code{};
	std::filesystem::create_directories(attempt.directory, error);
	if (error) {
		attempt.diagnostics.push_back(filesystem_diagnostic(
			core::DiagnosticSeverity::RecoverableWarning,
			"previous_copy_failed",
			"Previous metadata copy group could not be created.",
			attempt.directory, error));
		return attempt;
	}

	auto copied_count = std::size_t{};
	auto failed_count = std::size_t{};
	for (const auto relative_text : canonical_metadata_paths()) {
		const auto relative_path =
			std::filesystem::path{std::string{relative_text}};
		const auto source_path = active_catalog_root / relative_path;
		error.clear();
		const auto source_exists = std::filesystem::exists(source_path, error);
		if (error || !source_exists)
			continue;

		const auto destination_path = attempt.directory / relative_path;
		error.clear();
		std::filesystem::create_directories(destination_path.parent_path(),
											error);
		if (error) {
			++failed_count;
			continue;
		}

		error.clear();
		std::filesystem::copy_file(
			source_path, destination_path,
			std::filesystem::copy_options::overwrite_existing, error);
		if (error) {
			++failed_count;
			continue;
		}

		++copied_count;
	}

	if (copied_count == canonical_metadata_paths().size()
		&& failed_count == 0U) {
		attempt.created = true;
		return attempt;
	}

	attempt.diagnostics.push_back(make_diagnostic(
		core::DiagnosticSeverity::RecoverableWarning,
		"previous_copy_incomplete",
		"Previous metadata copy group was incomplete; metadata commit will "
		"continue.",
		"copied=" + std::to_string(copied_count)
			+ "; failed=" + std::to_string(failed_count)
			+ "; expected=" + std::to_string(canonical_metadata_paths().size())
			+ "; directory=" + path_text(attempt.directory)));
	return attempt;
}

[[nodiscard]] std::optional<core::Diagnostic> cleanup_previous_copy_retention(
	const std::filesystem::path& active_catalog_root, std::size_t retention) {
	const auto copies_root = active_catalog_root
							 / std::filesystem::path{std::string{
								 previous_data_copies_directory_path}};

	auto error = std::error_code{};
	if (!std::filesystem::exists(copies_root, error))
		return std::nullopt;
	if (error)
		return filesystem_diagnostic(
			core::DiagnosticSeverity::RecoverableWarning,
			"previous_copy_retention_failed",
			"Previous metadata copy root status could not be checked.",
			copies_root, error);

	auto groups	  = std::vector<std::filesystem::path>{};
	auto iterator = std::filesystem::directory_iterator{copies_root, error};
	if (error)
		return filesystem_diagnostic(
			core::DiagnosticSeverity::RecoverableWarning,
			"previous_copy_retention_failed",
			"Previous metadata copy groups could not be listed.", copies_root,
			error);

	for (const auto& entry : iterator) {
		error.clear();
		if (entry.is_directory(error) && !error)
			groups.push_back(entry.path());
	}

	if (groups.size() <= retention)
		return std::nullopt;

	std::ranges::sort(groups, [](const auto& left, const auto& right) {
		return left.filename().string() < right.filename().string();
	});

	const auto remove_count = groups.size() - retention;
	for (auto index = std::size_t{}; index < remove_count; ++index) {
		error.clear();
		std::filesystem::remove_all(groups[index], error);
		if (error)
			return filesystem_diagnostic(
				core::DiagnosticSeverity::RecoverableWarning,
				"previous_copy_retention_failed",
				"Old previous metadata copy group could not be removed.",
				groups[index], error);
	}

	return std::nullopt;
}

[[nodiscard]] std::optional<core::Diagnostic> validate_manifest_text(
	std::string_view text) {
	const auto parsed = parse_manifest_json(text);
	if (parsed.succeeded())
		return std::nullopt;

	auto details = std::string{};
	if (parsed.diagnostic)
		details = parsed.diagnostic->message;
	return make_diagnostic(
		core::DiagnosticSeverity::WriteBlockingError,
		"manifest_validation_failed",
		"Serialized manifest failed validation before replacement.", details);
}

[[nodiscard]] std::optional<core::Diagnostic> validate_settings_text(
	std::string_view text) {
	const auto parsed = parse_settings_json(text);
	if (parsed.succeeded())
		return std::nullopt;

	auto details = std::string{};
	if (parsed.diagnostic)
		details = parsed.diagnostic->message;
	return make_diagnostic(
		core::DiagnosticSeverity::WriteBlockingError,
		"settings_validation_failed",
		"Serialized settings failed validation before replacement.", details);
}

[[nodiscard]] std::optional<core::Diagnostic> validate_empty_jsonl_text(
	std::string_view text) {
	if (text.empty())
		return std::nullopt;

	return make_diagnostic(
		core::DiagnosticSeverity::WriteBlockingError,
		"empty_jsonl_validation_failed",
		"Empty catalog initializer attempted to write non-empty entity JSONL.");
}

void append_cleanup_warning(CatalogStorageResult& result, std::string code,
							std::string message,
							const std::filesystem::path& path,
							const std::error_code& error) {
	result.add_diagnostic(filesystem_diagnostic(
		core::DiagnosticSeverity::RecoverableWarning, std::move(code),
		std::move(message), path, error));
}

void cleanup_directory_contents(CatalogStorageResult& result,
								const std::filesystem::path& directory) {
	auto error = std::error_code{};
	std::filesystem::create_directories(directory, error);
	if (error) {
		append_cleanup_warning(
			result, "temp_cleanup_failed",
			"Temporary directory could not be created for cleanup.", directory,
			error);
		return;
	}

	auto iterator = std::filesystem::directory_iterator{directory, error};
	if (error) {
		append_cleanup_warning(
			result, "temp_cleanup_failed",
			"Temporary directory could not be listed for cleanup.", directory,
			error);
		return;
	}

	for (const auto& entry : iterator) {
		error.clear();
		std::filesystem::remove_all(entry.path(), error);
		if (error)
			append_cleanup_warning(result, "temp_cleanup_failed",
								   "Temporary leftover could not be removed.",
								   entry.path(), error);
	}
}

void cleanup_metadata_temp_files(CatalogStorageResult& result,
								 const std::filesystem::path& directory) {
	auto error = std::error_code{};
	if (!std::filesystem::exists(directory, error) || error)
		return;

	auto iterator = std::filesystem::directory_iterator{directory, error};
	if (error) {
		append_cleanup_warning(
			result, "temp_cleanup_failed",
			"Metadata directory could not be listed for temp cleanup.",
			directory, error);
		return;
	}

	for (const auto& entry : iterator) {
		if (!is_metadata_temp_name(entry.path().filename()))
			continue;

		error.clear();
		std::filesystem::remove_all(entry.path(), error);
		if (error)
			append_cleanup_warning(
				result, "temp_cleanup_failed",
				"Metadata temp leftover could not be removed.", entry.path(),
				error);
	}
}

[[nodiscard]] bool active_catalog_contains_canonical_metadata(
	const std::filesystem::path& active_catalog_root,
	core::Diagnostic& diagnostic) {
	for (const auto relative_text : canonical_metadata_paths()) {
		const auto path	  = active_catalog_root
							/ std::filesystem::path{std::string{relative_text}};
		auto error		  = std::error_code{};
		const auto exists = std::filesystem::exists(path, error);
		if (error) {
			diagnostic = filesystem_diagnostic(
				core::DiagnosticSeverity::WriteBlockingError,
				"metadata_target_status_failed",
				"Existing catalog metadata status could not be checked.", path,
				error);
			return true;
		}

		if (exists)
			return true;
	}

	return false;
}

[[nodiscard]] CatalogStorageResult write_initial_file(
	const std::filesystem::path& active_catalog_root,
	const std::filesystem::path& relative_target_path, std::string_view text,
	std::string_view group_name, const MetadataTextValidator& validator) {
	auto prepared =
		prepare_validated_temp_file(active_catalog_root, relative_target_path,
									text, group_name, validator, false);
	if (prepared.failed())
		return prepared;

	const auto target_path = active_catalog_root / relative_target_path;
	if (auto diagnostic = replace_temp_file(*prepared.temp_path, target_path)) {
		return make_failure(core::OperationResultCategory::ReplacementFailure,
							std::move(*diagnostic), prepared.temp_path);
	}

	prepared.changed_canonical_file = true;
	return prepared;
}

void cleanup_partial_active_catalog(
	const std::filesystem::path& active_catalog_root,
	CatalogStorageResult& result) {
	auto error = std::error_code{};
	std::filesystem::remove_all(active_catalog_root, error);
	if (error)
		result.add_diagnostic(filesystem_diagnostic(
			core::DiagnosticSeverity::RecoverableWarning,
			"partial_init_cleanup_failed",
			"Partial empty catalog initialization could not be cleaned up.",
			active_catalog_root, error));
}
}	 // namespace

bool CatalogStorageResult::succeeded() const noexcept {
	return category == core::OperationResultCategory::Success;
}

bool CatalogStorageResult::failed() const noexcept {
	return !succeeded();
}

bool CatalogStorageResult::has_warnings() const noexcept {
	return std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
		return diagnostic.severity
			   == core::DiagnosticSeverity::RecoverableWarning;
	});
}

CatalogStorageResult::operator bool() const noexcept {
	return succeeded();
}

void CatalogStorageResult::add_diagnostic(core::Diagnostic diagnostic) {
	diagnostics.push_back(std::move(diagnostic));
}

CatalogContainerLayout make_catalog_container_layout(
	std::filesystem::path app_private_root) {
	auto layout				= CatalogContainerLayout{};
	layout.app_private_root = std::move(app_private_root);
	layout.active_catalog_root =
		layout.app_private_root
		/ std::filesystem::path{std::string{active_catalog_directory_name}};
	layout.catalog_rollbacks_root =
		layout.app_private_root
		/ std::filesystem::path{std::string{catalog_rollbacks_directory_name}};
	layout.operation_tmp_root =
		layout.app_private_root
		/ std::filesystem::path{std::string{operation_tmp_directory_name}};
	return layout;
}

std::string previous_copy_group_name(
	core::EpochMilliseconds timestamp,
	const core::OperationIdentifier& operation_id) {
	const auto count = timestamp.count();
	auto output		 = std::ostringstream{};
	output << (count < 0 ? 'n' : 'p') << std::setw(20) << std::setfill('0')
		   << absolute_milliseconds(count) << '-' << operation_id.value();
	return output.str();
}

std::filesystem::path previous_metadata_copy_directory(
	const std::filesystem::path& active_catalog_root,
	std::string_view group_name) {
	return active_catalog_root
		   / std::filesystem::path{std::string{
			   previous_data_copies_directory_path}}
		   / std::filesystem::path{std::string{group_name}};
}

std::filesystem::path metadata_temp_file_path(
	const std::filesystem::path& active_catalog_root,
	const std::filesystem::path& relative_target_path,
	std::string_view group_name) {
	const auto target_path = active_catalog_root / relative_target_path;
	auto temp_name = std::string{"."} + target_path.filename().string() + "."
					 + std::string{group_name} + ".tmp";
	return target_path.parent_path() / std::move(temp_name);
}

CatalogStorageResult initialize_empty_catalog(
	const EmptyCatalogInitializationRequest& request) {
	const auto layout = make_catalog_container_layout(request.app_private_root);
	const auto group_name =
		previous_copy_group_name(request.created_at, request.operation_id);

	auto status_diagnostic = core::Diagnostic{};
	if (active_catalog_contains_canonical_metadata(layout.active_catalog_root,
												   status_diagnostic)) {
		if (!status_diagnostic.code.empty()) {
			return make_failure(
				core::OperationResultCategory::DestinationUnavailable,
				std::move(status_diagnostic));
		}

		return make_failure(
			core::OperationResultCategory::ValidationFailure,
			make_diagnostic(core::DiagnosticSeverity::ActionValidationError,
							"catalog_already_initialized",
							"Empty catalog initialization would overwrite "
							"existing canonical metadata.",
							path_text(layout.active_catalog_root)));
	}

	if (auto diagnostic = create_directories_or_diagnostic(
			layout.app_private_root, "app_private_root_unavailable",
			"App-private catalog container directory could not be created.")) {
		return make_failure(
			core::OperationResultCategory::DestinationUnavailable,
			std::move(*diagnostic));
	}

	if (auto diagnostic = create_directories_or_diagnostic(
			layout.catalog_rollbacks_root, "rollback_root_unavailable",
			"Full-catalog rollback root directory could not be created.")) {
		return make_failure(
			core::OperationResultCategory::DestinationUnavailable,
			std::move(*diagnostic));
	}

	if (auto diagnostic = create_directories_or_diagnostic(
			layout.operation_tmp_root, "operation_tmp_unavailable",
			"Operation temporary directory could not be created.")) {
		return make_failure(
			core::OperationResultCategory::DestinationUnavailable,
			std::move(*diagnostic));
	}

	for (const auto relative_text : empty_catalog_directories()) {
		const auto directory =
			layout.active_catalog_root
			/ std::filesystem::path{std::string{relative_text}};
		if (auto diagnostic = create_directories_or_diagnostic(
				directory, "catalog_directory_unavailable",
				"Empty catalog directory could not be created.")) {
			auto result = make_failure(
				core::OperationResultCategory::DestinationUnavailable,
				std::move(*diagnostic));
			cleanup_partial_active_catalog(layout.active_catalog_root, result);
			return result;
		}
	}

	const auto fixture =
		make_empty_catalog_fixture(request.catalog_id, request.created_at);
	const auto manifest_json = serialize_manifest_json(fixture.manifest);
	if (!manifest_json.succeeded()) {
		auto result = make_failure(
			core::OperationResultCategory::InternalError,
			make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
							"manifest_serialization_failed",
							"Empty catalog manifest could not be serialized."));
		cleanup_partial_active_catalog(layout.active_catalog_root, result);
		return result;
	}

	const auto settings_json = serialize_settings_json(fixture.settings);
	if (!settings_json.succeeded()) {
		auto result = make_failure(
			core::OperationResultCategory::InternalError,
			make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
							"settings_serialization_failed",
							"Empty catalog settings could not be serialized."));
		cleanup_partial_active_catalog(layout.active_catalog_root, result);
		return result;
	}

	const auto files = std::array{
		std::pair{std::filesystem::path{std::string{manifest_file_path}},
				  std::pair{std::string_view{manifest_json.json},
							MetadataTextValidator{validate_manifest_text}}},
		std::pair{std::filesystem::path{std::string{settings_file_path}},
				  std::pair{std::string_view{settings_json.json},
							MetadataTextValidator{validate_settings_text}}},
		std::pair{std::filesystem::path{std::string{items_data_file_path}},
				  std::pair{std::string_view{fixture.items_jsonl},
							MetadataTextValidator{validate_empty_jsonl_text}}},
		std::pair{std::filesystem::path{std::string{storages_data_file_path}},
				  std::pair{std::string_view{fixture.storages_jsonl},
							MetadataTextValidator{validate_empty_jsonl_text}}},
		std::pair{std::filesystem::path{std::string{photos_data_file_path}},
				  std::pair{std::string_view{fixture.photos_jsonl},
							MetadataTextValidator{validate_empty_jsonl_text}}}};

	auto result = CatalogStorageResult{};
	for (const auto& [relative_path, text_and_validator] : files) {
		const auto& [text, validator] = text_and_validator;
		auto written =
			write_initial_file(layout.active_catalog_root, relative_path, text,
							   group_name, validator);
		if (written.failed()) {
			cleanup_partial_active_catalog(layout.active_catalog_root, written);
			return written;
		}

		result.changed_canonical_file = true;
	}

	return result;
}

CatalogStorageResult commit_metadata_file(
	const CatalogMetadataCommitRequest& request) {
	if (auto validation = validate_commit_target(request.relative_target_path,
												 request.operation_id);
		validation.failed()) {
		return validation;
	}

	const auto group_name =
		previous_copy_group_name(request.committed_at, request.operation_id);
	auto result = prepare_validated_temp_file(
		request.active_catalog_root, request.relative_target_path,
		request.serialized_content, group_name, request.validator, true);
	if (result.failed())
		return result;

	const auto target_path =
		request.active_catalog_root / request.relative_target_path;

	auto error				 = std::error_code{};
	const auto target_exists = std::filesystem::exists(target_path, error);
	if (error) {
		remove_temp_if_owned(*result.temp_path);
		return make_failure(
			core::OperationResultCategory::DestinationUnavailable,
			filesystem_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
								  "metadata_target_status_failed",
								  "Canonical metadata target status could not "
								  "be checked before previous copy.",
								  target_path, error),
			result.temp_path);
	}

	if (request.create_previous_copy && target_exists) {
		auto previous = create_previous_metadata_copy_group(
			request.active_catalog_root, group_name);
		result.previous_copy_directory = previous.directory;
		result.previous_copy_created   = previous.created;
		for (auto& diagnostic : previous.diagnostics)
			result.add_diagnostic(std::move(diagnostic));
	}

	if (auto diagnostic = replace_temp_file(*result.temp_path, target_path)) {
		result.category = core::OperationResultCategory::ReplacementFailure;
		result.add_diagnostic(std::move(*diagnostic));
		return result;
	}

	result.changed_canonical_file = true;

	if (request.create_previous_copy && request.previous_copy_retention > 0U) {
		if (auto diagnostic = cleanup_previous_copy_retention(
				request.active_catalog_root, request.previous_copy_retention)) {
			result.add_diagnostic(std::move(*diagnostic));
		}
	}

	return result;
}

CatalogStorageResult cleanup_startup_temporary_files(
	const std::filesystem::path& app_private_root) {
	const auto layout = make_catalog_container_layout(app_private_root);
	auto result		  = CatalogStorageResult{};

	cleanup_directory_contents(
		result, layout.active_catalog_root
					/ std::filesystem::path{
						std::string{active_catalog_tmp_directory_path}});
	cleanup_directory_contents(result, layout.operation_tmp_root);
	cleanup_metadata_temp_files(result, layout.active_catalog_root);
	cleanup_metadata_temp_files(
		result, layout.active_catalog_root
					/ std::filesystem::path{std::string{data_directory_path}});
	return result;
}
}	 // namespace shuba::persistence
