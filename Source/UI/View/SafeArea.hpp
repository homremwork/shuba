#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>

namespace shuba::ui {
struct FullscreenSafeAreaInsets final {
	int top{};
	int left{};
	int right{};
};

[[nodiscard]] inline FullscreenSafeAreaInsets make_fullscreen_safe_area_insets(
	const juce::BorderSize<int>& safe_area) noexcept {
	return FullscreenSafeAreaInsets{.top   = std::max(0, safe_area.getTop()),
									.left  = std::max(0, safe_area.getLeft()),
									.right = std::max(0, safe_area.getRight())};
}

[[nodiscard]] inline juce::Rectangle<int> apply_fullscreen_safe_area(
	juce::Rectangle<int> bounds, FullscreenSafeAreaInsets insets) noexcept {
	if (bounds.isEmpty())
		return bounds;

	const int clamped_left =
		std::min(std::max(0, insets.left), std::max(0, bounds.getWidth() - 1));
	const int width_after_left = std::max(1, bounds.getWidth() - clamped_left);
	const int clamped_right =
		std::min(std::max(0, insets.right), std::max(0, width_after_left - 1));
	const int clamped_top =
		std::min(std::max(0, insets.top), std::max(0, bounds.getHeight() - 1));

	return juce::Rectangle<int>{bounds.getX() + clamped_left,
								bounds.getY() + clamped_top,
								std::max(1, width_after_left - clamped_right),
								std::max(1, bounds.getHeight() - clamped_top)};
}

[[nodiscard]] inline juce::Rectangle<int> fullscreen_safe_content_bounds(
	const juce::Component& component) {
	juce::Rectangle<int> bounds = component.getLocalBounds();

#if JUCE_IOS || JUCE_ANDROID
	const juce::Displays::Display* display =
		juce::Desktop::getInstance().getDisplays().getDisplayForRect(
			component.getScreenBounds());
	if (display != nullptr)
		return apply_fullscreen_safe_area(
			bounds, make_fullscreen_safe_area_insets(display->safeAreaInsets));
#endif

	return bounds;
}
}	 // namespace shuba::ui
