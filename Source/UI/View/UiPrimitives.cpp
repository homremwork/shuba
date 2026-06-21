#include "UI/View/UiPrimitives.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace shuba::ui {
namespace {
[[nodiscard]] int fitted_line_count(int height, int vertical_padding,
									float font_height) noexcept {
	const int usable_height = std::max(1, height - vertical_padding);
	return std::max(1, static_cast<int>(usable_height / font_height));
}

void draw_card_background(juce::Graphics& graphics, juce::Rectangle<int> bounds,
						  juce::Colour colour, bool highlighted) {
	juce::Rectangle<float> area = bounds.toFloat().reduced(1.0f);
	juce::Colour fill			= highlighted ? colour.brighter(0.08f) : colour;
	graphics.setColour(fill);
	graphics.fillRoundedRectangle(area, 12.0f);
	graphics.setColour(outline_colour().withAlpha(0.52f));
	graphics.drawRoundedRectangle(area, 12.0f, 1.0f);
}

[[nodiscard]] bool touch_like_source(const juce::MouseEvent& event) noexcept {
	return event.source.isTouch() || event.source.isPen();
}

[[nodiscard]] std::string compact_byte_summary(
	const std::optional<std::uint64_t>& byte_count) {
	if (!byte_count.has_value())
		return "size unknown";
	if (*byte_count < 1024U)
		return std::to_string(*byte_count) + " B";
	if (*byte_count < 1024U * 1024U)
		return std::to_string(*byte_count / 1024U) + " KB";
	return std::to_string(*byte_count / (1024U * 1024U)) + " MB";
}

[[nodiscard]] std::string pending_photo_count_text(
	std::span<const PendingPhotoSource> sources) {
	std::uint64_t staged_count{};
	std::uint64_t failed_count{};
	for (const PendingPhotoSource& source : sources) {
		if (source.ready_for_import()) {
			++staged_count;
		} else if (source.status == PendingPhotoStatus::Failed
				   || source.status == PendingPhotoStatus::Cancelled) {
			++failed_count;
		}
	}

	std::string text = "Photos: " + std::to_string(staged_count) + " staged";
	if (failed_count > 0U)
		text += " · " + std::to_string(failed_count) + " failed";
	return text;
}

[[nodiscard]] std::string pending_photo_card_text(
	const PendingPhotoSource& source, std::size_t display_index) {
	std::string text = "#" + std::to_string(display_index) + " · "
					   + std::string{to_string(source.status)};
	if (!source.display_name.empty())
		text += " · " + source.display_name;
	text += " · " + compact_byte_summary(source.byte_count);
	if (!source.diagnostics.empty())
		text += " · " + std::to_string(source.diagnostics.size()) + " issue(s)";
	return text;
}
}	 // namespace

juce::String juce_text(std::string_view text) {
	return juce::String::fromUTF8(text.data(), static_cast<int>(text.size()));
}

juce::Colour background_colour() {
	return juce::Colour::fromRGB(17, 23, 32);
}

juce::Colour panel_colour() {
	return juce::Colour::fromRGB(28, 37, 50);
}

juce::Colour accent_colour() {
	return juce::Colour::fromRGB(96, 165, 250);
}

juce::Colour text_colour() {
	return juce::Colour::fromRGB(235, 242, 252);
}

juce::Colour muted_text_colour() {
	return juce::Colour::fromRGB(178, 190, 205);
}

juce::Colour surface_colour() {
	return juce::Colour::fromRGB(35, 46, 62);
}

juce::Colour elevated_surface_colour() {
	return juce::Colour::fromRGB(42, 55, 73);
}

juce::Colour outline_colour() {
	return juce::Colour::fromRGB(84, 101, 122);
}

juce::Colour warning_panel_colour() {
	return juce::Colour::fromRGB(129, 82, 16);
}

void style_text_button(juce::TextButton& button) {
	button.setColour(juce::TextButton::buttonColourId, surface_colour());
	button.setColour(juce::TextButton::buttonOnColourId,
					 accent_colour().withAlpha(0.70f));
	button.setColour(juce::TextButton::textColourOffId, text_colour());
	button.setColour(juce::TextButton::textColourOnId, text_colour());
}

void style_text_editor(juce::TextEditor& editor) {
	editor.setFont(juce::FontOptions(16.0f, juce::Font::plain));
	editor.setColour(juce::TextEditor::backgroundColourId, panel_colour());
	editor.setColour(juce::TextEditor::textColourId, text_colour());
	editor.setColour(juce::TextEditor::highlightColourId,
					 accent_colour().withAlpha(0.46f));
	editor.setColour(juce::TextEditor::highlightedTextColourId, text_colour());
	editor.setColour(juce::TextEditor::outlineColourId, outline_colour());
	editor.setColour(juce::TextEditor::focusedOutlineColourId, accent_colour());
}

