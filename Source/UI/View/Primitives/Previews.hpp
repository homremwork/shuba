#pragma once

#include "JuceHeader.h"
#include "UI/View/Primitives/PhotoViewerPinchGesture.hpp"
#include "UI/View/Primitives/TouchGuards.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace shuba::localization {
class Localization;
}

namespace shuba::ui {
enum class PreviewImageVisualState : std::uint8_t {
	Empty,
	Loading,
	Loaded,
	Broken,
	Staged,
};

[[nodiscard]] int preview_badge_width(const juce::String& text,
									  int container_width);

void draw_preview_image_slot(juce::Graphics& graphics,
							 juce::Rectangle<int> bounds,
							 const juce::Image& image,
							 const juce::String& placeholder,
							 PreviewImageVisualState state, bool compact,
							 const localization::Localization& localization);

class ImagePanelComponent final : public juce::Component {
public:
	ImagePanelComponent(juce::Image image_value, juce::String caption_value,
						juce::String placeholder_value);

	void paint(juce::Graphics& graphics) override;

private:
	juce::Image image;
	juce::String caption;
	juce::String placeholder;
};

struct PhotoCarouselSlide final {
	juce::Image image;
	juce::String title;
	juce::String caption;
	juce::String placeholder;
	PreviewImageVisualState state{PreviewImageVisualState::Loading};
	juce::String action_label;
	std::function<void()> action_handler;
	bool action_enabled{true};
};

class PhotoCarouselComponent final : public juce::Component {
public:
	PhotoCarouselComponent(std::vector<PhotoCarouselSlide> slides_value,
						   std::size_t selected_index_value,
						   localization::Localization& localization_value,
						   std::function<void(std::size_t)> select_handler,
						   std::function<void()> activate_handler);

	[[nodiscard]] std::size_t selected_index() const noexcept;
	[[nodiscard]] std::size_t slide_count() const noexcept;

	void resized() override;
	void paint(juce::Graphics& graphics) override;

private:
	void mouseDown(const juce::MouseEvent& event) override;
	void mouseDrag(const juce::MouseEvent& event) override;
	void mouseUp(const juce::MouseEvent& event) override;

	[[nodiscard]] const PhotoCarouselSlide* selected_slide() const noexcept;
	void move_selection(int direction);
	void select_slide(std::size_t slide_index);
	void refresh_controls();

	static constexpr int gesture_axis_threshold_pixels = 14;
	static constexpr int swipe_threshold_pixels		   = 42;
	static constexpr int tap_tolerance_pixels		   = 8;

	std::vector<PhotoCarouselSlide> slides;
	std::size_t selected_slide_index{};
	localization::Localization& localization;
	std::function<void(std::size_t)> on_select;
	std::function<void()> on_activate;
	juce::TextButton previous_button;
	juce::TextButton next_button;
	juce::TextButton slide_action_button;
	bool tracking_pointer{};
	bool horizontal_gesture{};
	bool vertical_gesture{};
};

struct PhotoViewerImageModel final {
	juce::Image image;
	juce::String title;
	juce::String caption;
	juce::String placeholder;
	PreviewImageVisualState state{PreviewImageVisualState::Loading};
	int rotation_quarter_turns{};
	bool multiple_photos{};
};

struct PhotoViewerImageHandlers final {
	std::function<void()> select_previous;
	std::function<void()> select_next;
};

class PhotoViewerImageComponent final : public juce::Component {
public:
	PhotoViewerImageComponent(PhotoViewerImageModel model_value,
							  PhotoViewerImageHandlers handlers_value,
							  localization::Localization& localization_value);

	void paint(juce::Graphics& graphics) override;

private:
	struct Layout final {
		juce::Rectangle<int> image;
		juce::Rectangle<int> caption;
	};

	void mouseDown(const juce::MouseEvent& event) override;
	void mouseDrag(const juce::MouseEvent& event) override;
	void mouseUp(const juce::MouseEvent& event) override;
	void mouseDoubleClick(const juce::MouseEvent& event) override;

	[[nodiscard]] Layout calculate_layout() const;
	[[nodiscard]] juce::Rectangle<float> image_gesture_bounds() const;
	[[nodiscard]] bool zoomed() const noexcept;
	void apply_pinch_update(const PhotoViewerPinchUpdate& update);
	void clamp_pan();
	void reset_zoom();
	void refresh_viewport_drag_policy();

	static constexpr int gesture_axis_threshold_pixels = 14;
	static constexpr int swipe_threshold_pixels		   = 42;
	static constexpr float minimum_zoom_scale		   = 1.0f;
	static constexpr float maximum_zoom_scale		   = 4.0f;

	PhotoViewerImageModel model;
	PhotoViewerImageHandlers handlers;
	localization::Localization& localization;
	float zoom_scale{minimum_zoom_scale};
	juce::Point<float> pan_offset;
	juce::Point<float> drag_start_pan;
	juce::Point<float> single_pointer_start_position;
	PhotoViewerPinchGesture pinch_gesture;
	int single_pointer_index{-1};
	bool tracking_pointer{};
	bool horizontal_gesture{};
	bool vertical_gesture{};
};

struct PreviewCardContent final {
	juce::Image image;
	juce::String title;
	juce::String subtitle;
	juce::String metadata;
	juce::String placeholder;
	PreviewImageVisualState state{PreviewImageVisualState::Loading};
};

class PreviewCardButtonComponent final : public juce::Button {
public:
	PreviewCardButtonComponent(PreviewCardContent content_value,
							   juce::Colour background_value,
							   localization::Localization& localization_value);

	void paintButton(juce::Graphics& graphics, bool highlighted,
					 bool down) override;

private:
	void mouseDown(const juce::MouseEvent& event) override;
	void mouseDrag(const juce::MouseEvent& event) override;
	void mouseUp(const juce::MouseEvent& event) override;

	PreviewCardContent content;
	juce::Colour background;
	localization::Localization& localization;
	TouchScrollActivationGuard touch_activation_guard;
};

struct CompactStorageCardContent final {
	juce::Image image;
	juce::String name;
	juce::String item_count;
	juce::String placeholder;
	PreviewImageVisualState state{PreviewImageVisualState::Loading};
	std::function<void()> on_activate;
	bool enabled{true};
};

class CompactStorageCardButtonComponent final : public juce::Button {
public:
	CompactStorageCardButtonComponent(
		CompactStorageCardContent content_value, juce::Colour background_value,
		localization::Localization& localization_value);

	void paintButton(juce::Graphics& graphics, bool highlighted,
					 bool down) override;

private:
	void mouseDown(const juce::MouseEvent& event) override;
	void mouseDrag(const juce::MouseEvent& event) override;
	void mouseUp(const juce::MouseEvent& event) override;

	CompactStorageCardContent content;
	juce::Colour background;
	localization::Localization& localization;
	TouchScrollActivationGuard touch_activation_guard;
};

class CompactStorageStripComponent final : public juce::Component {
public:
	explicit CompactStorageStripComponent(
		std::vector<CompactStorageCardContent> cards_value,
		localization::Localization& localization_value);

	[[nodiscard]] static int preferred_height() noexcept;

	void resized() override;
	void paint(juce::Graphics& graphics) override;

private:
	static constexpr int horizontal_padding = 12;
	static constexpr int vertical_padding	= 4;
	static constexpr int card_width			= 148;
	static constexpr int card_gap			= 8;

	std::unique_ptr<juce::Component> card_row;
	std::vector<std::unique_ptr<CompactStorageCardButtonComponent>> cards;
	localization::Localization& localization;
	juce::Viewport viewport{"storage-strip-viewport"};
};
}	 // namespace shuba::ui
