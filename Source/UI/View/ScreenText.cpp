#include "UI/View/ScreenText.hpp"

#include "Localization/Facade.hpp"

#include <algorithm>

namespace shuba::ui {
std::string status_text(domain::ItemStatus status,
						const localization::Localization& localization) {
	return localization.item_status_label(status);
}

std::string storage_lifecycle_text(
	domain::StorageLifecycleStatus status,
	const localization::Localization& localization) {
	return localization.storage_lifecycle_label(status);
}

std::string photo_presence_label(
	catalog::PhotoPresenceState state,
	const localization::Localization& localization) {
	return "[" + localization.photo_presence_label(state) + "]";
}

std::string photo_filter_label(catalog::SearchPhotoPresenceFilter filter,
							   const localization::Localization& localization) {
	return localization.photo_filter_label(filter);
}

std::string field_value_summary(
	std::string_view label, std::string_view value,
	const localization::Localization& localization) {
	if (value.empty())
		return localization.field_value(label, "—");
	return localization.field_value(label, value);
}

std::string tags_summary(std::span<const domain::TagRow> tags,
						 const localization::Localization& localization) {
	if (tags.empty())
		return localization.tags_summary("—");
	std::string text;
	for (std::size_t index = 0; index < tags.size(); ++index) {
		if (index > 0U)
			text += ", ";
		text += tags[index].key + "=" + tags[index].value;
	}
	return localization.tags_summary(text);
}

std::string money_summary(const std::optional<domain::MoneyAmount>& amount) {
	if (!amount)
		return "—";
	return domain::canonical_decimal_text(*amount) + " " + amount->currency;
}

std::string listing_summary(const domain::ListingData& listing,
							const localization::Localization& localization) {
	if (listing.empty())
		return localization.text(localization::MessageId::ScreenListingEmpty);
	return localization.listing_summary(
		listing.marketplace, listing.url,
		listing.price ? money_summary(listing.price) : std::string{},
		listing.note);
}

std::string finance_summary(const domain::AcquisitionData& acquisition,
							const domain::FinanceData& finance,
							const localization::Localization& localization) {
	if (acquisition.empty() && finance.empty())
		return localization.text(localization::MessageId::ScreenFinanceEmpty);
	const std::optional<domain::MoneyAmount> profit =
		domain::calculate_profit(acquisition, finance);
	return localization.finance_summary(
		acquisition.source,
		acquisition.cost ? money_summary(acquisition.cost) : std::string{},
		finance.real_sale_price ? money_summary(finance.real_sale_price)
								: std::string{},
		finance.expenses_total ? money_summary(finance.expenses_total)
							   : std::string{},
		profit ? money_summary(profit) : std::string{});
}

std::string storage_label(
	const catalog::CatalogRepositoryState& repository,
	const std::optional<core::StableIdentifier>& storage_id,
	const localization::Localization& localization) {
	if (!storage_id)
		return localization.text(
			localization::MessageId::StorageLabelUnassigned);
	const persistence::StorageEnvelope* storage =
		catalog::find_storage_envelope(repository, *storage_id);
	if (storage == nullptr)
		return localization.missing_storage_label(storage_id->value());
	return storage->record.display_name;
}

std::string storage_choice_label(
	const catalog::CatalogRepositoryState& repository,
	const persistence::StorageEnvelope& storage,
	const localization::Localization& localization) {
	const std::map<std::string, catalog::StorageProjection>::const_iterator
		projection =
			repository.storage_projections.find(storage.record.id.value());
	std::string location;
	if (projection != repository.storage_projections.end()
		&& !projection->second.path_label.empty()) {
		location = projection->second.path_label;
	} else if (!storage.record.location.empty()) {
		location = storage.record.location;
	}
	return localization.storage_choice(storage.record.display_name,
									   storage.record.storage_type, location);
}

std::optional<core::StableIdentifier> next_storage_choice(
	const catalog::CatalogRepositoryState& repository,
	const std::optional<core::StableIdentifier>& current,
	std::optional<core::StableIdentifier> excluded) {
	if (repository.storages.empty())
		return std::nullopt;
	std::vector<core::StableIdentifier> choices;
	for (const persistence::StorageEnvelope& storage : repository.storages) {
		if (excluded && storage.record.id == *excluded)
			continue;
		choices.push_back(storage.record.id);
	}
	if (choices.empty())
		return std::nullopt;
	if (!current)
		return choices.front();
	for (std::size_t index = 0; index < choices.size(); ++index) {
		if (choices[index] == *current) {
			return index + 1U < choices.size()
					   ? std::optional{choices[index + 1U]}
					   : std::nullopt;
		}
	}
	return choices.front();
}

std::string diagnostic_summary(
	std::span<const EntityEditDiagnostic> diagnostics) {
	if (diagnostics.empty())
		return "";
	std::string text;
	for (const EntityEditDiagnostic& diagnostic : diagnostics) {
		if (!text.empty())
			text += " · ";
		text += diagnostic.code + ": " + diagnostic.message;
	}
	return text;
}

std::string core_diagnostic_summary(
	std::span<const core::Diagnostic> diagnostics) {
	if (diagnostics.empty())
		return "";
	std::string text;
	for (const core::Diagnostic& diagnostic : diagnostics) {
		if (!text.empty())
			text += " · ";
		text += diagnostic.code + ": " + diagnostic.message;
	}
	return text;
}

std::string progress_summary(std::span<const platform::ProgressEvent> events,
							 const localization::Localization& localization) {
	if (events.empty())
		return localization.progress_no_events();
	return localization.progress_summary(events.back());
}

std::string pending_photo_summary(
	std::span<const PendingPhotoSource> pending_sources) {
	std::uint64_t staged_count{};
	std::uint64_t failed_count{};
	std::uint64_t removed_count{};
	std::uint64_t consumed_count{};
	for (const PendingPhotoSource& source : pending_sources) {
		if (source.ready_for_import()) {
			++staged_count;
		} else if (source.status == PendingPhotoStatus::Failed
				   || source.status == PendingPhotoStatus::Cancelled) {
			++failed_count;
		} else if (source.status == PendingPhotoStatus::Removed) {
			++removed_count;
		} else if (source.status == PendingPhotoStatus::Consumed) {
			++consumed_count;
		}
	}

	std::string text = "Pending photos: " + std::to_string(staged_count)
					   + " staged for import";
	if (failed_count > 0U)
		text += " · " + std::to_string(failed_count) + " failed";
	if (removed_count > 0U)
		text += " · " + std::to_string(removed_count) + " removed";
	if (consumed_count > 0U)
		text += " · " + std::to_string(consumed_count) + " consumed";
	return text;
}

std::string pending_photo_source_summary(const PendingPhotoSource& source,
										 std::size_t display_index) {
	std::string text = "Pending photo " + std::to_string(display_index) + ": "
					   + std::string{to_string(source.status)};
	if (source.ready_for_import())
		text += " · staged";
	if (source.byte_count)
		text += " · " + std::to_string(*source.byte_count) + " bytes";
	if (!source.diagnostics.empty())
		text += " · " + std::to_string(source.diagnostics.size()) + " issue(s)";
	return text;
}

std::string tag_row_count_summary(std::span<const domain::TagRow> tags) {
	if (tags.empty())
		return "Tags: no rows yet";
	return "Tags: " + std::to_string(tags.size())
		   + (tags.size() == 1U ? " row" : " rows");
}

void append_tag_key_candidate(std::vector<std::string>& keys,
							  std::set<std::string>& seen,
							  const domain::TagRow& tag) {
	if (!domain::is_tag_key_hint_candidate(tag))
		return;
	if (seen.insert(tag.key).second)
		keys.push_back(tag.key);
}

TagKeyCandidateGroups derive_tag_key_candidate_groups(
	const catalog::CatalogRepositoryState& repository) {
	TagKeyCandidateGroups groups;
	std::set<std::string> seen_item_keys;
	std::set<std::string> seen_storage_keys;
	for (const persistence::ItemEnvelope& item : repository.items)
		for (const domain::TagRow& tag : item.record.tags)
			append_tag_key_candidate(groups.item_keys, seen_item_keys, tag);
	for (const persistence::StorageEnvelope& storage : repository.storages) {
		for (const domain::TagRow& tag : storage.record.tags) {
			append_tag_key_candidate(groups.storage_keys, seen_storage_keys,
									 tag);
		}
	}
	return groups;
}

void apply_tag_key_candidate(std::vector<domain::TagRow>& tags,
							 std::string key) {
	for (domain::TagRow& tag : tags) {
		if (domain::is_blank_tag_key(tag.key)) {
			tag.key = std::move(key);
			return;
		}
	}
	tags.push_back(domain::TagRow{.key = std::move(key)});
}

bool has_ready_pending_photo(
	std::span<const PendingPhotoSource> pending_sources) noexcept {
	return std::ranges::any_of(pending_sources,
							   [](const PendingPhotoSource& source) {
		return source.ready_for_import();
	});
}

std::string recovery_action_summary(
	std::span<const RecoveryAction> actions,
	const localization::Localization& localization) {
	return localization.recovery_actions(actions);
}

std::string recovery_counts_summary(
	const CatalogRecoveryUiSummary& summary,
	const localization::Localization& localization) {
	return localization.recovery_counts(localization::RecoveryCountsFields{
		.accepted_items	   = summary.accepted_item_count,
		.accepted_storages = summary.accepted_storage_count,
		.accepted_photos   = summary.accepted_photo_count,
		.skipped_items	   = summary.skipped_item_count,
		.skipped_storages  = summary.skipped_storage_count,
		.skipped_photos	   = summary.skipped_photo_count,
		.broken_references = summary.broken_reference_count,
		.orphan_media	   = summary.orphan_media_count});
}

std::string import_validation_summary(
	const catalog::StagedCatalogValidationResult& validation,
	const localization::Localization& localization) {
	return localization.import_validation_summary(
		localization::ImportValidationFields{
			.load_status =
				localization.catalog_load_status_label(validation.load_status),
			.accepted_items	   = validation.items_accepted,
			.accepted_storages = validation.storages_accepted,
			.accepted_photos   = validation.photos_accepted,
			.broken_references =
				validation.derived_recovery_summary.broken_reference_count,
			.orphan_media =
				validation.derived_recovery_summary.orphan_media_count});
}

std::string recovery_summary(const CatalogRecoveryUiSummary& summary,
							 const localization::Localization& localization) {
	if (summary.fatal())
		return localization.text(localization::MessageId::RecoverySummaryFatal);
	if (summary.degraded())
		return localization.text(
			localization::MessageId::RecoverySummaryDegraded);
	return localization.text(localization::MessageId::RecoverySummaryNormal);
}

bool has_diagnostics(std::span<const core::Diagnostic> diagnostics) noexcept {
	return !diagnostics.empty();
}

std::string photo_summary(const persistence::PhotoEnvelope& photo,
						  std::size_t position, std::size_t total) {
	std::string text =
		"Photo " + std::to_string(position) + "/" + std::to_string(total);
	text += photo.record.is_main ? " · main" : " · not main";
	if (photo.record.width && photo.record.height) {
		text += " · " + std::to_string(*photo.record.width) + "x"
				+ std::to_string(*photo.record.height);
	}
	if (photo.record.encoded_bytes)
		text += " · " + std::to_string(*photo.record.encoded_bytes) + " bytes";
	if (!photo.record.source_mime_type.empty())
		text += " · " + photo.record.source_mime_type;
	return text;
}

std::string owner_caption(const catalog::CatalogRepositoryState& repository,
						  const domain::PhotoOwner& owner) {
	if (owner.type == domain::PhotoOwnerType::Item) {
		const persistence::ItemEnvelope* item =
			catalog::find_item_envelope(repository, owner.id);
		return item == nullptr ? "Missing item owner"
							   : "Item: " + item->record.display_name;
	}
	const persistence::StorageEnvelope* storage =
		catalog::find_storage_envelope(repository, owner.id);
	return storage == nullptr ? "Missing storage owner"
							  : "Storage: " + storage->record.display_name;
}

const catalog::OwnerPhotoProjection* owner_photo_projection(
	const catalog::CatalogRepositoryState& repository,
	const domain::PhotoOwner& owner) {
	const std::map<std::string, catalog::OwnerPhotoProjection>& projections =
		owner.type == domain::PhotoOwnerType::Item
			? repository.item_photo_projections
			: repository.storage_photo_projections;
	const std::map<std::string, catalog::OwnerPhotoProjection>::const_iterator
		found = projections.find(owner.id.value());
	return found == projections.end() ? nullptr : &found->second;
}

std::optional<std::size_t> find_photo_index_in_order(
	std::span<const core::StableIdentifier> ordered_photo_ids,
	const core::StableIdentifier& photo_id) {
	for (std::size_t index = 0; index < ordered_photo_ids.size(); ++index)
		if (ordered_photo_ids[index] == photo_id)
			return index;
	return std::nullopt;
}

std::optional<core::StableIdentifier> first_viewable_photo_id(
	const catalog::CatalogRepositoryState& repository,
	const domain::PhotoOwner& owner) {
	const catalog::OwnerPhotoProjection* projection =
		owner_photo_projection(repository, owner);
	if (projection == nullptr || projection->ordered_photo_ids.empty())
		return std::nullopt;
	if (projection->representative_usable_photo_id)
		return projection->representative_usable_photo_id;
	return projection->ordered_photo_ids.front();
}

std::optional<core::StableIdentifier> adjacent_photo_id(
	const catalog::CatalogRepositoryState& repository,
	const domain::PhotoOwner& owner, const core::StableIdentifier& photo_id,
	int direction) {
	const catalog::OwnerPhotoProjection* projection =
		owner_photo_projection(repository, owner);
	if (projection == nullptr || projection->ordered_photo_ids.empty())
		return std::nullopt;
	const std::optional<std::size_t> index =
		find_photo_index_in_order(projection->ordered_photo_ids, photo_id);
	if (!index.has_value())
		return projection->ordered_photo_ids.front();
	const std::size_t total = projection->ordered_photo_ids.size();
	if (direction < 0) {
		return projection
			->ordered_photo_ids[*index == 0U ? total - 1U : *index - 1U];
	}
	return projection->ordered_photo_ids[(*index + 1U) % total];
}

juce::Image juce_image_from_pixels(const platform::ImagePixels& pixels) {
	if (pixels.format != platform::PixelFormat::Rgba8 || pixels.width == 0U
		|| pixels.height == 0U
		|| pixels.width
			   > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
		|| pixels.height
			   > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
		return {};
	}
	platform::ImagePixelsValidation validation =
		platform::validate_image_pixels(pixels);
	if (!validation.valid())
		return {};
	const int width	 = static_cast<int>(pixels.width);
	const int height = static_cast<int>(pixels.height);
	juce::Image image{juce::Image::RGB, width, height, false};
	juce::Image::BitmapData bitmap{image, juce::Image::BitmapData::writeOnly};
	for (int y = 0; y < height; ++y) {
		const std::size_t source_row_offset =
			static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4U;
		juce::uint8* row = bitmap.getLinePointer(y);
		for (int x = 0; x < width; ++x) {
			const std::size_t source_offset =
				source_row_offset + static_cast<std::size_t>(x) * 4U;
			juce::uint8* target =
				row + static_cast<std::ptrdiff_t>(x) * bitmap.pixelStride;
			juce::PixelRGB* target_pixel =
				reinterpret_cast<juce::PixelRGB*>(target);
			target_pixel->setARGB(0xffU, pixels.bytes[source_offset],
								  pixels.bytes[source_offset + 1U],
								  pixels.bytes[source_offset + 2U]);
		}
	}
	return image;
}

bool contains_string(std::span<const std::string> values,
					 std::string_view value) {
	return std::ranges::any_of(values, [value](const std::string& candidate) {
		return candidate == value;
	});
}

void toggle_string(std::vector<std::string>& values, std::string value) {
	std::vector<std::string>::iterator found = std::ranges::find(values, value);
	if (found == values.end()) {
		values.push_back(std::move(value));
		std::ranges::sort(values);
		return;
	}
	values.erase(found);
}

bool contains_status(std::span<const domain::ItemStatus> values,
					 domain::ItemStatus status) {
	return std::ranges::find(values, status) != values.end();
}

void toggle_status(std::vector<domain::ItemStatus>& values,
				   domain::ItemStatus status) {
	std::vector<domain::ItemStatus>::iterator found =
		std::ranges::find(values, status);
	if (found == values.end()) {
		values.push_back(status);
		std::ranges::sort(values);
		return;
	}
	values.erase(found);
}

bool has_catalog_filters(
	const catalog::CatalogSearchFilters& filters) noexcept {
	return !filters.categories.empty() || !filters.statuses.empty()
		   || filters.include_archived || filters.storage_id.has_value()
		   || filters.storage_unassigned_only
		   || filters.photo_presence != catalog::SearchPhotoPresenceFilter::Any
		   || filters.listed_only || filters.sold_only;
}

std::string active_filter_summary(
	const catalog::CatalogSearchFilters& filters,
	const catalog::CatalogRepositoryState& repository,
	const localization::Localization& localization) {
	localization::CatalogFilterSummaryFields fields{
		.categories				= filters.categories,
		.storage_unassigned		= filters.storage_unassigned_only,
		.include_nested_storage = filters.include_nested_storage,
		.listed_shortcut		= filters.listed_only,
		.sold_shortcut			= filters.sold_only,
		.include_archived		= filters.include_archived};
	fields.statuses.reserve(filters.statuses.size());
	for (const domain::ItemStatus status : filters.statuses)
		fields.statuses.push_back(localization.item_status_label(status));

	if (filters.storage_id) {
		const persistence::StorageEnvelope* storage =
			catalog::find_storage_envelope(repository, *filters.storage_id);
		fields.storage = storage != nullptr ? storage->record.display_name
											: filters.storage_id->value();
	}
	if (filters.photo_presence != catalog::SearchPhotoPresenceFilter::Any) {
		fields.photo_presence =
			localization.photo_filter_label(filters.photo_presence);
	}
	return localization.catalog_filter_clauses(fields);
}

std::string first_note_or_tag_summary(const persistence::ItemEnvelope* item) {
	if (item == nullptr)
		return {};
	if (!item->record.notes.empty())
		return item->record.notes;
	if (!item->record.tags.empty()) {
		return item->record.tags.front().key + "="
			   + item->record.tags.front().value;
	}
	return {};
}

std::string first_storage_note_or_tag_summary(
	const persistence::StorageEnvelope* storage) {
	if (storage == nullptr)
		return {};
	if (!storage->record.notes.empty())
		return storage->record.notes;
	if (!storage->record.tags.empty()) {
		return storage->record.tags.front().key + "="
			   + storage->record.tags.front().value;
	}
	return {};
}

std::string warning_summary(const catalog::SearchWarningMarkers& warnings,
							const localization::Localization& localization) {
	std::vector<std::string> parts;
	if (warnings.no_photo_records)
		parts.push_back(localization.catalog_warning_label(
			localization::CatalogWarning::NoPhoto));
	if (warnings.broken_photos)
		parts.push_back(localization.catalog_warning_label(
			localization::CatalogWarning::BrokenPhotos));
	if (warnings.broken_storage_reference)
		parts.push_back(localization.catalog_warning_label(
			localization::CatalogWarning::BrokenStorage));
	if (warnings.broken_parent_reference)
		parts.push_back(localization.catalog_warning_label(
			localization::CatalogWarning::BrokenParent));
	if (warnings.archived_record)
		parts.push_back(localization.catalog_warning_label(
			localization::CatalogWarning::Archived));
	if (warnings.archived_storage)
		parts.push_back(localization.catalog_warning_label(
			localization::CatalogWarning::ArchivedStorage));

	std::string text;
	for (std::size_t index = 0; index < parts.size(); ++index) {
		if (index > 0U)
			text += ", ";
		text += parts[index];
	}
	return text;
}

std::string item_detail_header(const persistence::ItemEnvelope& item,
							   const catalog::ItemProjection& projection,
							   const localization::Localization& localization) {
	catalog::SearchWarningMarkers warning_markers{
		.broken_storage_reference = projection.broken_storage_reference,
		.archived_storage		  = projection.storage_archived};
	const std::string warnings = warning_summary(warning_markers, localization);
	const std::string photo_state =
		localization.photo_presence_label(projection.photo_presence);
	const std::string status =
		localization.item_status_label(item.record.status);
	return localization.item_header(localization::ItemHeaderFields{
		.name		  = item.record.display_name,
		.photo_state  = photo_state,
		.category	  = item.record.category,
		.status		  = status,
		.storage_path = projection.storage_path_label,
		.warnings	  = warnings});
}

std::string storage_detail_header(
	const persistence::StorageEnvelope& storage,
	const catalog::StorageProjection& projection,
	const localization::Localization& localization) {
	catalog::SearchWarningMarkers warning_markers{
		.broken_parent_reference = projection.parent_reference_state
								   == domain::ReferenceState::Broken};
	const std::string warnings = warning_summary(warning_markers, localization);
	return localization.storage_header(
		localization::StorageHeaderFields{.name = storage.record.display_name,
										  .type = storage.record.storage_type,
										  .path = projection.path_label,
										  .location = storage.record.location,
										  .notes	= storage.record.notes,
										  .warnings = warnings});
}

juce::String item_result_text(const catalog::SearchResult& result,
							  const CatalogSessionState& session,
							  const localization::Localization& localization) {
	const persistence::ItemEnvelope* item =
		catalog::find_item_envelope(session.repository, result.record_id);
	const std::string photo_state =
		localization.photo_presence_label(result.photo_presence);
	const std::string status =
		result.item_status.has_value()
			? localization.item_status_label(*result.item_status)
			: std::string{"—"};
	const std::string details  = first_note_or_tag_summary(item);
	const std::string warnings = warning_summary(result.warnings, localization);
	return juce_text(
		localization.item_result_card(localization::ItemResultFields{
			.title		 = result.display_title.empty()
							   ? std::string_view{"—"}
							   : std::string_view{result.display_title},
			.photo_state = photo_state,
			.category	 = result.category.empty()
							   ? std::string_view{"—"}
							   : std::string_view{result.category},
			.status		 = status,
			.location	 = result.location_text.empty()
							   ? std::string_view{"—"}
							   : std::string_view{result.location_text},
			.details	 = details.empty() ? std::string_view{"—"}
										   : std::string_view{details},
			.warnings	 = warnings.empty() ? std::string_view{"—"}
											: std::string_view{warnings}}));
}

juce::String storage_result_text(
	const catalog::SearchResult& result, const CatalogSessionState& session,
	const localization::Localization& localization) {
	const persistence::StorageEnvelope* storage =
		catalog::find_storage_envelope(session.repository, result.record_id);
	const std::string lifecycle = result.storage_lifecycle_status.has_value()
									  ? localization.storage_lifecycle_label(
											*result.storage_lifecycle_status)
									  : std::string{"—"};
	const std::string details	= first_storage_note_or_tag_summary(storage);
	const std::string warnings = warning_summary(result.warnings, localization);
	return juce_text(
		localization.storage_result_card(localization::StorageResultFields{
			.title			 = result.display_title.empty()
								   ? std::string_view{"—"}
								   : std::string_view{result.display_title},
			.type			 = result.storage_type.empty()
								   ? std::string_view{"—"}
								   : std::string_view{result.storage_type},
			.lifecycle		 = lifecycle,
			.location		 = result.location_text.empty()
								   ? std::string_view{"—"}
								   : std::string_view{result.location_text},
			.direct_children = result.direct_child_count,
			.direct_items	 = result.direct_item_count,
			.nested_items	 = result.nested_item_count,
			.details		 = details.empty() ? std::string_view{"—"}
											   : std::string_view{details},
			.warnings		 = warnings.empty() ? std::string_view{"—"}
												: std::string_view{warnings}}));
}

std::vector<std::string> distinct_categories(
	const catalog::SearchIndex& index) {
	std::set<std::string> category_set;
	for (const catalog::ItemSearchDocument& item : index.items)
		if (!item.category.empty())
			category_set.insert(item.category);
	return {category_set.begin(), category_set.end()};
}

std::set<std::string> storage_filter_id_set(
	const catalog::CatalogRepositoryState& repository,
	const core::StableIdentifier& selected_storage_id, bool include_nested) {
	std::set<std::string> ids;
	ids.insert(selected_storage_id.value());
	if (!include_nested)
		return ids;

	const std::map<std::string, catalog::StorageProjection>::const_iterator
		found =
			repository.storage_projections.find(selected_storage_id.value());
	if (found == repository.storage_projections.end())
		return ids;
	for (const core::StableIdentifier& descendant :
		 found->second.nested_descendant_storage_ids) {
		ids.insert(descendant.value());
	}
	return ids;
}
}	 // namespace shuba::ui
