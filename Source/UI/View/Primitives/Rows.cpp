#include "UI/View/Primitives/Rows.hpp"

#include "UI/View/Primitives/Palette.hpp"

#include <utility>

namespace shuba::ui {
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
		draw_forward_chevron(graphics, arrow_bounds, muted_text_colour());
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
}	 // namespace shuba::ui
