#include "UI/AppShell/ChromeComponent.hpp"

#include "Localization/Facade.hpp"
#include "UI/View/Primitives/Palette.hpp"
#include "UI/View/ScreenText.hpp"

#include <algorithm>
#include <utility>

namespace shuba::ui {
ChromeComponent::ChromeComponent(
	Callbacks callbacks_value, localization::Localization& localization_value)
	: callbacks(std::move(callbacks_value)), localization(localization_value) {
	setOpaque(false);

	title_label.setJustificationType(juce::Justification::centredLeft);
	title_label.setColour(juce::Label::textColourId, text_colour());
	title_label.setFont(juce::FontOptions(22.0f, juce::Font::bold));
	addAndMakeVisible(title_label);

	status_label.setJustificationType(juce::Justification::centredLeft);
	status_label.setColour(juce::Label::textColourId, muted_text_colour());
	status_label.setMinimumHorizontalScale(0.70f);
	status_label.setFont(juce::FontOptions(14.5f, juce::Font::plain));
	addAndMakeVisible(status_label);
	catalog_draft_count_label.setJustificationType(
		juce::Justification::centredLeft);
	catalog_draft_count_label.setColour(juce::Label::textColourId,
										muted_text_colour());
	catalog_draft_count_label.setMinimumHorizontalScale(0.70f);
	catalog_draft_count_label.setFont(
		juce::FontOptions(14.5f, juce::Font::plain));
	addAndMakeVisible(catalog_draft_count_label);

	catalog_search_editor.setTextToShowWhenEmpty(
		juce_text(localization.text(localization::MessageId::SearchCatalog)),
		muted_text_colour());
	style_text_editor(catalog_search_editor);
	catalog_search_editor.onTextChange = [this] {
		if (callbacks.catalog_search_changed)
			callbacks.catalog_search_changed();
	};
	addAndMakeVisible(catalog_search_editor);

	storage_search_editor.setTextToShowWhenEmpty(
		juce_text(localization.text(localization::MessageId::SearchStorages)),
		muted_text_colour());
	style_text_editor(storage_search_editor);
	storage_search_editor.onTextChange = [this] {
		if (callbacks.storage_search_changed)
			callbacks.storage_search_changed();
	};
	addAndMakeVisible(storage_search_editor);

	catalog_clear_button.onClick = [this] {
		if (callbacks.catalog_clear)
			callbacks.catalog_clear();
	};
	catalog_filter_button.onClick = [this] {
		if (callbacks.catalog_filter)
			callbacks.catalog_filter();
	};
	catalog_apply_filters_button.onClick = [this] {
		if (callbacks.catalog_apply_filters)
			callbacks.catalog_apply_filters();
	};
	catalog_clear_filters_button.onClick = [this] {
		if (callbacks.catalog_clear_filters)
			callbacks.catalog_clear_filters();
	};
	catalog_close_filters_button.onClick = [this] {
		if (callbacks.catalog_close_filters)
			callbacks.catalog_close_filters();
	};
	storage_clear_button.onClick = [this] {
		if (callbacks.storage_clear)
			callbacks.storage_clear();
	};
	form_cancel_button.onClick = [this] {
		if (callbacks.form_cancel)
			callbacks.form_cancel();
	};
	form_save_button.onClick = [this] {
		if (callbacks.form_save)
			callbacks.form_save();
	};

	for (juce::TextButton* button :
		 {&catalog_clear_button, &catalog_filter_button,
		  &catalog_apply_filters_button, &catalog_clear_filters_button,
		  &catalog_close_filters_button, &storage_clear_button,
		  &form_cancel_button, &form_save_button}) {
		style_text_button(*button);
		addAndMakeVisible(*button);
	}
	catalog_clear_button.setButtonText(
		juce_text(localization.text(localization::MessageId::Clear)));
	catalog_filter_button.setButtonText(
		juce_text(localization.text(localization::MessageId::Filters)));
	catalog_apply_filters_button.setButtonText(
		juce_text(localization.text(localization::MessageId::ApplyFilters)));
	catalog_clear_filters_button.setButtonText(
		juce_text(localization.text(localization::MessageId::ClearFilters)));
	catalog_close_filters_button.setButtonText(
		juce_text(localization.text(localization::MessageId::Close)));
	storage_clear_button.setButtonText(
		juce_text(localization.text(localization::MessageId::StorageClear)));
	form_cancel_button.setButtonText(
		juce_text(localization.text(localization::MessageId::Cancel)));

	catalog_nav_button.onClick = [this] {
		if (callbacks.select_catalog)
			callbacks.select_catalog();
	};
	storages_nav_button.onClick = [this] {
		if (callbacks.select_storages)
			callbacks.select_storages();
	};
	add_nav_button.onClick = [this] {
		if (callbacks.select_add)
			callbacks.select_add();
	};
	more_nav_button.onClick = [this] {
		if (callbacks.select_more)
			callbacks.select_more();
	};
	for (juce::TextButton* button : {&catalog_nav_button, &storages_nav_button,
									 &add_nav_button, &more_nav_button}) {
		style_text_button(*button);
		addAndMakeVisible(*button);
	}
	catalog_nav_button.setButtonText(juce_text(
		localization.text(localization::MessageId::NavigationCatalog)));
	storages_nav_button.setButtonText(juce_text(
		localization.text(localization::MessageId::NavigationStorages)));
	add_nav_button.setButtonText(
		juce_text(localization.text(localization::MessageId::NavigationAdd)));
	more_nav_button.setButtonText(
		juce_text(localization.text(localization::MessageId::NavigationMore)));
}

void ChromeComponent::update_model(const Model& model) {
	current_model = model;
	title_label.setText(current_model.title, juce::dontSendNotification);
	status_label.setText(current_model.status, juce::dontSendNotification);
	catalog_draft_count_label.setText(current_model.catalog_draft_result_count,
									  juce::dontSendNotification);
	form_save_button.setButtonText(
		current_model.destination == RootDestination::ItemForm
			? juce_text(localization.text(localization::MessageId::SaveItem))
		: current_model.destination == RootDestination::StorageForm
			? juce_text(localization.text(localization::MessageId::SaveStorage))
			: juce_text(localization.text(localization::MessageId::Save)));

	const bool navigation_enabled =
		!current_model.session_fatal && !current_model.shell_operation_active;
	catalog_nav_button.setEnabled(navigation_enabled);
	storages_nav_button.setEnabled(navigation_enabled);
	add_nav_button.setEnabled(navigation_enabled);
	more_nav_button.setEnabled(!current_model.shell_operation_active);
	form_cancel_button.setEnabled(!current_model.shell_operation_active);
	form_save_button.setEnabled(!current_model.shell_operation_active);

	const juce::Colour selected_colour = accent_colour().withAlpha(0.65f);
	const juce::Colour normal_colour   = panel_colour();
	catalog_nav_button.setColour(
		juce::TextButton::buttonColourId,
		current_model.destination == RootDestination::Catalog ? selected_colour
															  : normal_colour);
	catalog_filter_button.setColour(
		juce::TextButton::buttonColourId,
		current_model.catalog_filters_active ? selected_colour : normal_colour);
	catalog_apply_filters_button.setColour(juce::TextButton::buttonColourId,
										   accent_colour().withAlpha(0.82f));
	storages_nav_button.setColour(
		juce::TextButton::buttonColourId,
		current_model.destination == RootDestination::Storages
				|| current_model.destination == RootDestination::StorageDetail
			? selected_colour
			: normal_colour);
	add_nav_button.setColour(
		juce::TextButton::buttonColourId,
		current_model.destination == RootDestination::Add
				|| current_model.destination == RootDestination::ItemForm
				|| current_model.destination == RootDestination::StorageForm
			? selected_colour
			: normal_colour);
	more_nav_button.setColour(juce::TextButton::buttonColourId,
							  current_model.destination == RootDestination::More
								  ? selected_colour
								  : normal_colour);
}

juce::Rectangle<int> ChromeComponent::layout_shell(
	juce::Rectangle<int> bounds) {
	const bool form_visible =
		current_model.destination == RootDestination::ItemForm
		|| current_model.destination == RootDestination::StorageForm;
	const bool photo_viewer_visible =
		current_model.destination == RootDestination::PhotoViewer;
	const bool bottom_nav_visible = !form_visible && !photo_viewer_visible;
	catalog_nav_button.setVisible(bottom_nav_visible);
	storages_nav_button.setVisible(bottom_nav_visible);
	add_nav_button.setVisible(bottom_nav_visible);
	more_nav_button.setVisible(bottom_nav_visible);
	form_cancel_button.setVisible(form_visible);
	form_save_button.setVisible(form_visible);

	if (form_visible) {
		juce::Rectangle<int> form_actions = bounds.removeFromBottom(58);
		const int action_width = std::max(1, form_actions.getWidth() / 2);
		juce::Rectangle<int> cancel_area =
			form_actions.removeFromLeft(action_width);
		form_cancel_button.setBounds(cancel_area.reduced(3));
		form_save_button.setBounds(form_actions.reduced(3));
	} else if (bottom_nav_visible) {
		juce::Rectangle<int> nav = bounds.removeFromBottom(54);
		const int nav_width		 = nav.getWidth() / 4;
		catalog_nav_button.setBounds(nav.removeFromLeft(nav_width).reduced(3));
		storages_nav_button.setBounds(nav.removeFromLeft(nav_width).reduced(3));
		add_nav_button.setBounds(nav.removeFromLeft(nav_width).reduced(3));
		more_nav_button.setBounds(nav.reduced(3));
	} else {
		catalog_nav_button.setBounds(0, 0, 0, 0);
		storages_nav_button.setBounds(0, 0, 0, 0);
		add_nav_button.setBounds(0, 0, 0, 0);
		more_nav_button.setBounds(0, 0, 0, 0);
	}
	catalog_apply_filters_button.setBounds(0, 0, 0, 0);
	catalog_close_filters_button.setBounds(0, 0, 0, 0);
	catalog_clear_filters_button.setBounds(0, 0, 0, 0);

	const bool catalog_visible =
		current_model.destination == RootDestination::Catalog;
	if (catalog_visible && bottom_nav_visible) {
		const int strip_height =
			current_model.catalog_filter_panel_visible ? 82 : 54;
		juce::Rectangle<int> strip = bounds.removeFromBottom(strip_height);
		if (current_model.catalog_filter_panel_visible) {
			juce::Rectangle<int> count_row =
				strip.removeFromTop(30).reduced(3, 2);
			juce::Rectangle<int> action_row = strip.reduced(3, 2);
			catalog_search_editor.setBounds(0, 0, 0, 0);
			catalog_clear_button.setBounds(0, 0, 0, 0);
			catalog_filter_button.setBounds(0, 0, 0, 0);
			catalog_close_filters_button.setBounds(
				action_row.removeFromRight(70));
			action_row.removeFromRight(4);
			catalog_clear_filters_button.setBounds(
				action_row.removeFromRight(104));
			action_row.removeFromRight(4);
			catalog_apply_filters_button.setBounds(action_row);
			catalog_draft_count_label.setBounds(count_row);
		} else {
			strip = strip.reduced(3, 2);
			catalog_filter_button.setBounds(strip.removeFromRight(78));
			strip.removeFromRight(4);
			catalog_clear_button.setBounds(strip.removeFromRight(64));
			strip.removeFromRight(4);
			catalog_search_editor.setBounds(strip);
			catalog_apply_filters_button.setBounds(0, 0, 0, 0);
			catalog_close_filters_button.setBounds(0, 0, 0, 0);
		}
	}

	title_label.setBounds(bounds.removeFromTop(32));
	status_label.setBounds(bounds.removeFromTop(26));

	const bool storages_visible =
		current_model.destination == RootDestination::Storages;
	catalog_search_editor.setVisible(
		catalog_visible && !current_model.catalog_filter_panel_visible);
	catalog_clear_button.setVisible(
		catalog_visible && !current_model.catalog_filter_panel_visible);
	catalog_filter_button.setVisible(
		catalog_visible && !current_model.catalog_filter_panel_visible);
	catalog_apply_filters_button.setVisible(
		catalog_visible && current_model.catalog_filter_panel_visible);
	catalog_clear_filters_button.setVisible(
		catalog_visible && current_model.catalog_filter_panel_visible);
	catalog_close_filters_button.setVisible(
		catalog_visible && current_model.catalog_filter_panel_visible);
	catalog_draft_count_label.setVisible(
		catalog_visible && current_model.catalog_filter_panel_visible);
	storage_search_editor.setVisible(storages_visible);
	storage_clear_button.setVisible(storages_visible);

	if (storages_visible) {
		juce::Rectangle<int> controls = bounds.removeFromTop(44);
		storage_search_editor.setBounds(
			controls.removeFromLeft(std::max(140, controls.getWidth() - 72))
				.reduced(2));
		storage_clear_button.setBounds(controls.reduced(2));
	} else {
		storage_search_editor.setBounds(0, 0, 0, 0);
		storage_clear_button.setBounds(0, 0, 0, 0);
	}

	bounds.removeFromTop(4);
	return bounds;
}

std::string ChromeComponent::catalog_query() const {
	return catalog_search_editor.getText().toStdString();
}

std::string ChromeComponent::storage_query() const {
	return storage_search_editor.getText().toStdString();
}

void ChromeComponent::clear_catalog_query_without_notification() {
	catalog_search_editor.setText(juce::String{}, juce::dontSendNotification);
}

void ChromeComponent::clear_storage_query_without_notification() {
	storage_search_editor.setText(juce::String{}, juce::dontSendNotification);
}

void ChromeComponent::release_catalog_search_focus() {
	if (catalog_search_editor.hasKeyboardFocus(true))
		catalog_search_editor.giveAwayKeyboardFocus();
}

void ChromeComponent::paint(juce::Graphics&) {}
}	 // namespace shuba::ui
