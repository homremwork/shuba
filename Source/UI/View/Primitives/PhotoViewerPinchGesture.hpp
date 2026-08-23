#pragma once

#include "JuceHeader.h"

#include <optional>

namespace shuba::ui {
struct PhotoViewerPinchTouch final {
	int index{};
	juce::Point<float> position;
};

struct PhotoViewerPinchUpdate final {
	float scale_factor{1.0f};
	juce::Point<float> midpoint;
};

struct PhotoViewerPinchTransform final {
	float zoom_scale{1.0f};
	juce::Point<float> pan_offset;
};

struct PhotoViewerPinchGeometry final {
	juce::Point<float> slot_centre;
	juce::Rectangle<float> slot;
	juce::Rectangle<float> unzoomed_image;
};

[[nodiscard]] bool photo_viewer_gesture_starts_in_image_slot(
	const juce::Rectangle<float>& image_slot,
	juce::Point<float> position) noexcept;

[[nodiscard]] bool photo_viewer_blocks_viewport_drag(
	bool image_is_zoomed, bool tracking_image_pointer,
	bool pinch_is_active) noexcept;

[[nodiscard]] PhotoViewerPinchTransform apply_photo_viewer_pinch_update(
	PhotoViewerPinchTransform current, const PhotoViewerPinchUpdate& update,
	const PhotoViewerPinchGeometry& geometry, float minimum_zoom_scale,
	float maximum_zoom_scale) noexcept;

class PhotoViewerPinchGesture final {
public:
	void begin_touch(int touch_index, juce::Point<float> position) noexcept;
	[[nodiscard]] std::optional<PhotoViewerPinchUpdate> update_touch(
		int touch_index, juce::Point<float> position) noexcept;
	void end_touch(int touch_index) noexcept;
	void reset() noexcept;

	[[nodiscard]] bool active() const noexcept;
	[[nodiscard]] bool tracks_touch(int touch_index) const noexcept;
	[[nodiscard]] std::optional<PhotoViewerPinchTouch> single_touch()
		const noexcept;

private:
	[[nodiscard]] std::optional<PhotoViewerPinchUpdate> make_update() noexcept;
	[[nodiscard]] static float distance_between(
		const juce::Point<float>& first,
		const juce::Point<float>& second) noexcept;
	[[nodiscard]] static juce::Point<float> midpoint_between(
		const juce::Point<float>& first,
		const juce::Point<float>& second) noexcept;

	std::optional<PhotoViewerPinchTouch> first_touch;
	std::optional<PhotoViewerPinchTouch> second_touch;
	float previous_distance{};
};
}	 // namespace shuba::ui