void TouchScrollActivationGuard::begin(const juce::MouseEvent& event) noexcept {
	tracking_touch_like = touch_like_source(event);
	suppress_release	= false;
}

void TouchScrollActivationGuard::update(
	const juce::MouseEvent& event) noexcept {
	if (!tracking_touch_like || !touch_like_source(event))
		return;

	if (event.mouseWasDraggedSinceMouseDown()
		|| event.getDistanceFromDragStart()
			   > scroll_suppression_distance_pixels) {
		suppress_release = true;
	}
}

bool TouchScrollActivationGuard::consume_suppressed_release(
	const juce::MouseEvent& event) noexcept {
	update(event);
	const bool should_suppress = tracking_touch_like && suppress_release;
	tracking_touch_like		   = false;
	suppress_release		   = false;
	return should_suppress;
}

TextRowComponent::TextRowComponent(juce::String text_value,
								   juce::Colour background_value,
								   bool strong_value)
	: text(std::move(text_value))
	, background(background_value)
	, strong(strong_value) {
	setOpaque(true);
	setBufferedToImage(true);
}

void TextRowComponent::paint(juce::Graphics& graphics) {
	graphics.fillAll(background_colour());
	draw_card_background(graphics, getLocalBounds(), background, false);

	const float font_height			 = strong ? 16.5f : 14.8f;
	juce::Rectangle<int> text_bounds = getLocalBounds().reduced(14, 7);
	graphics.setColour(text_colour());
	graphics.setFont(juce::FontOptions(
		font_height, strong ? juce::Font::bold : juce::Font::plain));
	graphics.drawFittedText(
		text, text_bounds, juce::Justification::centredLeft,
		fitted_line_count(getHeight(), 14, font_height + 2.0f), 0.92f);
}

RowButtonComponent::RowButtonComponent(juce::String text_value,
									   juce::Colour background_value)
	: juce::Button(std::move(text_value)), background(background_value) {
	setOpaque(true);
	setBufferedToImage(true);
}

void RowButtonComponent::paintButton(juce::Graphics& graphics, bool highlighted,
									 bool down) {
	graphics.fillAll(background_colour());
	juce::Colour fill = background;
	if (down)
		fill = fill.brighter(0.14f);
	draw_card_background(graphics, getLocalBounds(), fill, highlighted);

	const float font_height			 = 15.0f;
	juce::Rectangle<int> text_bounds = getLocalBounds().reduced(14, 8);
	if (isEnabled())
		text_bounds.removeFromRight(18);

	graphics.setColour(isEnabled() ? text_colour()
								   : muted_text_colour().withAlpha(0.58f));
	graphics.setFont(juce::FontOptions(font_height, juce::Font::plain));
	graphics.drawFittedText(
		getButtonText(), text_bounds, juce::Justification::centredLeft,
		fitted_line_count(getHeight(), 16, font_height + 2.0f), 0.92f);

	if (isEnabled()) {
		juce::Rectangle<int> arrow_bounds =
			getLocalBounds().removeFromRight(30).reduced(8, 12);
		juce::Path chevron;
		const float centre_y = arrow_bounds.getCentreY();
		const float left_x	 = static_cast<float>(arrow_bounds.getX()) + 3.0f;
		const float right_x =
			static_cast<float>(arrow_bounds.getRight()) - 3.0f;
		const float top_y = static_cast<float>(arrow_bounds.getY()) + 2.0f;
		const float bottom_y =
			static_cast<float>(arrow_bounds.getBottom()) - 2.0f;
		chevron.startNewSubPath(left_x, top_y);
		chevron.lineTo(right_x, centre_y);
		chevron.lineTo(left_x, bottom_y);
		graphics.setColour(muted_text_colour());
		graphics.strokePath(
			chevron, juce::PathStrokeType{2.0f, juce::PathStrokeType::curved,
										  juce::PathStrokeType::rounded});
	}
}

void RowButtonComponent::mouseDown(const juce::MouseEvent& event) {
	touch_activation_guard.begin(event);
	juce::Button::mouseDown(event);
}

void RowButtonComponent::mouseDrag(const juce::MouseEvent& event) {
	touch_activation_guard.update(event);
	juce::Button::mouseDrag(event);
}

void RowButtonComponent::mouseUp(const juce::MouseEvent& event) {
	if (touch_activation_guard.consume_suppressed_release(event)) {
		setState(juce::Button::buttonNormal);
		return;
	}

	juce::Button::mouseUp(event);
}

