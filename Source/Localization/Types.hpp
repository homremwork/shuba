#pragma once

#include "Catalog/Search.hpp"
#include "Domain/Domain.hpp"
#include "Localization/Language.hpp"
#include "Persistence/JsonlCatalog.hpp"
#include "Platform/PlatformServices.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace shuba::ui {
enum class CatalogSessionStartupSource : std::uint8_t;
enum class PendingPhotoStatus : std::uint8_t;
enum class RecoveryAction : std::uint8_t;
}	 // namespace shuba::ui

namespace shuba::localization {
enum class CatalogWarning : std::uint8_t {
	NoPhoto,
	BrokenPhotos,
	BrokenStorage,
	BrokenParent,
	Archived,
	ArchivedStorage,
};

struct ItemHeaderFields final {
	std::string_view name;
	std::string_view photo_state;
	std::string_view category;
	std::string_view status;
	std::string_view storage_path;
	std::string_view warnings;
};

struct StorageHeaderFields final {
	std::string_view name;
	std::string_view type;
	std::string_view path;
	std::string_view location;
	std::string_view notes;
	std::string_view warnings;
};

struct ItemResultFields final {
	std::string_view title;
	std::string_view photo_state;
	std::string_view category;
	std::string_view status;
	std::string_view location;
	std::string_view details;
	std::string_view warnings;
};

struct StorageResultFields final {
	std::string_view title;
	std::string_view type;
	std::string_view lifecycle;
	std::string_view location;
	std::uint64_t direct_children{};
	std::uint64_t direct_items{};
	std::uint64_t nested_items{};
	std::string_view details;
	std::string_view warnings;
};

struct CatalogFilterSummaryFields final {
	std::vector<std::string> categories;
	std::vector<std::string> statuses;
	std::optional<std::string> storage;
	bool storage_unassigned{};
	bool include_nested_storage{};
	std::optional<std::string> photo_presence;
	bool listed_shortcut{};
	bool sold_shortcut{};
	bool include_archived{};
};

enum class CatalogFilterSummaryKind : std::uint8_t {
	Applied,
	Draft,
};

struct ProgressSummary final {
	std::string phase;
	std::string message;
	std::uint64_t current_units{};
	std::uint64_t total_units{};
	bool has_current_units{};
	bool has_total_units{};
	bool cancellable{};
};

struct ProgressMessageDefinition final {
	platform::ProgressMessageId id{platform::ProgressMessageId::Count};
	std::string_view phase_code;
	std::string_view english_phase;
	std::string_view english_message;
};

struct RecoveryCountsFields final {
	std::uint64_t accepted_items{};
	std::uint64_t accepted_storages{};
	std::uint64_t accepted_photos{};
	std::uint64_t skipped_items{};
	std::uint64_t skipped_storages{};
	std::uint64_t skipped_photos{};
	std::uint64_t broken_references{};
	std::uint64_t orphan_media{};
};

struct ImportValidationFields final {
	std::string load_status;
	std::uint64_t accepted_items{};
	std::uint64_t accepted_storages{};
	std::uint64_t accepted_photos{};
	std::uint64_t broken_references{};
	std::uint64_t orphan_media{};
};

struct ShellStatusFields final {
	persistence::CatalogLoadStatus load_status{
		persistence::CatalogLoadStatus::Normal};
	ui::CatalogSessionStartupSource source{};
	std::uint64_t item_count{};
	std::uint64_t storage_count{};
	bool demo_catalog_active{};
};

struct LocalizationIssue final {
	std::string code;
	std::string technical_details;

	friend bool operator==(const LocalizationIssue&,
						   const LocalizationIssue&) = default;
};

struct LocalizationInitialization final {
	Language requested_language{Language::English};
	Language active_language{Language::English};
	std::vector<LocalizationIssue> issues;

	[[nodiscard]] bool using_russian_catalog() const noexcept;
};
}	 // namespace shuba::localization
