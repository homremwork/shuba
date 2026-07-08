#include "UI/Screens/AppShellScreenRenderer.hpp"
#include "UI/View/AppShellContentComponent.hpp"
#include "UI/View/ScreenText.hpp"

#include "UI/Session/EntityEditSession.hpp"
#include "UI/Session/PhotoSession.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace shuba::ui {
namespace {
constexpr ImagePreviewSize viewer_preview_target_size{.max_width  = 640U,
													  .max_height = 420U};

template<typename Content>
void add_status_rows(
	Content& content, domain::ItemStatus selected_status,
	const std::function<void(domain::ItemStatus)>& choose_status) {
	content.add_inline_buttons(
		"Status",
		{InlineButtonRowComponent::Action{
			 .label = selected_status == domain::ItemStatus::Draft ? "* Draft"
																   : "Draft",
			 .handler =
				 [choose_status] { choose_status(domain::ItemStatus::Draft); }},
		 InlineButtonRowComponent::Action{
			 .label = selected_status == domain::ItemStatus::Planned ? "* Plan"
																	 : "Plan",
			 .handler =
				 [choose_status] {
		choose_status(domain::ItemStatus::Planned);
	}},
		 InlineButtonRowComponent::Action{
			 .label = selected_status == domain::ItemStatus::Listed ? "* List"
																	: "List",
			 .handler =
				 [choose_status] {
		choose_status(domain::ItemStatus::Listed);
	}},
		 InlineButtonRowComponent::Action{
			 .label = selected_status == domain::ItemStatus::Sold ? "* Sold"
																  : "Sold",
			 .handler =
				 [choose_status] { choose_status(domain::ItemStatus::Sold); }},
		 InlineButtonRowComponent::Action{
			 .label = selected_status == domain::ItemStatus::Archived ? "* Arch"
																	  : "Arch",
			 .handler =
				 [choose_status] {
		choose_status(domain::ItemStatus::Archived);
	}}},
		42);
}
template<typename Content>
void add_tag_rows(Content& content, std::vector<domain::TagRow>& tags,
				  const std::function<void()>& refresh) {
	content.add_label(juce_text(tag_row_count_summary(tags)), 38,
					  panel_colour(), true);
	if (tags.empty())
		content.add_label(
			"No tags yet. Add a row for brand, size, condition, or any custom "
			"fact.",
			50, panel_colour());

	for (std::size_t index = 0; index < tags.size(); ++index) {
		content.add_tag_editor_row(
			index, tags[index],
			[&tags](std::size_t changed_index, domain::TagRow changed_tag) {
			if (changed_index < tags.size())
				tags[changed_index] = std::move(changed_tag);
		}, [&tags, refresh](std::size_t removed_index) {
			if (removed_index >= tags.size())
				return;
			tags.erase(tags.begin()
					   + static_cast<std::ptrdiff_t>(removed_index));
			refresh();
		}, 52);
	}

	content.add_inline_buttons(
		"Tags",
		{InlineButtonRowComponent::Action{.label = "Add row",
										  .handler =
											  [&tags, refresh] {
		tags.push_back(domain::TagRow{});
		refresh();
	}},
		 InlineButtonRowComponent::Action{.label = "Clear all",
										  .handler =
											  [&tags, refresh] {
		tags.clear();
		refresh();
	},
										  .enabled = !tags.empty()}},
		40);
}
}	 // namespace

