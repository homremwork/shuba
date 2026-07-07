#pragma once

#include "Core/Clock.hpp"
#include "Core/Identifier.hpp"
#include "Core/Result.hpp"
#include "Persistence/MetadataSchema.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace shuba::persistence {
inline constexpr auto catalog_rollbacks_directory_name =
	std::string_view{"catalog-rollbacks"};
inline constexpr auto operation_tmp_directory_name =
	std::string_view{"operation-tmp"};
inline constexpr auto data_directory_path	= std::string_view{"data"};
inline constexpr auto backup_directory_path = std::string_view{"backup"};
inline constexpr auto active_catalog_tmp_directory_path =
	std::string_view{"tmp"};
inline constexpr auto previous_data_copies_directory_path =
	std::string_view{"backup/previous-data-copies"};
inline constexpr auto default_previous_metadata_copy_retention =
	std::size_t{10};

struct CatalogContainerLayout final {
	std::filesystem::path app_private_root;
	std::filesystem::path active_catalog_root;
	std::filesystem::path catalog_rollbacks_root;
	std::filesystem::path operation_tmp_root;

	friend bool operator==(const CatalogContainerLayout&,
						   const CatalogContainerLayout&) = default;
};

struct CatalogStorageResult final {
	core::OperationResultCategory category{
		core::OperationResultCategory::Success};
	std::vector<core::Diagnostic> diagnostics;
	bool changed_canonical_file{};
	bool previous_copy_created{};
	bool cleanup_attempted{};
	bool active_catalog_tmp_cleanup_attempted{};
	bool operation_tmp_cleanup_attempted{};
	bool metadata_temp_cleanup_attempted{};
	std::uint64_t cleanup_removed_entry_count{};
	std::optional<std::filesystem::path> previous_copy_directory;
	std::optional<std::filesystem::path> temp_path;

	[[nodiscard]] bool succeeded() const noexcept;
	[[nodiscard]] bool failed() const noexcept;
	[[nodiscard]] bool has_warnings() const noexcept;
	[[nodiscard]] explicit operator bool() const noexcept;

	void add_diagnostic(core::Diagnostic diagnostic);
};

using MetadataTextValidator =
	std::function<std::optional<core::Diagnostic>(std::string_view)>;

struct EmptyCatalogInitializationRequest final {
	std::filesystem::path app_private_root;
	core::StableIdentifier catalog_id;
	core::EpochMilliseconds created_at{};
	core::OperationIdentifier operation_id;
};

struct CatalogMetadataCommitRequest final {
	std::filesystem::path active_catalog_root;
	std::filesystem::path relative_target_path;
	std::string serialized_content;
	core::EpochMilliseconds committed_at{};
	core::OperationIdentifier operation_id;
	MetadataTextValidator validator;
	bool create_previous_copy{true};
	std::size_t previous_copy_retention{
		default_previous_metadata_copy_retention};
};

struct CatalogStartupCleanupRequest final {
	std::filesystem::path app_private_root;
	std::vector<std::filesystem::path> protected_paths;
};

[[nodiscard]] CatalogContainerLayout make_catalog_container_layout(
	std::filesystem::path app_private_root);

[[nodiscard]] std::string previous_copy_group_name(
	core::EpochMilliseconds timestamp,
	const core::OperationIdentifier& operation_id);
[[nodiscard]] std::filesystem::path previous_metadata_copy_directory(
	const std::filesystem::path& active_catalog_root,
	std::string_view group_name);
[[nodiscard]] std::filesystem::path metadata_temp_file_path(
	const std::filesystem::path& active_catalog_root,
	const std::filesystem::path& relative_target_path,
	std::string_view group_name);

[[nodiscard]] CatalogStorageResult initialize_empty_catalog(
	const EmptyCatalogInitializationRequest& request);
[[nodiscard]] CatalogStorageResult commit_metadata_file(
	const CatalogMetadataCommitRequest& request);
[[nodiscard]] CatalogStorageResult cleanup_startup_temporary_files(
	const std::filesystem::path& app_private_root);
[[nodiscard]] CatalogStorageResult cleanup_startup_temporary_files(
	const CatalogStartupCleanupRequest& request);
}	 // namespace shuba::persistence