TouchSafeToggleButton::TouchSafeToggleButton(juce::String text_value)
	: juce::ToggleButton(std::move(text_value)) {
	setBufferedToImage(true);
}

void TouchSafeToggleButton::mouseDown(const juce::MouseEvent& event) {
	touch_activation_guard.begin(event);
	juce::Button::mouseDown(event);
}

void TouchSafeToggleButton::mouseDrag(const juce::MouseEvent& event) {
	touch_activation_guard.update(event);
	juce::Button::mouseDrag(event);
}

void TouchSafeToggleButton::mouseUp(const juce::MouseEvent& event) {
	if (touch_activation_guard.consume_suppressed_release(event)) {
		setState(juce::Button::buttonNormal);
		return;
	}

	juce::Button::mouseUp(event);
}

ImagePanelComponent::ImagePanelComponent(juce::Image image_value,
										 juce::String caption_value,
										 juce::String placeholder_value)
	: image(std::move(image_value))
	, caption(std::move(caption_value))
	, placeholder(std::move(placeholder_value)) {
	setOpaque(true);
	setBufferedToImage(true);
}

void ImagePanelComponent::paint(juce::Graphics& graphics) {
	graphics.fillAll(background_colour());
	draw_card_background(graphics, getLocalBounds(), surface_colour(), false);
	juce::Rectangle<int> area		  = getLocalBounds().reduced(12, 10);
	juce::Rectangle<int> caption_area = area.removeFromBottom(34);
	if (image.isValid()) {
		graphics.drawImageWithin(
			image, area.getX(), area.getY(), area.getWidth(), area.getHeight(),
			juce::RectanglePlacement::centred
				| juce::RectanglePlacement::onlyReduceInSize);
	} else {
		graphics.setColour(panel_colour());
		graphics.fillRoundedRectangle(area.toFloat(), 10.0f);
		graphics.setColour(muted_text_colour());
		graphics.setFont(juce::FontOptions(16.0f, juce::Font::plain));
		graphics.drawFittedText(placeholder, area.reduced(10),
								juce::Justification::centred, 3, 0.92f);
	}

	graphics.setColour(text_colour());
	graphics.setFont(juce::FontOptions(15.5f, juce::Font::bold));
	graphics.drawFittedText(caption, caption_area,
							juce::Justification::centredLeft, 2, 0.92f);
}

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

TagRowEditorComponent::TagRowEditorComponent(
	std::size_t row_index_value, domain::TagRow tag_value,
	std::function<void(std::size_t, domain::TagRow)> change_handler,
	std::function<void(std::size_t)> remove_handler)
	: row_index(row_index_value)
	, on_change(std::move(change_handler))
	, on_remove(std::move(remove_handler)) {
	setOpaque(true);
	setBufferedToImage(true);
	key_editor.setText(juce_text(tag_value.key), juce::dontSendNotification);
	value_editor.setText(juce_text(tag_value.value),
						 juce::dontSendNotification);
	key_editor.setTextToShowWhenEmpty("Key", muted_text_colour());
	value_editor.setTextToShowWhenEmpty("Value", muted_text_colour());
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
										 juce::String first_placeholder,
										 juce::String second_placeholder)
	: first_editor(first_editor_value), second_editor(second_editor_value) {
	setOpaque(true);
	setBufferedToImage(true);
	prepare_editor(first_editor, std::move(first_placeholder));
	prepare_editor(second_editor, std::move(second_placeholder));
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
										 juce::String placeholder) {
	if (juce::Component* parent = editor.getParentComponent())
		parent->removeChildComponent(&editor);
	editor.setTextToShowWhenEmpty(std::move(placeholder), muted_text_colour());
	editor.setMultiLine(false);
	editor.setReturnKeyStartsNewLine(false);
	editor.setScrollbarsShown(false);
	style_text_editor(editor);
}

class PendingPhotoCardsComponent final : public juce::Component {
public:
	PendingPhotoCardsComponent(
		std::vector<PendingPhotoSource> sources_value,
		std::function<void(std::size_t)> remove_handler_value)
		: sources(std::move(sources_value)) {
		setOpaque(true);
		setBufferedToImage(true);
		for (std::size_t index = 0; index < sources.size(); ++index) {
			std::unique_ptr<juce::TextButton> button =
				std::make_unique<juce::TextButton>("Remove");
			style_text_button(*button);
			button->onClick = [remove_handler_value, index] {
				if (remove_handler_value)
					remove_handler_value(index);
			};
			addAndMakeVisible(*button);
			remove_buttons.push_back(std::move(button));
		}
		setSize(preferred_width(), 62);
	}