void AppShellScreenRenderer::build_item_form_content() {
	std::optional<domain::PhotoOwner> owner;
	if (item_form.mode == FormMode::Edit && item_form.draft.existing_id) {
		owner = domain::PhotoOwner{.type = domain::PhotoOwnerType::Item,
								   .id	 = *item_form.draft.existing_id};
	}
	add_photo_management_deck(
		owner, item_form.pending_photos, item_form.photo_deck,
		[this] { request_add_pending_item_photos(); }, [this] {
		cleanup_item_pending_photos();
		refresh_all();
	}, [this](std::size_t index) {
		remove_item_pending_photo(index);
	}, [this](std::size_t index) { set_item_pending_photo_as_main(index); });

	content->add_editor_pair(item_name_editor, "Display name (required)",
							 item_category_editor, "Category (required)", 54);
	item_name_editor.onTextChange = [this] {
		item_form.draft.display_name = item_name_editor.getText().toStdString();
	};
	item_category_editor.onTextChange = [this] {
		item_form.draft.category = item_category_editor.getText().toStdString();
	};

	content->add_inline_buttons(
		juce_text(
			"Storage: "
			+ storage_label(session.repository, item_form.draft.storage_id)),
		{InlineButtonRowComponent::Action{
			 .label = item_form.storage_candidates_expanded ? "Hide choices"
					  : item_form.draft.storage_id			? "Change"
															: "Choose",
			 .handler =
				 [this] {
		item_form.storage_candidates_expanded =
			!item_form.storage_candidates_expanded;
		refresh_all();
	}},
		 InlineButtonRowComponent::Action{
			 .label = "Clear",
			 .handler =
				 [this] {
		item_form.draft.storage_id.reset();
		item_form.storage_candidates_expanded = false;
		refresh_all();
	},
			 .enabled = item_form.draft.storage_id.has_value()}},
		42);

	if (item_form.storage_candidates_expanded) {
		std::vector<ButtonGridComponent::Action> storage_actions;
		storage_actions.push_back(ButtonGridComponent::Action{
			.label = "Unassigned storage", .handler = [this] {
			item_form.draft.storage_id.reset();
			item_form.storage_candidates_expanded = false;
			refresh_all();
		}});
		for (const persistence::StorageEnvelope& storage :
			 session.repository.storages) {
			const core::StableIdentifier storage_id = storage.record.id;
			storage_actions.push_back(ButtonGridComponent::Action{
				.label = juce_text(
					storage_choice_label(session.repository, storage)),
				.handler = [this, storage_id] {
				item_form.draft.storage_id			  = storage_id;
				item_form.storage_candidates_expanded = false;
				refresh_all();
			}});
		}
		const int storage_choices_height =
			ButtonGridComponent::preferred_height(
				static_cast<int>(storage_actions.size()), 2);
		content->add_button_grid("Storage choices", std::move(storage_actions),
								 2, storage_choices_height);
	}

	add_status_rows(*content, item_form.draft.status,
					[this](domain::ItemStatus status) {
		item_form.draft.status = status;
		refresh_all();
	});

	content->add_inline_buttons(
		juce_text(tag_row_count_summary(item_form.draft.tags)),
		{InlineButtonRowComponent::Action{.label = "Add row",
										  .handler =
											  [this] {
		item_form.draft.tags.push_back(domain::TagRow{});
		refresh_all();
	}},
		 InlineButtonRowComponent::Action{
			 .label =
				 item_form.tag_candidates_expanded ? "Hide keys" : "Key hints",
			 .handler =
				 [this] {
		item_form.tag_candidates_expanded = !item_form.tag_candidates_expanded;
		refresh_all();
	}},
		 InlineButtonRowComponent::Action{
			 .label = "Clear",
			 .handler =
				 [this] {
		item_form.draft.tags.clear();
		refresh_all();
	},
			 .enabled = !item_form.draft.tags.empty()}},
		42);

	if (item_form.tag_candidates_expanded) {
		const TagKeyCandidateGroups groups =
			derive_tag_key_candidate_groups(session.repository);
		if (groups.item_keys.empty() && groups.storage_keys.empty()) {
			content->add_label("No reusable tag keys in this catalog yet.", 34,
							   panel_colour());
		}
		if (!groups.item_keys.empty()) {
			std::vector<ButtonGridComponent::Action> item_key_actions;
			item_key_actions.reserve(groups.item_keys.size());
			for (const std::string& key : groups.item_keys) {
				item_key_actions.push_back(ButtonGridComponent::Action{
					.label = juce_text(key), .handler = [this, key] {
					apply_tag_key_candidate(item_form.draft.tags, key);
					refresh_all();
				}});
			}
			const int item_key_height = ButtonGridComponent::preferred_height(
				static_cast<int>(item_key_actions.size()), 3);
			content->add_button_grid("Item tag keys",
									 std::move(item_key_actions), 3,
									 item_key_height);
		}
		if (!groups.storage_keys.empty()) {
			std::vector<ButtonGridComponent::Action> storage_key_actions;
			storage_key_actions.reserve(groups.storage_keys.size());
			for (const std::string& key : groups.storage_keys) {
				storage_key_actions.push_back(ButtonGridComponent::Action{
					.label = juce_text(key), .handler = [this, key] {
					apply_tag_key_candidate(item_form.draft.tags, key);
					refresh_all();
				}});
			}
			const int storage_key_height =
				ButtonGridComponent::preferred_height(
					static_cast<int>(storage_key_actions.size()), 3);
			content->add_button_grid("Storage tag keys",
									 std::move(storage_key_actions), 3,
									 storage_key_height);
		}
	}

	if (item_form.draft.tags.empty())
		content->add_label("No tags yet. Use Key hints or Add row.", 34,
						   panel_colour());
	for (std::size_t index = 0; index < item_form.draft.tags.size(); ++index) {
		content->add_tag_editor_row(
			index, item_form.draft.tags[index],
			[this](std::size_t changed_index, domain::TagRow changed_tag) {
			if (changed_index < item_form.draft.tags.size())
				item_form.draft.tags[changed_index] = std::move(changed_tag);
		}, [this](std::size_t removed_index) {
			if (removed_index >= item_form.draft.tags.size())
				return;
			item_form.draft.tags.erase(
				item_form.draft.tags.begin()
				+ static_cast<std::ptrdiff_t>(removed_index));
			refresh_all();
		}, 52);
	}

	content->add_editor(item_notes_editor, "Notes", 80, true).onTextChange =
		[this] {
		item_form.draft.notes = item_notes_editor.getText().toStdString();
	};

	content->add_label(juce_text(listing_summary(item_form.draft.listing)), 46,
					   panel_colour(), !item_form.draft.listing.empty());
	juce::Button& listing_toggle = content->add_button(
		item_form.listing_expanded ? "Collapse listing details"
								   : "Add listing details",
		38);
	listing_toggle.onClick = [this] {
		item_form.listing_expanded = !item_form.listing_expanded;
		refresh_all();
	};
	if (item_form.listing_expanded) {
		content->add_editor(item_listing_marketplace_editor, "Marketplace", 44)
			.onTextChange = [this] {
			item_form.draft.listing.marketplace =
				item_listing_marketplace_editor.getText().toStdString();
		};
		content->add_editor(item_listing_url_editor, "Listing URL", 44)
			.onTextChange = [this] {
			item_form.draft.listing.url =
				item_listing_url_editor.getText().toStdString();
		};
		content->add_editor(item_listing_note_editor, "Listing note", 70, true)
			.onTextChange = [this] {
			item_form.draft.listing.note =
				item_listing_note_editor.getText().toStdString();
		};
	}

	content->add_label(juce_text(finance_summary(item_form.draft.acquisition,
												 item_form.draft.finance)),
					   46, panel_colour(),
					   !item_form.draft.acquisition.empty()
						   || !item_form.draft.finance.empty());
	juce::Button& finance_toggle = content->add_button(
		item_form.finance_expanded ? "Collapse finance details"
								   : "Add finance details",
		38);
	finance_toggle.onClick = [this] {
		item_form.finance_expanded = !item_form.finance_expanded;
		refresh_all();
	};
	if (item_form.finance_expanded) {
		content
			->add_editor(item_acquisition_source_editor, "Acquisition source",
						 44)
			.onTextChange = [this] {
			item_form.draft.acquisition.source =
				item_acquisition_source_editor.getText().toStdString();
		};
		content->add_label(
			"Money fields stay omitted from this compact JUCE form pass; "
			"existing values are preserved when editing.",
			52, panel_colour());
	}

	if (item_form.mode == FormMode::Edit) {
		content->add_label("Edit-only actions", 34, panel_colour(), true);
		juce::Button& archive = content->add_button("Archive item", 42);
		archive.onClick		  = [this] {
			if (!item_form.draft.existing_id)
				return;
			EntityEditResult result = archive_item_in_session(
				EntityEditRequest{.current_session = session,
								  .identifiers	   = edit_identifiers,
								  .clock		   = edit_clock},
				*item_form.draft.existing_id);
			apply_entity_edit_result(std::move(result));
		};
		juce::Button& hard_delete = content->add_button(
			"Hard delete disabled until deletion sequence tests pass", 46);
		hard_delete.setEnabled(false);
	}
}
void AppShellScreenRenderer::build_storage_form_content() {
	std::optional<domain::PhotoOwner> owner;
	if (storage_form.mode == FormMode::Edit && storage_form.draft.existing_id) {
		owner = domain::PhotoOwner{.type = domain::PhotoOwnerType::Storage,
								   .id	 = *storage_form.draft.existing_id};
	}
	add_photo_management_deck(
		owner, storage_form.pending_photos, storage_form.photo_deck,
		[this] { request_add_pending_storage_photos(); }, [this] {
		cleanup_storage_pending_photos();
		refresh_all();
	}, [this](std::size_t index) {
		remove_storage_pending_photo(index);
	}, [this](std::size_t index) { set_storage_pending_photo_as_main(index); });

	content->add_editor_pair(storage_name_editor, "Display name (required)",
							 storage_type_editor, "Storage type (required)",
							 54);
	storage_name_editor.onTextChange = [this] {
		storage_form.draft.display_name =
			storage_name_editor.getText().toStdString();
	};
	storage_type_editor.onTextChange = [this] {
		storage_form.draft.storage_type =
			storage_type_editor.getText().toStdString();
	};

	content->add_inline_buttons(
		juce_text("Parent: "
				  + storage_label(session.repository,
								  storage_form.draft.parent_storage_id)),
		{InlineButtonRowComponent::Action{
			 .label = storage_form.parent_candidates_expanded ? "Hide choices"
					  : storage_form.draft.parent_storage_id  ? "Change"
															  : "Choose",
			 .handler =
				 [this] {
		storage_form.parent_candidates_expanded =
			!storage_form.parent_candidates_expanded;
		refresh_all();
	}},
		 InlineButtonRowComponent::Action{
			 .label = "Clear",
			 .handler =
				 [this] {
		storage_form.draft.parent_storage_id.reset();
		storage_form.parent_candidates_expanded = false;
		refresh_all();
	},
			 .enabled = storage_form.draft.parent_storage_id.has_value()}},
		42);

	if (storage_form.parent_candidates_expanded) {
		std::vector<ButtonGridComponent::Action> parent_actions;
		parent_actions.push_back(ButtonGridComponent::Action{
			.label = "No parent storage", .handler = [this] {
			storage_form.draft.parent_storage_id.reset();
			storage_form.parent_candidates_expanded = false;
			refresh_all();
		}});
		for (const persistence::StorageEnvelope& storage :
			 session.repository.storages) {
			if (storage_form.draft.existing_id
				&& storage.record.id == *storage_form.draft.existing_id) {
				continue;
			}
			const core::StableIdentifier storage_id = storage.record.id;
			parent_actions.push_back(ButtonGridComponent::Action{
				.label = juce_text(
					storage_choice_label(session.repository, storage)),
				.handler = [this, storage_id] {
				storage_form.draft.parent_storage_id	= storage_id;
				storage_form.parent_candidates_expanded = false;
				refresh_all();
			}});
		}
		const int parent_choices_height = ButtonGridComponent::preferred_height(
			static_cast<int>(parent_actions.size()), 2);
		content->add_button_grid("Parent choices", std::move(parent_actions), 2,
								 parent_choices_height);
	}

	content->add_editor(storage_location_editor, "Physical location", 46)
		.onTextChange = [this] {
		storage_form.draft.location =
			storage_location_editor.getText().toStdString();
	};

	content->add_inline_buttons(
		juce_text(tag_row_count_summary(storage_form.draft.tags)),
		{InlineButtonRowComponent::Action{.label = "Add row",
										  .handler =
											  [this] {
		storage_form.draft.tags.push_back(domain::TagRow{});
		refresh_all();
	}},
		 InlineButtonRowComponent::Action{
			 .label = storage_form.tag_candidates_expanded ? "Hide keys"
														   : "Key hints",
			 .handler =
				 [this] {
		storage_form.tag_candidates_expanded =
			!storage_form.tag_candidates_expanded;
		refresh_all();
	}},
		 InlineButtonRowComponent::Action{
			 .label = "Clear",
			 .handler =
				 [this] {
		storage_form.draft.tags.clear();
		refresh_all();
	},
			 .enabled = !storage_form.draft.tags.empty()}},
		42);

	if (storage_form.tag_candidates_expanded) {
		const TagKeyCandidateGroups groups =
			derive_tag_key_candidate_groups(session.repository);
		if (groups.item_keys.empty() && groups.storage_keys.empty()) {
			content->add_label("No reusable tag keys in this catalog yet.", 34,
							   panel_colour());
		}
		if (!groups.item_keys.empty()) {
			std::vector<ButtonGridComponent::Action> item_key_actions;
			item_key_actions.reserve(groups.item_keys.size());
			for (const std::string& key : groups.item_keys) {
				item_key_actions.push_back(ButtonGridComponent::Action{
					.label = juce_text(key), .handler = [this, key] {
					apply_tag_key_candidate(storage_form.draft.tags, key);
					refresh_all();
				}});
			}
			const int storage_form_item_key_height =
				ButtonGridComponent::preferred_height(
					static_cast<int>(item_key_actions.size()), 3);
			content->add_button_grid("Item tag keys",
									 std::move(item_key_actions), 3,
									 storage_form_item_key_height);
		}
		if (!groups.storage_keys.empty()) {
			std::vector<ButtonGridComponent::Action> storage_key_actions;
			storage_key_actions.reserve(groups.storage_keys.size());
			for (const std::string& key : groups.storage_keys) {
				storage_key_actions.push_back(ButtonGridComponent::Action{
					.label = juce_text(key), .handler = [this, key] {
					apply_tag_key_candidate(storage_form.draft.tags, key);
					refresh_all();
				}});
			}
			const int storage_form_storage_key_height =
				ButtonGridComponent::preferred_height(
					static_cast<int>(storage_key_actions.size()), 3);
			content->add_button_grid("Storage tag keys",
									 std::move(storage_key_actions), 3,
									 storage_form_storage_key_height);
		}
	}

	if (storage_form.draft.tags.empty())
		content->add_label("No tags yet. Use Key hints or Add row.", 34,
						   panel_colour());
	for (std::size_t index = 0; index < storage_form.draft.tags.size();
		 ++index) {
		content->add_tag_editor_row(
			index, storage_form.draft.tags[index],
			[this](std::size_t changed_index, domain::TagRow changed_tag) {
			if (changed_index < storage_form.draft.tags.size())
				storage_form.draft.tags[changed_index] = std::move(changed_tag);
		}, [this](std::size_t removed_index) {
			if (removed_index >= storage_form.draft.tags.size())
				return;
			storage_form.draft.tags.erase(
				storage_form.draft.tags.begin()
				+ static_cast<std::ptrdiff_t>(removed_index));
			refresh_all();
		}, 52);
	}

	content->add_editor(storage_notes_editor, "Notes", 80, true).onTextChange =
		[this] {
		storage_form.draft.notes = storage_notes_editor.getText().toStdString();
	};
	if (storage_form.mode == FormMode::Edit) {
		content->add_label("Edit-only actions", 34, panel_colour(), true);
		juce::Button& archive = content->add_button(
			storage_form.draft.lifecycle_status
					== domain::StorageLifecycleStatus::Archived
				? "Storage archived"
				: "Archive storage",
			42);
		archive.onClick = [this] {
			storage_form.draft.lifecycle_status =
				domain::StorageLifecycleStatus::Archived;
			storage_form.draft.archive_warning_acknowledged =
				storage_form.archive_warning_acknowledged;
			refresh_all();
		};
		juce::ToggleButton& acknowledge = content->add_toggle(
			"Confirm archive-with-contents warning if shown",
			storage_form.archive_warning_acknowledged, 38);
		acknowledge.onClick = [this] {
			storage_form.archive_warning_acknowledged =
				!storage_form.archive_warning_acknowledged;
			storage_form.draft.archive_warning_acknowledged =
				storage_form.archive_warning_acknowledged;
			refresh_all();
		};
		juce::Button& hard_delete = content->add_button(
			"Hard delete disabled until deletion sequence tests pass", 46);
		hard_delete.setEnabled(false);
	}
}

