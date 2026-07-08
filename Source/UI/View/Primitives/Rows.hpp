#pragma once

#include "JuceHeader.h"
#include "UI/View/Primitives/TouchGuards.hpp"

namespace shuba::ui {
class TextRowComponent final : public juce::Component {
public:
	TextRowComponent(juce::String text_value, juce::Colour background_value,
					 bool strong_value);

	void paint(juce::Graphics& graphics) override;

private:
	juce::String text;
	juce::Colour background;
	bool strong{};
};

class RowButtonComponent final : public juce::Button {
public:
	RowButtonComponent(const juce::String& text_value,
					   juce::Colour background_value);

	void paintButton(juce::Graphics& graphics, bool highlighted,
					 bool down) override;

private:
	void mouseDown(const juce::MouseEvent& event) override;
	void mouseDrag(const juce::MouseEvent& event) override;
	void mouseUp(const juce::MouseEvent& event) override;

	juce::Colour background;
	TouchScrollActivationGuard touch_activation_guard;
};

class TouchSafeToggleButton final : public juce::ToggleButton {
public:
	explicit TouchSafeToggleButton(const juce::String& text_value);

private:
	void mouseDown(const juce::MouseEvent& event) override;
	void mouseDrag(const juce::MouseEvent& event) override;
	void mouseUp(const juce::MouseEvent& event) override;

	TouchScrollActivationGuard touch_activation_guard;
};
}	 // namespace shuba::ui
