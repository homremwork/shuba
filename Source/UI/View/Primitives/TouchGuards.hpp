#pragma once

#include "JuceHeader.h"

namespace shuba::ui {
class TouchScrollActivationGuard final {
public:
	void begin(const juce::MouseEvent& event) noexcept;
	void update(const juce::MouseEvent& event) noexcept;
	[[nodiscard]] bool consume_suppressed_release(
		const juce::MouseEvent& event) noexcept;

private:
	static constexpr int scroll_suppression_distance_pixels = 8;

	bool tracking_touch_like{};
	bool suppress_release{};
};
}	 // namespace shuba::ui
