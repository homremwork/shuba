#include "UI/Screens/AppShellScreenRenderer.hpp"
#include "UI/View/AppShellContentComponent.hpp"
#include "UI/View/ScreenText.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace shuba::ui {
void AppShellScreenRenderer::build_storages_content() {
	if (session.demo_catalog_active)
		content->add_label("Demo catalog is active.", 42,
						   accent_colour().withAlpha(0.34f), true);

	const std::string query = storage_query();
	catalog::StorageSearchFilters filters;
	catalog::CatalogSearchResultSet results =
		catalog::search_storages(session.search_index, query, filters);
	content->add_label(
		juce_text("Storages: " + std::to_string(results.total_count)), 38,
		panel_colour(), true);
	if (results.storage_results.empty()) {
		content->add_label("No storage records yet.", 52, panel_colour(), true);
		juce::Button& add_storage = content->add_button("Add storage", 42);
		add_storage.onClick = [this] { open_new_storage_form(std::nullopt); };
		return;
	}
	for (const catalog::SearchResult& result : results.storage_results) {
		juce::Button& button =
			content->add_button(storage_result_text(result, session), 78);
		core::StableIdentifier storage_id = result.record_id;
		button.onClick					  = [this, storage_id] {
			open_storage_detail(storage_id);
		};
	}
}

void AppShellScreenRenderer::build_item_detail_content() {
	if (!route.selected_item_id) {
		content->add_label("No item selected.", 54, panel_colour(), true);
		return;
	}
	const persistence::ItemEnvelope* item = catalog::find_item_envelope(
		session.repository, *route.selected_item_id);
	const std::map<std::string, catalog::ItemProjection>::const_iterator
		projection = session.repository.item_projections.find(
			route.selected_item_id->value());
	if (item == nullptr
		|| projection == session.repository.item_projections.end()) {
		content->add_label(
			"Selected item is missing from the accepted catalog.", 64,
			warning_panel_colour(), true);
		return;
	}

	std::string header = photo_presence_label(projection->second.photo_presence)
						 + " " + item->record.display_name + " · "
						 + item->record.category + " · "
						 + status_text(item->record.status);
	if (!projection->second.storage_path_label.empty())
		header += " · " + projection->second.storage_path_label;
	if (projection->second.storage_archived)
		header += " · ⚠ archived storage";
	if (projection->second.broken_storage_reference)
		header += " · ⚠ broken storage";
	content->add_label(juce_text(header), 92, surface_colour(), true);

	juce::Button& edit = content->add_button("Edit item", 42);
	edit.onClick	   = [this, item_id = item->record.id] {
		open_existing_item_form(item_id);
	};
	juce::Button& change_storage = content->add_button(
		juce_text("Change storage: "
				  + storage_label(session.repository, item->record.storage_id)),
		42);
	change_storage.onClick = [this, item_id = item->record.id] {
		open_existing_item_form(item_id);
	};
	domain::PhotoOwner owner{.type = domain::PhotoOwnerType::Item,
							 .id   = item->record.id};
	juce::Button& add_photo = content->add_button("Add photos", 42);
	add_photo.onClick		= [this, owner] { request_add_photos(owner); };
	juce::Button& viewer = content->add_button("Open photo viewer/export", 42);
	viewer.setEnabled(projection->second.representative_photo_id.has_value());
	viewer.onClick = [this, owner,
					  photo_id = projection->second.representative_photo_id] {
		open_photo_viewer(owner, photo_id);
	};
	juce::Button& export_current =
		content->add_button("Export representative photo as JPEG", 42);
	export_current.setEnabled(
		projection->second.representative_usable_photo_id.has_value());
	export_current.onClick =
		[this, photo_id = projection->second.representative_usable_photo_id] {
		if (photo_id)
			request_export_photo(*photo_id);
	};
	if (item->record.storage_id
		&& !projection->second.broken_storage_reference) {
		juce::Button& storage_button = content->add_button(
			juce_text(
				"Open storage: "
				+ storage_label(session.repository, item->record.storage_id)),
			42);
		core::StableIdentifier storage_id = *item->record.storage_id;
		storage_button.onClick			  = [this, storage_id] {
			open_storage_detail(storage_id);
		};
	}

	content->add_label(
		juce_text(field_value_summary("Notes", item->record.notes)), 58,
		panel_colour());
	content->add_label(juce_text(tags_summary(item->record.tags)), 58,
					   panel_colour());
	content->add_label(juce_text(listing_summary(item->record.listing)), 58,
					   panel_colour(), !item->record.listing.empty());
	content->add_label(
		juce_text(
			finance_summary(item->record.acquisition, item->record.finance)),
		58, panel_colour(),
		!item->record.acquisition.empty() || !item->record.finance.empty());
	if (projection->second.photo_presence
		!= catalog::PhotoPresenceState::HasUsablePhotos) {
		content->add_label(
			"Recovery/photo warning: photo state needs attention or "
			"photo import is still pending.",
			64, warning_panel_colour(), true);
	}
}

