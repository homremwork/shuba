#pragma once

#include "JuceHeader.h"

#include <string_view>

namespace shuba::ui {
[[nodiscard]] juce::String juce_text(std::string_view text);

[[nodiscard]] juce::Colour background_colour();
[[nodiscard]] juce::Colour panel_colour();
[[nodiscard]] juce::Colour accent_colour();
[[nodiscard]] juce::Colour text_colour();
[[nodiscard]] juce::Colour muted_text_colour();
[[nodiscard]] juce::Colour surface_colour();
[[nodiscard]] juce::Colour elevated_surface_colour();
[[nodiscard]] juce::Colour outline_colour();
[[nodiscard]] juce::Colour warning_panel_colour();

void style_text_button(juce::TextButton& button);
void style_text_editor(juce::TextEditor& editor);

[[nodiscard]] int fitted_line_count(int height, int vertical_padding,
									float font_height) noexcept;
void draw_card_background(juce::Graphics& graphics, juce::Rectangle<int> bounds,
						  juce::Colour colour, bool highlighted);
void draw_forward_chevron(juce::Graphics& graphics, juce::Rectangle<int> bounds,
						  juce::Colour colour);
}	 // namespace shuba::ui
