#include "UI/AppShell.hpp"
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
template<typename Content>
void add_status_rows(Content& content, domain::ItemStatus selected_status,
					 std::function<void(domain::ItemStatus)> choose_status) {
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
				  std::function<void()> refresh) {
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

void AppShellComponent::build_item_form_content() {
	content->add_pending_photo_strip(item_form_pending_photos, [this] {
		request_add_pending_item_photos();
	}, [this] {
		cleanup_item_pending_photos();
		refresh_all();
	}, [this](std::size_t index) { remove_item_pending_photo(index); }, 104);

	content->add_editor_pair(item_name_editor, "Display name (required)",
							 item_category_editor, "Category (required)", 54);
	item_name_editor.onTextChange = [this] {
		item_form_draft.display_name = item_name_editor.getText().toStdString();
	};
	item_category_editor.onTextChange = [this] {
		item_form_draft.category = item_category_editor.getText().toStdString();
	};

	content->add_inline_buttons(
		juce_text(
			"Storage: "
			+ storage_label(session.repository, item_form_draft.storage_id)),
		{InlineButtonRowComponent::Action{
			 .label = item_storage_candidates_expanded ? "Hide choices"
					  : item_form_draft.storage_id	   ? "Change"
													   : "Choose",
			 .handler =
				 [this] {
		item_storage_candidates_expanded = !item_storage_candidates_expanded;
		refresh_all();
	}},
		 InlineButtonRowComponent::Action{
			 .label = "Clear",
			 .handler =
				 [this] {
		item_form_draft.storage_id.reset();
		item_storage_candidates_expanded = false;
		refresh_all();
	},
			 .enabled = item_form_draft.storage_id.has_value()}},
		42);

	if (item_storage_candidates_expanded) {
		std::vector<ButtonGridComponent::Action> storage_actions;
		storage_actions.push_back(ButtonGridComponent::Action{
			.label = "Unassigned storage", .handler = [this] {
			item_form_draft.storage_id.reset();
			item_storage_candidates_expanded = false;
			refresh_all();
		}});
		for (const persistence::StorageEnvelope& storage :
			 session.repository.storages) {
			const core::StableIdentifier storage_id = storage.record.id;
			storage_actions.push_back(ButtonGridComponent::Action{
				.label = juce_text(
					storage_choice_label(session.repository, storage)),
				.handler = [this, storage_id] {
				item_form_draft.storage_id		 = storage_id;
				item_storage_candidates_expanded = false;
				refresh_all();
			}});
		}
		const int storage_choices_height =
			ButtonGridComponent::preferred_height(
				static_cast<int>(storage_actions.size()), 2);
		content->add_button_grid("Storage choices", std::move(storage_actions),
								 2, storage_choices_height);
	}

	add_status_rows(*content, item_form_draft.status,
					[this](domain::ItemStatus status) {
		item_form_draft.status = status;
		refresh_all();
	});

	content->add_inline_buttons(
		juce_text(tag_row_count_summary(item_form_draft.tags)),
		{InlineButtonRowComponent::Action{.label = "Add row",
										  .handler =
											  [this] {
		item_form_draft.tags.push_back(domain::TagRow{});
		refresh_all();
	}},
		 InlineButtonRowComponent::Action{
			 .label = item_tag_candidates_expanded ? "Hide keys" : "Key hints",
			 .handler =
				 [this] {
		item_tag_candidates_expanded = !item_tag_candidates_expanded;
		refresh_all();
	}},
		 InlineButtonRowComponent::Action{
			 .label = "Clear",
			 .handler =
				 [this] {
		item_form_draft.tags.clear();
		refresh_all();
	},
			 .enabled = !item_form_draft.tags.empty()}},
		42);

	if (item_tag_candidates_expanded) {
		const TagKeyCandidateGroups groups =
			derive_tag_key_candidate_groups(session.repository);
		if (groups.item_keys.empty() && groups.storage_keys.empty()) {
			content->add_label("No reusable tag keys in this catalog yet.", 34,
							   panel_colour());
		}
		if (!groups.item_keys.empty()) {
			std::vector<ButtonGridComponent::Action> item_key_actions;
			for (const std::string& key : groups.item_keys) {
				item_key_actions.push_back(ButtonGridComponent::Action{
					.label = juce_text(key), .handler = [this, key] {
					apply_tag_key_candidate(item_form_draft.tags, key);
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
			for (const std::string& key : groups.storage_keys) {
				storage_key_actions.push_back(ButtonGridComponent::Action{
					.label = juce_text(key), .handler = [this, key] {
					apply_tag_key_candidate(item_form_draft.tags, key);
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

	if (item_form_draft.tags.empty())
		content->add_label("No tags yet. Use Key hints or Add row.", 34,
						   panel_colour());
	for (std::size_t index = 0; index < item_form_draft.tags.size(); ++index) {
		content->add_tag_editor_row(
			index, item_form_draft.tags[index],
			[this](std::size_t changed_index, domain::TagRow changed_tag) {
			if (changed_index < item_form_draft.tags.size())
				item_form_draft.tags[changed_index] = std::move(changed_tag);
		}, [this](std::size_t removed_index) {
			if (removed_index >= item_form_draft.tags.size())
				return;
			item_form_draft.tags.erase(
				item_form_draft.tags.begin()
				+ static_cast<std::ptrdiff_t>(removed_index));
			refresh_all();
		}, 52);
	}

	content->add_editor(item_notes_editor, "Notes", 80, true).onTextChange =
		[this] {
		item_form_draft.notes = item_notes_editor.getText().toStdString();
	};

	content->add_label(juce_text(listing_summary(item_form_draft.listing)), 46,
					   panel_colour(), !item_form_draft.listing.empty());
	juce::Button& listing_toggle =
		content->add_button(item_listing_expanded ? "Collapse listing details"
												  : "Add listing details",
							38);
	listing_toggle.onClick = [this] {
		item_listing_expanded = !item_listing_expanded;
		refresh_all();
	};
	if (item_listing_expanded) {
		content->add_editor(item_listing_marketplace_editor, "Marketplace", 44)
			.onTextChange = [this] {
			item_form_draft.listing.marketplace =
				item_listing_marketplace_editor.getText().toStdString();
		};
		content->add_editor(item_listing_url_editor, "Listing URL", 44)
			.onTextChange = [this] {
			item_form_draft.listing.url =
				item_listing_url_editor.getText().toStdString();
		};
		content->add_editor(item_listing_note_editor, "Listing note", 70, true)
			.onTextChange = [this] {
			item_form_draft.listing.note =
				item_listing_note_editor.getText().toStdString();
		};
	}

	content->add_label(juce_text(finance_summary(item_form_draft.acquisition,
												 item_form_draft.finance)),
					   46, panel_colour(),
					   !item_form_draft.acquisition.empty()
						   || !item_form_draft.finance.empty());
	juce::Button& finance_toggle =
		content->add_button(item_finance_expanded ? "Collapse finance details"
												  : "Add finance details",
							38);
	finance_toggle.onClick = [this] {
		item_finance_expanded = !item_finance_expanded;
		refresh_all();
	};
	if (item_finance_expanded) {
		content
			->add_editor(item_acquisition_source_editor, "Acquisition source",
						 44)
			.onTextChange = [this] {
			item_form_draft.acquisition.source =
				item_acquisition_source_editor.getText().toStdString();
		};
		content->add_label(
			"Money fields stay omitted from this compact JUCE form pass; "
			"existing values are preserved when editing.",
			52, panel_colour());
	}

	if (item_form_mode == FormMode::Edit) {
		content->add_label("Edit-only actions", 34, panel_colour(), true);
		juce::Button& archive = content->add_button("Archive item", 42);
		archive.onClick		  = [this] {
			if (!item_form_draft.existing_id)
				return;
			EntityEditResult result = archive_item_in_session(
				EntityEditRequest{.current_session = session,
								  .identifiers	   = edit_identifiers,
								  .clock		   = edit_clock},
				*item_form_draft.existing_id);
			apply_entity_edit_result(std::move(result));
		};
		juce::Button& hard_delete = content->add_button(
			"Hard delete disabled until deletion sequence tests pass", 46);
		hard_delete.setEnabled(false);
	}
}
void AppShellComponent::build_storage_form_content() {
	content->add_editor_pair(storage_name_editor, "Display name (required)",
							 storage_type_editor, "Storage type (required)",
							 54);
	storage_name_editor.onTextChange = [this] {
		storage_form_draft.display_name =
			storage_name_editor.getText().toStdString();
	};
	storage_type_editor.onTextChange = [this] {
		storage_form_draft.storage_type =
			storage_type_editor.getText().toStdString();
	};

	content->add_inline_buttons(
		juce_text("Parent: "
				  + storage_label(session.repository,
								  storage_form_draft.parent_storage_id)),
		{InlineButtonRowComponent::Action{
			 .label = storage_parent_candidates_expanded	 ? "Hide choices"
					  : storage_form_draft.parent_storage_id ? "Change"
															 : "Choose",
			 .handler =
				 [this] {
		storage_parent_candidates_expanded =
			!storage_parent_candidates_expanded;
		refresh_all();
	}},
		 InlineButtonRowComponent::Action{
			 .label = "Clear",
			 .handler =
				 [this] {
		storage_form_draft.parent_storage_id.reset();
		storage_parent_candidates_expanded = false;
		refresh_all();
	},
			 .enabled = storage_form_draft.parent_storage_id.has_value()}},
		42);

	if (storage_parent_candidates_expanded) {
		std::vector<ButtonGridComponent::Action> parent_actions;
		parent_actions.push_back(ButtonGridComponent::Action{
			.label = "No parent storage", .handler = [this] {
			storage_form_draft.parent_storage_id.reset();
			storage_parent_candidates_expanded = false;
			refresh_all();
		}});
		for (const persistence::StorageEnvelope& storage :
			 session.repository.storages) {
			if (storage_form_draft.existing_id
				&& storage.record.id == *storage_form_draft.existing_id) {
				continue;
			}
			const core::StableIdentifier storage_id = storage.record.id;
			parent_actions.push_back(ButtonGridComponent::Action{
				.label = juce_text(
					storage_choice_label(session.repository, storage)),
				.handler = [this, storage_id] {
				storage_form_draft.parent_storage_id = storage_id;
				storage_parent_candidates_expanded	 = false;
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
		storage_form_draft.location =
			storage_location_editor.getText().toStdString();
	};

	content->add_inline_buttons(
		juce_text(tag_row_count_summary(storage_form_draft.tags)),
		{InlineButtonRowComponent::Action{.label = "Add row",
										  .handler =
											  [this] {
		storage_form_draft.tags.push_back(domain::TagRow{});
		refresh_all();
	}},
		 InlineButtonRowComponent::Action{
			 .label =
				 storage_tag_candidates_expanded ? "Hide keys" : "Key hints",
			 .handler =
				 [this] {
		storage_tag_candidates_expanded = !storage_tag_candidates_expanded;
		refresh_all();
	}},
		 InlineButtonRowComponent::Action{
			 .label = "Clear",
			 .handler =
				 [this] {
		storage_form_draft.tags.clear();
		refresh_all();
	},
			 .enabled = !storage_form_draft.tags.empty()}},
		42);

	if (storage_tag_candidates_expanded) {
		const TagKeyCandidateGroups groups =
			derive_tag_key_candidate_groups(session.repository);
		if (groups.item_keys.empty() && groups.storage_keys.empty()) {
			content->add_label("No reusable tag keys in this catalog yet.", 34,
							   panel_colour());
		}
		if (!groups.item_keys.empty()) {
			std::vector<ButtonGridComponent::Action> item_key_actions;
			for (const std::string& key : groups.item_keys) {
				item_key_actions.push_back(ButtonGridComponent::Action{
					.label = juce_text(key), .handler = [this, key] {
					apply_tag_key_candidate(storage_form_draft.tags, key);
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
			for (const std::string& key : groups.storage_keys) {
				storage_key_actions.push_back(ButtonGridComponent::Action{
					.label = juce_text(key), .handler = [this, key] {
					apply_tag_key_candidate(storage_form_draft.tags, key);
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

	if (storage_form_draft.tags.empty())
		content->add_label("No tags yet. Use Key hints or Add row.", 34,
						   panel_colour());
	for (std::size_t index = 0; index < storage_form_draft.tags.size();
		 ++index) {
		content->add_tag_editor_row(
			index, storage_form_draft.tags[index],
			[this](std::size_t changed_index, domain::TagRow changed_tag) {
			if (changed_index < storage_form_draft.tags.size())
				storage_form_draft.tags[changed_index] = std::move(changed_tag);
		}, [this](std::size_t removed_index) {
			if (removed_index >= storage_form_draft.tags.size())
				return;
			storage_form_draft.tags.erase(
				storage_form_draft.tags.begin()
				+ static_cast<std::ptrdiff_t>(removed_index));
			refresh_all();
		}, 52);
	}

	content->add_editor(storage_notes_editor, "Notes", 80, true).onTextChange =
		[this] {
		storage_form_draft.notes = storage_notes_editor.getText().toStdString();
	};
	if (storage_form_mode == FormMode::Edit) {
		content->add_label("Edit-only actions", 34, panel_colour(), true);
		juce::Button& archive = content->add_button(
			storage_form_draft.lifecycle_status
					== domain::StorageLifecycleStatus::Archived
				? "Storage archived"
				: "Archive storage",
			42);
		archive.onClick = [this] {
			storage_form_draft.lifecycle_status =
				domain::StorageLifecycleStatus::Archived;
			storage_form_draft.archive_warning_acknowledged =
				storage_archive_warning_acknowledged;
			refresh_all();
		};
		juce::ToggleButton& acknowledge = content->add_toggle(
			"Confirm archive-with-contents warning if shown",
			storage_archive_warning_acknowledged, 38);
		acknowledge.onClick = [this] {
			storage_archive_warning_acknowledged =
				!storage_archive_warning_acknowledged;
			storage_form_draft.archive_warning_acknowledged =
				storage_archive_warning_acknowledged;
			refresh_all();
		};
		juce::Button& hard_delete = content->add_button(
			"Hard delete disabled until deletion sequence tests pass", 46);
		hard_delete.setEnabled(false);
	}
}

void AppShellComponent::build_photo_viewer_content() {
	if (!selected_photo_owner) {
		content->add_label("No photo owner selected.", 54, panel_colour(),
						   true);
		return;
	}
	const domain::PhotoOwner owner = *selected_photo_owner;
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

	if (!selected_photo_id
		|| !find_photo_index_in_order(projection->ordered_photo_ids,
									  *selected_photo_id)) {
		selected_photo_id = first_viewable_photo_id(session.repository, owner);
	}
	if (!selected_photo_id) {
		content->add_label("No selected photo.", 54, panel_colour(), true);
		return;
	}

	const persistence::PhotoEnvelope* photo =
		catalog::find_photo_envelope(session.repository, *selected_photo_id);
	if (photo == nullptr) {
		content->add_label(
			"Selected photo record is missing from the accepted catalog.", 64,
			warning_panel_colour(), true);
		return;
	}

	const std::optional<std::size_t> index = find_photo_index_in_order(
		projection->ordered_photo_ids, *selected_photo_id);
	const std::size_t position = index.has_value() ? *index + 1U : 1U;
	const std::size_t total	   = projection->ordered_photo_ids.size();
	content->add_label(juce_text(owner_caption(session.repository, owner)), 48,
					   surface_colour(), true);

	if (last_display_photo_id != selected_photo_id) {
		catalog::PhotoExportUseCase export_use_case{
			edit_identifiers, ui_operation_gate, internal_photo_codec,
			jpeg_export_service, document_export_service};
		platform::ProgressCollector display_progress;
		last_photo_display_result = export_use_case.load_photo_for_display(
			catalog::PhotoDisplayRequest{.current_state = session.repository,
										 .paths			= *session.paths,
										 .photo_id		= *selected_photo_id},
			display_progress, never_cancelled);
		last_display_photo_id = selected_photo_id;
	}

	juce::Image image;
	juce::String placeholder{"Loading preview placeholder"};
	if (last_photo_display_result.succeeded()
		&& last_photo_display_result.pixels.has_value()) {
		image = juce_image_from_pixels(*last_photo_display_result.pixels);
		placeholder =
			image.isValid() ? "" : "Decoded image cannot be displayed.";
	} else if (last_photo_display_result.placeholder.has_value()) {
		placeholder = juce_text(last_photo_display_result.placeholder->message);
	} else if (last_photo_display_result.was_user_cancelled()) {
		placeholder = "Photo display was cancelled.";
	} else {
		placeholder =
			"Photo preview placeholder. Full decode is only attempted "
			"for this viewer, not for result lists.";
	}
	content->add_image_panel(image,
							 juce_text(photo_summary(*photo, position, total)),
							 placeholder, 260);

	if (!last_photo_message.empty()) {
		content->add_label(juce_text(last_photo_message), 54,
						   accent_colour().withAlpha(0.34f), true);
	}
	if (!last_photo_diagnostics.empty()) {
		content->add_label(
			juce_text(core_diagnostic_summary(last_photo_diagnostics)), 76,
			warning_panel_colour(), true);
	}
	if (!last_photo_display_result.diagnostics.empty()) {
		content->add_label(juce_text(core_diagnostic_summary(
							   last_photo_display_result.diagnostics)),
						   76, warning_panel_colour(), true);
	}
	content->add_label(
		juce_text(progress_summary(last_progress_events.events())), 50,
		panel_colour());

	juce::Button& previous = content->add_button("Previous photo", 40);
	previous.setEnabled(total > 1U);
	previous.onClick = [this, owner, photo_id = *selected_photo_id] {
		selected_photo_id =
			adjacent_photo_id(session.repository, owner, photo_id, -1);
		last_display_photo_id.reset();
		refresh_all();
	};
	juce::Button& next = content->add_button("Next photo", 40);
	next.setEnabled(total > 1U);
	next.onClick = [this, owner, photo_id = *selected_photo_id] {
		selected_photo_id =
			adjacent_photo_id(session.repository, owner, photo_id, 1);
		last_display_photo_id.reset();
		refresh_all();
	};
	juce::Button& set_main = content->add_button(
		photo->record.is_main ? "Already main photo" : "Set as main", 42);
	set_main.setEnabled(!photo->record.is_main);
	set_main.onClick = [this, photo_id = *selected_photo_id] {
		EntityEditResult result = set_main_photo_in_session(
			EntityEditRequest{.current_session = session,
							  .identifiers	   = edit_identifiers,
							  .clock		   = edit_clock},
			photo_id);
		apply_photo_edit_result(std::move(result), photo_id);
	};
	juce::Button& export_button =
		content->add_button("Export current photo as JPEG", 42);
	export_button.setEnabled(last_photo_display_result.succeeded());
	export_button.onClick = [this, photo_id = *selected_photo_id] {
		request_export_photo(photo_id);
	};
	juce::Button& add_photo = content->add_button("Add more photos", 42);
	add_photo.onClick		= [this, owner] { request_add_photos(owner); };
}

}	 // namespace shuba::ui
