#include "Localization/Facade.hpp"
#include "UI/AppShell/ScreenRenderer.hpp"
#include "UI/AppShell/ContentComponent.hpp"
#include "UI/View/ScreenText.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace shuba::ui {
namespace {
constexpr int preview_result_row_height		   = 104;
constexpr std::size_t compact_chip_label_limit = 18U;

[[nodiscard]] ImagePreviewRequestPriority list_preview_priority(
	std::size_t preview_candidate_index) noexcept {
	return preview_candidate_index < 8U ? ImagePreviewRequestPriority::Normal
										: ImagePreviewRequestPriority::Low;
}

[[nodiscard]] int category_chip_columns(
	const std::vector<std::string>& labels) noexcept {
	for (const std::string& label : labels)
		if (label.size() > compact_chip_label_limit)
			return 1;
	return 2;
}

[[nodiscard]] int storage_chip_columns(
	const std::vector<catalog::StorageSearchDocument>& storages) noexcept {
	for (const catalog::StorageSearchDocument& storage : storages)
		if (storage.projection.display_name.size() > compact_chip_label_limit)
			return 1;
	return 2;
}
}	 // namespace

void ScreenRenderer::build_catalog_content() {
	if (session.degraded()) {
		content->add_label(juce_text(localization.text(
							   localization::MessageId::CatalogBannerDegraded)),
						   70, warning_panel_colour(), true);
	}
	if (session.demo_catalog_active) {
		content->add_label(juce_text(localization.text(
							   localization::MessageId::CatalogBannerDemo)),
						   62, accent_colour().withAlpha(0.34f), true);
	}

	content->add_label(
		juce_text(localization.catalog_filter_summary(
			localization::CatalogFilterSummaryKind::Applied,
			active_filter_summary(catalog_filter_state.applied,
								  session.repository, localization))),
		48, surface_colour(), false);
	if (catalog_filter_state.panel_visible)
		build_filter_panel();

	const std::string query = catalog_query();
	catalog::CatalogSearchOptions options{
		.include_storage_results_for_empty_query = true};
	catalog::CatalogSearchResultSet results = catalog::search_catalog(
		session.search_index, query, catalog_filter_state.applied, options);

	content->add_label(
		juce_text(localization.catalog_result_count(results.total_count)), 38,
		surface_colour(), true);

	if (session.repository.items.empty()
		&& session.repository.storages.empty()) {
		content->add_label(juce_text(localization.text(
							   localization::MessageId::CatalogEmptyGuidance)),
						   84, panel_colour(), true);
		return;
	}
	if (results.total_count == 0U) {
		content->add_label(juce_text(localization.text(
							   localization::MessageId::CatalogNoResults)),
						   72, panel_colour(), true);
		return;
	}

	std::size_t preview_candidate_count{};
	if (!results.storage_results.empty()) {
		content->add_label(
			juce_text(localization.text(
				localization::MessageId::CatalogHeadingStorages)),
			36, panel_colour(), true);
		std::vector<CompactStorageCardContent> storage_cards;
		storage_cards.reserve(results.storage_results.size());
		for (const catalog::SearchResult& result : results.storage_results) {
			const ImagePreviewRequestPriority preview_priority =
				list_preview_priority(preview_candidate_count);
			if (result.representative_usable_photo_id.has_value())
				++preview_candidate_count;
			CompactStorageCardContent card =
				build_storage_result_compact_card(result, preview_priority);
			core::StableIdentifier storage_id = result.record_id;
			card.on_activate				  = [this, storage_id] {
				open_storage_detail(storage_id);
			};
			storage_cards.push_back(std::move(card));
		}
		content->add_compact_storage_strip(
			std::move(storage_cards), localization,
			CompactStorageStripComponent::preferred_height());
	}

	if (!results.item_results.empty())
		content->add_label(juce_text(localization.text(
							   localization::MessageId::CatalogHeadingItems)),
						   36, panel_colour(), true);
	for (const catalog::SearchResult& result : results.item_results) {
		const ImagePreviewRequestPriority preview_priority =
			list_preview_priority(preview_candidate_count);
		if (result.representative_usable_photo_id.has_value())
			++preview_candidate_count;
		PreviewCardBuildResult card =
			build_item_result_preview_card(result, preview_priority);
		PreviewCardButtonComponent& button = content->add_preview_card(
			std::move(card.content), localization, preview_result_row_height);
		core::StableIdentifier item_id = result.record_id;
		button.onClick = [this, item_id] { open_item_detail(item_id); };
	}
}

