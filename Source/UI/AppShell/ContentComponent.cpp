#include "UI/AppShell/ContentComponent.hpp"

#include <algorithm>

namespace shuba::ui {
ContentComponent::ContentComponent() {
	setOpaque(true);
}

ContentComponent::~ContentComponent() {
	for (Row& row : rows)
		if (!row.owned) {
			juce::Component* const released_component = row.component.release();
			juce::ignoreUnused(released_component);
		}
}

void ContentComponent::clear_rows() {
	for (Row& row : rows) {
		removeChildComponent(row.component.get());
		if (!row.owned) {
			juce::Component* const released_component = row.component.release();
			juce::ignoreUnused(released_component);
		}
	}
	rows.clear();
	if (!rebuilding)
		setSize(getWidth(), 1);
}

void ContentComponent::begin_rebuild() noexcept {
	rebuilding = true;
}

void ContentComponent::end_rebuild() {
	rebuilding = false;
	resized();
}

TextRowComponent& ContentComponent::add_label(juce::String text,
													  int height,
													  juce::Colour colour,
													  bool bold) {
	std::unique_ptr<TextRowComponent> label =
		std::make_unique<TextRowComponent>(std::move(text), colour, bold);
	TextRowComponent& reference = *label;
	add_row(std::move(label), height);
	return reference;
}

juce::Button& ContentComponent::add_button(const juce::String& text,
												   int height) {
	std::unique_ptr<RowButtonComponent> button =
		std::make_unique<RowButtonComponent>(text, elevated_surface_colour());
	RowButtonComponent& reference = *button;
	add_row(std::move(button), height);
	return reference;
}

ImagePanelComponent& ContentComponent::add_image_panel(
	juce::Image image, juce::String caption, juce::String placeholder,
	int height) {
	std::unique_ptr<ImagePanelComponent> panel =
		std::make_unique<ImagePanelComponent>(
			std::move(image), std::move(caption), std::move(placeholder));
	ImagePanelComponent& reference = *panel;
	add_row(std::move(panel), height);
	return reference;
}

PhotoCarouselComponent& ContentComponent::add_photo_carousel(
	std::vector<PhotoCarouselSlide> slides, std::size_t selected_index,
	localization::Localization& localization,
	std::function<void(std::size_t)> select_handler,
	std::function<void()> activate_handler, int height) {
	std::unique_ptr<PhotoCarouselComponent> carousel =
		std::make_unique<PhotoCarouselComponent>(
			std::move(slides), selected_index, localization,
			std::move(select_handler), std::move(activate_handler));
	PhotoCarouselComponent& reference = *carousel;
	add_row(std::move(carousel), height);
	return reference;
}

PhotoViewerImageComponent& ContentComponent::add_photo_viewer_image(
	PhotoViewerImageModel model, PhotoViewerImageHandlers handlers,
	localization::Localization& localization, int height) {
	std::unique_ptr<PhotoViewerImageComponent> viewer =
		std::make_unique<PhotoViewerImageComponent>(
			std::move(model), std::move(handlers), localization);
	PhotoViewerImageComponent& reference = *viewer;
	add_row(std::move(viewer), height);
	return reference;
}

PreviewCardButtonComponent& ContentComponent::add_preview_card(
	PreviewCardContent content, localization::Localization& localization,
	int height) {
	std::unique_ptr<PreviewCardButtonComponent> card =
		std::make_unique<PreviewCardButtonComponent>(
			std::move(content), elevated_surface_colour(), localization);
	PreviewCardButtonComponent& reference = *card;
	add_row(std::move(card), height);
	return reference;
}

CompactStorageStripComponent&
ContentComponent::add_compact_storage_strip(
	std::vector<CompactStorageCardContent> cards,
	localization::Localization& localization, int height) {
	std::unique_ptr<CompactStorageStripComponent> strip =
		std::make_unique<CompactStorageStripComponent>(std::move(cards),
													   localization);
	CompactStorageStripComponent& reference = *strip;
	add_row(std::move(strip), height);
	return reference;
}

InlineButtonRowComponent& ContentComponent::add_inline_buttons(
	juce::String title, std::vector<InlineButtonRowComponent::Action> actions,
	int height) {
	std::unique_ptr<InlineButtonRowComponent> row =
		std::make_unique<InlineButtonRowComponent>(std::move(title),
												   std::move(actions));
	InlineButtonRowComponent& reference = *row;
	add_row(std::move(row), height);
	return reference;
}

DirectChoiceGridComponent& ContentComponent::add_direct_choice_grid(
	juce::String title, std::vector<DirectChoiceGridComponent::Choice> choices,
	int height) {
	std::unique_ptr<DirectChoiceGridComponent> grid =
		std::make_unique<DirectChoiceGridComponent>(std::move(title),
													std::move(choices));
	DirectChoiceGridComponent& reference = *grid;
	add_row(std::move(grid), height);
	return reference;
}

ButtonGridComponent& ContentComponent::add_button_grid(
	juce::String title, std::vector<ButtonGridComponent::Action> actions,
	int column_count, int height) {
	std::unique_ptr<ButtonGridComponent> grid =
		std::make_unique<ButtonGridComponent>(std::move(title),
											  std::move(actions), column_count);
	ButtonGridComponent& reference = *grid;
	add_row(std::move(grid), height);
	return reference;
}

ChipGridComponent& ContentComponent::add_chip_grid(
	juce::String title, std::vector<ChipGridComponent::Action> actions,
	int column_count, int height) {
	std::unique_ptr<ChipGridComponent> grid =
		std::make_unique<ChipGridComponent>(std::move(title),
											std::move(actions), column_count);
	ChipGridComponent& reference = *grid;
	add_row(std::move(grid), height);
	return reference;
}

EditorPairComponent& ContentComponent::add_editor_pair(
	juce::TextEditor& first_editor, const juce::String& first_placeholder,
	juce::TextEditor& second_editor, const juce::String& second_placeholder,
	int height) {
	std::unique_ptr<EditorPairComponent> row =
		std::make_unique<EditorPairComponent>(
			first_editor, second_editor, first_placeholder, second_placeholder);
	EditorPairComponent& reference = *row;
	add_row(std::move(row), height);
	return reference;
}

ManagedPhotoDeckComponent& ContentComponent::add_managed_photo_deck(
	ManagedPhotoDeckModel model, ManagedPhotoDeckHandlers handlers,
	localization::Localization& localization, int height) {
	std::unique_ptr<ManagedPhotoDeckComponent> deck =
		std::make_unique<ManagedPhotoDeckComponent>(
			std::move(model), std::move(handlers), localization);
	ManagedPhotoDeckComponent& reference = *deck;
	add_row(std::move(deck), height);
	return reference;
}

TagRowEditorComponent& ContentComponent::add_tag_editor_row(
	std::size_t row_index, const domain::TagRow& tag,
	localization::Localization& localization,
	std::function<void(std::size_t, domain::TagRow)> change_handler,
	std::function<void(std::size_t)> remove_handler, int height, bool enabled) {
	std::unique_ptr<TagRowEditorComponent> row =
		std::make_unique<TagRowEditorComponent>(
			row_index, tag, localization, std::move(change_handler),
			std::move(remove_handler), enabled);
	TagRowEditorComponent& reference = *row;
	add_row(std::move(row), height);
	return reference;
}

juce::ToggleButton& ContentComponent::add_toggle(
	const juce::String& text, bool state, int height) {
	std::unique_ptr<TouchSafeToggleButton> toggle =
		std::make_unique<TouchSafeToggleButton>(text);
	TouchSafeToggleButton& reference = *toggle;
	toggle->setToggleState(state, juce::dontSendNotification);
	toggle->setColour(juce::ToggleButton::textColourId, text_colour());
	toggle->setColour(juce::ToggleButton::tickColourId, accent_colour());
	add_row(std::move(toggle), height);
	return reference;
}

juce::TextEditor& ContentComponent::add_editor(
	juce::TextEditor& editor, const juce::String& placeholder, int height,
	bool multiline) {
	removeChildComponent(&editor);
	editor.setTextToShowWhenEmpty(placeholder, muted_text_colour());
	editor.setMultiLine(multiline);
	editor.setReturnKeyStartsNewLine(multiline);
	editor.setScrollbarsShown(multiline);
	style_text_editor(editor);
	add_row(std::unique_ptr<juce::Component>(&editor), height, false);
	return editor;
}

void ContentComponent::resized() {
	juce::Rectangle<int> bounds = getLocalBounds().reduced(6, 4);
	int y						= bounds.getY();
	const int width				= bounds.getWidth();
	for (Row& row : rows) {
		row.component->setBounds(bounds.getX(), y, width, row.height - 4);
		y += row.height;
	}
	setSize(std::max(1, getWidth()), std::max(1, y + 8));
}

void ContentComponent::paint(juce::Graphics& graphics) {
	graphics.fillAll(background_colour());
}

void ContentComponent::set_viewport_height_hint(int height) noexcept {
	viewport_height_hint_value = std::max(1, height);
}

int ContentComponent::viewport_height_hint() const noexcept {
	return viewport_height_hint_value;
}

void ContentComponent::add_row(
	std::unique_ptr<juce::Component> component, int height, bool owned) {
	addAndMakeVisible(*component);
	rows.push_back(Row{
		.component = std::move(component), .height = height, .owned = owned});
	if (!rebuilding)
		resized();
}
}	 // namespace shuba::ui
