#include "UI/View/Primitives/Forms.hpp"

#include "Localization/Facade.hpp"
#include "UI/View/Primitives/Palette.hpp"

#include <algorithm>
#include <utility>

namespace shuba::ui {
InlineButtonRowComponent::InlineButtonRowComponent(
	juce::String title_value, std::vector<Action> actions_value)
	: title(std::move(title_value)) {
	setOpaque(true);
	setBufferedToImage(true);
	for (Action& action : actions_value) {
		std::unique_ptr<juce::TextButton> button =
			std::make_unique<juce::TextButton>(action.label);
		style_text_button(*button);
		button->setEnabled(action.enabled);
		button->onClick = std::move(action.handler);
		addAndMakeVisible(*button);
		buttons.push_back(std::move(button));
	}
}

void InlineButtonRowComponent::resized() {
	juce::Rectangle<int> area = getLocalBounds().reduced(12, 8);
	if (area.isEmpty())
		return;

	if (!title.isEmpty())
		area.removeFromLeft(std::min(112, area.getWidth() / 3));

	const int gap	= 6;
	const int count = static_cast<int>(buttons.size());
	if (count <= 0)
		return;

	const int button_width =
		std::max(1, (area.getWidth() - gap * (count - 1)) / count);
	for (std::unique_ptr<juce::TextButton>& button : buttons) {
		button->setBounds(area.removeFromLeft(button_width));
		area.removeFromLeft(gap);
	}
}

void InlineButtonRowComponent::paint(juce::Graphics& graphics) {
	graphics.fillAll(background_colour());
	draw_card_background(graphics, getLocalBounds(), panel_colour(), false);
	if (title.isEmpty())
		return;

	juce::Rectangle<int> title_area = getLocalBounds().reduced(12, 8);
	title_area.setWidth(std::min(112, title_area.getWidth() / 3));
	graphics.setColour(text_colour());
	graphics.setFont(juce::FontOptions(14.0f, juce::Font::bold));
	graphics.drawFittedText(title, title_area, juce::Justification::centredLeft,
							1, 0.88f);
}

DirectChoiceGridComponent::DirectChoiceGridComponent(
	juce::String title_value, std::vector<Choice> choices_value)
	: title(std::move(title_value)) {
	setOpaque(true);
	setBufferedToImage(true);
	for (Choice& choice : choices_value) {
		std::unique_ptr<juce::TextButton> button =
			std::make_unique<juce::TextButton>(choice.label);
		style_text_button(*button);
		button->setClickingTogglesState(false);
		button->setToggleState(choice.selected, juce::dontSendNotification);
		button->setEnabled(choice.enabled);
		button->setTooltip(choice.label);
		button->setColour(juce::TextButton::buttonColourId,
						  choice.selected ? accent_colour().withAlpha(0.78f)
										  : surface_colour());
		button->setColour(juce::TextButton::buttonOnColourId,
						  accent_colour().withAlpha(0.78f));
		button->onClick = std::move(choice.handler);
		addAndMakeVisible(*button);
		buttons.push_back(std::move(button));
	}
}

std::vector<juce::Rectangle<int>> DirectChoiceGridComponent::choice_bounds(
	juce::Rectangle<int> bounds, bool has_title, std::size_t choice_count) {
	std::vector<juce::Rectangle<int>> result;
	result.reserve(choice_count);
	juce::Rectangle<int> area = bounds.reduced(12, 8);
	if (area.isEmpty() || choice_count == 0U)
		return result;

	if (has_title) {
		const int title_width =
			std::min(112, std::max(72, area.getWidth() / 4));
		area.removeFromLeft(title_width);
		area.removeFromLeft(8);
	}

	constexpr int gap		  = 6;
	const int button_height	  = std::max(1, (area.getHeight() - gap) / 2);
	const int first_row_count = std::min(3, static_cast<int>(choice_count));
	const int first_row_width =
		std::max(1, (area.getWidth() - gap * std::max(0, first_row_count - 1))
						/ first_row_count);
	for (int index{}; index < first_row_count; ++index) {
		result.push_back(
			juce::Rectangle<int>{area.getX() + index * (first_row_width + gap),
								 area.getY(), first_row_width, button_height});
	}

	const int second_row_count =
		static_cast<int>(choice_count) - first_row_count;
	if (second_row_count <= 0)
		return result;
	const int second_row_width =
		std::max(1, (area.getWidth() - gap * std::max(0, second_row_count - 1))
						/ second_row_count);
	for (int index{}; index < second_row_count; ++index) {
		result.push_back(
			juce::Rectangle<int>{area.getX() + index * (second_row_width + gap),
								 area.getY() + button_height + gap,
								 second_row_width, button_height});
	}
	return result;
}

void DirectChoiceGridComponent::resized() {
	const std::vector<juce::Rectangle<int>> bounds =
		choice_bounds(getLocalBounds(), !title.isEmpty(), buttons.size());
	for (std::size_t index{}; index < buttons.size(); ++index) {
		buttons[index]->setBounds(
			index < bounds.size() ? bounds[index] : juce::Rectangle<int>{});
	}
}

void DirectChoiceGridComponent::paint(juce::Graphics& graphics) {
	graphics.fillAll(background_colour());
	draw_card_background(graphics, getLocalBounds(), panel_colour(), false);
	if (title.isEmpty())
		return;

	juce::Rectangle<int> title_area = getLocalBounds().reduced(12, 8);
	title_area.setWidth(std::min(112, std::max(72, title_area.getWidth() / 4)));
	graphics.setColour(text_colour());
	graphics.setFont(juce::FontOptions(14.0f, juce::Font::bold));
	graphics.drawFittedText(title, title_area, juce::Justification::centredLeft,
							1, 0.90f);
}

ButtonGridComponent::ButtonGridComponent(juce::String title_value,
										 std::vector<Action> actions_value,
										 int column_count_value)
	: title(std::move(title_value))
	, column_count(std::max(1, column_count_value)) {
	setOpaque(true);
	setBufferedToImage(true);
	for (Action& action : actions_value) {
		std::unique_ptr<juce::TextButton> button =
			std::make_unique<juce::TextButton>(action.label);
		style_text_button(*button);
		button->setEnabled(action.enabled);
		button->setTooltip(action.label);
		button->onClick = std::move(action.handler);
		addAndMakeVisible(*button);
		buttons.push_back(std::move(button));
	}
}

int ButtonGridComponent::preferred_height(int action_count,
										  int column_count_value) noexcept {
	const int safe_columns = std::max(1, column_count_value);
	const int row_count =
		std::max(1, (action_count + safe_columns - 1) / safe_columns);
	return 32 + row_count * 38 + 8;
}

void ButtonGridComponent::resized() {
	juce::Rectangle<int> area = getLocalBounds().reduced(10, 8);
	if (!title.isEmpty())
		area.removeFromTop(24);
	const int gap			= 6;
	const int button_height = 32;
	const int button_width	= std::max(
		1, (area.getWidth() - gap * (column_count - 1)) / column_count);
	for (std::size_t index = 0; index < buttons.size(); ++index) {
		const int row	 = static_cast<int>(index) / column_count;
		const int column = static_cast<int>(index) % column_count;
		buttons[index]->setBounds(area.getX() + column * (button_width + gap),
								  area.getY() + row * (button_height + gap),
								  button_width, button_height);
	}
}

void ButtonGridComponent::paint(juce::Graphics& graphics) {
	graphics.fillAll(background_colour());
	draw_card_background(graphics, getLocalBounds(), panel_colour(), false);
	if (title.isEmpty())
		return;

	juce::Rectangle<int> title_area = getLocalBounds().reduced(12, 8);
	title_area.setHeight(22);
	graphics.setColour(text_colour());
	graphics.setFont(juce::FontOptions(14.0f, juce::Font::bold));
	graphics.drawFittedText(title, title_area, juce::Justification::centredLeft,
							1, 0.90f);
}

ChipGridComponent::ChipGridComponent(juce::String title_value,
									 std::vector<Action> actions_value,
									 int column_count_value)
	: title(std::move(title_value))
	, column_count(std::max(1, column_count_value)) {
	setOpaque(true);
	setBufferedToImage(true);
	for (Action& action : actions_value) {
		std::unique_ptr<juce::TextButton> button =
			std::make_unique<juce::TextButton>(action.label);
		style_text_button(*button);
		button->setClickingTogglesState(false);
		button->setToggleState(action.selected, juce::dontSendNotification);
		button->setEnabled(action.enabled);
		button->setTooltip(action.label);
		button->setColour(juce::TextButton::buttonColourId,
						  action.selected ? accent_colour().withAlpha(0.78f)
										  : surface_colour());
		button->setColour(juce::TextButton::buttonOnColourId,
						  accent_colour().withAlpha(0.78f));
		button->onClick = std::move(action.handler);
		addAndMakeVisible(*button);
		buttons.push_back(std::move(button));
	}
}

int ChipGridComponent::preferred_height(int action_count,
										int column_count_value,
										bool has_title) noexcept {
	const int safe_columns = std::max(1, column_count_value);
	const int row_count =
		std::max(1, (action_count + safe_columns - 1) / safe_columns);
	return (has_title ? 30 : 12) + row_count * 34 + 10;
}

void ChipGridComponent::resized() {
	juce::Rectangle<int> area = getLocalBounds().reduced(10, 8);
	if (!title.isEmpty())
		area.removeFromTop(22);
	const int gap			= 6;
	const int button_height = 28;
	const int button_width	= std::max(
		1, (area.getWidth() - gap * (column_count - 1)) / column_count);
	for (std::size_t index = 0; index < buttons.size(); ++index) {
		const int row	 = static_cast<int>(index) / column_count;
		const int column = static_cast<int>(index) % column_count;
		buttons[index]->setBounds(area.getX() + column * (button_width + gap),
								  area.getY() + row * (button_height + gap),
								  button_width, button_height);
	}
}

void ChipGridComponent::paint(juce::Graphics& graphics) {
	graphics.fillAll(background_colour());
	draw_card_background(graphics, getLocalBounds(), panel_colour(), false);
	if (title.isEmpty())
		return;

	juce::Rectangle<int> title_area = getLocalBounds().reduced(12, 8);
	title_area.setHeight(20);
	graphics.setColour(text_colour());
	graphics.setFont(juce::FontOptions(13.5f, juce::Font::bold));
	graphics.drawFittedText(title, title_area, juce::Justification::centredLeft,
							1, 0.90f);
}

TagRowEditorComponent::TagRowEditorComponent(
	std::size_t row_index_value, const domain::TagRow& tag_value,
	localization::Localization& localization_value,
	std::function<void(std::size_t, domain::TagRow)> change_handler,
	std::function<void(std::size_t)> remove_handler)
	: row_index(row_index_value)
	, on_change(std::move(change_handler))
	, on_remove(std::move(remove_handler))
	, localization(localization_value) {
	setOpaque(true);
	setBufferedToImage(true);
	key_editor.setText(juce_text(tag_value.key), juce::dontSendNotification);
	value_editor.setText(juce_text(tag_value.value),
						 juce::dontSendNotification);
	key_editor.setTextToShowWhenEmpty(
		juce_text(
			localization.text(localization::MessageId::TagKeyPlaceholder)),
		muted_text_colour());
	value_editor.setTextToShowWhenEmpty(
		juce_text(
			localization.text(localization::MessageId::TagValuePlaceholder)),
		muted_text_colour());
	remove_button.setButtonText(
		juce_text(localization.text(localization::MessageId::TagRemove)));
	style_text_editor(key_editor);
	style_text_editor(value_editor);
	style_text_button(remove_button);
	key_editor.onTextChange	  = [this] { publish_change(); };
	value_editor.onTextChange = [this] { publish_change(); };
	remove_button.onClick	  = [this] {
		if (on_remove)
			on_remove(row_index);
	};
	addAndMakeVisible(key_editor);
	addAndMakeVisible(value_editor);
	addAndMakeVisible(remove_button);
}

void TagRowEditorComponent::resized() {
	juce::Rectangle<int> area		 = getLocalBounds().reduced(12, 8);
	juce::Rectangle<int> remove_area = area.removeFromRight(82);
	area.removeFromRight(6);
	const int key_width = std::max(86, area.getWidth() / 2);
	key_editor.setBounds(area.removeFromLeft(key_width));
	area.removeFromLeft(6);
	value_editor.setBounds(area);
	remove_button.setBounds(remove_area);
}

void TagRowEditorComponent::paint(juce::Graphics& graphics) {
	graphics.fillAll(background_colour());
	draw_card_background(graphics, getLocalBounds(), panel_colour(), false);
}

void TagRowEditorComponent::publish_change() {
	if (!on_change)
		return;

	on_change(row_index,
			  domain::TagRow{.key	= key_editor.getText().toStdString(),
							 .value = value_editor.getText().toStdString()});
}

EditorPairComponent::EditorPairComponent(juce::TextEditor& first_editor_value,
										 juce::TextEditor& second_editor_value,
										 const juce::String& first_placeholder,
										 const juce::String& second_placeholder)
	: first_editor(first_editor_value), second_editor(second_editor_value) {
	setOpaque(true);
	setBufferedToImage(true);
	prepare_editor(first_editor, first_placeholder);
	prepare_editor(second_editor, second_placeholder);
	addAndMakeVisible(first_editor);
	addAndMakeVisible(second_editor);
}

EditorPairComponent::~EditorPairComponent() {
	removeChildComponent(&first_editor);
	removeChildComponent(&second_editor);
}

void EditorPairComponent::resized() {
	juce::Rectangle<int> area = getLocalBounds().reduced(12, 8);
	const int gap			  = 6;
	const int editor_width	  = std::max(1, (area.getWidth() - gap) / 2);
	first_editor.setBounds(area.removeFromLeft(editor_width));
	area.removeFromLeft(gap);
	second_editor.setBounds(area);
}

void EditorPairComponent::paint(juce::Graphics& graphics) {
	graphics.fillAll(background_colour());
	draw_card_background(graphics, getLocalBounds(), panel_colour(), false);
}

void EditorPairComponent::prepare_editor(juce::TextEditor& editor,
										 const juce::String& placeholder) {
	if (juce::Component* parent = editor.getParentComponent())
		parent->removeChildComponent(&editor);
	editor.setTextToShowWhenEmpty(placeholder, muted_text_colour());
	editor.setMultiLine(false);
	editor.setReturnKeyStartsNewLine(false);
	editor.setScrollbarsShown(false);
	style_text_editor(editor);
}
}	 // namespace shuba::ui
