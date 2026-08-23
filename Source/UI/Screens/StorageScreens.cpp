#include "Localization/Facade.hpp"
#include "UI/Screens/AppShellScreenRenderer.hpp"
#include "UI/View/AppShellContentComponent.hpp"
#include "UI/View/ScreenText.hpp"

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace shuba::ui {
namespace {
constexpr int preview_result_row_height = 104;

[[nodiscard]] ImagePreviewRequestPriority list_preview_priority(
	std::size_t preview_candidate_index) noexcept {
	return preview_candidate_index < 8U ? ImagePreviewRequestPriority::Normal
										: ImagePreviewRequestPriority::Low;
}
}	 // namespace

std::vector<ItemDetailAction> item_detail_actions(
	const persistence::ItemEnvelope& item,
	const catalog::ItemProjection& projection,
	const catalog::CatalogRepositoryState& repository) {
	std::vector<ItemDetailAction> actions{
		ItemDetailAction{.kind			 = ItemDetailActionKind::EditItem,
						 .destination_id = item.record.id}};
	if (!item.record.storage_id || projection.broken_storage_reference)
		return actions;

	const persistence::StorageEnvelope* storage =
		catalog::find_storage_envelope(repository, *item.record.storage_id);
	if (storage != nullptr) {
		actions.push_back(
			ItemDetailAction{.kind = ItemDetailActionKind::OpenStorage,
							 .destination_id = storage->record.id});
	}
	return actions;
}

void AppShellScreenRenderer::build_storages_content() {
	if (session.demo_catalog_active)
		content->add_label(juce_text(localization.text(
							   localization::MessageId::CatalogBannerDemo)),
						   42, accent_colour().withAlpha(0.34f), true);

	const std::string query = storage_query();
	catalog::StorageSearchFilters filters;
	catalog::CatalogSearchResultSet results =
		catalog::search_storages(session.search_index, query, filters);
	content->add_label(
		juce_text(localization.catalog_result_count(results.total_count)), 38,
		panel_colour(), true);
	if (results.storage_results.empty()) {
		content->add_label(
			juce_text(localization.text(
				localization::MessageId::CatalogStorageNoSelection)),
			52, panel_colour(), true);
		juce::Button& add_storage = content->add_button(
			juce_text(
				localization.text(localization::MessageId::TitleAddStorage)),
			42);
		add_storage.onClick = [this] { open_new_storage_form(std::nullopt); };
		add_storage.setEnabled(!shell_operation_state.active());
		return;
	}
	std::size_t preview_candidate_count{};
	for (const catalog::SearchResult& result : results.storage_results) {
		const ImagePreviewRequestPriority preview_priority =
			list_preview_priority(preview_candidate_count);
		if (result.representative_usable_photo_id.has_value())
			++preview_candidate_count;
		PreviewCardBuildResult card =
			build_storage_result_preview_card(result, preview_priority);
		PreviewCardButtonComponent& button = content->add_preview_card(
			std::move(card.content), localization, preview_result_row_height);
		core::StableIdentifier storage_id = result.record_id;
		button.onClick					  = [this, storage_id] {
			open_storage_detail(storage_id);
		};
	}
}

void AppShellScreenRenderer::build_item_detail_content() {
	const bool mutation_allowed = !shell_operation_state.active();
	if (!route.selected_item_id) {
		content->add_label(
			juce_text(localization.text(
				localization::MessageId::CatalogItemNoSelection)),
			54, panel_colour(), true);
		return;
	}
	const persistence::ItemEnvelope* item = catalog::find_item_envelope(
		session.repository, *route.selected_item_id);
	const std::map<std::string, catalog::ItemProjection>::const_iterator
		projection = session.repository.item_projections.find(
			route.selected_item_id->value());
	if (item == nullptr
		|| projection == session.repository.item_projections.end()) {
		content->add_label(juce_text(localization.text(
							   localization::MessageId::CatalogItemMissing)),
						   64, warning_panel_colour(), true);
		return;
	}
	domain::PhotoOwner owner{.type = domain::PhotoOwnerType::Item,
							 .id   = item->record.id};
	add_owner_photo_carousel(
		owner, projection->second.photo_presence,
		juce_text(localization.text(
			localization::MessageId::CatalogItemNoPhotosTitle)),
		juce_text(localization.text(
			localization::MessageId::CatalogItemNoPhotosCaption)));

	content->add_label(
		juce_text(item_detail_header(*item, projection->second, localization)),
		92, surface_colour(), true);

	for (const ItemDetailAction& action :
		 item_detail_actions(*item, projection->second, session.repository)) {
		if (!action.destination_id)
			continue;
		if (action.kind == ItemDetailActionKind::EditItem) {
			juce::Button& edit = content->add_button(
				juce_text(localization.text(
					localization::MessageId::EntityActionEditItem)),
				42);
			edit.onClick = [this, item_id = *action.destination_id] {
				open_existing_item_form(item_id);
			};
			edit.setEnabled(mutation_allowed);
		} else {
			const persistence::StorageEnvelope* assigned_storage =
				catalog::find_storage_envelope(session.repository,
											   *action.destination_id);
			if (assigned_storage == nullptr)
				continue;
			juce::Button& storage_button =
				content->add_button(juce_text(localization.open_storage_action(
										assigned_storage->record.display_name)),
									42);
			storage_button.onClick = [this,
									  storage_id = *action.destination_id] {
				open_storage_detail(storage_id);
			};
			storage_button.setEnabled(mutation_allowed);
		}
	}
	juce::Button& add_photo = content->add_button(
		juce_text(localization.text(localization::MessageId::PhotoAdd)), 42);
	add_photo.onClick = [this, owner] { request_add_photos(owner); };
	add_photo.setEnabled(mutation_allowed);
	juce::Button& viewer = content->add_button(
		juce_text(
			localization.text(localization::MessageId::PhotoActionOpenViewer)),
		42);
	viewer.setEnabled(
		mutation_allowed
		&& projection->second.representative_photo_id.has_value());
	viewer.onClick = [this, owner] {
		const std::optional<core::StableIdentifier> photo_id =
			selected_photo_id_for_owner(owner);
		open_photo_viewer(owner, photo_id);
	};
	juce::Button& export_current = content->add_button(
		juce_text(localization.text(localization::MessageId::PhotoExportJpeg)),
		42);
	export_current.setEnabled(
		mutation_allowed
		&& selected_usable_photo_id_for_owner(owner).has_value());
	export_current.onClick = [this, owner] {
		const std::optional<core::StableIdentifier> photo_id =
			selected_usable_photo_id_for_owner(owner);
		if (photo_id)
			request_export_photo(*photo_id);
	};
	content->add_label(
		juce_text(field_value_summary(
			localization.text(localization::MessageId::FormsNotes),
			item->record.notes, localization)),
		58, panel_colour());
	content->add_label(juce_text(tags_summary(item->record.tags, localization)),
					   58, panel_colour());
	content->add_label(
		juce_text(listing_summary(item->record.listing, localization)), 58,
		panel_colour(), !item->record.listing.empty());
	content->add_label(
		juce_text(finance_summary(item->record.acquisition,
								  item->record.finance, localization)),
		58, panel_colour(),
		!item->record.acquisition.empty() || !item->record.finance.empty());
	if (projection->second.photo_presence
		!= catalog::PhotoPresenceState::HasUsablePhotos) {
		content->add_label(
			juce_text(localization.text(
				localization::MessageId::CatalogItemPhotoWarning)),
			64, warning_panel_colour(), true);
	}
}

void AppShellScreenRenderer::build_storage_detail_content() {
	const bool mutation_allowed = !shell_operation_state.active();
	if (!route.selected_storage_id) {
		content->add_label(
			juce_text(localization.text(
				localization::MessageId::CatalogStorageNoSelection)),
			54, panel_colour(), true);
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
		content->add_label(juce_text(localization.text(
							   localization::MessageId::CatalogStorageMissing)),
						   64, warning_panel_colour(), true);
		return;
	}
	domain::PhotoOwner owner{.type = domain::PhotoOwnerType::Storage,
							 .id   = *route.selected_storage_id};
	add_owner_photo_carousel(
		owner, projection->second.photo_presence,
		juce_text(localization.text(
			localization::MessageId::CatalogStorageNoPhotosTitle)),
		juce_text(localization.text(
			localization::MessageId::CatalogStorageNoPhotosCaption)));

	content->add_label(juce_text(storage_detail_header(
						   *storage, projection->second, localization)),
					   84, surface_colour(), true);

	juce::ToggleButton& include_nested = content->add_toggle(
		juce_text(localization.text(
			localization::MessageId::CatalogStorageIncludeNested)),
		storage_detail.include_nested, 34);
	include_nested.onClick = [this] {
		storage_detail.include_nested = !storage_detail.include_nested;
		refresh_content();
	};
	include_nested.setEnabled(mutation_allowed);
	content->add_label(
		juce_text(localization.text(
			storage_detail.include_nested
				? localization::MessageId::CatalogStorageNestedDefault
				: localization::MessageId::CatalogStorageDirectOnly)),
		34, panel_colour());

	content->add_label(
		juce_text(localization.text(
			localization::MessageId::CatalogStorageChildStorages)),
		36, panel_colour(), true);
	std::vector<core::StableIdentifier> child_ids =
		storage_detail.include_nested
			? projection->second.nested_descendant_storage_ids
			: projection->second.direct_child_storage_ids;
	std::size_t preview_candidate_count{};
	std::vector<CompactStorageCardContent> storage_cards;
	storage_cards.reserve(child_ids.size());
	for (const core::StableIdentifier& child_id : child_ids) {
		const persistence::StorageEnvelope* child =
			catalog::find_storage_envelope(session.repository, child_id);
		if (child == nullptr)
			continue;
		const std::map<std::string, catalog::StorageProjection>::const_iterator
			child_projection =
				session.repository.storage_projections.find(child_id.value());
		if (child_projection == session.repository.storage_projections.end())
			continue;
		const ImagePreviewRequestPriority preview_priority =
			list_preview_priority(preview_candidate_count);
		if (child_projection->second.representative_usable_photo_id.has_value())
			++preview_candidate_count;
		CompactStorageCardContent card = build_storage_compact_card(
			*child, child_projection->second, preview_priority);
		card.on_activate = [this, child_id] { open_storage_detail(child_id); };
		storage_cards.push_back(std::move(card));
	}
	if (storage_cards.empty()) {
		content->add_label(
			juce_text(localization.text(
				localization::MessageId::CatalogStorageNoChildren)),
			34);
	} else {
		content->add_compact_storage_strip(
			std::move(storage_cards), localization,
			CompactStorageStripComponent::preferred_height());
	}

	content->add_label(juce_text(localization.text(
						   localization::MessageId::CatalogStorageItemsInside)),
					   36, panel_colour(), true);
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
		const ImagePreviewRequestPriority preview_priority =
			list_preview_priority(preview_candidate_count);
		if (found->second.representative_usable_photo_id.has_value())
			++preview_candidate_count;
		PreviewCardBuildResult card =
			build_item_preview_card(item, found->second, preview_priority);
		PreviewCardButtonComponent& button = content->add_preview_card(
			std::move(card.content), localization, preview_result_row_height);
		core::StableIdentifier item_id = item.record.id;
		button.onClick = [this, item_id] { open_item_detail(item_id); };
	}
	if (item_count == 0U)
		content->add_label(juce_text(localization.text(
							   localization::MessageId::CatalogStorageNoItems)),
						   34);

	content->add_label(juce_text(localization.text(
						   localization::MessageId::CatalogStorageActions)),
					   36, panel_colour(), true);
	juce::Button& add_item = content->add_button(
		juce_text(
			localization.text(localization::MessageId::CatalogStorageAddItem)),
		42);
	add_item.onClick = [this, storage_id = *route.selected_storage_id] {
		open_new_item_form(storage_id);
	};
	add_item.setEnabled(mutation_allowed);
	juce::Button& add_storage = content->add_button(
		juce_text(localization.text(
			localization::MessageId::CatalogStorageAddNestedStorage)),
		42);
	add_storage.onClick = [this, storage_id = *route.selected_storage_id] {
		open_new_storage_form(storage_id);
	};
	add_storage.setEnabled(mutation_allowed);
	juce::Button& add_photo = content->add_button(
		juce_text(localization.text(localization::MessageId::PhotoAdd)), 42);
	add_photo.onClick = [this, owner] { request_add_photos(owner); };
	add_photo.setEnabled(mutation_allowed);
	juce::Button& viewer = content->add_button(
		juce_text(localization.text(
			localization::MessageId::PhotoActionOpenStorageViewer)),
		42);
	viewer.setEnabled(
		mutation_allowed
		&& projection->second.representative_photo_id.has_value());
	viewer.onClick = [this, owner] {
		const std::optional<core::StableIdentifier> photo_id =
			selected_photo_id_for_owner(owner);
		open_photo_viewer(owner, photo_id);
	};
	juce::Button& export_current = content->add_button(
		juce_text(localization.text(
			localization::MessageId::PhotoActionExportStorageJpeg)),
		42);
	export_current.setEnabled(
		mutation_allowed
		&& selected_usable_photo_id_for_owner(owner).has_value());
	export_current.onClick = [this, owner] {
		const std::optional<core::StableIdentifier> photo_id =
			selected_usable_photo_id_for_owner(owner);
		if (photo_id)
			request_export_photo(*photo_id);
	};
	juce::Button& edit = content->add_button(
		juce_text(localization.text(
			localization::MessageId::EntityActionEditStorage)),
		42);
	edit.onClick = [this, storage_id = *route.selected_storage_id] {
		open_existing_storage_form(storage_id);
	};
	edit.setEnabled(mutation_allowed);
}

}	 // namespace shuba::ui
