#include "UI/View/Primitives/Previews.hpp"

#include "UI/View/Primitives/Palette.hpp"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <utility>

namespace shuba::ui {
namespace {
[[nodiscard]] juce::String preview_state_badge_text(
	PreviewImageVisualState state) {
	switch (state) {
		case PreviewImageVisualState::Empty:
			return "No photos";
		case PreviewImageVisualState::Loading:
			return "Loading";
		case PreviewImageVisualState::Loaded:
			return "Preview";
		case PreviewImageVisualState::Broken:
			return "Broken";
		case PreviewImageVisualState::Staged:
			return "Staged";
	}

	return "Preview";
}

[[nodiscard]] juce::String preview_placeholder_text(
	const juce::String& placeholder, PreviewImageVisualState state) {
	if (!placeholder.isEmpty())
		return placeholder;

	switch (state) {
		case PreviewImageVisualState::Empty:
			return "No photos yet.";
		case PreviewImageVisualState::Loading:
			return "Preview will load when requested.";
		case PreviewImageVisualState::Loaded:
			return "Decoded image cannot be displayed.";
		case PreviewImageVisualState::Broken:
			return "Photo preview is unavailable.";
		case PreviewImageVisualState::Staged:
			return "Staged photo preview is unavailable.";
	}

	return "Photo preview is unavailable.";
}

[[nodiscard]] juce::Colour preview_state_colour(PreviewImageVisualState state) {
	switch (state) {
		case PreviewImageVisualState::Empty:
			return muted_text_colour();
		case PreviewImageVisualState::Loading:
			return accent_colour();
		case PreviewImageVisualState::Loaded:
			return outline_colour();
		case PreviewImageVisualState::Broken:
			return warning_panel_colour().brighter(0.24f);
		case PreviewImageVisualState::Staged:
			return accent_colour().brighter(0.18f);
	}

	return outline_colour();
}

void draw_preview_badge(juce::Graphics& graphics, juce::Rectangle<int> bounds,
						PreviewImageVisualState state) {
	if (state == PreviewImageVisualState::Loaded)
		return;

	juce::String text			   = preview_state_badge_text(state);
	const int character_width	   = 7;
	const int estimated_text_width = text.length() * character_width;
	const int badge_width = std::min(std::max(68, estimated_text_width + 18),
									 std::max(1, bounds.getWidth() - 12));
	juce::Rectangle<int> badge =
		bounds.removeFromTop(25).removeFromRight(badge_width).reduced(4, 3);
	graphics.setColour(preview_state_colour(state).withAlpha(0.86f));
	graphics.fillRoundedRectangle(badge.toFloat(), 8.0f);
	graphics.setColour(text_colour());
	graphics.setFont(juce::FontOptions(11.5f, juce::Font::bold));
	graphics.drawFittedText(text, badge.reduced(6, 0),
							juce::Justification::centred, 1, 0.90f);
}

void draw_image_within_full_opacity(juce::Graphics& graphics,
									const juce::Image& image,
									juce::Rectangle<int> image_area) {
	juce::Graphics::ScopedSaveState saved_state{graphics};
	graphics.setOpacity(1.0f);
	graphics.drawImageWithin(image, image_area.getX(), image_area.getY(),
							 image_area.getWidth(), image_area.getHeight(),
							 juce::RectanglePlacement::centred
								 | juce::RectanglePlacement::onlyReduceInSize,
							 false);
}

void draw_carousel_indicator(juce::Graphics& graphics,
							 juce::Rectangle<int> bounds, std::size_t count,
							 std::size_t selected_index) {
	if (count == 0U)
		return;

	if (count <= 7U) {
		const int dot_count = static_cast<int>(count);
		const int dot_size	= 7;
		const int dot_gap	= 7;
		const int total_width =
			dot_count * dot_size + (dot_count - 1) * dot_gap;
		int x		= bounds.getCentreX() - total_width / 2;
		const int y = bounds.getCentreY() - dot_size / 2;
		for (std::size_t index = 0; index < count; ++index) {
			graphics.setColour(index == selected_index
								   ? accent_colour()
								   : muted_text_colour().withAlpha(0.48f));
			graphics.fillEllipse(static_cast<float>(x), static_cast<float>(y),
								 static_cast<float>(dot_size),
								 static_cast<float>(dot_size));
			x += dot_size + dot_gap;
		}
		return;
	}

	graphics.setColour(muted_text_colour());
	graphics.setFont(juce::FontOptions(13.0f, juce::Font::plain));
	graphics.drawFittedText(juce::String(static_cast<int>(selected_index + 1U))
								+ " / " + juce::String(static_cast<int>(count)),
							bounds, juce::Justification::centred, 1, 0.90f);
}

[[nodiscard]] int normalised_quarter_turns(int quarter_turns) noexcept {
	int value = quarter_turns % 4;
	if (value < 0)
		value += 4;
	return value;
}

[[nodiscard]] float viewer_rotation_radians(int quarter_turns) noexcept {
	return static_cast<float>(normalised_quarter_turns(quarter_turns))
		   * juce::MathConstants<float>::halfPi;
}

struct ViewerImageMetrics final {
	float fit_scale{1.0f};
	float drawn_width{};
	float drawn_height{};
};

[[nodiscard]] ViewerImageMetrics calculate_viewer_image_metrics(
	const juce::Image& image, juce::Rectangle<int> slot, int quarter_turns,
	float zoom_scale) noexcept {
	ViewerImageMetrics metrics;
	if (!image.isValid() || slot.isEmpty())
		return metrics;

	const int normalised_turns = normalised_quarter_turns(quarter_turns);
	const float image_width	   = static_cast<float>(image.getWidth());
	const float image_height   = static_cast<float>(image.getHeight());
	const float rotated_width =
		normalised_turns % 2 == 0 ? image_width : image_height;
	const float rotated_height =
		normalised_turns % 2 == 0 ? image_height : image_width;
	metrics.fit_scale = std::min(
		static_cast<float>(slot.getWidth()) / std::max(1.0f, rotated_width),
		static_cast<float>(slot.getHeight()) / std::max(1.0f, rotated_height));
	const float safe_zoom = std::max(1.0f, zoom_scale);
	metrics.drawn_width	  = rotated_width * metrics.fit_scale * safe_zoom;
	metrics.drawn_height  = rotated_height * metrics.fit_scale * safe_zoom;
	return metrics;
}

void draw_viewer_image_transformed(juce::Graphics& graphics,
								   const juce::Image& image,
								   juce::Rectangle<int> slot, int quarter_turns,
								   float zoom_scale,
								   juce::Point<float> pan_offset) {
	if (!image.isValid() || slot.isEmpty())
		return;

	const ViewerImageMetrics metrics =
		calculate_viewer_image_metrics(image, slot, quarter_turns, zoom_scale);
	if (metrics.fit_scale <= 0.0f)
		return;
	const float draw_scale = metrics.fit_scale * std::max(1.0f, zoom_scale);
	const float centre_x = static_cast<float>(slot.getCentreX()) + pan_offset.x;
	const float centre_y = static_cast<float>(slot.getCentreY()) + pan_offset.y;
	const float image_width	 = static_cast<float>(image.getWidth());
	const float image_height = static_cast<float>(image.getHeight());
	juce::AffineTransform transform =
		juce::AffineTransform::translation(-image_width * 0.5f,
										   -image_height * 0.5f)
			.rotated(viewer_rotation_radians(quarter_turns))
			.scaled(draw_scale)
			.translated(centre_x, centre_y);

	juce::Graphics::ScopedSaveState saved_state{graphics};
	graphics.reduceClipRegion(slot);
	graphics.setOpacity(1.0f);
	graphics.drawImageTransformed(image, transform, false);
}
}	 // namespace

void draw_preview_image_slot(juce::Graphics& graphics,
							 juce::Rectangle<int> bounds,
							 const juce::Image& image,
							 const juce::String& placeholder,
							 PreviewImageVisualState state, bool compact) {
	juce::Rectangle<int> slot = bounds.reduced(compact ? 3 : 6);
	graphics.setColour(juce::Colours::black.withAlpha(0.24f));
	graphics.fillRoundedRectangle(slot.toFloat(), compact ? 8.0f : 12.0f);
	graphics.setColour(outline_colour().withAlpha(0.46f));
	graphics.drawRoundedRectangle(slot.toFloat(), compact ? 8.0f : 12.0f, 1.0f);

	if (image.isValid()) {
		juce::Rectangle<int> image_area = slot.reduced(compact ? 3 : 6);
		draw_image_within_full_opacity(graphics, image, image_area);
	} else {
		graphics.setColour(panel_colour());
		graphics.fillRoundedRectangle(slot.reduced(4).toFloat(),
									  compact ? 6.0f : 10.0f);
		graphics.setColour(muted_text_colour());
		graphics.setFont(
			juce::FontOptions(compact ? 11.5f : 16.0f, juce::Font::plain));
		graphics.drawFittedText(preview_placeholder_text(placeholder, state),
								slot.reduced(compact ? 6 : 12),
								juce::Justification::centred, compact ? 3 : 4,
								0.90f);
	}

	if (!image.isValid())
		draw_preview_badge(graphics, slot, state);
}

ImagePanelComponent::ImagePanelComponent(juce::Image image_value,
										 juce::String caption_value,
										 juce::String placeholder_value)
	: image(std::move(image_value))
	, caption(std::move(caption_value))
	, placeholder(std::move(placeholder_value)) {
	setOpaque(true);
	setBufferedToImage(true);
}

void ImagePanelComponent::paint(juce::Graphics& graphics) {
	graphics.fillAll(background_colour());
	draw_card_background(graphics, getLocalBounds(), surface_colour(), false);
	juce::Rectangle<int> area		  = getLocalBounds().reduced(12, 10);
	juce::Rectangle<int> caption_area = area.removeFromBottom(34);
	if (image.isValid()) {
		draw_image_within_full_opacity(graphics, image, area);
	} else {
		graphics.setColour(panel_colour());
		graphics.fillRoundedRectangle(area.toFloat(), 10.0f);
		graphics.setColour(muted_text_colour());
		graphics.setFont(juce::FontOptions(16.0f, juce::Font::plain));
		graphics.drawFittedText(placeholder, area.reduced(10),
								juce::Justification::centred, 3, 0.92f);
	}

	graphics.setColour(text_colour());
	graphics.setFont(juce::FontOptions(15.5f, juce::Font::bold));
	graphics.drawFittedText(caption, caption_area,
							juce::Justification::centredLeft, 2, 0.92f);
}

PhotoCarouselComponent::PhotoCarouselComponent(
	std::vector<PhotoCarouselSlide> slides_value,
	std::size_t selected_index_value,
	std::function<void(std::size_t)> select_handler,
	std::function<void()> activate_handler)
	: slides(std::move(slides_value))
	, on_select(std::move(select_handler))
	, on_activate(std::move(activate_handler)) {
	setOpaque(true);
	setBufferedToImage(true);
	if (!slides.empty())
		selected_slide_index =
			std::min(selected_index_value, slides.size() - 1U);
	style_text_button(previous_button);
	style_text_button(next_button);
	style_text_button(slide_action_button);
	previous_button.onClick		= [this] { move_selection(-1); };
	next_button.onClick			= [this] { move_selection(1); };
	slide_action_button.onClick = [this] {
		const PhotoCarouselSlide* slide = selected_slide();
		if (slide != nullptr && slide->action_handler)
			slide->action_handler();
	};
	addAndMakeVisible(previous_button);
	addAndMakeVisible(next_button);
	addAndMakeVisible(slide_action_button);
	refresh_controls();
}

std::size_t PhotoCarouselComponent::selected_index() const noexcept {
	return selected_slide_index;
}

std::size_t PhotoCarouselComponent::slide_count() const noexcept {
	return slides.size();
}

void PhotoCarouselComponent::resized() {
	juce::Rectangle<int> area	  = getLocalBounds().reduced(12, 10);
	juce::Rectangle<int> controls = area.removeFromBottom(38);
	const int side_width = std::max(1, std::min(112, controls.getWidth() / 3));
	previous_button.setBounds(controls.removeFromLeft(side_width).reduced(2));
	next_button.setBounds(controls.removeFromRight(side_width).reduced(2));
	if (slide_action_button.isVisible()) {
		const int action_width =
			std::max(1, std::min(136, controls.getWidth()));
		slide_action_button.setBounds(
			controls
				.withSizeKeepingCentre(action_width,
									   std::max(1, controls.getHeight()))
				.reduced(2));
	} else {
		slide_action_button.setBounds(0, 0, 0, 0);
	}
}

void PhotoCarouselComponent::paint(juce::Graphics& graphics) {
	graphics.fillAll(background_colour());
	draw_card_background(graphics, getLocalBounds(), surface_colour(), false);

	juce::Rectangle<int> area	  = getLocalBounds().reduced(12, 10);
	juce::Rectangle<int> controls = area.removeFromBottom(38);
	area.removeFromBottom(4);
	juce::Rectangle<int> caption_area = area.removeFromBottom(44);
	juce::Rectangle<int> image_area	  = area;

	const PhotoCarouselSlide* slide = selected_slide();
	if (slide == nullptr) {
		draw_preview_image_slot(graphics, image_area, {}, "No photos yet.",
								PreviewImageVisualState::Empty, false);
		graphics.setColour(text_colour());
		graphics.setFont(juce::FontOptions(15.5f, juce::Font::bold));
		graphics.drawFittedText("No photos", caption_area,
								juce::Justification::centredLeft, 1, 0.90f);
		return;
	}

	draw_preview_image_slot(graphics, image_area, slide->image,
							slide->placeholder, slide->state, false);

	juce::Rectangle<int> title_line = caption_area.removeFromTop(22);
	juce::Rectangle<int> count_area = title_line.removeFromRight(82);
	juce::String title =
		slide->title.isEmpty() ? "Photo preview" : slide->title;
	graphics.setColour(text_colour());
	graphics.setFont(juce::FontOptions(15.5f, juce::Font::bold));
	graphics.drawFittedText(title, title_line, juce::Justification::centredLeft,
							1, 0.90f);
	graphics.setColour(muted_text_colour());
	graphics.setFont(juce::FontOptions(13.0f, juce::Font::plain));
	graphics.drawFittedText(
		juce::String(static_cast<int>(selected_slide_index + 1U)) + " / "
			+ juce::String(static_cast<int>(slides.size())),
		count_area, juce::Justification::centredRight, 1, 0.90f);
	graphics.drawFittedText(slide->caption, caption_area,
							juce::Justification::centredLeft, 2, 0.88f);

	const int side_width = std::max(1, std::min(112, controls.getWidth() / 3));
	controls.removeFromLeft(side_width);
	controls.removeFromRight(side_width);
	if (!slide_action_button.isVisible())
		draw_carousel_indicator(graphics, controls, slides.size(),
								selected_slide_index);
}

void PhotoCarouselComponent::mouseDown(const juce::MouseEvent&) {
	tracking_pointer   = true;
	horizontal_gesture = false;
	vertical_gesture   = false;
}

void PhotoCarouselComponent::mouseDrag(const juce::MouseEvent& event) {
	if (!tracking_pointer || horizontal_gesture || vertical_gesture)
		return;

	const int distance_x = std::abs(event.getDistanceFromDragStartX());
	const int distance_y = std::abs(event.getDistanceFromDragStartY());
	if (distance_x < gesture_axis_threshold_pixels
		&& distance_y < gesture_axis_threshold_pixels) {
		return;
	}

	if (distance_x > distance_y * 2)
		horizontal_gesture = true;
	else if (distance_y >= distance_x)
		vertical_gesture = true;
}

void PhotoCarouselComponent::mouseUp(const juce::MouseEvent& event) {
	if (!tracking_pointer)
		return;

	tracking_pointer	 = false;
	const int distance_x = event.getDistanceFromDragStartX();
	const int distance_y = event.getDistanceFromDragStartY();
	if (horizontal_gesture && std::abs(distance_x) >= swipe_threshold_pixels
		&& std::abs(distance_x) > std::abs(distance_y)) {
		move_selection(distance_x < 0 ? 1 : -1);
		return;
	}

	if (!vertical_gesture && std::abs(distance_x) <= tap_tolerance_pixels
		&& std::abs(distance_y) <= tap_tolerance_pixels && on_activate) {
		std::function<void()> handler = on_activate;
		handler();
	}
}

const PhotoCarouselSlide* PhotoCarouselComponent::selected_slide()
	const noexcept {
	if (slides.empty() || selected_slide_index >= slides.size())
		return nullptr;
	return &slides[selected_slide_index];
}

void PhotoCarouselComponent::move_selection(int direction) {
	if (slides.size() <= 1U)
		return;

	std::size_t next_index = selected_slide_index;
	if (direction < 0) {
		next_index = selected_slide_index == 0U ? slides.size() - 1U
												: selected_slide_index - 1U;
	} else {
		next_index = (selected_slide_index + 1U) % slides.size();
	}
	select_slide(next_index);
}

void PhotoCarouselComponent::select_slide(std::size_t slide_index) {
	if (slide_index >= slides.size() || slide_index == selected_slide_index)
		return;

	selected_slide_index = slide_index;
	refresh_controls();
	resized();
	repaint();
	std::function<void(std::size_t)> handler = on_select;
	if (handler)
		handler(selected_slide_index);
}

void PhotoCarouselComponent::refresh_controls() {
	const bool multiple_slides = slides.size() > 1U;
	previous_button.setEnabled(multiple_slides);
	next_button.setEnabled(multiple_slides);
	const PhotoCarouselSlide* slide = selected_slide();
	const bool has_action = slide != nullptr && !slide->action_label.isEmpty()
							&& static_cast<bool>(slide->action_handler);
	slide_action_button.setVisible(has_action);
	if (has_action) {
		slide_action_button.setButtonText(slide->action_label);
		slide_action_button.setEnabled(slide->action_enabled);
	}
}

PhotoViewerImageComponent::PhotoViewerImageComponent(
	PhotoViewerImageModel model_value, PhotoViewerImageHandlers handlers_value)
	: model(std::move(model_value)), handlers(std::move(handlers_value)) {
	setOpaque(true);
	setBufferedToImage(true);
	refresh_viewport_drag_policy();
}

PhotoViewerImageComponent::Layout PhotoViewerImageComponent::calculate_layout()
	const {
	Layout layout;
	juce::Rectangle<int> area = getLocalBounds().reduced(8, 8);
	layout.caption			  = area.removeFromBottom(58);
	area.removeFromBottom(4);
	layout.image = area;
	return layout;
}

bool PhotoViewerImageComponent::zoomed() const noexcept {
	return zoom_scale > minimum_zoom_scale + 0.01f;
}

void PhotoViewerImageComponent::clamp_pan() {
	if (!model.image.isValid() || !zoomed()) {
		pan_offset = {};
		return;
	}

	const Layout layout				 = calculate_layout();
	const juce::Rectangle<int> slot	 = layout.image.reduced(8);
	const ViewerImageMetrics metrics = calculate_viewer_image_metrics(
		model.image, slot, model.rotation_quarter_turns, zoom_scale);
	const float maximum_x = std::max(
		0.0f,
		(metrics.drawn_width - static_cast<float>(slot.getWidth())) * 0.5f);
	const float maximum_y = std::max(
		0.0f,
		(metrics.drawn_height - static_cast<float>(slot.getHeight())) * 0.5f);
	pan_offset.x = std::clamp(pan_offset.x, -maximum_x, maximum_x);
	pan_offset.y = std::clamp(pan_offset.y, -maximum_y, maximum_y);
}

void PhotoViewerImageComponent::reset_zoom() {
	zoom_scale = minimum_zoom_scale;
	pan_offset = {};
	refresh_viewport_drag_policy();
	repaint();
}

void PhotoViewerImageComponent::refresh_viewport_drag_policy() {
	setViewportIgnoreDragFlag(zoomed());
}

void PhotoViewerImageComponent::paint(juce::Graphics& graphics) {
	graphics.fillAll(background_colour());
	draw_card_background(graphics, getLocalBounds(), surface_colour(), false);
	const Layout layout		  = calculate_layout();
	juce::Rectangle<int> slot = layout.image.reduced(8);
	graphics.setColour(juce::Colours::black.withAlpha(0.42f));
	graphics.fillRoundedRectangle(slot.toFloat(), 14.0f);
	graphics.setColour(outline_colour().withAlpha(0.62f));
	graphics.drawRoundedRectangle(slot.toFloat(), 14.0f, 1.0f);

	if (model.image.isValid()) {
		draw_viewer_image_transformed(graphics, model.image, slot.reduced(6),
									  model.rotation_quarter_turns, zoom_scale,
									  pan_offset);
	} else {
		graphics.setColour(panel_colour());
		graphics.fillRoundedRectangle(slot.reduced(6).toFloat(), 11.0f);
		graphics.setColour(muted_text_colour());
		graphics.setFont(juce::FontOptions(17.0f, juce::Font::plain));
		graphics.drawFittedText(
			preview_placeholder_text(model.placeholder, model.state),
			slot.reduced(16), juce::Justification::centred, 4, 0.92f);
		draw_preview_badge(graphics, slot, model.state);
	}

	juce::Rectangle<int> caption	= layout.caption.reduced(6, 0);
	juce::Rectangle<int> title_line = caption.removeFromTop(22);
	juce::Rectangle<int> state_line = caption.removeFromBottom(18);
	graphics.setColour(text_colour());
	graphics.setFont(juce::FontOptions(15.5f, juce::Font::bold));
	graphics.drawFittedText(
		model.title.isEmpty() ? "Photo viewer" : model.title, title_line,
		juce::Justification::centredLeft, 1, 0.90f);
	graphics.setColour(muted_text_colour());
	graphics.setFont(juce::FontOptions(12.8f, juce::Font::plain));
	graphics.drawFittedText(model.caption, caption,
							juce::Justification::centredLeft, 2, 0.88f);
	juce::String state_text =
		zoomed() ? juce::String{"Zoom "} + juce::String(zoom_scale, 2)
					   + "x · drag to pan · double-tap to fit"
		: model.multiple_photos
			? "Fit view · double-tap to zoom · swipe horizontally"
			: "Fit view · double-tap to zoom · rotate with buttons";
	graphics.drawFittedText(state_text, state_line,
							juce::Justification::centredLeft, 1, 0.88f);
}

void PhotoViewerImageComponent::mouseDown(const juce::MouseEvent&) {
	tracking_pointer   = true;
	horizontal_gesture = false;
	vertical_gesture   = false;
	drag_start_pan	   = pan_offset;
}

void PhotoViewerImageComponent::mouseDrag(const juce::MouseEvent& event) {
	if (!tracking_pointer)
		return;

	if (zoomed()) {
		pan_offset = drag_start_pan
					 + juce::Point<float>{
						 static_cast<float>(event.getDistanceFromDragStartX()),
						 static_cast<float>(event.getDistanceFromDragStartY())};
		clamp_pan();
		repaint();
		return;
	}

	if (horizontal_gesture || vertical_gesture)
		return;

	const int distance_x = std::abs(event.getDistanceFromDragStartX());
	const int distance_y = std::abs(event.getDistanceFromDragStartY());
	if (distance_x < gesture_axis_threshold_pixels
		&& distance_y < gesture_axis_threshold_pixels) {
		return;
	}
	if (distance_x > distance_y * 2)
		horizontal_gesture = true;
	else if (distance_y >= distance_x)
		vertical_gesture = true;
}

void PhotoViewerImageComponent::mouseUp(const juce::MouseEvent& event) {
	if (!tracking_pointer)
		return;

	tracking_pointer = false;
	if (zoomed())
		return;

	const int distance_x = event.getDistanceFromDragStartX();
	const int distance_y = event.getDistanceFromDragStartY();
	if (!horizontal_gesture || std::abs(distance_x) < swipe_threshold_pixels
		|| std::abs(distance_x) <= std::abs(distance_y)) {
		return;
	}
	if (distance_x < 0) {
		if (handlers.select_next)
			handlers.select_next();
	} else if (handlers.select_previous) {
		handlers.select_previous();
	}
}

void PhotoViewerImageComponent::mouseDoubleClick(const juce::MouseEvent&) {
	if (!model.image.isValid())
		return;

	if (zoomed()) {
		reset_zoom();
		return;
	}

	zoom_scale = 2.0f;
	pan_offset = {};
	clamp_pan();
	refresh_viewport_drag_policy();
	repaint();
}

PreviewCardButtonComponent::PreviewCardButtonComponent(
	PreviewCardContent content_value, juce::Colour background_value)
	: juce::Button(content_value.title)
	, content(std::move(content_value))
	, background(background_value) {
	setOpaque(true);
	setBufferedToImage(true);
}

void PreviewCardButtonComponent::paintButton(juce::Graphics& graphics,
											 bool highlighted, bool down) {
	graphics.fillAll(background_colour());
	juce::Colour fill = background;
	if (down)
		fill = fill.brighter(0.14f);
	draw_card_background(graphics, getLocalBounds(), fill, highlighted);

	juce::Rectangle<int> area = getLocalBounds().reduced(10, 8);
	int preview_size		  = std::min(82, std::max(1, area.getHeight()));
	preview_size =
		std::min(preview_size, std::max(44, std::max(1, area.getWidth() / 4)));
	juce::Rectangle<int> preview_area = area.removeFromLeft(preview_size);
	area.removeFromLeft(10);
	if (isEnabled())
		area.removeFromRight(18);

	draw_preview_image_slot(graphics, preview_area, content.image,
							content.placeholder, content.state, true);

	graphics.setColour(isEnabled() ? text_colour()
								   : muted_text_colour().withAlpha(0.58f));
	graphics.setFont(juce::FontOptions(14.5f, juce::Font::bold));
	juce::Rectangle<int> title_area = area.removeFromTop(21);
	graphics.drawFittedText(content.title, title_area,
							juce::Justification::centredLeft, 1, 0.90f);
	graphics.setColour(isEnabled() ? muted_text_colour()
								   : muted_text_colour().withAlpha(0.52f));
	graphics.setFont(juce::FontOptions(12.8f, juce::Font::plain));
	juce::Rectangle<int> metadata_area = area.removeFromBottom(18);
	graphics.drawFittedText(
		content.subtitle, area, juce::Justification::centredLeft,
		fitted_line_count(area.getHeight(), 0, 14.0f), 0.88f);
	graphics.drawFittedText(content.metadata, metadata_area,
							juce::Justification::centredLeft, 1, 0.86f);

	if (isEnabled()) {
		juce::Rectangle<int> arrow_bounds =
			getLocalBounds().removeFromRight(30).reduced(8, 12);
		draw_forward_chevron(graphics, arrow_bounds, muted_text_colour());
	}
}

void PreviewCardButtonComponent::mouseDown(const juce::MouseEvent& event) {
	touch_activation_guard.begin(event);
	juce::Button::mouseDown(event);
}

void PreviewCardButtonComponent::mouseDrag(const juce::MouseEvent& event) {
	touch_activation_guard.update(event);
	juce::Button::mouseDrag(event);
}

void PreviewCardButtonComponent::mouseUp(const juce::MouseEvent& event) {
	if (touch_activation_guard.consume_suppressed_release(event)) {
		setState(juce::Button::buttonNormal);
		return;
	}

	juce::Button::mouseUp(event);
}

CompactStorageCardButtonComponent::CompactStorageCardButtonComponent(
	CompactStorageCardContent content_value, juce::Colour background_value)
	: juce::Button(content_value.name)
	, content(std::move(content_value))
	, background(background_value) {
	setOpaque(true);
	setBufferedToImage(true);
	setEnabled(content.enabled);
	onClick = [this] {
		if (content.on_activate)
			content.on_activate();
	};
}

void CompactStorageCardButtonComponent::paintButton(juce::Graphics& graphics,
													bool highlighted,
													bool down) {
	graphics.fillAll(background_colour());
	juce::Colour fill = background;
	if (down)
		fill = fill.brighter(0.14f);
	draw_card_background(graphics, getLocalBounds(), fill, highlighted);

	juce::Rectangle<int> area = getLocalBounds().reduced(8, 8);
	const int preview_height = std::min(88, std::max(1, area.getHeight() - 48));
	juce::Rectangle<int> preview_area = area.removeFromTop(preview_height);
	area.removeFromTop(6);
	juce::Rectangle<int> item_count_area = area.removeFromBottom(20);

	draw_preview_image_slot(graphics, preview_area, content.image,
							content.placeholder, content.state, true);

	graphics.setColour(isEnabled() ? text_colour()
								   : muted_text_colour().withAlpha(0.58f));
	graphics.setFont(juce::FontOptions(13.8f, juce::Font::bold));
	graphics.drawFittedText(
		content.name, area, juce::Justification::centredLeft,
		fitted_line_count(area.getHeight(), 0, 14.8f), 0.88f);
	graphics.setColour(isEnabled() ? muted_text_colour()
								   : muted_text_colour().withAlpha(0.52f));
	graphics.setFont(juce::FontOptions(12.8f, juce::Font::plain));
	graphics.drawFittedText(content.item_count, item_count_area,
							juce::Justification::centredLeft, 1, 0.86f);
}

void CompactStorageCardButtonComponent::mouseDown(
	const juce::MouseEvent& event) {
	touch_activation_guard.begin(event);
	juce::Button::mouseDown(event);
}

void CompactStorageCardButtonComponent::mouseDrag(
	const juce::MouseEvent& event) {
	touch_activation_guard.update(event);
	juce::Button::mouseDrag(event);
}

void CompactStorageCardButtonComponent::mouseUp(const juce::MouseEvent& event) {
	if (touch_activation_guard.consume_suppressed_release(event)) {
		setState(juce::Button::buttonNormal);
		return;
	}

	juce::Button::mouseUp(event);
}

CompactStorageStripComponent::CompactStorageStripComponent(
	std::vector<CompactStorageCardContent> cards_value)
	: card_row(std::make_unique<juce::Component>()) {
	setOpaque(true);
	setBufferedToImage(true);
	viewport.setViewedComponent(card_row.get(), false);
	viewport.setScrollBarsShown(false, true, false, true);
	viewport.setScrollBarThickness(5);
	viewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::nonHover);
	addAndMakeVisible(viewport);
	cards.reserve(cards_value.size());
	for (CompactStorageCardContent& card_content : cards_value) {
		std::unique_ptr<CompactStorageCardButtonComponent> card =
			std::make_unique<CompactStorageCardButtonComponent>(
				std::move(card_content), elevated_surface_colour());
		card_row->addAndMakeVisible(*card);
		cards.push_back(std::move(card));
	}
}

int CompactStorageStripComponent::preferred_height() noexcept {
	return 164;
}

void CompactStorageStripComponent::resized() {
	viewport.setBounds(getLocalBounds());
	const int card_count	= static_cast<int>(cards.size());
	const int natural_width = horizontal_padding * 2 + card_count * card_width
							  + std::max(0, card_count - 1) * card_gap;
	card_row->setSize(std::max(viewport.getWidth(), natural_width),
					  std::max(1, getHeight()));

	int x				  = horizontal_padding;
	const int card_height = std::max(1, getHeight() - vertical_padding * 2 - 4);
	for (std::unique_ptr<CompactStorageCardButtonComponent>& card : cards) {
		card->setBounds(x, vertical_padding, card_width, card_height);
		x += card_width + card_gap;
	}
}

void CompactStorageStripComponent::paint(juce::Graphics& graphics) {
	graphics.fillAll(background_colour());
}
}	 // namespace shuba::ui
