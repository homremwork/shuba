#include "UI/View/Primitives/Palette.hpp"

#include <algorithm>

namespace shuba::ui {
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

int fitted_line_count(int height, int vertical_padding,
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

void draw_forward_chevron(juce::Graphics& graphics, juce::Rectangle<int> bounds,
						  juce::Colour colour) {
	juce::Path chevron;
	const float centre_y = static_cast<float>(bounds.getCentreY());
	const float left_x	 = static_cast<float>(bounds.getX()) + 3.0f;
	const float right_x	 = static_cast<float>(bounds.getRight()) - 3.0f;
	const float top_y	 = static_cast<float>(bounds.getY()) + 2.0f;
	const float bottom_y = static_cast<float>(bounds.getBottom()) - 2.0f;
	chevron.startNewSubPath(left_x, top_y);
	chevron.lineTo(right_x, centre_y);
	chevron.lineTo(left_x, bottom_y);
	graphics.setColour(colour);
	graphics.strokePath(chevron,
						juce::PathStrokeType{2.0f, juce::PathStrokeType::curved,
											 juce::PathStrokeType::rounded});
}
}	 // namespace shuba::ui
