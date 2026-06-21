#include "UI/Screens/AppShellScreenRenderer.hpp"
#include "UI/View/AppShellContentComponent.hpp"
#include "UI/View/ScreenText.hpp"

#include <string>

namespace shuba::ui {
void AppShellScreenRenderer::build_catalog_content() {
	if (session.degraded()) {
		content->add_label(
			"Degraded load: some records or media need attention. "
			"Accepted records remain browsable.",
			70, warning_panel_colour(), true);
	}
	if (session.demo_catalog_active) {
		content->add_label(
			"Demo catalog is active. Existing canonical catalogs "
			"are never overwritten by debug seeding.",
			62, accent_colour().withAlpha(0.34f), true);
	}

	content->add_label(juce_text(active_filter_summary(
						   catalog_filter_state.applied, session.repository)),
					   48, surface_colour(), false);
	if (catalog_filter_state.panel_visible)
		build_filter_panel();

	const std::string query = catalog_query();
	catalog::CatalogSearchOptions options{
		.include_storage_results_for_empty_query = true};
	catalog::CatalogSearchResultSet results = catalog::search_catalog(
		session.search_index, query, catalog_filter_state.applied, options);

	content->add_label(
		juce_text("Results: " + std::to_string(results.total_count)), 38,
		surface_colour(), true);

	if (session.repository.items.empty()
		&& session.repository.storages.empty()) {
		content->add_label(
			"Empty catalog. Add item and Add storage are available from Add; "
			"backup/import/recovery are available from More.",
			84, panel_colour(), true);
		return;
	}
	if (results.total_count == 0U) {
		content->add_label(
			"No results. Try clearing the query or filters, "
			"or include archived records.",
			72, panel_colour(), true);
		return;
	}

	content->add_label("Items", 36, panel_colour(), true);
	for (const catalog::SearchResult& result : results.item_results) {
		juce::Button& button =
			content->add_button(item_result_text(result, session), 78);
		core::StableIdentifier item_id = result.record_id;
		button.onClick = [this, item_id] { open_item_detail(item_id); };
	}

	if (!results.storage_results.empty()) {
		content->add_label("Storages", 36, panel_colour(), true);
		for (const catalog::SearchResult& result : results.storage_results) {
			juce::Button& button =
				content->add_button(storage_result_text(result, session), 78);
			core::StableIdentifier storage_id = result.record_id;
			button.onClick					  = [this, storage_id] {
				open_storage_detail(storage_id);
			};
		}
	}
}

void AppShellScreenRenderer::build_filter_panel() {
	content->add_label("Filter workflow", 38, accent_colour().withAlpha(0.35f),
					   true);

	content->add_label("Category multi-select", 32, panel_colour(), true);
	const std::vector<std::string> categories =
		distinct_categories(session.search_index);
	if (categories.empty())
		content->add_label("No category values yet.", 32);
	for (const std::string& category : categories) {
		juce::ToggleButton& toggle = content->add_toggle(
			juce_text("Category: " + category),
			contains_string(catalog_filter_state.draft.categories, category),
			34);
		toggle.onClick = [this, category] {
			toggle_string(catalog_filter_state.draft.categories, category);
			refresh_content();
		};
	}

	content->add_label("Status multi-select", 32, panel_colour(), true);
	for (domain::ItemStatus status :
		 {domain::ItemStatus::Draft, domain::ItemStatus::Planned,
		  domain::ItemStatus::Listed, domain::ItemStatus::Sold,
		  domain::ItemStatus::Archived}) {
		juce::ToggleButton& toggle = content->add_toggle(
			juce_text("Status: " + status_text(status)),
			contains_status(catalog_filter_state.draft.statuses, status), 34);
		toggle.onClick = [this, status] {
			toggle_status(catalog_filter_state.draft.statuses, status);
			refresh_content();
		};
	}

	content->add_label("Storage selector", 32, panel_colour(), true);
	juce::Button& any_storage = content->add_button(
		catalog_filter_state.draft.storage_id
				|| catalog_filter_state.draft.storage_unassigned_only
			? "Any storage"
			: "* Any storage",
		34);
	any_storage.onClick = [this] {
		catalog_filter_state.draft.storage_id.reset();
		catalog_filter_state.draft.storage_unassigned_only = false;
		refresh_content();
	};
	juce::Button& unassigned = content->add_button(
		catalog_filter_state.draft.storage_unassigned_only ? "* Unassigned"
														   : "Unassigned",
		34);
	unassigned.onClick = [this] {
		catalog_filter_state.draft.storage_id.reset();
		catalog_filter_state.draft.storage_unassigned_only = true;
		refresh_content();
	};
	for (const catalog::StorageSearchDocument& storage :
		 session.search_index.storages) {
		std::string title = storage.projection.display_name;
		if (catalog_filter_state.draft.storage_id
			&& *catalog_filter_state.draft.storage_id
				   == storage.projection.id) {
			title = "* " + title;
		}
		juce::Button& button = content->add_button(juce_text(title), 34);
		core::StableIdentifier storage_id = storage.projection.id;
		button.onClick					  = [this, storage_id] {
			catalog_filter_state.draft.storage_id			   = storage_id;
			catalog_filter_state.draft.storage_unassigned_only = false;
			catalog_filter_state.draft.include_nested_storage  = true;
			refresh_content();
		};
	}

	juce::ToggleButton& nested = content->add_toggle(
		"Include nested contents",
		catalog_filter_state.draft.include_nested_storage, 34);
	nested.setEnabled(catalog_filter_state.draft.storage_id.has_value());
	nested.onClick = [this] {
		catalog_filter_state.draft.include_nested_storage =
			!catalog_filter_state.draft.include_nested_storage;
		refresh_content();
	};

	content->add_label("Photo presence", 32, panel_colour(), true);
	for (catalog::SearchPhotoPresenceFilter filter :
		 {catalog::SearchPhotoPresenceFilter::Any,
		  catalog::SearchPhotoPresenceFilter::HasPhotos,
		  catalog::SearchPhotoPresenceFilter::NoPhotos,
		  catalog::SearchPhotoPresenceFilter::BrokenPhotos}) {
		std::string label = photo_filter_label(filter);
		if (catalog_filter_state.draft.photo_presence == filter)
			label = "* " + label;
		juce::Button& button = content->add_button(juce_text(label), 34);
		button.onClick		 = [this, filter] {
			catalog_filter_state.draft.photo_presence = filter;
			refresh_content();
		};
	}

	juce::ToggleButton& listed = content->add_toggle(
		"Listed shortcut", catalog_filter_state.draft.listed_only, 34);
	listed.onClick = [this] {
		catalog_filter_state.draft.listed_only =
			!catalog_filter_state.draft.listed_only;
		refresh_content();
	};
	juce::ToggleButton& sold = content->add_toggle(
		"Sold shortcut", catalog_filter_state.draft.sold_only, 34);
	sold.onClick = [this] {
		catalog_filter_state.draft.sold_only =
			!catalog_filter_state.draft.sold_only;
		refresh_content();
	};
	juce::ToggleButton& archived = content->add_toggle(
		"Include archived", catalog_filter_state.draft.include_archived, 34);
	archived.onClick = [this] {
		catalog_filter_state.draft.include_archived =
			!catalog_filter_state.draft.include_archived;
		refresh_content();
	};

	juce::Button& apply = content->add_button("Apply filters", 38);
	apply.onClick		= [this] {
		catalog_filter_state.applied	   = catalog_filter_state.draft;
		catalog_filter_state.panel_visible = false;
		refresh_all();
	};
	juce::Button& clear = content->add_button("Clear filters", 38);
	clear.onClick		= [this] {
		reset_catalog_filters();
		refresh_all();
	};
}

}	 // namespace shuba::ui