	[[nodiscard]] int preferred_width() const noexcept {
		if (sources.empty())
			return 1;
		return 10 + static_cast<int>(sources.size()) * (card_width + card_gap);
	}

	void resized() override {
		for (std::size_t index = 0; index < remove_buttons.size(); ++index) {
			juce::Rectangle<int> card = card_bounds(index).reduced(8, 7);
			remove_buttons[index]->setBounds(card.removeFromBottom(24));
		}
	}

	void paint(juce::Graphics& graphics) override {
		graphics.fillAll(panel_colour());
		if (sources.empty()) {
			graphics.setColour(muted_text_colour());
			graphics.setFont(juce::FontOptions(14.0f, juce::Font::plain));
			graphics.drawFittedText("No staged entries yet.", getLocalBounds(),
									juce::Justification::centredLeft, 1, 0.90f);
			return;
		}

		for (std::size_t index = 0; index < sources.size(); ++index) {
			const PendingPhotoSource& source = sources[index];
			const bool failed =
				source.status == PendingPhotoStatus::Failed
				|| source.status == PendingPhotoStatus::Cancelled;
			juce::Colour colour =
				failed ? warning_panel_colour() : elevated_surface_colour();
			juce::Rectangle<int> card = card_bounds(index);
			draw_card_background(graphics, card, colour,
								 source.ready_for_import());
			juce::Rectangle<int> text_area = card.reduced(8, 7);
			text_area.removeFromBottom(26);
			graphics.setColour(text_colour());
			graphics.setFont(juce::FontOptions(13.0f, juce::Font::plain));
			graphics.drawFittedText(
				juce_text(pending_photo_card_text(source, index + 1U)),
				text_area, juce::Justification::centredLeft, 3, 0.88f);
		}
	}

private:
	[[nodiscard]] juce::Rectangle<int> card_bounds(
		std::size_t index) const noexcept {
		return juce::Rectangle<int>{
			6 + static_cast<int>(index) * (card_width + card_gap), 2,
			card_width, std::max(1, getHeight() - 4)};
	}

	static constexpr int card_width = 142;
	static constexpr int card_gap	= 8;

	std::vector<PendingPhotoSource> sources;
	std::vector<std::unique_ptr<juce::TextButton>> remove_buttons;
};

PendingPhotoStripComponent::PendingPhotoStripComponent(
	std::vector<PendingPhotoSource> sources_value,
	std::function<void()> add_handler, std::function<void()> clear_handler,
	std::function<void(std::size_t)> remove_handler)
	: sources(std::move(sources_value))
	, cards(std::make_unique<PendingPhotoCardsComponent>(
		  sources, std::move(remove_handler))) {
	setOpaque(true);
	setBufferedToImage(true);
	style_text_button(add_button);
	style_text_button(clear_button);
	add_button.onClick	 = std::move(add_handler);
	clear_button.onClick = std::move(clear_handler);
	clear_button.setEnabled(!sources.empty());
	card_viewport.setViewedComponent(cards.get(), false);
	card_viewport.setScrollBarsShown(false, true);
	card_viewport.setScrollOnDragMode(
		juce::Viewport::ScrollOnDragMode::nonHover);
	addAndMakeVisible(add_button);
	addAndMakeVisible(clear_button);
	addAndMakeVisible(card_viewport);
}

PendingPhotoStripComponent::~PendingPhotoStripComponent() {
	card_viewport.setViewedComponent(nullptr, false);
}

void PendingPhotoStripComponent::resized() {
	juce::Rectangle<int> area	= getLocalBounds().reduced(10, 8);
	juce::Rectangle<int> header = area.removeFromTop(32);
	clear_button.setBounds(header.removeFromRight(82).reduced(2));
	header.removeFromRight(6);
	add_button.setBounds(header.removeFromRight(96).reduced(2));
	area.removeFromTop(4);
	card_viewport.setBounds(area);
	if (cards) {
		cards->setSize(cards->preferred_width(),
					   std::max(1, card_viewport.getHeight()));
	}
}

void PendingPhotoStripComponent::paint(juce::Graphics& graphics) {
	graphics.fillAll(background_colour());
	draw_card_background(graphics, getLocalBounds(), panel_colour(), false);
	juce::Rectangle<int> header = getLocalBounds().reduced(12, 8);
	header.setHeight(30);
	header.removeFromRight(188);
	graphics.setColour(text_colour());
	graphics.setFont(juce::FontOptions(14.5f, juce::Font::bold));
	graphics.drawFittedText(juce_text(pending_photo_count_text(sources)),
							header, juce::Justification::centredLeft, 1, 0.90f);
}
}	 // namespace shuba::ui