void AppShellScreenRenderer::build_photo_viewer_content() {
	if (!route.selected_photo_owner) {
		content->add_label("No photo owner selected.", 54, panel_colour(),
						   true);
		return;
	}
	const domain::PhotoOwner owner = *route.selected_photo_owner;
	const catalog::OwnerPhotoProjection* projection =
		owner_photo_projection(session.repository, owner);
	if (projection == nullptr || projection->ordered_photo_ids.empty()) {
		content->add_label(juce_text(owner_caption(session.repository, owner)),
						   50, surface_colour(), true);
		content->add_label(
			"No photo records for this owner yet. Add photos to import into "
			"app-private JPEG XL storage.",
			70, panel_colour(), true);
		juce::Button& add_photo = content->add_button("Add photos", 42);
		add_photo.onClick		= [this, owner] { request_add_photos(owner); };
		return;
	}

	if (!route.selected_photo_id
		|| !find_photo_index_in_order(projection->ordered_photo_ids,
									  *route.selected_photo_id)) {
		route.selected_photo_id =
			first_viewable_photo_id(session.repository, owner);
	}
	if (!route.selected_photo_id) {
		content->add_label("No selected photo.", 54, panel_colour(), true);
		return;
	}

	const persistence::PhotoEnvelope* photo = catalog::find_photo_envelope(
		session.repository, *route.selected_photo_id);
	if (photo == nullptr) {
		content->add_label(
			"Selected photo record is missing from the accepted catalog.", 64,
			warning_panel_colour(), true);
		return;
	}

	const std::optional<std::size_t> index = find_photo_index_in_order(
		projection->ordered_photo_ids, *route.selected_photo_id);
	const std::size_t position = index.has_value() ? *index + 1U : 1U;
	const std::size_t total	   = projection->ordered_photo_ids.size();
	content->add_label(juce_text(owner_caption(session.repository, owner)), 48,
					   surface_colour(), true);
	if (photo_display.viewer_transform_photo_id != route.selected_photo_id) {
		photo_display.viewer_transform_photo_id		= route.selected_photo_id;
		photo_display.viewer_rotation_quarter_turns = 0;
	}

	if (photo_display.displayed_photo_id != route.selected_photo_id) {
		if (photo_display.requested_display_photo_id
			!= route.selected_photo_id) {
			photo_display.result = catalog::PhotoDisplayResult{};
			if (request_photo_display_handler)
				request_photo_display_handler(*route.selected_photo_id);
		}
	}

	juce::Image image;
	juce::String placeholder{"Loading full photo in background."};
	PreviewImageVisualState viewer_state{PreviewImageVisualState::Loading};
	if (photo_display.result.succeeded()
		&& photo_display.result.pixels.has_value()) {
		image = juce_image_from_pixels(*photo_display.result.pixels);
		if (image.isValid()) {
			placeholder	 = "";
			viewer_state = PreviewImageVisualState::Loaded;
		} else {
			placeholder	 = "Decoded image cannot be displayed.";
			viewer_state = PreviewImageVisualState::Broken;
		}
	} else {
		const ImagePreviewRenderState preview = load_internal_preview_image(
			*route.selected_photo_id, viewer_preview_target_size,
			ImagePreviewRequestPriority::High);
		image = preview.image;
		if (!preview.placeholder.isEmpty())
			placeholder = preview.placeholder;
		viewer_state = preview.state;
		if (image.isValid()) {
			placeholder	 = "";
			viewer_state = PreviewImageVisualState::Loaded;
		} else if (viewer_state == PreviewImageVisualState::Broken) {
			placeholder = preview.placeholder.isEmpty()
							  ? "Cached preview cannot be displayed."
							  : preview.placeholder;
		}

		if (total > 1U) {
			const std::optional<core::StableIdentifier> previous_id =
				adjacent_photo_id(session.repository, owner,
								  *route.selected_photo_id, -1);
			const std::optional<core::StableIdentifier> next_id =
				adjacent_photo_id(session.repository, owner,
								  *route.selected_photo_id, 1);
			if (previous_id.has_value()) {
				(void)load_internal_preview_image(
					*previous_id, viewer_preview_target_size,
					ImagePreviewRequestPriority::Normal);
			}
			if (next_id.has_value() && next_id != previous_id) {
				(void)load_internal_preview_image(
					*next_id, viewer_preview_target_size,
					ImagePreviewRequestPriority::Normal);
			}
		}
	}
	if (image.isValid()) {
		placeholder = "";
	} else if (photo_display.result.placeholder.has_value()) {
		placeholder	 = juce_text(photo_display.result.placeholder->message);
		viewer_state = PreviewImageVisualState::Broken;
	} else if (photo_display.result.was_user_cancelled()) {
		placeholder	 = "Photo display was cancelled.";
		viewer_state = PreviewImageVisualState::Broken;
	} else {
		placeholder =
			"Photo preview placeholder. Full decode is only attempted "
			"for this viewer, not for result lists.";
	}

	std::function<void(int)> select_adjacent = [this, owner](int direction) {
		if (!route.selected_photo_id)
			return;
		const std::optional<core::StableIdentifier> adjacent_id =
			adjacent_photo_id(session.repository, owner,
							  *route.selected_photo_id, direction);
		if (!adjacent_id.has_value())
			return;
		route.selected_photo_id = adjacent_id;
		photo_display.displayed_photo_id.reset();
		photo_display.viewer_transform_photo_id.reset();
		photo_display.viewer_rotation_quarter_turns = 0;
		refresh_all();
	};
	std::function<void(int)> rotate_viewer = [this](int direction) {
		int rotation = photo_display.viewer_rotation_quarter_turns + direction;
		rotation %= 4;
		if (rotation < 0)
			rotation += 4;
		photo_display.viewer_rotation_quarter_turns = rotation;
		refresh_content();
	};

	PhotoViewerImageModel viewer_model;
	viewer_model.image	 = image;
	viewer_model.title	 = juce_text(photo_summary(*photo, position, total));
	viewer_model.caption = juce_text(
		owner_caption(session.repository, owner)
		+ " · double-tap to zoom, drag when zoomed, swipe when fitted");
	viewer_model.placeholder = placeholder;
	viewer_model.state		 = viewer_state;
	viewer_model.rotation_quarter_turns =
		photo_display.viewer_rotation_quarter_turns;
	viewer_model.multiple_photos = total > 1U;
	PhotoViewerImageHandlers viewer_handlers{
		.select_previous = [select_adjacent] { select_adjacent(-1); },
		.select_next	 = [select_adjacent] { select_adjacent(1); }};
	const int viewer_height =
		std::max(360, content->viewport_height_hint() - 12);
	content->add_photo_viewer_image(std::move(viewer_model),
									std::move(viewer_handlers), viewer_height);

	const bool multiple_photos = total > 1U;
	const bool rotate_enabled  = image.isValid();
	std::vector<ButtonGridComponent::Action> viewer_actions;
	viewer_actions.push_back(ButtonGridComponent::Action{
		.label = "Previous", .handler = [select_adjacent] {
		select_adjacent(-1);
	}, .enabled = multiple_photos});
	viewer_actions.push_back(ButtonGridComponent::Action{
		.label = "Next", .handler = [select_adjacent] {
		select_adjacent(1);
	}, .enabled = multiple_photos});
	viewer_actions.push_back(ButtonGridComponent::Action{
		.label = "Rotate left", .handler = [rotate_viewer] {
		rotate_viewer(-1);
	}, .enabled = rotate_enabled});
	viewer_actions.push_back(ButtonGridComponent::Action{
		.label = "Rotate right", .handler = [rotate_viewer] {
		rotate_viewer(1);
	}, .enabled = rotate_enabled});
	content->add_button_grid("Viewer controls", std::move(viewer_actions), 2,
							 ButtonGridComponent::preferred_height(4, 2));

	if (!feedback.photo_message.empty()) {
		content->add_label(juce_text(feedback.photo_message), 54,
						   accent_colour().withAlpha(0.34f), true);
	}
	if (!feedback.photo_diagnostics.empty()) {
		content->add_label(
			juce_text(core_diagnostic_summary(feedback.photo_diagnostics)), 76,
			warning_panel_colour(), true);
	}
	if (!photo_display.result.diagnostics.empty()) {
		content->add_label(juce_text(core_diagnostic_summary(
							   photo_display.result.diagnostics)),
						   76, warning_panel_colour(), true);
	}
	content->add_label(
		juce_text(progress_summary(last_progress_events.events())), 50,
		panel_colour());

	juce::Button& set_main = content->add_button(
		photo->record.is_main ? "Already main photo" : "Set as main", 42);
	set_main.setEnabled(!photo->record.is_main);
	set_main.onClick = [this, photo_id = *route.selected_photo_id] {
		EntityEditResult result = set_main_photo_in_session(
			EntityEditRequest{.current_session = session,
							  .identifiers	   = edit_identifiers,
							  .clock		   = edit_clock},
			photo_id);
		apply_photo_edit_result(std::move(result), photo_id);
	};
	juce::Button& export_button =
		content->add_button("Export current photo as JPEG", 42);
	export_button.setEnabled(photo_display.result.succeeded());
	export_button.onClick = [this, photo_id = *route.selected_photo_id] {
		request_export_photo(photo_id);
	};
	if (photo_display.pending_delete_photo_id.has_value()
		&& *photo_display.pending_delete_photo_id == *route.selected_photo_id) {
		content->add_inline_buttons(
			"Delete selected photo? Metadata is removed first.",
			{InlineButtonRowComponent::Action{
				 .label = "Confirm delete",
				 .handler =
					 [this, photo_id = *route.selected_photo_id] {
			confirm_delete_photo(photo_id);
		}},
			 InlineButtonRowComponent::Action{
				 .label	  = "Cancel",
				 .handler = [this] { cancel_delete_photo(); }}},
			44);
	} else {
		juce::Button& delete_button =
			content->add_button("Delete current photo", 42);
		delete_button.onClick = [this, photo_id = *route.selected_photo_id] {
			request_delete_photo(photo_id);
		};
	}
	juce::Button& add_photo = content->add_button("Add more photos", 42);
	add_photo.onClick		= [this, owner] { request_add_photos(owner); };
}

}	 // namespace shuba::ui
