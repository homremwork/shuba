#include "Catalog/Search.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <utility>

namespace shuba::catalog {
namespace {
struct OriginalSearchText final {
	std::string display_name;
	std::string classifier;
	std::string tags;
	std::string location;
	std::string detail;
};

struct MatchEvaluation final {
	bool matched{};
	SearchMatchTier tier{SearchMatchTier::OtherText};
	std::string summary;
};

struct ScoredSearchResult final {
	SearchResult result;
	bool archived{};
	bool has_representative_photo{};
	core::EpochMilliseconds updated_at{};
	std::string normalized_title;
	std::string stable_id;
};

[[nodiscard]] bool is_ascii_digit(char32_t code_point) noexcept {
	return code_point >= U'0' && code_point <= U'9';
}

[[nodiscard]] bool is_ascii_lower_alpha(char32_t code_point) noexcept {
	return code_point >= U'a' && code_point <= U'z';
}

[[nodiscard]] bool is_lower_cyrillic_letter(char32_t code_point) noexcept {
	return code_point >= U'а' && code_point <= U'я';
}

[[nodiscard]] bool is_search_word_code_point(char32_t code_point) noexcept {
	return is_ascii_lower_alpha(code_point) || is_ascii_digit(code_point)
		   || is_lower_cyrillic_letter(code_point);
}

[[nodiscard]] bool is_combining_mark(char32_t code_point) noexcept {
	return code_point >= U'\u0300' && code_point <= U'\u036F';
}

[[nodiscard]] char32_t lower_and_fold_code_point(char32_t code_point) noexcept {
	if (code_point >= U'A' && code_point <= U'Z')
		return code_point + (U'a' - U'A');
	if (code_point >= U'А' && code_point <= U'Я')
		return code_point + (U'а' - U'А');
	if (code_point == U'Ё' || code_point == U'ё')
		return U'е';
	return code_point;
}

[[nodiscard]] char32_t decode_next_utf8_code_point(
	std::string_view text, std::size_t& index) noexcept {
	const unsigned char first = static_cast<unsigned char>(text[index]);
	if (first < 0x80U) {
		++index;
		return first;
	}

	std::size_t length	= 0;
	char32_t code_point = 0;
	if ((first & 0xE0U) == 0xC0U) {
		length	   = 2;
		code_point = first & 0x1FU;
	} else if ((first & 0xF0U) == 0xE0U) {
		length	   = 3;
		code_point = first & 0x0FU;
	} else if ((first & 0xF8U) == 0xF0U) {
		length	   = 4;
		code_point = first & 0x07U;
	} else {
		++index;
		return U' ';
	}

	if (index + length > text.size()) {
		++index;
		return U' ';
	}

	for (std::size_t offset = 1; offset < length; ++offset) {
		const unsigned char next =
			static_cast<unsigned char>(text[index + offset]);
		if ((next & 0xC0U) != 0x80U) {
			++index;
			return U' ';
		}
		code_point = (code_point << 6U) | (next & 0x3FU);
	}

	const bool overlong	 = (length == 2 && code_point < 0x80U)
						   || (length == 3 && code_point < 0x800U)
						   || (length == 4 && code_point < 0x10000U);
	const bool surrogate = code_point >= 0xD800U && code_point <= 0xDFFFU;
	if (overlong || surrogate || code_point > 0x10FFFFU) {
		++index;
		return U' ';
	}

	index += length;
	return code_point;
}

void append_utf8(std::string& output, char32_t code_point) {
	if (code_point <= 0x7FU) {
		output.push_back(static_cast<char>(code_point));
	} else if (code_point <= 0x7FFU) {
		output.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
		output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
	} else if (code_point <= 0xFFFFU) {
		output.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
		output.push_back(
			static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
		output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
	} else {
		output.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
		output.push_back(
			static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU)));
		output.push_back(
			static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
		output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
	}
}

void append_text_piece(std::string& target, std::string_view piece) {
	if (piece.empty())
		return;
	if (!target.empty())
		target.push_back(' ');
	target.append(piece);
}

void append_money_piece(std::string& target,
						const domain::MoneyAmount& amount) {
	append_text_piece(target, domain::canonical_decimal_text(amount));
	append_text_piece(target, amount.currency);
}

void append_business_date_piece(
	std::string& target, const std::optional<domain::BusinessDate>& date) {
	if (date)
		append_text_piece(target, date->value);
}

void append_tags_text(std::string& target,
					  const std::vector<domain::TagRow>& tags) {
	for (const domain::TagRow& tag : tags) {
		if (!tag.key.empty() && !tag.value.empty())
			append_text_piece(target, tag.key + ":" + tag.value);
		append_text_piece(target, tag.key);
		append_text_piece(target, tag.value);
	}
}

void append_listing_text(std::string& target,
						 const domain::ListingData& listing) {
	append_text_piece(target, listing.marketplace);
	append_text_piece(target, listing.url);
	append_business_date_piece(target, listing.listed_on);
	if (listing.price)
		append_money_piece(target, *listing.price);
	append_text_piece(target, listing.note);
}

void append_acquisition_text(std::string& target,
							 const domain::AcquisitionData& acquisition) {
	append_text_piece(target, acquisition.source);
	append_business_date_piece(target, acquisition.acquired_on);
	if (acquisition.cost)
		append_money_piece(target, *acquisition.cost);
}

void append_finance_text(std::string& target,
						 const domain::AcquisitionData& acquisition,
						 const domain::FinanceData& finance) {
	if (finance.original_price)
		append_money_piece(target, *finance.original_price);
	if (finance.real_sale_price)
		append_money_piece(target, *finance.real_sale_price);
	if (finance.expenses_total)
		append_money_piece(target, *finance.expenses_total);
	const std::optional<domain::MoneyAmount> profit =
		domain::calculate_profit(acquisition, finance);
	if (profit)
		append_money_piece(target, *profit);
}

[[nodiscard]] SearchDocumentText normalize_document_text(
	const OriginalSearchText& original) {
	std::string full_text;
	append_text_piece(full_text, original.display_name);
	append_text_piece(full_text, original.classifier);
	append_text_piece(full_text, original.tags);
	append_text_piece(full_text, original.location);
	append_text_piece(full_text, original.detail);

	return SearchDocumentText{
		.display_name = normalize_search_text(original.display_name),
		.classifier	  = normalize_search_text(original.classifier),
		.tags		  = normalize_search_text(original.tags),
		.location	  = normalize_search_text(original.location),
		.detail		  = normalize_search_text(original.detail),
		.full_text	  = normalize_search_text(full_text)};
}

[[nodiscard]] const ItemSearchProjection* find_item_search_projection(
	const SearchRebuildProjection& projection,
	const core::StableIdentifier& id) {
	const std::vector<ItemSearchProjection>::const_iterator found =
		std::ranges::find_if(
			projection.items,
			[id](const ItemSearchProjection& item) { return item.id == id; });
	if (found == projection.items.end())
		return nullptr;
	return &*found;
}

[[nodiscard]] const StorageSearchProjection* find_storage_search_projection(
	const SearchRebuildProjection& projection,
	const core::StableIdentifier& id) {
	const std::vector<StorageSearchProjection>::const_iterator found =
		std::ranges::find_if(projection.storages,
							 [id](const StorageSearchProjection& storage) {
		return storage.id == id;
	});
	if (found == projection.storages.end())
		return nullptr;
	return &*found;
}

[[nodiscard]] std::string item_location_text(
	const CatalogRepositoryState& state,
	const ItemSearchProjection& projection) {
	std::string text;
	append_text_piece(text, projection.storage_display_name);
	append_text_piece(text, projection.storage_path_label);
	if (!projection.storage_id)
		return text;

	const persistence::StorageEnvelope* storage =
		find_storage_envelope(state, *projection.storage_id);
	if (storage == nullptr)
		return text;

	append_text_piece(text, storage->record.location);
	return text;
}

void append_item_detail_text(std::string& detail,
							 const CatalogRepositoryState& state,
							 const persistence::ItemEnvelope& item,
							 const ItemSearchProjection& projection) {
	append_text_piece(detail, item.record.notes);
	append_listing_text(detail, item.record.listing);
	append_acquisition_text(detail, item.record.acquisition);
	append_finance_text(detail, item.record.acquisition, item.record.finance);

	if (!projection.storage_id)
		return;

	const persistence::StorageEnvelope* storage =
		find_storage_envelope(state, *projection.storage_id);
	if (storage == nullptr)
		return;

	append_tags_text(detail, storage->record.tags);
	append_text_piece(detail, storage->record.notes);
}

[[nodiscard]] ItemSearchDocument make_item_document(
	const CatalogRepositoryState& state, const persistence::ItemEnvelope& item,
	const ItemSearchProjection& projection) {
	std::string classifier;
	append_text_piece(classifier, item.record.category);
	append_text_piece(classifier, domain::to_string(item.record.status));

	std::string tags;
	append_tags_text(tags, item.record.tags);

	std::string detail;
	append_item_detail_text(detail, state, item, projection);

	const OriginalSearchText original{
		.display_name = item.record.display_name,
		.classifier	  = classifier,
		.tags		  = tags,
		.location	  = item_location_text(state, projection),
		.detail		  = detail};

	return ItemSearchDocument{
		.projection		 = projection,
		.category		 = item.record.category,
		.status			 = item.record.status,
		.updated_at		 = item.record.timestamps.updated_at,
		.normalized_text = normalize_document_text(original)};
}

[[nodiscard]] StorageSearchDocument make_storage_document(
	const persistence::StorageEnvelope& storage,
	const StorageSearchProjection& projection,
	domain::ReferenceState parent_reference_state) {
	std::string classifier;
	append_text_piece(classifier, storage.record.storage_type);
	append_text_piece(classifier,
					  domain::to_string(storage.record.lifecycle_status));

	std::string tags;
	append_tags_text(tags, storage.record.tags);

	std::string detail;
	append_text_piece(detail, storage.record.notes);

	std::string location;
	append_text_piece(location, projection.parent_path_label);
	append_text_piece(location, storage.record.location);

	const OriginalSearchText original{
		.display_name = storage.record.display_name,
		.classifier	  = classifier,
		.tags		  = tags,
		.location	  = location,
		.detail		  = detail};

	return StorageSearchDocument{
		.projection				= projection,
		.parent_storage_id		= storage.record.parent_storage_id,
		.parent_reference_state = parent_reference_state,
		.storage_type			= storage.record.storage_type,
		.location				= storage.record.location,
		.updated_at				= storage.record.timestamps.updated_at,
		.normalized_text		= normalize_document_text(original)};
}

[[nodiscard]] bool field_contains_all_tokens(
	std::string_view field, const std::vector<std::string>& tokens) {
	if (tokens.empty())
		return false;
	for (const std::string& token : tokens)
		if (field.find(token) == std::string_view::npos)
			return false;
	return true;
}

[[nodiscard]] bool field_matches(std::string_view field,
								 std::string_view normalized_query,
								 const std::vector<std::string>& tokens) {
	if (field.empty())
		return false;
	if (field.find(normalized_query) != std::string_view::npos)
		return true;
	return field_contains_all_tokens(field, tokens);
}

[[nodiscard]] MatchEvaluation evaluate_document_match(
	const SearchDocumentText& text, std::string_view normalized_query,
	const std::vector<std::string>& tokens) {
	if (normalized_query.empty())
		return {.matched = true,
				.tier	 = SearchMatchTier::Browse,
				.summary = "browse"};

	if (text.display_name == normalized_query)
		return {.matched = true,
				.tier	 = SearchMatchTier::DisplayNameExact,
				.summary = "display name"};
	if (text.display_name.starts_with(normalized_query))
		return {.matched = true,
				.tier	 = SearchMatchTier::DisplayNamePrefix,
				.summary = "display name"};
	if (text.display_name.find(normalized_query) != std::string::npos)
		return {.matched = true,
				.tier	 = SearchMatchTier::DisplayNameSubstring,
				.summary = "display name"};
	if (field_matches(text.classifier, normalized_query, tokens))
		return {.matched = true,
				.tier	 = SearchMatchTier::Classifier,
				.summary = "category or status"};
	if (field_matches(text.tags, normalized_query, tokens))
		return {
			.matched = true, .tier = SearchMatchTier::Tag, .summary = "tags"};
	if (field_matches(text.location, normalized_query, tokens))
		return {.matched = true,
				.tier	 = SearchMatchTier::Location,
				.summary = "location"};
	if (field_matches(text.detail, normalized_query, tokens))
		return {.matched = true,
				.tier	 = SearchMatchTier::Detail,
				.summary = "details"};
	if (field_matches(text.full_text, normalized_query, tokens))
		return {.matched = true,
				.tier	 = SearchMatchTier::OtherText,
				.summary = "combined text"};
	return {};
}

[[nodiscard]] std::set<std::string> normalized_filter_values(
	const std::vector<std::string>& values) {
	std::set<std::string> normalized;
	for (const std::string& value : values) {
		const std::string normalized_value = normalize_search_text(value);
		if (!normalized_value.empty())
			normalized.insert(normalized_value);
	}
	return normalized;
}

[[nodiscard]] bool matches_normalized_value_filter(
	std::string_view value, const std::set<std::string>& normalized_values) {
	if (normalized_values.empty())
		return true;
	return normalized_values.contains(normalize_search_text(value));
}

[[nodiscard]] std::set<domain::ItemStatus> effective_item_status_filter(
	const CatalogSearchFilters& filters) {
	std::set<domain::ItemStatus> statuses;
	for (const domain::ItemStatus status : filters.statuses)
		statuses.insert(status);
	if (filters.listed_only)
		statuses.insert(domain::ItemStatus::Listed);
	if (filters.sold_only)
		statuses.insert(domain::ItemStatus::Sold);
	return statuses;
}

[[nodiscard]] bool matches_status_and_archive_filter(
	const ItemSearchDocument& document, const CatalogSearchFilters& filters,
	const std::set<domain::ItemStatus>& statuses) {
	if (!statuses.empty())
		return statuses.contains(document.status);
	if (!filters.include_archived
		&& document.status == domain::ItemStatus::Archived)
		return false;
	return true;
}

[[nodiscard]] bool contains_stable_identifier(
	const std::vector<core::StableIdentifier>& identifiers,
	const core::StableIdentifier& id) {
	return std::ranges::any_of(identifiers,
							   [id](const core::StableIdentifier& candidate) {
		return candidate == id;
	});
}

[[nodiscard]] bool matches_storage_filter(const SearchIndex& index,
										  const ItemSearchDocument& document,
										  const CatalogSearchFilters& filters) {
	if (filters.storage_unassigned_only)
		return !document.projection.storage_id.has_value();
	if (!filters.storage_id)
		return true;
	if (document.projection.broken_storage_reference
		|| !document.projection.storage_id)
		return false;
	if (*document.projection.storage_id == *filters.storage_id)
		return true;
	if (!filters.include_nested_storage)
		return false;

	const std::map<std::string,
				   std::vector<core::StableIdentifier>>::const_iterator found =
		index.nested_storage_descendants_by_id.find(
			filters.storage_id->value());
	if (found == index.nested_storage_descendants_by_id.end())
		return false;
	return contains_stable_identifier(found->second,
									  *document.projection.storage_id);
}

[[nodiscard]] bool matches_photo_presence_filter(
	PhotoPresenceState state, SearchPhotoPresenceFilter filter) noexcept {
	switch (filter) {
		case SearchPhotoPresenceFilter::Any:
			return true;
		case SearchPhotoPresenceFilter::HasPhotos:
			return state == PhotoPresenceState::HasUsablePhotos
				   || state == PhotoPresenceState::MixedUsableAndBrokenPhotos;
		case SearchPhotoPresenceFilter::NoPhotos:
			return state == PhotoPresenceState::NoPhotoRecords;
		case SearchPhotoPresenceFilter::BrokenPhotos:
			return state == PhotoPresenceState::OnlyBrokenPhotos
				   || state == PhotoPresenceState::MixedUsableAndBrokenPhotos;
	}
	return false;
}

[[nodiscard]] bool matches_item_filters(
	const SearchIndex& index, const ItemSearchDocument& document,
	const CatalogSearchFilters& filters,
	const std::set<std::string>& normalized_categories,
	const std::set<domain::ItemStatus>& statuses) {
	return matches_normalized_value_filter(document.category,
										   normalized_categories)
		   && matches_status_and_archive_filter(document, filters, statuses)
		   && matches_storage_filter(index, document, filters)
		   && matches_photo_presence_filter(document.projection.photo_presence,
											filters.photo_presence);
}

[[nodiscard]] bool has_item_specific_filter(
	const CatalogSearchFilters& filters,
	const std::set<domain::ItemStatus>& statuses) noexcept {
	return !filters.categories.empty() || !statuses.empty()
		   || filters.storage_id.has_value() || filters.storage_unassigned_only;
}

[[nodiscard]] bool matches_storage_global_filters(
	const StorageSearchDocument& document,
	const CatalogSearchFilters& filters) {
	if (!filters.include_archived && !document.projection.visible_by_default)
		return false;
	return matches_photo_presence_filter(document.projection.photo_presence,
										 filters.photo_presence);
}

[[nodiscard]] bool matches_storage_search_filters(
	const StorageSearchDocument& document, const StorageSearchFilters& filters,
	const std::set<std::string>& normalized_storage_types) {
	if (!filters.include_archived && !document.projection.visible_by_default)
		return false;
	if (!matches_normalized_value_filter(document.storage_type,
										 normalized_storage_types))
		return false;
	if (filters.root_only && document.parent_storage_id.has_value())
		return false;
	if (filters.parent_storage_id) {
		if (!document.parent_storage_id)
			return false;
		if (!(*document.parent_storage_id == *filters.parent_storage_id))
			return false;
	}
	return matches_photo_presence_filter(document.projection.photo_presence,
										 filters.photo_presence);
}

[[nodiscard]] std::string item_location_result_text(
	const ItemSearchProjection& projection) {
	if (projection.broken_storage_reference)
		return "Broken storage reference";
	if (!projection.storage_id)
		return "Unassigned";
	if (!projection.storage_path_label.empty())
		return projection.storage_path_label;
	return projection.storage_display_name;
}

[[nodiscard]] SearchWarningMarkers item_warning_markers(
	const ItemSearchDocument& document) noexcept {
	return SearchWarningMarkers{
		.no_photo_records = document.projection.photo_presence
							== PhotoPresenceState::NoPhotoRecords,
		.broken_photos	  = document.projection.has_broken_photos,
		.broken_storage_reference =
			document.projection.broken_storage_reference,
		.archived_record  = document.status == domain::ItemStatus::Archived,
		.archived_storage = document.projection.storage_archived};
}

[[nodiscard]] SearchWarningMarkers storage_warning_markers(
	const StorageSearchDocument& document) noexcept {
	return SearchWarningMarkers{
		.no_photo_records = document.projection.photo_presence
							== PhotoPresenceState::NoPhotoRecords,
		.broken_photos	  = document.projection.has_broken_photos,
		.broken_parent_reference =
			document.parent_reference_state == domain::ReferenceState::Broken,
		.archived_record = document.projection.lifecycle_status
						   == domain::StorageLifecycleStatus::Archived};
}

[[nodiscard]] SearchResult make_item_result(const ItemSearchDocument& document,
											const MatchEvaluation& match) {
	return SearchResult{
		.type		   = SearchResultType::Item,
		.record_id	   = document.projection.id,
		.display_title = document.projection.display_name,
		.subtitle	   = document.category + " · "
						 + std::string{domain::to_string(document.status)},
		.location_text = item_location_result_text(document.projection),
		.category	   = document.category,
		.item_status   = document.status,
		.representative_photo_id = document.projection.representative_photo_id,
		.representative_usable_photo_id =
			document.projection.representative_usable_photo_id,
		.warnings		= item_warning_markers(document),
		.photo_presence = document.projection.photo_presence,
		.match_summary	= match.summary,
		.match_tier		= match.tier};
}

[[nodiscard]] std::string storage_location_result_text(
	const StorageSearchDocument& document) {
	std::string text;
	append_text_piece(text, document.projection.parent_path_label);
	append_text_piece(text, document.location);
	return text;
}

[[nodiscard]] SearchResult make_storage_result(
	const StorageSearchDocument& document, const MatchEvaluation& match) {
	return SearchResult{
		.type					  = SearchResultType::Storage,
		.record_id				  = document.projection.id,
		.display_title			  = document.projection.display_name,
		.subtitle				  = document.storage_type + " · "
									+ std::string{domain::to_string(
										document.projection.lifecycle_status)},
		.location_text			  = storage_location_result_text(document),
		.storage_type			  = document.storage_type,
		.storage_lifecycle_status = document.projection.lifecycle_status,
		.representative_photo_id  = document.projection.representative_photo_id,
		.representative_usable_photo_id =
			document.projection.representative_usable_photo_id,
		.warnings			= storage_warning_markers(document),
		.photo_presence		= document.projection.photo_presence,
		.direct_child_count = document.projection.direct_child_count,
		.direct_item_count	= document.projection.direct_item_count,
		.nested_item_count	= document.projection.nested_item_count,
		.match_summary		= match.summary,
		.match_tier			= match.tier};
}

[[nodiscard]] bool has_result_photo(const SearchResult& result) noexcept {
	return result.representative_usable_photo_id.has_value()
		   || result.representative_photo_id.has_value();
}

[[nodiscard]] ScoredSearchResult score_item_result(
	const ItemSearchDocument& document, SearchResult result) {
	const bool result_has_representative_photo = has_result_photo(result);
	return ScoredSearchResult{
		.result	  = std::move(result),
		.archived = document.status == domain::ItemStatus::Archived,
		.has_representative_photo = result_has_representative_photo,
		.updated_at				  = document.updated_at,
		.normalized_title		  = document.normalized_text.display_name,
		.stable_id				  = document.projection.id.value()};
}

[[nodiscard]] ScoredSearchResult score_storage_result(
	const StorageSearchDocument& document, SearchResult result) {
	const bool result_has_representative_photo = has_result_photo(result);
	return ScoredSearchResult{
		.result					  = std::move(result),
		.archived				  = document.projection.lifecycle_status
									== domain::StorageLifecycleStatus::Archived,
		.has_representative_photo = result_has_representative_photo,
		.updated_at				  = document.updated_at,
		.normalized_title		  = document.normalized_text.display_name,
		.stable_id				  = document.projection.id.value()};
}

[[nodiscard]] bool compare_scored_results(const ScoredSearchResult& left,
										  const ScoredSearchResult& right,
										  bool archived_only,
										  bool prefer_photos) {
	if (left.result.match_tier != right.result.match_tier)
		return left.result.match_tier < right.result.match_tier;
	if (!archived_only && left.archived != right.archived)
		return !left.archived;
	if (prefer_photos
		&& left.has_representative_photo != right.has_representative_photo)
		return left.has_representative_photo;
	if (left.updated_at != right.updated_at)
		return left.updated_at > right.updated_at;
	if (left.normalized_title != right.normalized_title)
		return left.normalized_title < right.normalized_title;
	return left.stable_id < right.stable_id;
}

void sort_item_results(std::vector<ScoredSearchResult>& results,
					   bool archived_only) {
	std::ranges::sort(results,
					  [archived_only](const ScoredSearchResult& left,
									  const ScoredSearchResult& right) {
		return compare_scored_results(left, right, archived_only, true);
	});
}

void sort_storage_results(std::vector<ScoredSearchResult>& results,
						  bool archived_only) {
	std::ranges::sort(results,
					  [archived_only](const ScoredSearchResult& left,
									  const ScoredSearchResult& right) {
		return compare_scored_results(left, right, archived_only, false);
	});
}

[[nodiscard]] std::vector<SearchResult> strip_scores(
	std::vector<ScoredSearchResult> scored_results) {
	std::vector<SearchResult> results;
	results.reserve(scored_results.size());
	for (ScoredSearchResult& scored_result : scored_results)
		results.push_back(std::move(scored_result.result));
	return results;
}

[[nodiscard]] bool item_archived_only(
	const std::set<domain::ItemStatus>& statuses) noexcept {
	return statuses.size() == 1U
		   && statuses.contains(domain::ItemStatus::Archived);
}
}	 // namespace

std::string_view to_string(SearchResultType type) noexcept {
	switch (type) {
		case SearchResultType::Item:
			return "item";
		case SearchResultType::Storage:
			return "storage";
	}
	return "unknown search result type";
}

std::string_view to_string(SearchPhotoPresenceFilter filter) noexcept {
	switch (filter) {
		case SearchPhotoPresenceFilter::Any:
			return "any";
		case SearchPhotoPresenceFilter::HasPhotos:
			return "has photos";
		case SearchPhotoPresenceFilter::NoPhotos:
			return "no photos";
		case SearchPhotoPresenceFilter::BrokenPhotos:
			return "broken photos";
	}
	return "unknown photo presence filter";
}

std::string_view to_string(SearchMatchTier tier) noexcept {
	switch (tier) {
		case SearchMatchTier::DisplayNameExact:
			return "display name exact";
		case SearchMatchTier::DisplayNamePrefix:
			return "display name prefix";
		case SearchMatchTier::DisplayNameSubstring:
			return "display name substring";
		case SearchMatchTier::Classifier:
			return "classifier";
		case SearchMatchTier::Tag:
			return "tag";
		case SearchMatchTier::Location:
			return "location";
		case SearchMatchTier::Detail:
			return "detail";
		case SearchMatchTier::OtherText:
			return "other text";
		case SearchMatchTier::Browse:
			return "browse";
	}
	return "unknown search match tier";
}

SearchNormalizationDecision search_normalization_decision() {
	return SearchNormalizationDecision{
		.dependency_name					= "custom minimal UTF-8 normalizer",
		.standard_byte_lowercase_sufficient = false,
		.juce_lowercase_sufficient			= false,
		.notes =
			"B09 spike found byte-wise standard lowercasing and JUCE String "
			"lowercasing insufficient for representative Cyrillic uppercase. "
			"The first search index therefore uses a small dependency-free "
			"UTF-8 normalizer for Latin and Russian Cyrillic case folding, "
			"ё-to-е folding, combining-mark skipping, punctuation splitting, "
			"whitespace collapse, and digit preservation."};
}

std::string normalize_search_text(std::string_view text) {
	std::string output;
	bool pending_space = false;
	std::size_t index  = 0;
	while (index < text.size()) {
		char32_t code_point = decode_next_utf8_code_point(text, index);
		code_point			= lower_and_fold_code_point(code_point);
		if (is_combining_mark(code_point))
			continue;

		if (is_search_word_code_point(code_point)) {
			if (pending_space && !output.empty())
				output.push_back(' ');
			append_utf8(output, code_point);
			pending_space = false;
		} else if (!output.empty()) {
			pending_space = true;
		}
	}
	return output;
}

std::vector<std::string> tokenize_normalized_search_text(
	std::string_view normalized_text) {
	std::vector<std::string> tokens;
	std::size_t first = 0;
	while (first < normalized_text.size()) {
		while (first < normalized_text.size() && normalized_text[first] == ' ')
			++first;
		std::size_t last = first;
		while (last < normalized_text.size() && normalized_text[last] != ' ')
			++last;
		if (last > first)
			tokens.emplace_back(normalized_text.substr(first, last - first));
		first = last;
	}
	return tokens;
}

SearchIndex build_search_index(const CatalogRepositoryState& state) {
	SearchIndex index{
		.tag_key_hints			= state.search_projection.tag_key_hints,
		.normalization_decision = search_normalization_decision()};

	for (const std::pair<const std::string, StorageProjection>& entry :
		 state.storage_projections) {
		index.nested_storage_descendants_by_id.emplace(
			entry.first, entry.second.nested_descendant_storage_ids);
	}

	for (const persistence::ItemEnvelope& item : state.items) {
		const ItemSearchProjection* projection = find_item_search_projection(
			state.search_projection, item.record.id);
		if (projection == nullptr)
			continue;
		index.items.push_back(make_item_document(state, item, *projection));
	}

	for (const persistence::StorageEnvelope& storage : state.storages) {
		const StorageSearchProjection* projection =
			find_storage_search_projection(state.search_projection,
										   storage.record.id);
		if (projection == nullptr)
			continue;
		domain::ReferenceState parent_reference_state =
			domain::ReferenceState::Absent;
		const std::map<std::string, StorageProjection>::const_iterator
			storage_projection =
				state.storage_projections.find(storage.record.id.value());
		if (storage_projection != state.storage_projections.end())
			parent_reference_state =
				storage_projection->second.parent_reference_state;
		index.storages.push_back(make_storage_document(storage, *projection,
													   parent_reference_state));
	}

	return index;
}

CatalogSearchResultSet search_catalog(const SearchIndex& index,
									  std::string_view query,
									  const CatalogSearchFilters& filters,
									  CatalogSearchOptions options) {
	const std::string normalized_query = normalize_search_text(query);
	const std::vector<std::string> tokens =
		tokenize_normalized_search_text(normalized_query);
	const bool query_is_empty = normalized_query.empty();
	const std::set<std::string> normalized_categories =
		normalized_filter_values(filters.categories);
	const std::set<domain::ItemStatus> statuses =
		effective_item_status_filter(filters);

	std::vector<ScoredSearchResult> item_results;
	for (const ItemSearchDocument& document : index.items) {
		if (!matches_item_filters(index, document, filters,
								  normalized_categories, statuses))
			continue;
		const MatchEvaluation match = evaluate_document_match(
			document.normalized_text, normalized_query, tokens);
		if (!match.matched)
			continue;
		item_results.push_back(
			score_item_result(document, make_item_result(document, match)));
	}
	sort_item_results(item_results, item_archived_only(statuses));

	std::vector<ScoredSearchResult> storage_results;
	const bool suppress_storage_results =
		has_item_specific_filter(filters, statuses);
	if (!suppress_storage_results
		&& (!query_is_empty
			|| options.include_storage_results_for_empty_query)) {
		for (const StorageSearchDocument& document : index.storages) {
			if (!matches_storage_global_filters(document, filters))
				continue;
			const MatchEvaluation match = evaluate_document_match(
				document.normalized_text, normalized_query, tokens);
			if (!match.matched)
				continue;
			storage_results.push_back(score_storage_result(
				document, make_storage_result(document, match)));
		}
	}
	sort_storage_results(storage_results, false);

	CatalogSearchResultSet result{
		.item_results	 = strip_scores(item_results),
		.storage_results = strip_scores(storage_results),
		.query_is_empty	 = query_is_empty};
	result.total_count =
		result.item_results.size() + result.storage_results.size();
	return result;
}

CatalogSearchResultSet search_storages(const SearchIndex& index,
									   std::string_view query,
									   const StorageSearchFilters& filters) {
	const std::string normalized_query = normalize_search_text(query);
	const std::vector<std::string> tokens =
		tokenize_normalized_search_text(normalized_query);
	const std::set<std::string> normalized_storage_types =
		normalized_filter_values(filters.storage_types);

	std::vector<ScoredSearchResult> storage_results;
	for (const StorageSearchDocument& document : index.storages) {
		if (!matches_storage_search_filters(document, filters,
											normalized_storage_types))
			continue;
		const MatchEvaluation match = evaluate_document_match(
			document.normalized_text, normalized_query, tokens);
		if (!match.matched)
			continue;
		storage_results.push_back(score_storage_result(
			document, make_storage_result(document, match)));
	}
	sort_storage_results(storage_results, false);

	CatalogSearchResultSet result{
		.storage_results = strip_scores(storage_results),
		.query_is_empty	 = normalized_query.empty()};
	result.total_count = result.storage_results.size();
	return result;
}
}	 // namespace shuba::catalog