void ScreenRenderer::build_filter_panel() {
	catalog::CatalogSearchOptions options{
		.include_storage_results_for_empty_query = true};
	const catalog::CatalogSearchResultSet draft_results =
		catalog::search_catalog(session.search_index, catalog_query(),
								catalog_filter_state.draft, options);
	content->add_label(
		juce_text(localization.draft_result_count(draft_results.total_count)),
		42, accent_colour().withAlpha(0.35f), true);
	content->add_label(
		juce_text(localization.catalog_filter_summary(
			localization::CatalogFilterSummaryKind::Draft,
			active_filter_summary(catalog_filter_state.draft,
								  session.repository, localization))),
		46, surface_colour(), false);

	const std::vector<std::string> categories =
		distinct_categories(session.search_index);
	if (categories.empty()) {
		content->add_label(
			juce_text(localization.text(
				localization::MessageId::CatalogCategoriesEmpty)),
			34, panel_colour());
	} else {
		std::vector<ChipGridComponent::Action> category_actions;
		category_actions.reserve(categories.size());
		for (const std::string& category : categories) {
			category_actions.push_back(ChipGridComponent::Action{
				.label	  = juce_text(category),
				.selected = contains_string(
					catalog_filter_state.draft.categories, category),
				.enabled = true,
				.handler = [this, category] {
				toggle_string(catalog_filter_state.draft.categories, category);
				refresh_all();
			}});
		}
		const int columns = category_chip_columns(categories);
		content->add_chip_grid(
			juce_text(
				localization.text(localization::MessageId::CatalogCategories)),
			std::move(category_actions), columns,
			ChipGridComponent::preferred_height(
				static_cast<int>(categories.size()), columns));
	}

	std::vector<ChipGridComponent::Action> status_actions;
	for (domain::ItemStatus status :
		 {domain::ItemStatus::Draft, domain::ItemStatus::Planned,
		  domain::ItemStatus::Listed, domain::ItemStatus::Sold,
		  domain::ItemStatus::Archived}) {
		status_actions.push_back(ChipGridComponent::Action{
			.label = juce_text(localization.item_status_label(status)),
			.selected =
				contains_status(catalog_filter_state.draft.statuses, status),
			.enabled = true,
			.handler = [this, status] {
			toggle_status(catalog_filter_state.draft.statuses, status);
			refresh_all();
		}});
	}
	content->add_chip_grid(
		juce_text(localization.text(localization::MessageId::CatalogStatus)),
		std::move(status_actions), 2,
		ChipGridComponent::preferred_height(5, 2));

	std::vector<ChipGridComponent::Action> storage_actions;
	storage_actions.reserve(session.search_index.storages.size() + 2U);
	storage_actions.push_back(ChipGridComponent::Action{
		.label = juce_text(
			localization.text(localization::MessageId::CatalogStorageAny)),
		.selected = !catalog_filter_state.draft.storage_id.has_value()
					&& !catalog_filter_state.draft.storage_unassigned_only,
		.enabled  = true,
		.handler  = [this] {
		catalog_filter_state.draft.storage_id.reset();
		catalog_filter_state.draft.storage_unassigned_only = false;
		refresh_all();
	}});
	storage_actions.push_back(ChipGridComponent::Action{
		.label	  = juce_text(localization.text(
			localization::MessageId::CatalogStorageUnassigned)),
		.selected = catalog_filter_state.draft.storage_unassigned_only,
		.enabled  = true,
		.handler  = [this] {
		catalog_filter_state.draft.storage_id.reset();
		catalog_filter_state.draft.storage_unassigned_only = true;
		refresh_all();
	}});
	for (const catalog::StorageSearchDocument& storage :
		 session.search_index.storages) {
		const core::StableIdentifier storage_id = storage.projection.id;
		storage_actions.push_back(ChipGridComponent::Action{
			.label	  = juce_text(storage.projection.display_name),
			.selected = catalog_filter_state.draft.storage_id.has_value()
						&& *catalog_filter_state.draft.storage_id == storage_id,
			.enabled  = true,
			.handler  = [this, storage_id] {
			catalog_filter_state.draft.storage_id			   = storage_id;
			catalog_filter_state.draft.storage_unassigned_only = false;
			catalog_filter_state.draft.include_nested_storage  = true;
			refresh_all();
		}});
	}
	const int storage_columns =
		storage_chip_columns(session.search_index.storages);
	content->add_chip_grid(
		juce_text(localization.text(localization::MessageId::CatalogStorage)),
		std::move(storage_actions), storage_columns,
		ChipGridComponent::preferred_height(
			static_cast<int>(session.search_index.storages.size() + 2U),
			storage_columns));

	content->add_chip_grid(
		juce_text(
			localization.text(localization::MessageId::CatalogStorageScope)),
		{ChipGridComponent::Action{
			.label	  = juce_text(localization.text(
				localization::MessageId::CatalogNestedContents)),
			.selected = catalog_filter_state.draft.include_nested_storage,
			.enabled  = catalog_filter_state.draft.storage_id.has_value(),
			.handler =
				[this] {
		catalog_filter_state.draft.include_nested_storage =
			!catalog_filter_state.draft.include_nested_storage;
		refresh_all();
	}}},
		1, ChipGridComponent::preferred_height(1, 1));

	std::vector<ChipGridComponent::Action> photo_actions;
	for (catalog::SearchPhotoPresenceFilter filter :
		 {catalog::SearchPhotoPresenceFilter::Any,
		  catalog::SearchPhotoPresenceFilter::HasPhotos,
		  catalog::SearchPhotoPresenceFilter::NoPhotos,
		  catalog::SearchPhotoPresenceFilter::BrokenPhotos}) {
		photo_actions.push_back(ChipGridComponent::Action{
			.label	  = juce_text(localization.photo_filter_label(filter)),
			.selected = catalog_filter_state.draft.photo_presence == filter,
			.enabled  = true,
			.handler  = [this, filter] {
			catalog_filter_state.draft.photo_presence = filter;
			refresh_all();
		}});
	}
	content->add_chip_grid(
		juce_text(
			localization.text(localization::MessageId::CatalogPhotoPresence)),
		std::move(photo_actions), 2, ChipGridComponent::preferred_height(4, 2));

	content->add_chip_grid(
		juce_text(localization.text(localization::MessageId::CatalogShortcuts)),
		{ChipGridComponent::Action{
			 .label	   = juce_text(localization.text(
				 localization::MessageId::CatalogShortcutListed)),
			 .selected = catalog_filter_state.draft.listed_only,
			 .enabled  = true,
			 .handler =
				 [this] {
		catalog_filter_state.draft.listed_only =
			!catalog_filter_state.draft.listed_only;
		refresh_all();
	}},
		 ChipGridComponent::Action{
			 .label	   = juce_text(localization.text(
				 localization::MessageId::CatalogShortcutSold)),
			 .selected = catalog_filter_state.draft.sold_only,
			 .enabled  = true,
			 .handler =
				 [this] {
		catalog_filter_state.draft.sold_only =
			!catalog_filter_state.draft.sold_only;
		refresh_all();
	}},
		 ChipGridComponent::Action{
			 .label	   = juce_text(localization.text(
				 localization::MessageId::CatalogShortcutArchived)),
			 .selected = catalog_filter_state.draft.include_archived,
			 .enabled  = true,
			 .handler =
				 [this] {
		catalog_filter_state.draft.include_archived =
			!catalog_filter_state.draft.include_archived;
		refresh_all();
	}}},
		2, ChipGridComponent::preferred_height(3, 2));
}
}	 // namespace shuba::ui
