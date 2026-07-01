#include "UI/View/Primitives/TouchGuards.hpp"

namespace shuba::ui {
namespace {
[[nodiscard]] bool touch_like_source(const juce::MouseEvent& event) noexcept {
	return event.source.isTouch() || event.source.isPen();
}
}	 // namespace

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
}	 // namespace shuba::ui
