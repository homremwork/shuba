#include "UI/View/Primitives/PhotoViewerPinchGesture.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace shuba::ui {
namespace {
[[nodiscard]] float clamp_pinch_pan_axis(float pan, float unzoomed_extent,
										 float slot_extent,
										 float zoom_scale) noexcept {
	const float maximum_pan =
		std::max(0.0f, (unzoomed_extent * zoom_scale - slot_extent) * 0.5f);
	return std::clamp(pan, -maximum_pan, maximum_pan);
}
}	 // namespace

bool photo_viewer_gesture_starts_in_image_slot(
	const juce::Rectangle<float>& image_slot,
	juce::Point<float> position) noexcept {
	return !image_slot.isEmpty() && image_slot.contains(position);
}

bool photo_viewer_blocks_viewport_drag(bool image_is_zoomed,
									   bool tracking_image_pointer,
									   bool pinch_is_active) noexcept {
	return pinch_is_active || (image_is_zoomed && tracking_image_pointer);
}

PhotoViewerPinchTransform apply_photo_viewer_pinch_update(
	PhotoViewerPinchTransform current, const PhotoViewerPinchUpdate& update,
	const PhotoViewerPinchGeometry& geometry, float minimum_zoom_scale,
	float maximum_zoom_scale) noexcept {
	if (!std::isfinite(current.zoom_scale) || current.zoom_scale <= 0.0f
		|| !std::isfinite(update.scale_factor) || update.scale_factor <= 0.0f
		|| !std::isfinite(minimum_zoom_scale)
		|| !std::isfinite(maximum_zoom_scale) || minimum_zoom_scale <= 0.0f
		|| maximum_zoom_scale < minimum_zoom_scale) {
		return current;
	}

	const float zoom_scale =
		std::clamp(current.zoom_scale * update.scale_factor, minimum_zoom_scale,
				   maximum_zoom_scale);
	const float scale_ratio = zoom_scale / current.zoom_scale;
	const juce::Point<float> current_image_centre =
		geometry.slot_centre + current.pan_offset;
	const juce::Point<float> unclamped_pan =
		update.midpoint - (update.midpoint - current_image_centre) * scale_ratio
		- geometry.slot_centre;
	return PhotoViewerPinchTransform{
		.zoom_scale = zoom_scale,
		.pan_offset = {clamp_pinch_pan_axis(
						   unclamped_pan.x, geometry.unzoomed_image.getWidth(),
						   geometry.slot.getWidth(), zoom_scale),
					   clamp_pinch_pan_axis(
						   unclamped_pan.y, geometry.unzoomed_image.getHeight(),
						   geometry.slot.getHeight(), zoom_scale)}};
}

void PhotoViewerPinchGesture::begin_touch(
	int touch_index, juce::Point<float> position) noexcept {
	if (touch_index < 0)
		return;

	if (first_touch.has_value() && first_touch->index == touch_index) {
		first_touch->position = position;
		return;
	}
	if (second_touch.has_value() && second_touch->index == touch_index) {
		second_touch->position = position;
		return;
	}
	if (!first_touch.has_value()) {
		first_touch =
			PhotoViewerPinchTouch{.index = touch_index, .position = position};
		return;
	}
	if (!second_touch.has_value()) {
		second_touch =
			PhotoViewerPinchTouch{.index = touch_index, .position = position};
		previous_distance =
			distance_between(first_touch->position, second_touch->position);
	}
}

std::optional<PhotoViewerPinchUpdate> PhotoViewerPinchGesture::update_touch(
	int touch_index, juce::Point<float> position) noexcept {
	if (touch_index < 0)
		return std::nullopt;

	if (first_touch.has_value() && first_touch->index == touch_index)
		first_touch->position = position;
	else if (second_touch.has_value() && second_touch->index == touch_index)
		second_touch->position = position;
	else
		return std::nullopt;

	return make_update();
}

void PhotoViewerPinchGesture::end_touch(int touch_index) noexcept {
	if (touch_index < 0)
		return;

	if (first_touch.has_value() && first_touch->index == touch_index) {
		first_touch = std::move(second_touch);
		second_touch.reset();
		previous_distance = 0.0f;
		return;
	}
	if (second_touch.has_value() && second_touch->index == touch_index) {
		second_touch.reset();
		previous_distance = 0.0f;
		return;
	}
}

void PhotoViewerPinchGesture::reset() noexcept {
	first_touch.reset();
	second_touch.reset();
	previous_distance = 0.0f;
}

bool PhotoViewerPinchGesture::active() const noexcept {
	return first_touch.has_value() && second_touch.has_value();
}

bool PhotoViewerPinchGesture::tracks_touch(int touch_index) const noexcept {
	return (first_touch.has_value() && first_touch->index == touch_index)
		   || (second_touch.has_value() && second_touch->index == touch_index);
}

std::optional<PhotoViewerPinchTouch> PhotoViewerPinchGesture::single_touch()
	const noexcept {
	if (second_touch.has_value())
		return std::nullopt;
	return first_touch;
}

std::optional<PhotoViewerPinchUpdate>
PhotoViewerPinchGesture::make_update() noexcept {
	if (!active())
		return std::nullopt;

	const float current_distance =
		distance_between(first_touch->position, second_touch->position);
	if (current_distance <= 0.0f || previous_distance <= 0.0f) {
		previous_distance = current_distance;
		return std::nullopt;
	}

	const PhotoViewerPinchUpdate update{
		.scale_factor = current_distance / previous_distance,
		.midpoint =
			midpoint_between(first_touch->position, second_touch->position)};
	previous_distance = current_distance;
	return update;
}

float PhotoViewerPinchGesture::distance_between(
	const juce::Point<float>& first,
	const juce::Point<float>& second) noexcept {
	const float delta_x = second.x - first.x;
	const float delta_y = second.y - first.y;
	return std::sqrt(delta_x * delta_x + delta_y * delta_y);
}

juce::Point<float> PhotoViewerPinchGesture::midpoint_between(
	const juce::Point<float>& first,
	const juce::Point<float>& second) noexcept {
	return {(first.x + second.x) * 0.5f, (first.y + second.y) * 0.5f};
}
}	 // namespace shuba::ui
