#include "UI/View/AppShellContentComponent.hpp"

#include <algorithm>

namespace shuba::ui {
AppShellContentComponent::AppShellContentComponent() {
	setOpaque(true);
}

AppShellContentComponent::~AppShellContentComponent() {
	for (Row& row : rows)
		if (!row.owned)
			row.component.release();
}

void AppShellContentComponent::clear_rows() {
	for (Row& row : rows) {
		removeChildComponent(row.component.get());
		if (!row.owned)
			row.component.release();
	}
	rows.clear();
	if (!rebuilding)
		setSize(getWidth(), 1);
}

void AppShellContentComponent::begin_rebuild() noexcept {
	rebuilding = true;
}

void AppShellContentComponent::end_rebuild() {
	rebuilding = false;
	resized();
}

TextRowComponent& AppShellContentComponent::add_label(juce::String text,
													  int height,
													  juce::Colour colour,
													  bool bold) {
	std::unique_ptr<TextRowComponent> label =
		std::make_unique<TextRowComponent>(std::move(text), colour, bold);
	TextRowComponent& reference = *label;
	add_row(std::move(label), height);
	return reference;
}

juce::Button& AppShellContentComponent::add_button(juce::String text,
												   int height) {
	std::unique_ptr<RowButtonComponent> button =
		std::make_unique<RowButtonComponent>(std::move(text),
											 elevated_surface_colour());
	RowButtonComponent& reference = *button;
	add_row(std::move(button), height);
	return reference;
}

ImagePanelComponent& AppShellContentComponent::add_image_panel(
	juce::Image image, juce::String caption, juce::String placeholder,
	int height) {
	std::unique_ptr<ImagePanelComponent> panel =
		std::make_unique<ImagePanelComponent>(
			std::move(image), std::move(caption), std::move(placeholder));
	ImagePanelComponent& reference = *panel;
	add_row(std::move(panel), height);
	return reference;
}

PhotoCarouselComponent& AppShellContentComponent::add_photo_carousel(
	std::vector<PhotoCarouselSlide> slides, std::size_t selected_index,
	std::function<void(std::size_t)> select_handler,
	std::function<void()> activate_handler, int height) {
	std::unique_ptr<PhotoCarouselComponent> carousel =
		std::make_unique<PhotoCarouselComponent>(
			std::move(slides), selected_index, std::move(select_handler),
			std::move(activate_handler));
	PhotoCarouselComponent& reference = *carousel;
	add_row(std::move(carousel), height);
	return reference;
}

PhotoViewerImageComponent& AppShellContentComponent::add_photo_viewer_image(
	PhotoViewerImageModel model, PhotoViewerImageHandlers handlers,
	int height) {
	std::unique_ptr<PhotoViewerImageComponent> viewer =
		std::make_unique<PhotoViewerImageComponent>(std::move(model),
													std::move(handlers));
	PhotoViewerImageComponent& reference = *viewer;
	add_row(std::move(viewer), height);
	return reference;
}

PreviewCardButtonComponent& AppShellContentComponent::add_preview_card(
	PreviewCardContent content, int height) {
	std::unique_ptr<PreviewCardButtonComponent> card =
		std::make_unique<PreviewCardButtonComponent>(std::move(content),
													 elevated_surface_colour());
	PreviewCardButtonComponent& reference = *card;
	add_row(std::move(card), height);
	return reference;
}

CompactStorageStripComponent&
AppShellContentComponent::add_compact_storage_strip(
	std::vector<CompactStorageCardContent> cards, int height) {
	std::unique_ptr<CompactStorageStripComponent> strip =
		std::make_unique<CompactStorageStripComponent>(std::move(cards));
	CompactStorageStripComponent& reference = *strip;
	add_row(std::move(strip), height);
	return reference;
}

InlineButtonRowComponent& AppShellContentComponent::add_inline_buttons(
	juce::String title, std::vector<InlineButtonRowComponent::Action> actions,
	int height) {
	std::unique_ptr<InlineButtonRowComponent> row =
		std::make_unique<InlineButtonRowComponent>(std::move(title),
												   std::move(actions));
	InlineButtonRowComponent& reference = *row;
	add_row(std::move(row), height);
	return reference;
}

ButtonGridComponent& AppShellContentComponent::add_button_grid(
	juce::String title, std::vector<ButtonGridComponent::Action> actions,
	int column_count, int height) {
	std::unique_ptr<ButtonGridComponent> grid =
		std::make_unique<ButtonGridComponent>(std::move(title),
											  std::move(actions), column_count);
	ButtonGridComponent& reference = *grid;
	add_row(std::move(grid), height);
	return reference;
}

ChipGridComponent& AppShellContentComponent::add_chip_grid(
	juce::String title, std::vector<ChipGridComponent::Action> actions,
	int column_count, int height) {
	std::unique_ptr<ChipGridComponent> grid =
		std::make_unique<ChipGridComponent>(std::move(title),
											std::move(actions), column_count);
	ChipGridComponent& reference = *grid;
	add_row(std::move(grid), height);
	return reference;
}

EditorPairComponent& AppShellContentComponent::add_editor_pair(
	juce::TextEditor& first_editor, juce::String first_placeholder,
	juce::TextEditor& second_editor, juce::String second_placeholder,
	int height) {
	std::unique_ptr<EditorPairComponent> row =
		std::make_unique<EditorPairComponent>(first_editor, second_editor,
											  std::move(first_placeholder),
											  std::move(second_placeholder));
	EditorPairComponent& reference = *row;
	add_row(std::move(row), height);
	return reference;
}

ManagedPhotoDeckComponent& AppShellContentComponent::add_managed_photo_deck(
	ManagedPhotoDeckModel model, ManagedPhotoDeckHandlers handlers,
	int height) {
	std::unique_ptr<ManagedPhotoDeckComponent> deck =
		std::make_unique<ManagedPhotoDeckComponent>(std::move(model),
													std::move(handlers));
	ManagedPhotoDeckComponent& reference = *deck;
	add_row(std::move(deck), height);
	return reference;
}

TagRowEditorComponent& AppShellContentComponent::add_tag_editor_row(
	std::size_t row_index, domain::TagRow tag,
	std::function<void(std::size_t, domain::TagRow)> change_handler,
	std::function<void(std::size_t)> remove_handler, int height) {
	std::unique_ptr<TagRowEditorComponent> row =
		std::make_unique<TagRowEditorComponent>(row_index, std::move(tag),
												std::move(change_handler),
												std::move(remove_handler));
	TagRowEditorComponent& reference = *row;
	add_row(std::move(row), height);
	return reference;
}

juce::ToggleButton& AppShellContentComponent::add_toggle(juce::String text,
														 bool state,
														 int height) {
	std::unique_ptr<TouchSafeToggleButton> toggle =
		std::make_unique<TouchSafeToggleButton>(std::move(text));
	TouchSafeToggleButton& reference = *toggle;
	toggle->setToggleState(state, juce::dontSendNotification);
	toggle->setColour(juce::ToggleButton::textColourId, text_colour());
	toggle->setColour(juce::ToggleButton::tickColourId, accent_colour());
	add_row(std::move(toggle), height);
	return reference;
}

juce::TextEditor& AppShellContentComponent::add_editor(juce::TextEditor& editor,
													   juce::String placeholder,
													   int height,
													   bool multiline) {
	removeChildComponent(&editor);
	editor.setTextToShowWhenEmpty(std::move(placeholder), muted_text_colour());
	editor.setMultiLine(multiline);
	editor.setReturnKeyStartsNewLine(multiline);
	editor.setScrollbarsShown(multiline);
	style_text_editor(editor);
	add_row(std::unique_ptr<juce::Component>(&editor), height, false);
	return editor;
}

void AppShellContentComponent::resized() {
	juce::Rectangle<int> bounds = getLocalBounds().reduced(6, 4);
	int y						= bounds.getY();
	const int width				= bounds.getWidth();
	for (Row& row : rows) {
		row.component->setBounds(bounds.getX(), y, width, row.height - 4);
		y += row.height;
	}
	setSize(std::max(1, getWidth()), std::max(1, y + 8));
}

void AppShellContentComponent::paint(juce::Graphics& graphics) {
	graphics.fillAll(background_colour());
}

void AppShellContentComponent::set_viewport_height_hint(int height) noexcept {
	viewport_height_hint_value = std::max(1, height);
}

int AppShellContentComponent::viewport_height_hint() const noexcept {
	return viewport_height_hint_value;
}

void AppShellContentComponent::add_row(
	std::unique_ptr<juce::Component> component, int height, bool owned) {
	addAndMakeVisible(*component);
	rows.push_back(Row{
		.component = std::move(component), .height = height, .owned = owned});
	if (!rebuilding)
		resized();
}
}	 // namespace shuba::ui
