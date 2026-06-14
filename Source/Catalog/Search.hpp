#pragma once

#include "Catalog/CatalogRepository.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace shuba::catalog {
enum class SearchResultType : std::uint8_t {
	Item,
	Storage,
};

[[nodiscard]] std::string_view to_string(SearchResultType type) noexcept;

enum class SearchPhotoPresenceFilter : std::uint8_t {
	Any,
	HasPhotos,
	NoPhotos,
	BrokenPhotos,
};

[[nodiscard]] std::string_view to_string(
	SearchPhotoPresenceFilter filter) noexcept;

enum class SearchMatchTier : std::uint8_t {
	DisplayNameExact,
	DisplayNamePrefix,
	DisplayNameSubstring,
	Classifier,
	Tag,
	Location,
	Detail,
	OtherText,
	Browse,
};

[[nodiscard]] std::string_view to_string(SearchMatchTier tier) noexcept;

struct SearchNormalizationDecision final {
	std::string dependency_name;
	bool standard_byte_lowercase_sufficient{};
	bool juce_lowercase_sufficient{};
	std::string notes;

	friend bool operator==(const SearchNormalizationDecision&,
						   const SearchNormalizationDecision&) = default;
};

struct SearchDocumentText final {
	std::string display_name;
	std::string classifier;
	std::string tags;
	std::string location;
	std::string detail;
	std::string full_text;

	friend bool operator==(const SearchDocumentText&,
						   const SearchDocumentText&) = default;
};

struct ItemSearchDocument final {
	ItemSearchProjection projection;
	std::string category;
	domain::ItemStatus status{domain::ItemStatus::Draft};
	core::EpochMilliseconds updated_at{};
	SearchDocumentText normalized_text;

	friend bool operator==(const ItemSearchDocument&,
						   const ItemSearchDocument&) = default;
};

struct StorageSearchDocument final {
	StorageSearchProjection projection;
	std::optional<core::StableIdentifier> parent_storage_id;
	domain::ReferenceState parent_reference_state{
		domain::ReferenceState::Absent};
	std::string storage_type;
	std::string location;
	core::EpochMilliseconds updated_at{};
	SearchDocumentText normalized_text;

	friend bool operator==(const StorageSearchDocument&,
						   const StorageSearchDocument&) = default;
};

struct SearchIndex final {
	std::vector<ItemSearchDocument> items;
	std::vector<StorageSearchDocument> storages;
	std::map<std::string, std::vector<core::StableIdentifier>>
		nested_storage_descendants_by_id;
	std::vector<std::string> tag_key_hints;
	SearchNormalizationDecision normalization_decision;

	friend bool operator==(const SearchIndex&, const SearchIndex&) = default;
};

struct SearchWarningMarkers final {
	bool no_photo_records{};
	bool broken_photos{};
	bool broken_storage_reference{};
	bool broken_parent_reference{};
	bool archived_record{};
	bool archived_storage{};

	friend bool operator==(const SearchWarningMarkers&,
						   const SearchWarningMarkers&) = default;
};

struct SearchResult final {
	SearchResultType type{SearchResultType::Item};
	core::StableIdentifier record_id;
	std::string display_title;
	std::string subtitle;
	std::string location_text;
	std::string category;
	std::string storage_type;
	std::optional<domain::ItemStatus> item_status;
	std::optional<domain::StorageLifecycleStatus> storage_lifecycle_status;
	std::optional<core::StableIdentifier> representative_photo_id;
	std::optional<core::StableIdentifier> representative_usable_photo_id;
	SearchWarningMarkers warnings;
	PhotoPresenceState photo_presence{PhotoPresenceState::NoPhotoRecords};
	std::uint64_t direct_child_count{};
	std::uint64_t direct_item_count{};
	std::uint64_t nested_item_count{};
	std::string match_summary;
	SearchMatchTier match_tier{SearchMatchTier::Browse};

	friend bool operator==(const SearchResult&, const SearchResult&) = default;
};

struct CatalogSearchResultSet final {
	std::vector<SearchResult> item_results;
	std::vector<SearchResult> storage_results;
	bool query_is_empty{};
	std::uint64_t total_count{};

	friend bool operator==(const CatalogSearchResultSet&,
						   const CatalogSearchResultSet&) = default;
};

struct CatalogSearchFilters final {
	std::vector<std::string> categories;
	std::vector<domain::ItemStatus> statuses;
	bool include_archived{};
	std::optional<core::StableIdentifier> storage_id;
	bool storage_unassigned_only{};
	bool include_nested_storage{true};
	SearchPhotoPresenceFilter photo_presence{SearchPhotoPresenceFilter::Any};
	bool listed_only{};
	bool sold_only{};

	friend bool operator==(const CatalogSearchFilters&,
						   const CatalogSearchFilters&) = default;
};

struct CatalogSearchOptions final {
	bool include_storage_results_for_empty_query{};

	friend bool operator==(const CatalogSearchOptions&,
						   const CatalogSearchOptions&) = default;
};

struct StorageSearchFilters final {
	std::vector<std::string> storage_types;
	std::optional<core::StableIdentifier> parent_storage_id;
	bool root_only{};
	bool include_archived{};
	SearchPhotoPresenceFilter photo_presence{SearchPhotoPresenceFilter::Any};

	friend bool operator==(const StorageSearchFilters&,
						   const StorageSearchFilters&) = default;
};

[[nodiscard]] SearchNormalizationDecision search_normalization_decision();
[[nodiscard]] std::string normalize_search_text(std::string_view text);
[[nodiscard]] std::vector<std::string> tokenize_normalized_search_text(
	std::string_view normalized_text);

[[nodiscard]] SearchIndex build_search_index(
	const CatalogRepositoryState& state);
[[nodiscard]] CatalogSearchResultSet search_catalog(
	const SearchIndex& index, std::string_view query,
	const CatalogSearchFilters& filters = CatalogSearchFilters{},
	CatalogSearchOptions options		= {});
[[nodiscard]] CatalogSearchResultSet search_storages(
	const SearchIndex& index, std::string_view query,
	const StorageSearchFilters& filters = StorageSearchFilters{});
}	 // namespace shuba::catalog