void AppShellScreenRenderer::build_storage_detail_content() {
	if (!route.selected_storage_id) {
		content->add_label("No storage selected.", 54, panel_colour(), true);
		return;
	}

	const persistence::StorageEnvelope* storage =
		catalog::find_storage_envelope(session.repository,
									   *route.selected_storage_id);
	const std::map<std::string, catalog::StorageProjection>::const_iterator
		projection = session.repository.storage_projections.find(
			route.selected_storage_id->value());
	if (storage == nullptr
		|| projection == session.repository.storage_projections.end()) {
		content->add_label(
			"Selected storage is missing from the accepted catalog.", 64,
			warning_panel_colour(), true);
		return;
	}

	std::string header =
		storage->record.display_name + " · " + storage->record.storage_type;
	if (!projection->second.path_label.empty())
		header += " · " + projection->second.path_label;
	if (!storage->record.location.empty())
		header += " · " + storage->record.location;
	if (!storage->record.notes.empty())
		header += " · " + storage->record.notes;
	if (projection->second.parent_reference_state
		== domain::ReferenceState::Broken) {
		header += " · ⚠ broken parent";
	}
	content->add_label(juce_text(header), 84, surface_colour(), true);

	juce::ToggleButton& include_nested = content->add_toggle(
		"Include nested contents", storage_detail.include_nested, 34);
	include_nested.onClick = [this] {
		storage_detail.include_nested = !storage_detail.include_nested;
		refresh_content();
	};
	content->add_label(storage_detail.include_nested
						   ? "Showing nested contents by default."
						   : "Direct contents only mode.",
					   34, panel_colour());

	content->add_label("Child storages", 36, panel_colour(), true);
	std::vector<core::StableIdentifier> child_ids =
		projection->second.direct_child_storage_ids;
	if (storage_detail.include_nested) {
		child_ids.insert(
			child_ids.end(),
			projection->second.nested_descendant_storage_ids.begin(),
			projection->second.nested_descendant_storage_ids.end());
	}
	if (child_ids.empty())
		content->add_label("No child storages.", 34);
	for (const core::StableIdentifier& child_id : child_ids) {
		const persistence::StorageEnvelope* child =
			catalog::find_storage_envelope(session.repository, child_id);
		if (child == nullptr)
			continue;
		juce::Button& button = content->add_button(
			juce_text("[storage] " + child->record.display_name + " · "
					  + child->record.storage_type),
			54);
		button.onClick = [this, child_id] { open_storage_detail(child_id); };
	}

	content->add_label("Items inside", 36, panel_colour(), true);
	const std::set<std::string> accepted_storage_ids =
		storage_filter_id_set(session.repository, *route.selected_storage_id,
							  storage_detail.include_nested);
	std::size_t item_count = 0;
	for (const persistence::ItemEnvelope& item : session.repository.items) {
		if (!item.record.storage_id
			|| !accepted_storage_ids.contains(
				item.record.storage_id->value())) {
			continue;
		}
		const std::map<std::string, catalog::ItemProjection>::const_iterator
			found = session.repository.item_projections.find(
				item.record.id.value());
		if (found == session.repository.item_projections.end())
			continue;
		++item_count;
		std::string row = photo_presence_label(found->second.photo_presence)
						  + " " + item.record.display_name + " · "
						  + item.record.category + " · "
						  + status_text(item.record.status);
		if (!item.record.notes.empty())
			row += " · " + item.record.notes;
		juce::Button& button = content->add_button(juce_text(row), 62);
		core::StableIdentifier item_id = item.record.id;
		button.onClick = [this, item_id] { open_item_detail(item_id); };
	}
	if (item_count == 0U)
		content->add_label("No items in this mode.", 34);

	content->add_label("Actions", 36, panel_colour(), true);
	juce::Button& add_item = content->add_button("Add item here", 42);
	add_item.onClick	   = [this, storage_id = *route.selected_storage_id] {
		open_new_item_form(storage_id);
	};
	juce::Button& add_storage = content->add_button("Add nested storage", 42);
	add_storage.onClick = [this, storage_id = *route.selected_storage_id] {
		open_new_storage_form(storage_id);
	};
	domain::PhotoOwner owner{.type = domain::PhotoOwnerType::Storage,
							 .id   = *route.selected_storage_id};
	juce::Button& add_photo = content->add_button("Add photos", 42);
	add_photo.onClick		= [this, owner] { request_add_photos(owner); };
	juce::Button& viewer = content->add_button("Open storage photo viewer", 42);
	viewer.setEnabled(projection->second.representative_photo_id.has_value());
	viewer.onClick = [this, owner,
					  photo_id = projection->second.representative_photo_id] {
		open_photo_viewer(owner, photo_id);
	};
	juce::Button& edit = content->add_button("Edit storage", 42);
	edit.onClick	   = [this, storage_id = *route.selected_storage_id] {
		open_existing_storage_form(storage_id);
	};
}

}	 // namespace shuba::ui
