#include "UI/AppShell.hpp"

#include "Domain/Domain.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace shuba::ui {
namespace {
[[nodiscard]] juce::String juce_text(std::string_view text) {
	return juce::String::fromUTF8(text.data(), static_cast<int>(text.size()));
}

[[nodiscard]] juce::Colour background_colour() {
	return juce::Colour::fromRGB(17, 23, 32);
}

[[nodiscard]] juce::Colour panel_colour() {
	return juce::Colour::fromRGB(28, 37, 50);
}

[[nodiscard]] juce::Colour accent_colour() {
	return juce::Colour::fromRGB(96, 165, 250);
}

[[nodiscard]] juce::Colour text_colour() {
	return juce::Colour::fromRGB(235, 242, 252);
}

[[nodiscard]] juce::Colour muted_text_colour() {
	return juce::Colour::fromRGB(178, 190, 205);
}

[[nodiscard]] juce::Colour surface_colour() {
	return juce::Colour::fromRGB(35, 46, 62);
}

[[nodiscard]] juce::Colour elevated_surface_colour() {
	return juce::Colour::fromRGB(42, 55, 73);
}

[[nodiscard]] juce::Colour outline_colour() {
	return juce::Colour::fromRGB(84, 101, 122);
}

[[nodiscard]] juce::Colour warning_panel_colour() {
	return juce::Colour::fromRGB(129, 82, 16);
}

[[nodiscard]] int fitted_line_count(int height, int vertical_padding,
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

[[nodiscard]] bool touch_like_source(const juce::MouseEvent& event) noexcept {
	return event.source.isTouch() || event.source.isPen();
}

void style_text_button(juce::TextButton& button);
void style_text_editor(juce::TextEditor& editor);

[[nodiscard]] std::string compact_byte_summary(
	const std::optional<std::uint64_t>& byte_count) {
	if (!byte_count.has_value())
		return "size unknown";
	if (*byte_count < 1024U)
		return std::to_string(*byte_count) + " B";
	if (*byte_count < 1024U * 1024U)
		return std::to_string(*byte_count / 1024U) + " KB";
	return std::to_string(*byte_count / (1024U * 1024U)) + " MB";
}

[[nodiscard]] std::string pending_photo_count_text(
	std::span<const PendingPhotoSource> sources) {
	std::uint64_t staged_count{};
	std::uint64_t failed_count{};
	for (const PendingPhotoSource& source : sources)
		if (source.ready_for_import())
			++staged_count;
		else if (source.status == PendingPhotoStatus::Failed
				 || source.status == PendingPhotoStatus::Cancelled)
			++failed_count;

	std::string text = "Photos: " + std::to_string(staged_count) + " staged";
	if (failed_count > 0U)
		text += " · " + std::to_string(failed_count) + " failed";
	return text;
}

[[nodiscard]] std::string pending_photo_card_text(
	const PendingPhotoSource& source, std::size_t display_index) {
	std::string text = "#" + std::to_string(display_index) + " · "
					   + std::string{to_string(source.status)};
	if (!source.display_name.empty())
		text += " · " + source.display_name;
	text += " · " + compact_byte_summary(source.byte_count);
	if (!source.diagnostics.empty())
		text += " · " + std::to_string(source.diagnostics.size()) + " issue(s)";
	return text;
}

class TouchScrollActivationGuard final {
public:
	void begin(const juce::MouseEvent& event) noexcept {
		tracking_touch_like = touch_like_source(event);
		suppress_release	= false;
	}

	void update(const juce::MouseEvent& event) noexcept {
		if (!tracking_touch_like || !touch_like_source(event))
			return;

		if (event.mouseWasDraggedSinceMouseDown()
			|| event.getDistanceFromDragStart()
				   > scroll_suppression_distance_pixels) {
			suppress_release = true;
		}
	}

	[[nodiscard]] bool consume_suppressed_release(
		const juce::MouseEvent& event) noexcept {
		update(event);
		const bool should_suppress = tracking_touch_like && suppress_release;
		tracking_touch_like		   = false;
		suppress_release		   = false;
		return should_suppress;
	}

private:
	static constexpr int scroll_suppression_distance_pixels = 8;

	bool tracking_touch_like{};
	bool suppress_release{};
};

class TextRowComponent final : public juce::Component {
public:
	TextRowComponent(juce::String text_value, juce::Colour background_value,
					 bool strong_value)
		: text(std::move(text_value))
		, background(background_value)
		, strong(strong_value) {
		setOpaque(true);
		setBufferedToImage(true);
	}

	void paint(juce::Graphics& graphics) override {
		graphics.fillAll(background_colour());
		draw_card_background(graphics, getLocalBounds(), background, false);

		const float font_height			 = strong ? 16.5f : 14.8f;
		juce::Rectangle<int> text_bounds = getLocalBounds().reduced(14, 7);
		graphics.setColour(text_colour());
		graphics.setFont(juce::FontOptions(
			font_height, strong ? juce::Font::bold : juce::Font::plain));
		graphics.drawFittedText(
			text, text_bounds, juce::Justification::centredLeft,
			fitted_line_count(getHeight(), 14, font_height + 2.0f), 0.92f);
	}

private:
	juce::String text;
	juce::Colour background;
	bool strong{};
};

class RowButtonComponent final : public juce::Button {
public:
	RowButtonComponent(juce::String text_value, juce::Colour background_value)
		: juce::Button(std::move(text_value)), background(background_value) {
		setOpaque(true);
		setBufferedToImage(true);
	}

	void paintButton(juce::Graphics& graphics, bool highlighted,
					 bool down) override {
		graphics.fillAll(background_colour());
		juce::Colour fill = background;
		if (down)
			fill = fill.brighter(0.14f);
		draw_card_background(graphics, getLocalBounds(), fill, highlighted);

		const float font_height			 = 15.0f;
		juce::Rectangle<int> text_bounds = getLocalBounds().reduced(14, 8);
		if (isEnabled())
			text_bounds.removeFromRight(18);

		graphics.setColour(isEnabled() ? text_colour()
									   : muted_text_colour().withAlpha(0.58f));
		graphics.setFont(juce::FontOptions(font_height, juce::Font::plain));
		graphics.drawFittedText(
			getButtonText(), text_bounds, juce::Justification::centredLeft,
			fitted_line_count(getHeight(), 16, font_height + 2.0f), 0.92f);

		if (isEnabled()) {
			juce::Rectangle<int> arrow_bounds =
				getLocalBounds().removeFromRight(30).reduced(8, 12);
			juce::Path chevron;
			const float centre_y = arrow_bounds.getCentreY();
			const float left_x = static_cast<float>(arrow_bounds.getX()) + 3.0f;
			const float right_x =
				static_cast<float>(arrow_bounds.getRight()) - 3.0f;
			const float top_y = static_cast<float>(arrow_bounds.getY()) + 2.0f;
			const float bottom_y =
				static_cast<float>(arrow_bounds.getBottom()) - 2.0f;
			chevron.startNewSubPath(left_x, top_y);
			chevron.lineTo(right_x, centre_y);
			chevron.lineTo(left_x, bottom_y);
			graphics.setColour(muted_text_colour());
			graphics.strokePath(chevron, juce::PathStrokeType{
											 2.0f, juce::PathStrokeType::curved,
											 juce::PathStrokeType::rounded});
		}
	}

private:
	void mouseDown(const juce::MouseEvent& event) override {
		touch_activation_guard.begin(event);
		juce::Button::mouseDown(event);
	}

	void mouseDrag(const juce::MouseEvent& event) override {
		touch_activation_guard.update(event);
		juce::Button::mouseDrag(event);
	}

	void mouseUp(const juce::MouseEvent& event) override {
		if (touch_activation_guard.consume_suppressed_release(event)) {
			setState(juce::Button::buttonNormal);
			return;
		}

		juce::Button::mouseUp(event);
	}

	juce::Colour background;
	TouchScrollActivationGuard touch_activation_guard;
};

class TouchSafeToggleButton final : public juce::ToggleButton {
public:
	explicit TouchSafeToggleButton(juce::String text_value)
		: juce::ToggleButton(std::move(text_value)) {
		setBufferedToImage(true);
	}

private:
	void mouseDown(const juce::MouseEvent& event) override {
		touch_activation_guard.begin(event);
		juce::Button::mouseDown(event);
	}

	void mouseDrag(const juce::MouseEvent& event) override {
		touch_activation_guard.update(event);
		juce::Button::mouseDrag(event);
	}

	void mouseUp(const juce::MouseEvent& event) override {
		if (touch_activation_guard.consume_suppressed_release(event)) {
			setState(juce::Button::buttonNormal);
			return;
		}

		juce::Button::mouseUp(event);
	}

	TouchScrollActivationGuard touch_activation_guard;
};

class ImagePanelComponent final : public juce::Component {
public:
	ImagePanelComponent(juce::Image image_value, juce::String caption_value,
						juce::String placeholder_value)
		: image(std::move(image_value))
		, caption(std::move(caption_value))
		, placeholder(std::move(placeholder_value)) {
		setOpaque(true);
		setBufferedToImage(true);
	}

	void paint(juce::Graphics& graphics) override {
		graphics.fillAll(background_colour());
		draw_card_background(graphics, getLocalBounds(), surface_colour(),
							 false);
		juce::Rectangle<int> area		  = getLocalBounds().reduced(12, 10);
		juce::Rectangle<int> caption_area = area.removeFromBottom(34);
		if (image.isValid()) {
			graphics.drawImageWithin(
				image, area.getX(), area.getY(), area.getWidth(),
				area.getHeight(),
				juce::RectanglePlacement::centred
					| juce::RectanglePlacement::onlyReduceInSize);
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

private:
	juce::Image image;
	juce::String caption;
	juce::String placeholder;
};

class InlineButtonRowComponent final : public juce::Component {
public:
	struct Action final {
		juce::String label;
		std::function<void()> handler;
		bool enabled{true};
	};

	InlineButtonRowComponent(juce::String title_value,
							 std::vector<Action> actions_value)
		: title(std::move(title_value)) {
		setOpaque(true);
		setBufferedToImage(true);
		for (Action& action : actions_value) {
			std::unique_ptr<juce::TextButton> button =
				std::make_unique<juce::TextButton>(action.label);
			style_text_button(*button);
			button->setEnabled(action.enabled);
			button->onClick = std::move(action.handler);
			addAndMakeVisible(*button);
			buttons.push_back(std::move(button));
		}
	}

	void resized() override {
		juce::Rectangle<int> area = getLocalBounds().reduced(12, 8);
		if (area.isEmpty())
			return;

		if (!title.isEmpty())
			area.removeFromLeft(std::min(112, area.getWidth() / 3));

		const int gap	= 6;
		const int count = static_cast<int>(buttons.size());
		if (count <= 0)
			return;

		const int button_width =
			std::max(1, (area.getWidth() - gap * (count - 1)) / count);
		for (std::unique_ptr<juce::TextButton>& button : buttons) {
			button->setBounds(area.removeFromLeft(button_width));
			area.removeFromLeft(gap);
		}
	}

	void paint(juce::Graphics& graphics) override {
		graphics.fillAll(background_colour());
		draw_card_background(graphics, getLocalBounds(), panel_colour(), false);
		if (title.isEmpty())
			return;

		juce::Rectangle<int> title_area = getLocalBounds().reduced(12, 8);
		title_area.setWidth(std::min(112, title_area.getWidth() / 3));
		graphics.setColour(text_colour());
		graphics.setFont(juce::FontOptions(14.0f, juce::Font::bold));
		graphics.drawFittedText(title, title_area,
								juce::Justification::centredLeft, 1, 0.88f);
	}

private:
	juce::String title;
	std::vector<std::unique_ptr<juce::TextButton>> buttons;
};

class ButtonGridComponent final : public juce::Component {
public:
	struct Action final {
		juce::String label;
		std::function<void()> handler;
		bool enabled{true};
	};

	ButtonGridComponent(juce::String title_value,
						std::vector<Action> actions_value,
						int column_count_value)
		: title(std::move(title_value))
		, column_count(std::max(1, column_count_value)) {
		setOpaque(true);
		setBufferedToImage(true);
		for (Action& action : actions_value) {
			std::unique_ptr<juce::TextButton> button =
				std::make_unique<juce::TextButton>(action.label);
			style_text_button(*button);
			button->setEnabled(action.enabled);
			button->setTooltip(action.label);
			button->onClick = std::move(action.handler);
			addAndMakeVisible(*button);
			buttons.push_back(std::move(button));
		}
	}

	[[nodiscard]] static int preferred_height(int action_count,
											  int column_count_value) noexcept {
		const int safe_columns = std::max(1, column_count_value);
		const int row_count =
			std::max(1, (action_count + safe_columns - 1) / safe_columns);
		return 32 + row_count * 38 + 8;
	}

	void resized() override {
		juce::Rectangle<int> area = getLocalBounds().reduced(10, 8);
		if (!title.isEmpty())
			area.removeFromTop(24);
		const int gap			= 6;
		const int button_height = 32;
		const int button_width	= std::max(
			1, (area.getWidth() - gap * (column_count - 1)) / column_count);
		for (std::size_t index = 0; index < buttons.size(); ++index) {
			const int row	 = static_cast<int>(index) / column_count;
			const int column = static_cast<int>(index) % column_count;
			buttons[index]->setBounds(
				area.getX() + column * (button_width + gap),
				area.getY() + row * (button_height + gap), button_width,
				button_height);
		}
	}

	void paint(juce::Graphics& graphics) override {
		graphics.fillAll(background_colour());
		draw_card_background(graphics, getLocalBounds(), panel_colour(), false);
		if (title.isEmpty())
			return;

		juce::Rectangle<int> title_area = getLocalBounds().reduced(12, 8);
		title_area.setHeight(22);
		graphics.setColour(text_colour());
		graphics.setFont(juce::FontOptions(14.0f, juce::Font::bold));
		graphics.drawFittedText(title, title_area,
								juce::Justification::centredLeft, 1, 0.90f);
	}

private:
	juce::String title;
	int column_count{1};
	std::vector<std::unique_ptr<juce::TextButton>> buttons;
};

class TagRowEditorComponent final : public juce::Component {
public:
	TagRowEditorComponent(
		std::size_t row_index_value, domain::TagRow tag_value,
		std::function<void(std::size_t, domain::TagRow)> change_handler,
		std::function<void(std::size_t)> remove_handler)
		: row_index(row_index_value)
		, on_change(std::move(change_handler))
		, on_remove(std::move(remove_handler)) {
		setOpaque(true);
		setBufferedToImage(true);
		key_editor.setText(juce_text(tag_value.key),
						   juce::dontSendNotification);
		value_editor.setText(juce_text(tag_value.value),
							 juce::dontSendNotification);
		key_editor.setTextToShowWhenEmpty("Key", muted_text_colour());
		value_editor.setTextToShowWhenEmpty("Value", muted_text_colour());
		style_text_editor(key_editor);
		style_text_editor(value_editor);
		style_text_button(remove_button);
		key_editor.onTextChange	  = [this] { publish_change(); };
		value_editor.onTextChange = [this] { publish_change(); };
		remove_button.onClick	  = [this] {
			if (on_remove)
				on_remove(row_index);
		};
		addAndMakeVisible(key_editor);
		addAndMakeVisible(value_editor);
		addAndMakeVisible(remove_button);
	}

	void resized() override {
		juce::Rectangle<int> area		 = getLocalBounds().reduced(12, 8);
		juce::Rectangle<int> remove_area = area.removeFromRight(82);
		area.removeFromRight(6);
		const int key_width = std::max(86, area.getWidth() / 2);
		key_editor.setBounds(area.removeFromLeft(key_width));
		area.removeFromLeft(6);
		value_editor.setBounds(area);
		remove_button.setBounds(remove_area);
	}

	void paint(juce::Graphics& graphics) override {
		graphics.fillAll(background_colour());
		draw_card_background(graphics, getLocalBounds(), panel_colour(), false);
	}

private:
	void publish_change() {
		if (!on_change)
			return;

		on_change(
			row_index,
			domain::TagRow{.key	  = key_editor.getText().toStdString(),
						   .value = value_editor.getText().toStdString()});
	}

	std::size_t row_index{};
	std::function<void(std::size_t, domain::TagRow)> on_change;
	std::function<void(std::size_t)> on_remove;
	juce::TextEditor key_editor;
	juce::TextEditor value_editor;
	juce::TextButton remove_button{"Remove"};
};

class EditorPairComponent final : public juce::Component {
public:
	EditorPairComponent(juce::TextEditor& first_editor_value,
						juce::TextEditor& second_editor_value,
						juce::String first_placeholder,
						juce::String second_placeholder)
		: first_editor(first_editor_value), second_editor(second_editor_value) {
		setOpaque(true);
		setBufferedToImage(true);
		prepare_editor(first_editor, std::move(first_placeholder));
		prepare_editor(second_editor, std::move(second_placeholder));
		addAndMakeVisible(first_editor);
		addAndMakeVisible(second_editor);
	}

	~EditorPairComponent() override {
		removeChildComponent(&first_editor);
		removeChildComponent(&second_editor);
	}

	void resized() override {
		juce::Rectangle<int> area = getLocalBounds().reduced(12, 8);
		const int gap			  = 6;
		const int editor_width	  = std::max(1, (area.getWidth() - gap) / 2);
		first_editor.setBounds(area.removeFromLeft(editor_width));
		area.removeFromLeft(gap);
		second_editor.setBounds(area);
	}

	void paint(juce::Graphics& graphics) override {
		graphics.fillAll(background_colour());
		draw_card_background(graphics, getLocalBounds(), panel_colour(), false);
	}

private:
	static void prepare_editor(juce::TextEditor& editor,
							   juce::String placeholder) {
		if (juce::Component* parent = editor.getParentComponent())
			parent->removeChildComponent(&editor);
		editor.setTextToShowWhenEmpty(std::move(placeholder),
									  muted_text_colour());
		editor.setMultiLine(false);
		editor.setReturnKeyStartsNewLine(false);
		editor.setScrollbarsShown(false);
		style_text_editor(editor);
	}

	juce::TextEditor& first_editor;
	juce::TextEditor& second_editor;
};

class PendingPhotoCardsComponent final : public juce::Component {
public:
	PendingPhotoCardsComponent(
		std::vector<PendingPhotoSource> sources_value,
		std::function<void(std::size_t)> remove_handler_value)
		: sources(std::move(sources_value)) {
		setOpaque(true);
		setBufferedToImage(true);
		for (std::size_t index = 0; index < sources.size(); ++index) {
			std::unique_ptr<juce::TextButton> button =
				std::make_unique<juce::TextButton>("Remove");
			style_text_button(*button);
			button->onClick = [remove_handler_value, index] {
				if (remove_handler_value)
					remove_handler_value(index);
			};
			addAndMakeVisible(*button);
			remove_buttons.push_back(std::move(button));
		}
		setSize(preferred_width(), 62);
	}

	[[nodiscard]] int preferred_width() const noexcept {
		if (sources.empty())
			return 1;
		return 10 + static_cast<int>(sources.size()) * (card_width + card_gap);
	}

	void resized() override {
		for (std::size_t index = 0; index < remove_buttons.size(); ++index) {
			juce::Rectangle<int> card = card_bounds(index).reduced(8, 7);
			remove_buttons[index]->setBounds(card.removeFromBottom(24));
		}
	}

	void paint(juce::Graphics& graphics) override {
		graphics.fillAll(panel_colour());
		if (sources.empty()) {
			graphics.setColour(muted_text_colour());
			graphics.setFont(juce::FontOptions(14.0f, juce::Font::plain));
			graphics.drawFittedText("No staged entries yet.", getLocalBounds(),
									juce::Justification::centredLeft, 1, 0.90f);
			return;
		}

		for (std::size_t index = 0; index < sources.size(); ++index) {
			const PendingPhotoSource& source = sources[index];
			const bool failed =
				source.status == PendingPhotoStatus::Failed
				|| source.status == PendingPhotoStatus::Cancelled;
			juce::Colour colour =
				failed ? warning_panel_colour() : elevated_surface_colour();
			juce::Rectangle<int> card = card_bounds(index);
			draw_card_background(graphics, card, colour,
								 source.ready_for_import());
			juce::Rectangle<int> text_area = card.reduced(8, 7);
			text_area.removeFromBottom(26);
			graphics.setColour(text_colour());
			graphics.setFont(juce::FontOptions(13.0f, juce::Font::plain));
			graphics.drawFittedText(
				juce_text(pending_photo_card_text(source, index + 1U)),
				text_area, juce::Justification::centredLeft, 3, 0.88f);
		}
	}

private:
	[[nodiscard]] juce::Rectangle<int> card_bounds(
		std::size_t index) const noexcept {
		return juce::Rectangle<int>{
			6 + static_cast<int>(index) * (card_width + card_gap), 2,
			card_width, std::max(1, getHeight() - 4)};
	}

	static constexpr int card_width = 142;
	static constexpr int card_gap	= 8;

	std::vector<PendingPhotoSource> sources;
	std::vector<std::unique_ptr<juce::TextButton>> remove_buttons;
};

class PendingPhotoStripComponent final : public juce::Component {
public:
	PendingPhotoStripComponent(std::vector<PendingPhotoSource> sources_value,
							   std::function<void()> add_handler,
							   std::function<void()> clear_handler,
							   std::function<void(std::size_t)> remove_handler)
		: sources(std::move(sources_value))
		, cards(std::make_unique<PendingPhotoCardsComponent>(
			  sources, std::move(remove_handler))) {
		setOpaque(true);
		setBufferedToImage(true);
		style_text_button(add_button);
		style_text_button(clear_button);
		add_button.onClick	 = std::move(add_handler);
		clear_button.onClick = std::move(clear_handler);
		clear_button.setEnabled(!sources.empty());
		card_viewport.setViewedComponent(cards.get(), false);
		card_viewport.setScrollBarsShown(false, true);
		card_viewport.setScrollOnDragMode(
			juce::Viewport::ScrollOnDragMode::nonHover);
		addAndMakeVisible(add_button);
		addAndMakeVisible(clear_button);
		addAndMakeVisible(card_viewport);
	}

	~PendingPhotoStripComponent() override {
		card_viewport.setViewedComponent(nullptr, false);
	}

	void resized() override {
		juce::Rectangle<int> area	= getLocalBounds().reduced(10, 8);
		juce::Rectangle<int> header = area.removeFromTop(32);
		clear_button.setBounds(header.removeFromRight(82).reduced(2));
		header.removeFromRight(6);
		add_button.setBounds(header.removeFromRight(96).reduced(2));
		area.removeFromTop(4);
		card_viewport.setBounds(area);
		if (cards)
			cards->setSize(cards->preferred_width(),
						   std::max(1, card_viewport.getHeight()));
	}

	void paint(juce::Graphics& graphics) override {
		graphics.fillAll(background_colour());
		draw_card_background(graphics, getLocalBounds(), panel_colour(), false);
		juce::Rectangle<int> header = getLocalBounds().reduced(12, 8);
		header.setHeight(30);
		header.removeFromRight(188);
		graphics.setColour(text_colour());
		graphics.setFont(juce::FontOptions(14.5f, juce::Font::bold));
		graphics.drawFittedText(juce_text(pending_photo_count_text(sources)),
								header, juce::Justification::centredLeft, 1,
								0.90f);
	}

private:
	std::vector<PendingPhotoSource> sources;
	juce::TextButton add_button{"Add photos"};
	juce::TextButton clear_button{"Clear"};
	juce::Viewport card_viewport;
	std::unique_ptr<PendingPhotoCardsComponent> cards;
};

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

[[nodiscard]] std::string status_text(domain::ItemStatus status) {
	return std::string{domain::to_string(status)};
}

[[nodiscard]] std::string storage_lifecycle_text(
	domain::StorageLifecycleStatus status) {
	return std::string{domain::to_string(status)};
}

[[nodiscard]] std::string photo_presence_label(
	catalog::PhotoPresenceState state) {
	switch (state) {
		case catalog::PhotoPresenceState::HasUsablePhotos:
			return "[photo]";
		case catalog::PhotoPresenceState::NoPhotoRecords:
			return "[no photo]";
		case catalog::PhotoPresenceState::OnlyBrokenPhotos:
			return "[broken photo]";
		case catalog::PhotoPresenceState::MixedUsableAndBrokenPhotos:
			return "[mixed photo]";
	}
	return "[photo state unknown]";
}

[[nodiscard]] std::string photo_filter_label(
	catalog::SearchPhotoPresenceFilter filter) {
	return std::string{catalog::to_string(filter)};
}

[[nodiscard]] std::string field_value_summary(std::string_view label,
											  std::string_view value) {
	if (value.empty())
		return std::string{label} + ": —";
	return std::string{label} + ": " + std::string{value};
}

[[nodiscard]] std::string tags_summary(std::span<const domain::TagRow> tags) {
	if (tags.empty())
		return "Tags: —";
	std::string text = "Tags: ";
	for (std::size_t index = 0; index < tags.size(); ++index) {
		if (index > 0U)
			text += ", ";
		text += tags[index].key + "=" + tags[index].value;
	}
	return text;
}

[[nodiscard]] std::string money_summary(
	const std::optional<domain::MoneyAmount>& amount) {
	if (!amount)
		return "—";
	return domain::canonical_decimal_text(*amount) + " " + amount->currency;
}

[[nodiscard]] std::string listing_summary(const domain::ListingData& listing) {
	if (listing.empty())
		return "Listing: no values yet";
	std::vector<std::string> parts;
	if (!listing.marketplace.empty())
		parts.push_back("market=" + listing.marketplace);
	if (!listing.url.empty())
		parts.emplace_back("url set");
	if (listing.price)
		parts.push_back("price=" + money_summary(listing.price));
	if (!listing.note.empty())
		parts.push_back("note=" + listing.note);
	std::string text = "Listing: ";
	for (std::size_t index = 0; index < parts.size(); ++index) {
		if (index > 0U)
			text += " · ";
		text += parts[index];
	}
	return text;
}

[[nodiscard]] std::string finance_summary(
	const domain::AcquisitionData& acquisition,
	const domain::FinanceData& finance) {
	if (acquisition.empty() && finance.empty())
		return "Finance: no values yet";
	std::vector<std::string> parts;
	if (!acquisition.source.empty())
		parts.push_back("source=" + acquisition.source);
	if (acquisition.cost)
		parts.push_back("cost=" + money_summary(acquisition.cost));
	if (finance.real_sale_price)
		parts.push_back("sale=" + money_summary(finance.real_sale_price));
	if (finance.expenses_total)
		parts.push_back("expenses=" + money_summary(finance.expenses_total));
	if (const std::optional<domain::MoneyAmount> profit =
			domain::calculate_profit(acquisition, finance)) {
		parts.push_back("profit=" + money_summary(profit));
	}
	std::string text = "Finance: ";
	for (std::size_t index = 0; index < parts.size(); ++index) {
		if (index > 0U)
			text += " · ";
		text += parts[index];
	}
	return text;
}

[[nodiscard]] std::string storage_label(
	const catalog::CatalogRepositoryState& repository,
	const std::optional<core::StableIdentifier>& storage_id) {
	if (!storage_id)
		return "Unassigned storage";
	const persistence::StorageEnvelope* storage =
		catalog::find_storage_envelope(repository, *storage_id);
	if (storage == nullptr)
		return "Missing storage: " + storage_id->value();
	return storage->record.display_name;
}

[[nodiscard]] std::string storage_choice_label(
	const catalog::CatalogRepositoryState& repository,
	const persistence::StorageEnvelope& storage) {
	std::string text = storage.record.display_name;
	if (!storage.record.storage_type.empty())
		text += " · " + storage.record.storage_type;

	const std::map<std::string, catalog::StorageProjection>::const_iterator
		projection =
			repository.storage_projections.find(storage.record.id.value());
	if (projection != repository.storage_projections.end()
		&& !projection->second.path_label.empty()) {
		text += " · " + projection->second.path_label;
	} else if (!storage.record.location.empty()) {
		text += " · " + storage.record.location;
	}
	return text;
}

[[nodiscard]] std::optional<core::StableIdentifier> next_storage_choice(
	const catalog::CatalogRepositoryState& repository,
	const std::optional<core::StableIdentifier>& current,
	std::optional<core::StableIdentifier> excluded = std::nullopt) {
	if (repository.storages.empty())
		return std::nullopt;
	std::vector<core::StableIdentifier> choices;
	for (const persistence::StorageEnvelope& storage : repository.storages) {
		if (excluded && storage.record.id == *excluded)
			continue;
		choices.push_back(storage.record.id);
	}
	if (choices.empty())
		return std::nullopt;
	if (!current)
		return choices.front();
	for (std::size_t index = 0; index < choices.size(); ++index)
		if (choices[index] == *current)
			return index + 1U < choices.size()
					   ? std::optional{choices[index + 1U]}
					   : std::nullopt;
	return choices.front();
}

[[nodiscard]] std::string diagnostic_summary(
	std::span<const EntityEditDiagnostic> diagnostics) {
	if (diagnostics.empty())
		return "";
	std::string text;
	for (const EntityEditDiagnostic& diagnostic : diagnostics) {
		if (!text.empty())
			text += " · ";
		text += diagnostic.code + ": " + diagnostic.message;
	}
	return text;
}

[[nodiscard]] std::string core_diagnostic_summary(
	std::span<const core::Diagnostic> diagnostics) {
	if (diagnostics.empty())
		return "";
	std::string text;
	for (const core::Diagnostic& diagnostic : diagnostics) {
		if (!text.empty())
			text += " · ";
		text += diagnostic.code + ": " + diagnostic.message;
	}
	return text;
}

[[nodiscard]] std::string progress_summary(
	std::span<const platform::ProgressEvent> events) {
	if (events.empty())
		return "Progress: no events reported yet.";
	const platform::ProgressEvent& latest = events.back();
	std::string text					  = "Progress: " + latest.phase;
	if (!latest.message.empty())
		text += " · " + latest.message;
	if (latest.current_units.has_value()) {
		text += " · " + std::to_string(*latest.current_units);
		if (latest.total_units.has_value())
			text += "/" + std::to_string(*latest.total_units);
	}
	text += latest.cancellable ? " · cancellable" : " · not cancellable";
	return text;
}

[[nodiscard]] std::string pending_photo_summary(
	std::span<const PendingPhotoSource> pending_sources) {
	std::uint64_t staged_count{};
	std::uint64_t failed_count{};
	std::uint64_t removed_count{};
	std::uint64_t consumed_count{};
	for (const PendingPhotoSource& source : pending_sources)
		if (source.ready_for_import())
			++staged_count;
		else if (source.status == PendingPhotoStatus::Failed
				 || source.status == PendingPhotoStatus::Cancelled)
			++failed_count;
		else if (source.status == PendingPhotoStatus::Removed)
			++removed_count;
		else if (source.status == PendingPhotoStatus::Consumed)
			++consumed_count;

	std::string text = "Pending photos: " + std::to_string(staged_count)
					   + " staged for import";
	if (failed_count > 0U)
		text += " · " + std::to_string(failed_count) + " failed";
	if (removed_count > 0U)
		text += " · " + std::to_string(removed_count) + " removed";
	if (consumed_count > 0U)
		text += " · " + std::to_string(consumed_count) + " consumed";
	return text;
}

[[nodiscard]] std::string pending_photo_source_summary(
	const PendingPhotoSource& source, std::size_t display_index) {
	std::string text = "Pending photo " + std::to_string(display_index) + ": "
					   + std::string{to_string(source.status)};
	if (source.ready_for_import())
		text += " · staged";
	if (source.byte_count)
		text += " · " + std::to_string(*source.byte_count) + " bytes";
	if (!source.diagnostics.empty())
		text += " · " + std::to_string(source.diagnostics.size()) + " issue(s)";
	return text;
}

[[nodiscard]] std::string tag_row_count_summary(
	std::span<const domain::TagRow> tags) {
	if (tags.empty())
		return "Tags: no rows yet";
	return "Tags: " + std::to_string(tags.size())
		   + (tags.size() == 1U ? " row" : " rows");
}

struct TagKeyCandidateGroups final {
	std::vector<std::string> item_keys;
	std::vector<std::string> storage_keys;
};

void append_tag_key_candidate(std::vector<std::string>& keys,
							  std::set<std::string>& seen,
							  const domain::TagRow& tag) {
	if (!domain::is_tag_key_hint_candidate(tag))
		return;
	if (seen.insert(tag.key).second)
		keys.push_back(tag.key);
}

[[nodiscard]] TagKeyCandidateGroups derive_tag_key_candidate_groups(
	const catalog::CatalogRepositoryState& repository) {
	TagKeyCandidateGroups groups;
	std::set<std::string> seen_item_keys;
	std::set<std::string> seen_storage_keys;
	for (const persistence::ItemEnvelope& item : repository.items)
		for (const domain::TagRow& tag : item.record.tags)
			append_tag_key_candidate(groups.item_keys, seen_item_keys, tag);
	for (const persistence::StorageEnvelope& storage : repository.storages)
		for (const domain::TagRow& tag : storage.record.tags)
			append_tag_key_candidate(groups.storage_keys, seen_storage_keys,
									 tag);
	return groups;
}

void apply_tag_key_candidate(std::vector<domain::TagRow>& tags,
							 std::string key) {
	for (domain::TagRow& tag : tags) {
		if (domain::is_blank_tag_key(tag.key)) {
			tag.key = std::move(key);
			return;
		}
	}
	tags.push_back(domain::TagRow{.key = std::move(key)});
}

[[nodiscard]] bool has_ready_pending_photo(
	std::span<const PendingPhotoSource> pending_sources) noexcept {
	return std::ranges::any_of(pending_sources,
							   [](const PendingPhotoSource& source) {
		return source.ready_for_import();
	});
}

[[nodiscard]] std::string recovery_action_summary(
	std::span<const std::string> actions) {
	if (actions.empty())
		return "Safe actions: —";
	std::string text = "Safe actions: ";
	for (std::size_t index = 0; index < actions.size(); ++index) {
		if (index > 0U)
			text += " · ";
		text += actions[index];
	}
	return text;
}

[[nodiscard]] std::string recovery_counts_summary(
	const CatalogRecoveryUiSummary& summary) {
	std::string text =
		"Accepted: items=" + std::to_string(summary.accepted_item_count);
	text += " · storages=" + std::to_string(summary.accepted_storage_count);
	text += " · photos=" + std::to_string(summary.accepted_photo_count);
	text +=
		" · skipped lines: items=" + std::to_string(summary.skipped_item_count);
	text += " storages=" + std::to_string(summary.skipped_storage_count);
	text += " photos=" + std::to_string(summary.skipped_photo_count);
	text += " · broken refs=" + std::to_string(summary.broken_reference_count);
	text += " · orphan media=" + std::to_string(summary.orphan_media_count);
	return text;
}

[[nodiscard]] std::string import_validation_summary(
	const catalog::StagedCatalogValidationResult& validation) {
	std::string text = "Staged import: ";
	text += std::string{persistence::to_string(validation.load_status)};
	text += " · items=" + std::to_string(validation.items_accepted);
	text += " · storages=" + std::to_string(validation.storages_accepted);
	text += " · photos=" + std::to_string(validation.photos_accepted);
	text += " · broken refs="
			+ std::to_string(
				validation.derived_recovery_summary.broken_reference_count);
	text += " · orphan media="
			+ std::to_string(
				validation.derived_recovery_summary.orphan_media_count);
	return text;
}

[[nodiscard]] bool has_diagnostics(
	std::span<const core::Diagnostic> diagnostics) noexcept {
	return !diagnostics.empty();
}

[[nodiscard]] std::string photo_summary(const persistence::PhotoEnvelope& photo,
										std::size_t position,
										std::size_t total) {
	std::string text =
		"Photo " + std::to_string(position) + "/" + std::to_string(total);
	text += photo.record.is_main ? " · main" : " · not main";
	if (photo.record.width && photo.record.height) {
		text += " · " + std::to_string(*photo.record.width) + "x"
				+ std::to_string(*photo.record.height);
	}
	if (photo.record.encoded_bytes)
		text += " · " + std::to_string(*photo.record.encoded_bytes) + " bytes";
	if (!photo.record.source_mime_type.empty())
		text += " · " + photo.record.source_mime_type;
	return text;
}

[[nodiscard]] std::string owner_caption(
	const catalog::CatalogRepositoryState& repository,
	const domain::PhotoOwner& owner) {
	if (owner.type == domain::PhotoOwnerType::Item) {
		const persistence::ItemEnvelope* item =
			catalog::find_item_envelope(repository, owner.id);
		return item == nullptr ? "Missing item owner"
							   : "Item: " + item->record.display_name;
	}
	const persistence::StorageEnvelope* storage =
		catalog::find_storage_envelope(repository, owner.id);
	return storage == nullptr ? "Missing storage owner"
							  : "Storage: " + storage->record.display_name;
}

[[nodiscard]] const catalog::OwnerPhotoProjection* owner_photo_projection(
	const catalog::CatalogRepositoryState& repository,
	const domain::PhotoOwner& owner) {
	const std::map<std::string, catalog::OwnerPhotoProjection>& projections =
		owner.type == domain::PhotoOwnerType::Item
			? repository.item_photo_projections
			: repository.storage_photo_projections;
	const std::map<std::string, catalog::OwnerPhotoProjection>::const_iterator
		found = projections.find(owner.id.value());
	return found == projections.end() ? nullptr : &found->second;
}

[[nodiscard]] std::optional<std::size_t> find_photo_index_in_order(
	std::span<const core::StableIdentifier> ordered_photo_ids,
	const core::StableIdentifier& photo_id) {
	for (std::size_t index = 0; index < ordered_photo_ids.size(); ++index)
		if (ordered_photo_ids[index] == photo_id)
			return index;
	return std::nullopt;
}

[[nodiscard]] std::optional<core::StableIdentifier> first_viewable_photo_id(
	const catalog::CatalogRepositoryState& repository,
	const domain::PhotoOwner& owner) {
	const catalog::OwnerPhotoProjection* projection =
		owner_photo_projection(repository, owner);
	if (projection == nullptr || projection->ordered_photo_ids.empty())
		return std::nullopt;
	if (projection->representative_usable_photo_id)
		return projection->representative_usable_photo_id;
	return projection->ordered_photo_ids.front();
}

[[nodiscard]] std::optional<core::StableIdentifier> adjacent_photo_id(
	const catalog::CatalogRepositoryState& repository,
	const domain::PhotoOwner& owner, const core::StableIdentifier& photo_id,
	int direction) {
	const catalog::OwnerPhotoProjection* projection =
		owner_photo_projection(repository, owner);
	if (projection == nullptr || projection->ordered_photo_ids.empty())
		return std::nullopt;
	const std::optional<std::size_t> index =
		find_photo_index_in_order(projection->ordered_photo_ids, photo_id);
	if (!index.has_value())
		return projection->ordered_photo_ids.front();
	const std::size_t total = projection->ordered_photo_ids.size();
	if (direction < 0)
		return projection
			->ordered_photo_ids[*index == 0U ? total - 1U : *index - 1U];
	return projection->ordered_photo_ids[(*index + 1U) % total];
}

[[nodiscard]] juce::Image juce_image_from_pixels(
	const platform::ImagePixels& pixels) {
	if (pixels.format != platform::PixelFormat::Rgba8 || pixels.width == 0U
		|| pixels.height == 0U
		|| pixels.width
			   > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
		|| pixels.height
			   > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
		return {};
	}
	platform::ImagePixelsValidation validation =
		platform::validate_image_pixels(pixels);
	if (!validation.valid())
		return {};
	const int width	 = static_cast<int>(pixels.width);
	const int height = static_cast<int>(pixels.height);
	juce::Image image{juce::Image::RGB, width, height, false};
	juce::Image::BitmapData bitmap{image, juce::Image::BitmapData::writeOnly};
	for (int y = 0; y < height; ++y) {
		const std::size_t source_row_offset =
			static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4U;
		juce::uint8* row = bitmap.getLinePointer(y);
		for (int x = 0; x < width; ++x) {
			const std::size_t source_offset =
				source_row_offset + static_cast<std::size_t>(x) * 4U;
			juce::uint8* target =
				row + static_cast<std::ptrdiff_t>(x) * bitmap.pixelStride;
			juce::PixelRGB* target_pixel =
				reinterpret_cast<juce::PixelRGB*>(target);
			target_pixel->setARGB(0xffU, pixels.bytes[source_offset],
								  pixels.bytes[source_offset + 1U],
								  pixels.bytes[source_offset + 2U]);
		}
	}
	return image;
}

[[nodiscard]] bool contains_string(std::span<const std::string> values,
								   std::string_view value) {
	return std::ranges::any_of(values, [value](const std::string& candidate) {
		return candidate == value;
	});
}

void toggle_string(std::vector<std::string>& values, std::string value) {
	std::vector<std::string>::iterator found = std::ranges::find(values, value);
	if (found == values.end()) {
		values.push_back(std::move(value));
		std::ranges::sort(values);
		return;
	}
	values.erase(found);
}

[[nodiscard]] bool contains_status(std::span<const domain::ItemStatus> values,
								   domain::ItemStatus status) {
	return std::ranges::find(values, status) != values.end();
}

void toggle_status(std::vector<domain::ItemStatus>& values,
				   domain::ItemStatus status) {
	std::vector<domain::ItemStatus>::iterator found =
		std::ranges::find(values, status);
	if (found == values.end()) {
		values.push_back(status);
		std::ranges::sort(values);
		return;
	}
	values.erase(found);
}

[[nodiscard]] bool has_catalog_filters(
	const catalog::CatalogSearchFilters& filters) noexcept {
	return !filters.categories.empty() || !filters.statuses.empty()
		   || filters.include_archived || filters.storage_id.has_value()
		   || filters.storage_unassigned_only
		   || filters.photo_presence != catalog::SearchPhotoPresenceFilter::Any
		   || filters.listed_only || filters.sold_only;
}

[[nodiscard]] std::string active_filter_summary(
	const catalog::CatalogSearchFilters& filters,
	const catalog::CatalogRepositoryState& repository) {
	std::vector<std::string> parts;
	if (!filters.categories.empty()) {
		std::string categories = "categories=";
		for (std::size_t index = 0; index < filters.categories.size();
			 ++index) {
			if (index > 0U)
				categories += ",";
			categories += filters.categories[index];
		}
		parts.push_back(std::move(categories));
	}
	if (!filters.statuses.empty()) {
		std::string statuses = "statuses=";
		for (std::size_t index = 0; index < filters.statuses.size(); ++index) {
			if (index > 0U)
				statuses += ",";
			statuses += status_text(filters.statuses[index]);
		}
		parts.push_back(std::move(statuses));
	}
	if (filters.storage_unassigned_only)
		parts.emplace_back("storage=unassigned");
	if (filters.storage_id) {
		const persistence::StorageEnvelope* storage =
			catalog::find_storage_envelope(repository, *filters.storage_id);
		parts.push_back("storage="
						+ (storage != nullptr ? storage->record.display_name
											  : filters.storage_id->value()));
		if (filters.include_nested_storage)
			parts.emplace_back("nested=on");
	}
	if (filters.photo_presence != catalog::SearchPhotoPresenceFilter::Any)
		parts.push_back("photos=" + photo_filter_label(filters.photo_presence));
	if (filters.listed_only)
		parts.emplace_back("listed shortcut");
	if (filters.sold_only)
		parts.emplace_back("sold shortcut");
	if (filters.include_archived)
		parts.emplace_back("include archived");

	if (parts.empty())
		return "No active filters";

	std::string summary;
	for (std::size_t index = 0; index < parts.size(); ++index) {
		if (index > 0U)
			summary += " · ";
		summary += parts[index];
	}
	return summary;
}

[[nodiscard]] std::string first_note_or_tag_summary(
	const persistence::ItemEnvelope* item) {
	if (item == nullptr)
		return {};
	if (!item->record.notes.empty())
		return item->record.notes;
	if (!item->record.tags.empty())
		return item->record.tags.front().key + "="
			   + item->record.tags.front().value;
	return {};
}

[[nodiscard]] std::string first_storage_note_or_tag_summary(
	const persistence::StorageEnvelope* storage) {
	if (storage == nullptr)
		return {};
	if (!storage->record.notes.empty())
		return storage->record.notes;
	if (!storage->record.tags.empty())
		return storage->record.tags.front().key + "="
			   + storage->record.tags.front().value;
	return {};
}

[[nodiscard]] std::string warning_summary(
	const catalog::SearchWarningMarkers& warnings) {
	std::vector<std::string> parts;
	if (warnings.no_photo_records)
		parts.emplace_back("no photo");
	if (warnings.broken_photos)
		parts.emplace_back("broken photos");
	if (warnings.broken_storage_reference)
		parts.emplace_back("broken storage");
	if (warnings.broken_parent_reference)
		parts.emplace_back("broken parent");
	if (warnings.archived_record)
		parts.emplace_back("archived");
	if (warnings.archived_storage)
		parts.emplace_back("archived storage");

	std::string text;
	for (std::size_t index = 0; index < parts.size(); ++index) {
		if (index > 0U)
			text += ", ";
		text += parts[index];
	}
	return text;
}

[[nodiscard]] juce::String item_result_text(
	const catalog::SearchResult& result, const CatalogSessionState& session) {
	const persistence::ItemEnvelope* item =
		catalog::find_item_envelope(session.repository, result.record_id);
	std::string text = photo_presence_label(result.photo_presence) + " "
					   + result.display_title + " · " + result.category;
	if (result.item_status)
		text += " · " + status_text(*result.item_status);
	if (!result.location_text.empty())
		text += " · " + result.location_text;
	const std::string details = first_note_or_tag_summary(item);
	if (!details.empty())
		text += " · " + details;
	const std::string warnings = warning_summary(result.warnings);
	if (!warnings.empty())
		text += " · ⚠ " + warnings;
	return juce_text(text);
}

[[nodiscard]] juce::String storage_result_text(
	const catalog::SearchResult& result, const CatalogSessionState& session) {
	const persistence::StorageEnvelope* storage =
		catalog::find_storage_envelope(session.repository, result.record_id);
	std::string text = "[storage] " + result.display_title;
	if (!result.storage_type.empty())
		text += " · " + result.storage_type;
	if (result.storage_lifecycle_status)
		text +=
			" · " + storage_lifecycle_text(*result.storage_lifecycle_status);
	if (!result.location_text.empty())
		text += " · " + result.location_text;
	text += " · child storages=" + std::to_string(result.direct_child_count);
	text += " · items=" + std::to_string(result.direct_item_count) + "/"
			+ std::to_string(result.nested_item_count);
	const std::string details = first_storage_note_or_tag_summary(storage);
	if (!details.empty())
		text += " · " + details;
	const std::string warnings = warning_summary(result.warnings);
	if (!warnings.empty())
		text += " · ⚠ " + warnings;
	return juce_text(text);
}

[[nodiscard]] std::vector<std::string> distinct_categories(
	const catalog::SearchIndex& index) {
	std::set<std::string> category_set;
	for (const catalog::ItemSearchDocument& item : index.items)
		if (!item.category.empty())
			category_set.insert(item.category);
	return {category_set.begin(), category_set.end()};
}

[[nodiscard]] std::set<std::string> storage_filter_id_set(
	const catalog::CatalogRepositoryState& repository,
	const core::StableIdentifier& selected_storage_id, bool include_nested) {
	std::set<std::string> ids;
	ids.insert(selected_storage_id.value());
	if (!include_nested)
		return ids;

	const std::map<std::string, catalog::StorageProjection>::const_iterator
		found =
			repository.storage_projections.find(selected_storage_id.value());
	if (found == repository.storage_projections.end())
		return ids;
	for (const core::StableIdentifier& descendant :
		 found->second.nested_descendant_storage_ids) {
		ids.insert(descendant.value());
	}
	return ids;
}
}	 // namespace

core::StableIdentifier ShellIdentifierSource::next_stable_identifier() {
	return random_identifiers.next_stable_identifier();
}

core::OperationIdentifier ShellIdentifierSource::next_operation_identifier() {
	return random_identifiers.next_operation_identifier();
}

core::EpochMilliseconds ShellClock::now() const {
	return core::SystemClock{}.now();
}

struct AppShellComponent::ContentComponent final : public juce::Component {
	struct Row final {
		std::unique_ptr<juce::Component> component;
		int height{};
		bool owned{true};
	};

	ContentComponent() { setOpaque(true); }

	~ContentComponent() override {
		for (Row& row : rows)
			if (!row.owned)
				row.component.release();
	}

	void clear_rows() {
		for (Row& row : rows) {
			removeChildComponent(row.component.get());
			if (!row.owned)
				row.component.release();
		}
		rows.clear();
		if (!rebuilding)
			setSize(getWidth(), 1);
	}

	void begin_rebuild() noexcept { rebuilding = true; }

	void end_rebuild() {
		rebuilding = false;
		resized();
	}

	TextRowComponent& add_label(juce::String text, int height,
								juce::Colour colour = panel_colour(),
								bool bold			= false) {
		std::unique_ptr<TextRowComponent> label =
			std::make_unique<TextRowComponent>(std::move(text), colour, bold);
		TextRowComponent& reference = *label;
		add_row(std::move(label), height);
		return reference;
	}

	juce::Button& add_button(juce::String text, int height) {
		std::unique_ptr<RowButtonComponent> button =
			std::make_unique<RowButtonComponent>(std::move(text),
												 elevated_surface_colour());
		RowButtonComponent& reference = *button;
		add_row(std::move(button), height);
		return reference;
	}

	ImagePanelComponent& add_image_panel(juce::Image image,
										 juce::String caption,
										 juce::String placeholder, int height) {
		std::unique_ptr<ImagePanelComponent> panel =
			std::make_unique<ImagePanelComponent>(
				std::move(image), std::move(caption), std::move(placeholder));
		ImagePanelComponent& reference = *panel;
		add_row(std::move(panel), height);
		return reference;
	}

	InlineButtonRowComponent& add_inline_buttons(
		juce::String title,
		std::vector<InlineButtonRowComponent::Action> actions, int height) {
		std::unique_ptr<InlineButtonRowComponent> row =
			std::make_unique<InlineButtonRowComponent>(std::move(title),
													   std::move(actions));
		InlineButtonRowComponent& reference = *row;
		add_row(std::move(row), height);
		return reference;
	}

	ButtonGridComponent& add_button_grid(
		juce::String title, std::vector<ButtonGridComponent::Action> actions,
		int column_count, int height) {
		std::unique_ptr<ButtonGridComponent> grid =
			std::make_unique<ButtonGridComponent>(
				std::move(title), std::move(actions), column_count);
		ButtonGridComponent& reference = *grid;
		add_row(std::move(grid), height);
		return reference;
	}

	EditorPairComponent& add_editor_pair(juce::TextEditor& first_editor,
										 juce::String first_placeholder,
										 juce::TextEditor& second_editor,
										 juce::String second_placeholder,
										 int height) {
		std::unique_ptr<EditorPairComponent> row =
			std::make_unique<EditorPairComponent>(
				first_editor, second_editor, std::move(first_placeholder),
				std::move(second_placeholder));
		EditorPairComponent& reference = *row;
		add_row(std::move(row), height);
		return reference;
	}

	PendingPhotoStripComponent& add_pending_photo_strip(
		std::vector<PendingPhotoSource> sources,
		std::function<void()> add_handler, std::function<void()> clear_handler,
		std::function<void(std::size_t)> remove_handler, int height) {
		std::unique_ptr<PendingPhotoStripComponent> strip =
			std::make_unique<PendingPhotoStripComponent>(
				std::move(sources), std::move(add_handler),
				std::move(clear_handler), std::move(remove_handler));
		PendingPhotoStripComponent& reference = *strip;
		add_row(std::move(strip), height);
		return reference;
	}

	TagRowEditorComponent& add_tag_editor_row(
		std::size_t row_index, domain::TagRow tag,
		std::function<void(std::size_t, domain::TagRow)> change_handler,
		std::function<void(std::size_t)> remove_handler, int height) {
		std::unique_ptr<TagRowEditorComponent> row =
			std::make_unique<TagRowEditorComponent>(row_index, std::move(tag),
													std::move(change_handler),
													std::move(remove_handler));
		TagRowEditorComponent& reference = *row;
		add_row(std::move(row), height);
		return reference;
	}

	juce::ToggleButton& add_toggle(juce::String text, bool state, int height) {
		std::unique_ptr<TouchSafeToggleButton> toggle =
			std::make_unique<TouchSafeToggleButton>(std::move(text));
		TouchSafeToggleButton& reference = *toggle;
		toggle->setToggleState(state, juce::dontSendNotification);
		toggle->setColour(juce::ToggleButton::textColourId, text_colour());
		toggle->setColour(juce::ToggleButton::tickColourId, accent_colour());
		add_row(std::move(toggle), height);
		return reference;
	}

	juce::TextEditor& add_editor(juce::TextEditor& editor,
								 juce::String placeholder, int height,
								 bool multiline = false) {
		removeChildComponent(&editor);
		editor.setTextToShowWhenEmpty(std::move(placeholder),
									  muted_text_colour());
		editor.setMultiLine(multiline);
		editor.setReturnKeyStartsNewLine(multiline);
		editor.setScrollbarsShown(multiline);
		style_text_editor(editor);
		add_row(std::unique_ptr<juce::Component>(&editor), height, false);
		return editor;
	}

	void resized() override {
		juce::Rectangle<int> bounds = getLocalBounds().reduced(6, 4);
		int y						= bounds.getY();
		const int width				= bounds.getWidth();
		for (Row& row : rows) {
			row.component->setBounds(bounds.getX(), y, width, row.height - 4);
			y += row.height;
		}
		setSize(std::max(1, getWidth()), std::max(1, y + 8));
	}

	void paint(juce::Graphics& graphics) override {
		graphics.fillAll(background_colour());
	}

private:
	void add_row(std::unique_ptr<juce::Component> component, int height,
				 bool owned = true) {
		addAndMakeVisible(*component);
		rows.push_back(Row{.component = std::move(component),
						   .height	  = height,
						   .owned	  = owned});
		if (!rebuilding)
			resized();
	}

	std::vector<Row> rows;
	bool rebuilding{};
};

AppShellComponent::AppShellComponent(CatalogSessionState session_state,
									 PlatformServices platform_services)
	: session(std::move(session_state))
	, internal_photo_codec(platform_services.internal_photo_codec)
	, content(std::make_unique<ContentComponent>()) {
	setOpaque(true);
	setSize(480, 720);

	title_label.setJustificationType(juce::Justification::centredLeft);
	title_label.setColour(juce::Label::textColourId, text_colour());
	title_label.setFont(juce::FontOptions(22.0f, juce::Font::bold));
	addAndMakeVisible(title_label);

	status_label.setJustificationType(juce::Justification::centredLeft);
	status_label.setColour(juce::Label::textColourId, muted_text_colour());
	status_label.setMinimumHorizontalScale(0.70f);
	status_label.setFont(juce::FontOptions(14.5f, juce::Font::plain));
	addAndMakeVisible(status_label);

	catalog_search_editor.setTextToShowWhenEmpty("Search catalog",
												 muted_text_colour());
	style_text_editor(catalog_search_editor);
	catalog_search_editor.onTextChange = [this] { schedule_content_refresh(); };
	addAndMakeVisible(catalog_search_editor);

	storage_search_editor.setTextToShowWhenEmpty("Search storages",
												 muted_text_colour());
	style_text_editor(storage_search_editor);
	storage_search_editor.onTextChange = [this] { schedule_content_refresh(); };
	addAndMakeVisible(storage_search_editor);

	catalog_clear_button.onClick = [this] {
		catalog_search_editor.setText(juce::String{},
									  juce::dontSendNotification);
		refresh_content();
	};
	catalog_filter_button.onClick = [this] {
		catalog_filter_draft		 = catalog_filters;
		catalog_filter_panel_visible = !catalog_filter_panel_visible;
		refresh_all();
	};
	catalog_clear_filters_button.onClick = [this] {
		reset_catalog_filters();
		refresh_all();
	};
	storage_clear_button.onClick = [this] {
		storage_search_editor.setText(juce::String{},
									  juce::dontSendNotification);
		refresh_content();
	};
	form_cancel_button.onClick = [this] {
		select_root(form_return_destination.value_or(RootDestination::Catalog));
	};
	form_save_button.onClick = [this] {
		if (destination == RootDestination::ItemForm)
			save_item_form();
		else if (destination == RootDestination::StorageForm)
			save_storage_form();
	};
	back_button.onClick = [this] {
		if (destination == RootDestination::ItemDetail) {
			select_root(RootDestination::Catalog);
		} else if (destination == RootDestination::PhotoViewer
				   && selected_photo_owner
				   && selected_photo_owner->type
						  == domain::PhotoOwnerType::Item) {
			selected_item_id = selected_photo_owner->id;
			select_root(RootDestination::ItemDetail);
		} else if (destination == RootDestination::PhotoViewer
				   && selected_photo_owner
				   && selected_photo_owner->type
						  == domain::PhotoOwnerType::Storage) {
			selected_storage_id = selected_photo_owner->id;
			select_root(RootDestination::StorageDetail);
		} else if (destination == RootDestination::ItemForm
				   || destination == RootDestination::StorageForm) {
			select_root(
				form_return_destination.value_or(RootDestination::Catalog));
		} else if (destination == RootDestination::BackupRecovery) {
			select_root(RootDestination::More);
		} else {
			select_root(RootDestination::Storages);
		}
	};
	for (juce::TextButton* button :
		 {&catalog_clear_button, &catalog_filter_button,
		  &catalog_clear_filters_button, &storage_clear_button, &back_button}) {
		style_text_button(*button);
		addAndMakeVisible(*button);
	}
	for (juce::TextButton* button : {&form_cancel_button, &form_save_button}) {
		style_text_button(*button);
		addAndMakeVisible(*button);
	}

	viewport.setViewedComponent(content.get(), false);
	viewport.setScrollBarsShown(true, false);
	viewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::nonHover);
	addAndMakeVisible(viewport);

	catalog_nav_button.onClick = [this] {
		select_root(RootDestination::Catalog);
	};
	storages_nav_button.onClick = [this] {
		select_root(RootDestination::Storages);
	};
	add_nav_button.onClick	= [this] { select_root(RootDestination::Add); };
	more_nav_button.onClick = [this] { select_root(RootDestination::More); };
	for (juce::TextButton* button : {&catalog_nav_button, &storages_nav_button,
									 &add_nav_button, &more_nav_button}) {
		style_text_button(*button);
		addAndMakeVisible(*button);
	}

	refresh_all();
}

AppShellComponent::~AppShellComponent() {
	stopTimer();
}

void AppShellComponent::paint(juce::Graphics& graphics) {
	graphics.fillAll(background_colour());
}

void AppShellComponent::resized() {
	juce::Rectangle<int> bounds = getLocalBounds().reduced(10);
	const bool form_visible = destination == RootDestination::ItemForm
							  || destination == RootDestination::StorageForm;
	catalog_nav_button.setVisible(!form_visible);
	storages_nav_button.setVisible(!form_visible);
	add_nav_button.setVisible(!form_visible);
	more_nav_button.setVisible(!form_visible);
	form_cancel_button.setVisible(form_visible);
	form_save_button.setVisible(form_visible);

	if (form_visible) {
		juce::Rectangle<int> form_actions = bounds.removeFromBottom(58);
		const int action_width = std::max(1, form_actions.getWidth() / 2);
		juce::Rectangle<int> cancel_area =
			form_actions.removeFromLeft(action_width);
		form_cancel_button.setBounds(cancel_area.reduced(3));
		form_save_button.setBounds(form_actions.reduced(3));
	} else {
		juce::Rectangle<int> nav = bounds.removeFromBottom(54);
		const int nav_width		 = nav.getWidth() / 4;
		catalog_nav_button.setBounds(nav.removeFromLeft(nav_width).reduced(3));
		storages_nav_button.setBounds(nav.removeFromLeft(nav_width).reduced(3));
		add_nav_button.setBounds(nav.removeFromLeft(nav_width).reduced(3));
		more_nav_button.setBounds(nav.reduced(3));
	}

	title_label.setBounds(bounds.removeFromTop(32));
	status_label.setBounds(bounds.removeFromTop(26));

	juce::Rectangle<int> controls = bounds.removeFromTop(44);
	const bool catalog_visible	  = destination == RootDestination::Catalog;
	const bool storages_visible	  = destination == RootDestination::Storages;
	const bool detail_visible =
		destination == RootDestination::ItemDetail
		|| destination == RootDestination::StorageDetail
		|| destination == RootDestination::ItemForm
		|| destination == RootDestination::StorageForm
		|| destination == RootDestination::PhotoViewer
		|| destination == RootDestination::BackupRecovery;
	catalog_search_editor.setVisible(catalog_visible);
	catalog_clear_button.setVisible(catalog_visible);
	catalog_filter_button.setVisible(catalog_visible);
	catalog_clear_filters_button.setVisible(
		catalog_visible && has_catalog_filters(catalog_filters));
	storage_search_editor.setVisible(storages_visible);
	storage_clear_button.setVisible(storages_visible);
	back_button.setVisible(detail_visible);

	if (catalog_visible) {
		catalog_search_editor.setBounds(
			controls.removeFromLeft(std::max(120, controls.getWidth() - 244))
				.reduced(2));
		catalog_clear_button.setBounds(controls.removeFromLeft(64).reduced(2));
		catalog_filter_button.setBounds(controls.removeFromLeft(78).reduced(2));
		catalog_clear_filters_button.setBounds(controls.reduced(2));
	} else if (storages_visible) {
		storage_search_editor.setBounds(
			controls.removeFromLeft(std::max(140, controls.getWidth() - 72))
				.reduced(2));
		storage_clear_button.setBounds(controls.reduced(2));
	} else if (detail_visible) {
		back_button.setBounds(controls.removeFromLeft(92).reduced(2));
	}

	bounds.removeFromTop(4);
	viewport.setBounds(bounds);
	if (content)
		content->setSize(viewport.getWidth(), content->getHeight());
}

void AppShellComponent::select_root(RootDestination destination_value) {
	const RootDestination previous_destination = destination;
	if (previous_destination == RootDestination::ItemForm
		&& destination_value != RootDestination::ItemForm) {
		cleanup_item_pending_photos();
	}
	destination = destination_value;
	if (destination != RootDestination::ItemDetail
		&& destination != RootDestination::ItemForm
		&& !(destination == RootDestination::PhotoViewer && selected_photo_owner
			 && selected_photo_owner->type == domain::PhotoOwnerType::Item)) {
		selected_item_id.reset();
	}
	if (destination != RootDestination::StorageDetail
		&& destination != RootDestination::StorageForm
		&& destination != RootDestination::ItemForm
		&& !(destination == RootDestination::PhotoViewer && selected_photo_owner
			 && selected_photo_owner->type
					== domain::PhotoOwnerType::Storage)) {
		selected_storage_id.reset();
	}
	if (destination != RootDestination::PhotoViewer) {
		selected_photo_owner.reset();
		selected_photo_id.reset();
		last_display_photo_id.reset();
		last_photo_display_result = catalog::PhotoDisplayResult{};
	}
	if (destination != RootDestination::ItemForm
		&& destination != RootDestination::StorageForm) {
		form_return_destination.reset();
	}
	if (destination != RootDestination::BackupRecovery) {
		pending_import_staging.reset();
		pending_import_degraded_acknowledged = false;
	}
	refresh_all();
}

void AppShellComponent::open_item_detail(core::StableIdentifier item_id) {
	selected_item_id = std::move(item_id);
	destination		 = RootDestination::ItemDetail;
	refresh_all();
}

void AppShellComponent::open_storage_detail(core::StableIdentifier storage_id) {
	selected_storage_id			  = std::move(storage_id);
	destination					  = RootDestination::StorageDetail;
	storage_detail_include_nested = true;
	refresh_all();
}

void AppShellComponent::open_photo_viewer(
	domain::PhotoOwner owner,
	std::optional<core::StableIdentifier> requested_photo_id) {
	selected_photo_owner = owner;
	selected_photo_id =
		requested_photo_id.has_value()
			? requested_photo_id
			: first_viewable_photo_id(session.repository, owner);
	if (owner.type == domain::PhotoOwnerType::Item)
		selected_item_id = owner.id;
	else
		selected_storage_id = owner.id;
	last_photo_message.clear();
	last_photo_diagnostics.clear();
	last_display_photo_id.reset();
	last_photo_display_result = catalog::PhotoDisplayResult{};
	destination				  = RootDestination::PhotoViewer;
	refresh_all();
}

void AppShellComponent::open_new_item_form(
	std::optional<core::StableIdentifier> storage_id) {
	reset_item_form();
	item_form_mode			   = FormMode::Create;
	item_form_draft.storage_id = std::move(storage_id);
	form_return_destination	   = selected_storage_id
									 ? RootDestination::StorageDetail
									 : RootDestination::Add;
	destination				   = RootDestination::ItemForm;
	refresh_all();
}

void AppShellComponent::open_existing_item_form(
	core::StableIdentifier item_id) {
	const persistence::ItemEnvelope* item =
		catalog::find_item_envelope(session.repository, item_id);
	if (item == nullptr)
		return;
	load_item_form_from_record(*item);
	selected_item_id		= std::move(item_id);
	item_form_mode			= FormMode::Edit;
	form_return_destination = RootDestination::ItemDetail;
	destination				= RootDestination::ItemForm;
	refresh_all();
}

void AppShellComponent::open_new_storage_form(
	std::optional<core::StableIdentifier> parent_id) {
	reset_storage_form();
	storage_form_mode					 = FormMode::Create;
	storage_form_draft.parent_storage_id = std::move(parent_id);
	form_return_destination				 = selected_storage_id
											   ? RootDestination::StorageDetail
											   : RootDestination::Add;
	destination							 = RootDestination::StorageForm;
	refresh_all();
}

void AppShellComponent::open_existing_storage_form(
	core::StableIdentifier storage_id) {
	const persistence::StorageEnvelope* storage =
		catalog::find_storage_envelope(session.repository, storage_id);
	if (storage == nullptr)
		return;
	load_storage_form_from_record(*storage);
	selected_storage_id		= std::move(storage_id);
	storage_form_mode		= FormMode::Edit;
	form_return_destination = RootDestination::StorageDetail;
	destination				= RootDestination::StorageForm;
	refresh_all();
}

void AppShellComponent::request_add_photos(domain::PhotoOwner owner) {
	last_progress_events.clear();
	last_photo_diagnostics.clear();
	last_photo_message = "Select photos to import.";
	core::OperationResult picker_started =
		photo_selection_service.request_photo_selection(
			platform::PhotoSelectionRequest{
				.allow_multiple		 = true,
				.accepted_mime_types = {"image/jpeg", "image/png", "image/webp",
										"image/heic", "image/heif"}},
			[this, owner](platform::PlatformValueResult<
						  std::vector<platform::ContentSourceDescriptor>>
							  result) mutable {
		if (result.was_user_cancelled()) {
			last_photo_message = "Photo selection cancelled.";
			refresh_all();
			return;
		}
		if (result.failed()) {
			last_photo_message	   = "Photo selection failed.";
			last_photo_diagnostics = std::move(result.diagnostics);
			refresh_all();
			return;
		}
		PhotoImportSessionResult import_result = import_photos_into_session(
			PhotoImportSessionRequest{
				.current_session = session,
				.identifiers	 = edit_identifiers,
				.clock			 = edit_clock,
				.operation_gate	 = ui_operation_gate,
				.staging_service = content_staging_service,
				.decode_service	 = source_decode_service,
				.photo_codec	 = internal_photo_codec,
				.owner			 = owner,
				.sources		 = std::move(*result.value)},
			last_progress_events, never_cancelled);
		apply_photo_import_result(std::move(import_result));
	});
	if (picker_started.failed()) {
		last_photo_message	   = "Photo picker could not be opened.";
		last_photo_diagnostics = picker_started.diagnostics();
		refresh_all();
	}
}

void AppShellComponent::request_add_pending_item_photos() {
	last_progress_events.clear();
	last_photo_diagnostics.clear();
	last_photo_message = "Select photos to stage before saving the item.";
	core::OperationResult picker_started =
		photo_selection_service.request_photo_selection(
			platform::PhotoSelectionRequest{
				.allow_multiple		 = true,
				.accepted_mime_types = {"image/jpeg", "image/png", "image/webp",
										"image/heic", "image/heif"}},
			[this](platform::PlatformValueResult<
				   std::vector<platform::ContentSourceDescriptor>>
					   result) mutable {
		if (result.was_user_cancelled()) {
			last_photo_message = "Pending photo selection cancelled.";
			refresh_all();
			return;
		}
		if (result.failed()) {
			last_photo_message	   = "Pending photo selection failed.";
			last_photo_diagnostics = std::move(result.diagnostics);
			refresh_all();
			return;
		}
		if (result.value->empty()) {
			last_photo_message = "No photos selected for pending staging.";
			refresh_all();
			return;
		}

		PendingPhotoStagingResult staging_result =
			stage_pending_photos_for_session(
				PendingPhotoStagingRequest{
					.current_session = session,
					.identifiers	 = edit_identifiers,
					.operation_gate	 = ui_operation_gate,
					.staging_service = content_staging_service,
					.sources		 = std::move(*result.value)},
				last_progress_events, never_cancelled);
		apply_pending_photo_staging_result(std::move(staging_result));
	});
	if (picker_started.failed()) {
		last_photo_message	   = "Pending photo picker could not be opened.";
		last_photo_diagnostics = picker_started.diagnostics();
		refresh_all();
	}
}

void AppShellComponent::request_export_photo(core::StableIdentifier photo_id) {
	last_progress_events.clear();
	last_photo_diagnostics.clear();
	const std::string suggested_name =
		catalog::suggested_jpeg_export_file_name(session.repository, photo_id);
	core::OperationResult destination_started =
		document_export_service.request_export_destination_selection(
			platform::DocumentExportRequest{
				.suggested_file_name = suggested_name,
				.mime_type			 = "image/jpeg",
				.purpose			 = "photo JPEG export"},
			[this, photo_id](platform::PlatformValueResult<
							 platform::DocumentDestinationDescriptor>
								 result) mutable {
		if (result.was_user_cancelled()) {
			last_photo_message = "JPEG export destination cancelled.";
			refresh_all();
			return;
		}
		if (result.failed()) {
			last_photo_message	   = "JPEG export destination failed.";
			last_photo_diagnostics = std::move(result.diagnostics);
			refresh_all();
			return;
		}
		catalog::PhotoExportUseCase export_use_case{
			edit_identifiers, ui_operation_gate, internal_photo_codec,
			jpeg_export_service, document_export_service};
		catalog::PhotoExportResult exported =
			export_use_case.export_photo_as_jpeg(
				catalog::PhotoExportRequest{
					.current_state = session.repository,
					.paths		   = *session.paths,
					.photo_id	   = photo_id,
					.destination   = std::move(*result.value),
					.jpeg_quality  = 90},
				last_progress_events, never_cancelled);
		last_photo_diagnostics = std::move(exported.diagnostics);
		if (exported.succeeded())
			last_photo_message = "JPEG export completed: "
								 + std::to_string(exported.bytes_written)
								 + " bytes.";
		else if (exported.was_user_cancelled())
			last_photo_message = "JPEG export cancelled.";
		else
			last_photo_message = "JPEG export failed.";
		refresh_all();
	});
	if (destination_started.failed()) {
		last_photo_message =
			"JPEG export destination picker could not be opened.";
		last_photo_diagnostics = destination_started.diagnostics();
		refresh_all();
	}
}

void AppShellComponent::request_export_backup() {
	last_progress_events.clear();
	last_backup_diagnostics.clear();
	const std::string suggested_name =
		catalog::suggested_backup_file_name(edit_clock.now());
	core::OperationResult destination_started =
		document_export_service.request_export_destination_selection(
			platform::DocumentExportRequest{
				.suggested_file_name = suggested_name,
				.mime_type = std::string{catalog::backup_zip_mime_type},
				.purpose   = "catalog backup ZIP export"},
			[this](platform::PlatformValueResult<
				   platform::DocumentDestinationDescriptor>
					   result) mutable {
		if (result.was_user_cancelled()) {
			last_backup_message = "Backup export destination cancelled.";
			refresh_all();
			return;
		}
		if (result.failed()) {
			last_backup_message		= "Backup export destination failed.";
			last_backup_diagnostics = std::move(result.diagnostics);
			refresh_all();
			return;
		}
		BackupExportSessionResult exported = export_backup_from_session(
			BackupExportSessionRequest{
				.current_session		 = session,
				.identifiers			 = edit_identifiers,
				.clock					 = edit_clock,
				.operation_gate			 = ui_operation_gate,
				.zip_archive_service	 = zip_archive_service,
				.document_export_service = document_export_service,
				.content_staging_service = content_staging_service,
				.destination			 = std::move(*result.value)},
			last_progress_events, never_cancelled);
		apply_backup_export_result(std::move(exported), false);
	});
	if (destination_started.failed()) {
		last_backup_message =
			"Backup export destination picker could not be opened.";
		last_backup_diagnostics = destination_started.diagnostics();
		refresh_all();
	}
}

void AppShellComponent::request_export_diagnostic_archive() {
	last_progress_events.clear();
	last_backup_diagnostics.clear();
	const std::string suggested_name =
		catalog::suggested_diagnostic_archive_file_name(edit_clock.now());
	core::OperationResult destination_started =
		document_export_service.request_export_destination_selection(
			platform::DocumentExportRequest{
				.suggested_file_name = suggested_name,
				.mime_type = std::string{catalog::backup_zip_mime_type},
				.purpose   = "diagnostic archive ZIP export"},
			[this](platform::PlatformValueResult<
				   platform::DocumentDestinationDescriptor>
					   result) mutable {
		if (result.was_user_cancelled()) {
			last_backup_message = "Diagnostic archive destination cancelled.";
			refresh_all();
			return;
		}
		if (result.failed()) {
			last_backup_message		= "Diagnostic archive destination failed.";
			last_backup_diagnostics = std::move(result.diagnostics);
			refresh_all();
			return;
		}
		BackupExportSessionResult exported =
			export_diagnostic_archive_from_session(
				BackupExportSessionRequest{
					.current_session		 = session,
					.identifiers			 = edit_identifiers,
					.clock					 = edit_clock,
					.operation_gate			 = ui_operation_gate,
					.zip_archive_service	 = zip_archive_service,
					.document_export_service = document_export_service,
					.content_staging_service = content_staging_service,
					.destination			 = std::move(*result.value)},
				last_progress_events, never_cancelled);
		apply_backup_export_result(std::move(exported), true);
	});
	if (destination_started.failed()) {
		last_backup_message =
			"Diagnostic archive destination picker could not be opened.";
		last_backup_diagnostics = destination_started.diagnostics();
		refresh_all();
	}
}

void AppShellComponent::request_import_backup() {
	last_progress_events.clear();
	last_backup_diagnostics.clear();
	pending_import_staging.reset();
	pending_import_degraded_acknowledged = false;
	core::OperationResult import_started =
		document_import_service.request_import_document_selection(
			platform::DocumentImportRequest{.accepted_mime_types = {std::string{
												catalog::backup_zip_mime_type}},
											.purpose = "backup ZIP import"},
			[this](
				platform::PlatformValueResult<platform::ContentSourceDescriptor>
					result) mutable {
		if (result.was_user_cancelled()) {
			last_backup_message = "Backup import source selection cancelled.";
			refresh_all();
			return;
		}
		if (result.failed()) {
			last_backup_message		= "Backup import source selection failed.";
			last_backup_diagnostics = std::move(result.diagnostics);
			refresh_all();
			return;
		}
		BackupImportStagingSessionResult staged =
			stage_backup_import_for_session(
				BackupImportStagingSessionRequest{
					.current_session		 = session,
					.identifiers			 = edit_identifiers,
					.clock					 = edit_clock,
					.operation_gate			 = ui_operation_gate,
					.zip_archive_service	 = zip_archive_service,
					.document_export_service = document_export_service,
					.content_staging_service = content_staging_service,
					.source					 = std::move(*result.value)},
				last_progress_events, never_cancelled);
		apply_backup_import_staging_result(std::move(staged));
	});
	if (import_started.failed()) {
		last_backup_message		= "Backup import picker could not be opened.";
		last_backup_diagnostics = import_started.diagnostics();
		refresh_all();
	}
}

void AppShellComponent::apply_backup_export_result(
	BackupExportSessionResult result, bool diagnostic_archive) {
	last_backup_diagnostics = std::move(result.diagnostics);
	if (result.succeeded()) {
		last_backup_message = diagnostic_archive
								  ? "Diagnostic archive export completed."
								  : "Backup ZIP export completed.";
		if (result.degraded_backup_warning_required)
			last_backup_message +=
				" Degraded catalog state was preserved as raw files.";
	} else if (result.was_user_cancelled()) {
		last_backup_message = diagnostic_archive
								  ? "Diagnostic export cancelled."
								  : "Backup export cancelled.";
	} else {
		last_backup_message = diagnostic_archive ? "Diagnostic export failed."
												 : "Backup export failed.";
	}
	refresh_all();
}

void AppShellComponent::apply_backup_import_staging_result(
	BackupImportStagingSessionResult result) {
	last_backup_diagnostics = std::move(result.diagnostics);
	if (result.succeeded()) {
		pending_import_staging	= std::move(result.staging_result);
		last_backup_diagnostics = pending_import_staging->diagnostics;
		pending_import_degraded_acknowledged = false;
		last_backup_message =
			pending_import_staging->validation.explicit_warning_required()
				? "Backup ZIP validated as degraded. Review summary and "
				  "confirm degraded import before replacement."
				: "Backup ZIP validated. Confirm replacement to import.";
	} else if (result.was_user_cancelled()) {
		last_backup_message = "Backup import staging cancelled.";
	} else {
		last_backup_diagnostics = result.staging_result.diagnostics;
		pending_import_staging.reset();
		last_backup_message = "Backup import rejected before replacement.";
	}
	select_root(RootDestination::BackupRecovery);
}

void AppShellComponent::confirm_staged_backup_import() {
	if (!pending_import_staging
		|| !pending_import_staging->staging_catalog_root.has_value()) {
		last_backup_message = "No validated staged backup is ready to import.";
		refresh_all();
		return;
	}
	const bool degraded =
		pending_import_staging->validation.explicit_warning_required();
	if (degraded && !pending_import_degraded_acknowledged) {
		pending_import_degraded_acknowledged = true;
		last_backup_message =
			"Degraded import warning acknowledged. Press confirm again to "
			"replace the current catalog.";
		refresh_all();
		return;
	}
	last_progress_events.clear();
	BackupImportReplacementSessionResult replaced =
		replace_session_with_staged_import(
			BackupImportReplacementSessionRequest{
				.current_session = session,
				.identifiers	 = edit_identifiers,
				.clock			 = edit_clock,
				.operation_gate	 = ui_operation_gate,
				.staged_catalog_root =
					*pending_import_staging->staging_catalog_root,
				.replacement_confirmed	   = true,
				.degraded_import_confirmed = degraded},
			last_progress_events, never_cancelled);
	apply_backup_import_replacement_result(std::move(replaced));
}

void AppShellComponent::apply_backup_import_replacement_result(
	BackupImportReplacementSessionResult result) {
	last_backup_diagnostics = std::move(result.diagnostics);
	if (result.succeeded()) {
		session = std::move(result.session);
		pending_import_staging.reset();
		pending_import_degraded_acknowledged = false;
		last_backup_message = "Backup import completed and catalog reloaded.";
	} else if (result.was_user_cancelled()) {
		last_backup_message = "Backup import replacement cancelled.";
	} else if (result.fatal_recovery_required) {
		session = std::move(result.session);
		pending_import_staging.reset();
		last_backup_message =
			"Catalog replacement failed and rollback failed. Fatal recovery "
			"actions are required.";
	} else {
		last_backup_message =
			"Backup replacement failed; current catalog was not replaced or "
			"was rolled back.";
	}
	select_root(RootDestination::BackupRecovery);
}

void AppShellComponent::apply_pending_photo_staging_result(
	PendingPhotoStagingResult result) {
	last_photo_diagnostics = std::move(result.diagnostics);
	for (PendingPhotoSource& source : result.sources)
		item_form_pending_photos.push_back(std::move(source));

	if (result.succeeded()) {
		last_photo_message = "Pending photo staging completed: "
							 + std::to_string(result.staged_count) + " staged, "
							 + std::to_string(result.failure_count)
							 + " failed.";
	} else if (result.was_user_cancelled()) {
		last_photo_message = "Pending photo staging cancelled.";
	} else {
		last_photo_message = "Pending photo staging failed.";
	}
	refresh_all();
}

void AppShellComponent::apply_photo_import_result(
	PhotoImportSessionResult result) {
	last_photo_diagnostics.clear();
	for (const EntityEditDiagnostic& diagnostic : result.diagnostics) {
		last_photo_diagnostics.push_back(core::Diagnostic{
			.severity		   = diagnostic.severity,
			.code			   = diagnostic.code,
			.message		   = diagnostic.message,
			.technical_details = diagnostic.technical_details});
	}
	if (result.succeeded()) {
		session = std::move(result.session);
		last_photo_message =
			"Photo import completed: "
			+ std::to_string(result.summary.success_count) + " imported, "
			+ std::to_string(result.summary.failure_count) + " failed.";
		if (!result.imported_photo_ids.empty())
			selected_photo_id = result.imported_photo_ids.front();
		last_display_photo_id.reset();
	} else if (result.was_user_cancelled()) {
		last_photo_message = "Photo import cancelled.";
	} else {
		last_photo_message = "Photo import failed.";
	}
	refresh_all();
}

void AppShellComponent::apply_photo_edit_result(
	EntityEditResult result, core::StableIdentifier selected_photo_id_value) {
	last_photo_diagnostics.clear();
	for (const EntityEditDiagnostic& diagnostic : result.diagnostics) {
		last_photo_diagnostics.push_back(core::Diagnostic{
			.severity		   = diagnostic.severity,
			.code			   = diagnostic.code,
			.message		   = diagnostic.message,
			.technical_details = diagnostic.technical_details});
	}
	if (result.failed()) {
		last_photo_message = "Photo metadata update failed.";
		refresh_all();
		return;
	}
	session			  = std::move(result.session);
	selected_photo_id = selected_photo_id_value;
	last_display_photo_id.reset();
	last_photo_message = result.metadata_changed ? "Main photo updated."
												 : "Main photo unchanged.";
	refresh_all();
}

void AppShellComponent::cleanup_item_pending_photos() {
	if (item_form_pending_photos.empty())
		return;

	PendingPhotoCleanupResult cleanup =
		cleanup_pending_photo_sources(item_form_pending_photos);
	last_photo_diagnostics = cleanup.diagnostics;
	std::erase_if(item_form_pending_photos,
				  [](const PendingPhotoSource& source) {
		return !source.staged_path.has_value();
	});
	last_photo_message =
		cleanup.failed() ? "Some pending staged photos could not be cleaned."
						 : "Pending photos cleared.";
}

void AppShellComponent::remove_item_pending_photo(
	std::size_t pending_photo_index) {
	if (pending_photo_index >= item_form_pending_photos.size())
		return;

	std::vector<PendingPhotoSource> cleanup_sources;
	cleanup_sources.push_back(item_form_pending_photos[pending_photo_index]);
	PendingPhotoCleanupResult cleanup =
		cleanup_pending_photo_sources(cleanup_sources);
	last_photo_diagnostics = cleanup.diagnostics;
	if (cleanup.failed()) {
		PendingPhotoSource& source =
			item_form_pending_photos[pending_photo_index];
		source.diagnostics.insert(source.diagnostics.end(),
								  cleanup.diagnostics.begin(),
								  cleanup.diagnostics.end());
		last_photo_message =
			"Pending photo cleanup needs attention; source kept for retry.";
		refresh_all();
		return;
	}

	item_form_pending_photos.erase(
		item_form_pending_photos.begin()
		+ static_cast<std::ptrdiff_t>(pending_photo_index));
	last_photo_message = "Pending photo removed.";
	refresh_all();
}

void AppShellComponent::reset_catalog_filters() {
	catalog_filters				 = catalog::CatalogSearchFilters{};
	catalog_filter_draft		 = catalog_filters;
	catalog_filter_panel_visible = false;
}

void AppShellComponent::reset_item_form() {
	cleanup_item_pending_photos();
	item_form_draft					 = ItemDraft{};
	item_form_mode					 = FormMode::Create;
	item_storage_candidates_expanded = false;
	item_tag_candidates_expanded	 = false;
	item_listing_expanded			 = false;
	item_finance_expanded			 = false;
	for (juce::TextEditor* editor :
		 {&item_name_editor, &item_category_editor, &item_notes_editor,
		  &item_listing_marketplace_editor, &item_listing_url_editor,
		  &item_listing_note_editor, &item_acquisition_source_editor}) {
		editor->setText(juce::String{}, juce::dontSendNotification);
	}
}

void AppShellComponent::reset_storage_form() {
	storage_form_draft					 = StorageDraft{};
	storage_form_mode					 = FormMode::Create;
	storage_parent_candidates_expanded	 = false;
	storage_tag_candidates_expanded		 = false;
	storage_archive_warning_acknowledged = false;
	for (juce::TextEditor* editor :
		 {&storage_name_editor, &storage_type_editor, &storage_location_editor,
		  &storage_notes_editor}) {
		editor->setText(juce::String{}, juce::dontSendNotification);
	}
}

void AppShellComponent::load_item_form_from_record(
	const persistence::ItemEnvelope& item) {
	item_form_draft = ItemDraft{.existing_id  = item.record.id,
								.display_name = item.record.display_name,
								.category	  = item.record.category,
								.storage_id	  = item.record.storage_id,
								.tags		  = item.record.tags,
								.notes		  = item.record.notes,
								.status		  = item.record.status,
								.listing	  = item.record.listing,
								.acquisition  = item.record.acquisition,
								.finance	  = item.record.finance,
								.warning_acknowledged = true};
	item_name_editor.setText(juce_text(item.record.display_name),
							 juce::dontSendNotification);
	item_category_editor.setText(juce_text(item.record.category),
								 juce::dontSendNotification);
	item_notes_editor.setText(juce_text(item.record.notes),
							  juce::dontSendNotification);
	item_listing_marketplace_editor.setText(
		juce_text(item.record.listing.marketplace), juce::dontSendNotification);
	item_listing_url_editor.setText(juce_text(item.record.listing.url),
									juce::dontSendNotification);
	item_listing_note_editor.setText(juce_text(item.record.listing.note),
									 juce::dontSendNotification);
	item_acquisition_source_editor.setText(
		juce_text(item.record.acquisition.source), juce::dontSendNotification);
	item_listing_expanded = !item.record.listing.empty();
	item_finance_expanded =
		!item.record.acquisition.empty() || !item.record.finance.empty();
	item_storage_candidates_expanded = false;
	item_tag_candidates_expanded	 = false;
}

void AppShellComponent::load_storage_form_from_record(
	const persistence::StorageEnvelope& storage) {
	storage_form_draft =
		StorageDraft{.existing_id		= storage.record.id,
					 .display_name		= storage.record.display_name,
					 .storage_type		= storage.record.storage_type,
					 .parent_storage_id = storage.record.parent_storage_id,
					 .location			= storage.record.location,
					 .tags				= storage.record.tags,
					 .notes				= storage.record.notes,
					 .lifecycle_status	= storage.record.lifecycle_status,
					 .archive_warning_acknowledged = true};
	storage_name_editor.setText(juce_text(storage.record.display_name),
								juce::dontSendNotification);
	storage_type_editor.setText(juce_text(storage.record.storage_type),
								juce::dontSendNotification);
	storage_location_editor.setText(juce_text(storage.record.location),
									juce::dontSendNotification);
	storage_notes_editor.setText(juce_text(storage.record.notes),
								 juce::dontSendNotification);
	storage_parent_candidates_expanded	 = false;
	storage_tag_candidates_expanded		 = false;
	storage_archive_warning_acknowledged = false;
}

void AppShellComponent::save_item_form() {
	item_form_draft.display_name = item_name_editor.getText().toStdString();
	item_form_draft.category	 = item_category_editor.getText().toStdString();
	item_form_draft.notes		 = item_notes_editor.getText().toStdString();
	item_form_draft.listing.marketplace =
		item_listing_marketplace_editor.getText().toStdString();
	item_form_draft.listing.url =
		item_listing_url_editor.getText().toStdString();
	item_form_draft.listing.note =
		item_listing_note_editor.getText().toStdString();
	item_form_draft.acquisition.source =
		item_acquisition_source_editor.getText().toStdString();
	item_form_draft.pending_photo_import_planned =
		has_ready_pending_photo(item_form_pending_photos);

	ItemSaveWithPendingPhotosResult result =
		save_item_draft_and_import_pending_photos(
			ItemSaveWithPendingPhotosRequest{
				.current_session = session,
				.identifiers	 = edit_identifiers,
				.clock			 = edit_clock,
				.operation_gate	 = ui_operation_gate,
				.staging_service = content_staging_service,
				.decode_service	 = source_decode_service,
				.photo_codec	 = internal_photo_codec,
				.draft			 = item_form_draft,
				.pending_sources = item_form_pending_photos},
			last_progress_events, never_cancelled);
	if (result.warning_acknowledgement_required())
		item_form_draft.warning_acknowledged = true;
	if (result.warning_acknowledgement_required()
		&& result.save_result.saved_record_id) {
		item_form_draft.reserved_new_id = result.save_result.saved_record_id;
	}
	if (result.item_saved())
		item_form_draft.existing_id = result.save_result.saved_record_id;
	apply_item_save_with_pending_photos_result(std::move(result));
}

void AppShellComponent::save_storage_form() {
	storage_form_draft.display_name =
		storage_name_editor.getText().toStdString();
	storage_form_draft.storage_type =
		storage_type_editor.getText().toStdString();
	storage_form_draft.location =
		storage_location_editor.getText().toStdString();
	storage_form_draft.notes = storage_notes_editor.getText().toStdString();
	storage_form_draft.archive_warning_acknowledged =
		storage_archive_warning_acknowledged;
	EntityEditResult result =
		save_storage_draft(EntityEditRequest{.current_session = session,
											 .identifiers = edit_identifiers,
											 .clock		  = edit_clock},
						   storage_form_draft);
	if (result.warning_acknowledgement_required)
		storage_archive_warning_acknowledged = true;
	if (result.warning_acknowledgement_required && result.saved_record_id)
		storage_form_draft.reserved_new_id = result.saved_record_id;
	if (result.succeeded() && result.saved_record_id)
		storage_form_draft.existing_id = result.saved_record_id;
	apply_entity_edit_result(std::move(result));
}

void AppShellComponent::apply_item_save_with_pending_photos_result(
	ItemSaveWithPendingPhotosResult result) {
	last_edit_diagnostics	 = result.save_result.diagnostics;
	item_form_pending_photos = std::move(result.pending_sources);
	std::erase_if(item_form_pending_photos,
				  [](const PendingPhotoSource& source) {
		return !source.staged_path.has_value();
	});
	if (result.warning_acknowledgement_required()) {
		last_edit_message = "Confirm warning and save again.";
		refresh_all();
		return;
	}
	if (result.save_result.failed()) {
		last_edit_message = "Save failed.";
		refresh_all();
		return;
	}

	last_photo_diagnostics.clear();
	if (result.import_attempted) {
		for (const EntityEditDiagnostic& diagnostic :
			 result.import_result.diagnostics) {
			last_photo_diagnostics.push_back(core::Diagnostic{
				.severity		   = diagnostic.severity,
				.code			   = diagnostic.code,
				.message		   = diagnostic.message,
				.technical_details = diagnostic.technical_details});
		}
		last_photo_diagnostics.insert(last_photo_diagnostics.end(),
									  result.cleanup_result.diagnostics.begin(),
									  result.cleanup_result.diagnostics.end());
		if (result.import_result.succeeded()) {
			last_photo_message =
				"Pending photo import completed: "
				+ std::to_string(result.import_result.summary.success_count)
				+ " imported, "
				+ std::to_string(result.import_result.summary.failure_count)
				+ " failed.";
		} else if (result.import_result.was_user_cancelled()) {
			last_photo_message = "Item saved, pending photo import cancelled.";
		} else {
			last_photo_message = "Item saved, but pending photo import failed.";
		}
		if (result.cleanup_attempted && result.cleanup_result.failed())
			last_photo_message += " Pending source cleanup needs attention.";
		if (!result.import_result.imported_photo_ids.empty())
			selected_photo_id = result.import_result.imported_photo_ids.front();
	} else if (!item_form_pending_photos.empty()) {
		last_photo_message =
			"Item saved, but no staged pending photos were ready.";
	}

	session = std::move(result.session);
	last_edit_message =
		result.save_result.metadata_changed ? "Saved." : "No changes.";
	last_edit_diagnostics.clear();
	if (item_form_draft.existing_id) {
		selected_item_id = *item_form_draft.existing_id;
		destination		 = RootDestination::ItemDetail;
	} else {
		destination =
			form_return_destination.value_or(RootDestination::Catalog);
	}
	refresh_all();
}

void AppShellComponent::apply_entity_edit_result(EntityEditResult result) {
	last_edit_diagnostics = std::move(result.diagnostics);
	if (result.warning_acknowledgement_required) {
		last_edit_message = "Confirm warning and save again.";
		refresh_all();
		return;
	}
	if (result.failed()) {
		last_edit_message = "Save failed.";
		refresh_all();
		return;
	}
	const RootDestination completed_destination = destination;
	const std::optional<core::StableIdentifier> saved_item_id =
		item_form_draft.existing_id;
	const std::optional<core::StableIdentifier> saved_storage_id =
		storage_form_draft.existing_id;
	session			  = std::move(result.session);
	last_edit_message = result.metadata_changed ? "Saved." : "No changes.";
	last_edit_diagnostics.clear();
	if (completed_destination == RootDestination::ItemForm && saved_item_id) {
		selected_item_id = *saved_item_id;
		destination		 = RootDestination::ItemDetail;
	} else if (completed_destination == RootDestination::StorageForm
			   && saved_storage_id) {
		selected_storage_id = *saved_storage_id;
		destination			= RootDestination::StorageDetail;
	} else {
		destination =
			form_return_destination.value_or(RootDestination::Catalog);
	}
	refresh_all();
}

void AppShellComponent::schedule_content_refresh() {
	startTimer(85);
}

void AppShellComponent::timerCallback() {
	stopTimer();
	refresh_content();
	resized();
	repaint();
}

void AppShellComponent::refresh_all() {
	stopTimer();
	refresh_controls();
	refresh_content();
	resized();
	repaint();
}

void AppShellComponent::refresh_controls() {
	juce::String title;
	switch (destination) {
		case RootDestination::Catalog:
			title = "Catalog";
			break;
		case RootDestination::Storages:
			title = "Storages";
			break;
		case RootDestination::Add:
			title = "Add";
			break;
		case RootDestination::More:
			title = "More";
			break;
		case RootDestination::ItemDetail:
			title = "Item detail";
			break;
		case RootDestination::StorageDetail:
			title = "Storage detail";
			break;
		case RootDestination::ItemForm:
			title =
				item_form_mode == FormMode::Create ? "Add item" : "Edit item";
			break;
		case RootDestination::StorageForm:
			title = storage_form_mode == FormMode::Create ? "Add storage"
														  : "Edit storage";
			break;
		case RootDestination::PhotoViewer:
			title = "Photo viewer";
			break;
		case RootDestination::BackupRecovery:
			title = session.fatal() ? "Fatal recovery" : "Backup and recovery";
			break;
	}
	if (session.fatal()) {
		destination = RootDestination::BackupRecovery;
		title		= "Fatal recovery";
	}
	title_label.setText(title, juce::dontSendNotification);
	form_save_button.setButtonText(
		destination == RootDestination::ItemForm	  ? "Save item"
		: destination == RootDestination::StorageForm ? "Save storage"
													  : "Save");

	if (session.fatal()) {
		catalog_nav_button.setEnabled(false);
		storages_nav_button.setEnabled(false);
		add_nav_button.setEnabled(false);
		more_nav_button.setEnabled(true);
	} else {
		catalog_nav_button.setEnabled(true);
		storages_nav_button.setEnabled(true);
		add_nav_button.setEnabled(true);
		more_nav_button.setEnabled(true);
	}

	std::string status =
		"Load: " + std::string{persistence::to_string(session.load_status)};
	status += " · " + std::string{to_string(session.source)};
	status += " · items=" + std::to_string(session.repository.items.size());
	status +=
		" · storages=" + std::to_string(session.repository.storages.size());
	if (session.demo_catalog_active)
		status += " · demo catalog";
	status_label.setText(juce_text(status), juce::dontSendNotification);

	const juce::Colour selected_colour = accent_colour().withAlpha(0.65f);
	const juce::Colour normal_colour   = panel_colour();
	catalog_nav_button.setColour(juce::TextButton::buttonColourId,
								 destination == RootDestination::Catalog
									 ? selected_colour
									 : normal_colour);
	storages_nav_button.setColour(
		juce::TextButton::buttonColourId,
		destination == RootDestination::Storages
				|| destination == RootDestination::StorageDetail
			? selected_colour
			: normal_colour);
	add_nav_button.setColour(
		juce::TextButton::buttonColourId,
		destination == RootDestination::Add
				|| destination == RootDestination::ItemForm
				|| destination == RootDestination::StorageForm
			? selected_colour
			: normal_colour);
	more_nav_button.setColour(
		juce::TextButton::buttonColourId,
		destination == RootDestination::More ? selected_colour : normal_colour);
}

void AppShellComponent::refresh_content() {
	content->begin_rebuild();
	content->clear_rows();
	if (!session.ready_for_browsing() && session.fatal()) {
		build_backup_recovery_content();
	} else if (!session.ready_for_browsing()) {
		content->add_label(
			"Catalog could not be loaded and app-private paths are "
			"unavailable. "
			"Review technical diagnostics before retrying.",
			86, warning_panel_colour(), true);
	} else {
		if (!last_edit_message.empty()) {
			content->add_label(juce_text(last_edit_message), 42,
							   accent_colour().withAlpha(0.34f), true);
		}
		if (!last_edit_diagnostics.empty()) {
			content->add_label(
				juce_text(diagnostic_summary(last_edit_diagnostics)), 76,
				warning_panel_colour(), true);
		}
		if (destination != RootDestination::PhotoViewer
			&& !last_photo_message.empty()) {
			content->add_label(juce_text(last_photo_message), 54,
							   accent_colour().withAlpha(0.34f), true);
		}
		if (destination != RootDestination::PhotoViewer
			&& !last_photo_diagnostics.empty()) {
			content->add_label(
				juce_text(core_diagnostic_summary(last_photo_diagnostics)), 76,
				warning_panel_colour(), true);
		}
		if (destination != RootDestination::BackupRecovery
			&& !last_backup_message.empty()) {
			content->add_label(juce_text(last_backup_message), 62,
							   accent_colour().withAlpha(0.34f), true);
		}
		if (destination != RootDestination::BackupRecovery
			&& !last_backup_diagnostics.empty()) {
			content->add_label(
				juce_text(core_diagnostic_summary(last_backup_diagnostics)), 76,
				warning_panel_colour(), true);
		}
		switch (destination) {
			case RootDestination::Catalog:
				build_catalog_content();
				break;
			case RootDestination::Storages:
				build_storages_content();
				break;
			case RootDestination::ItemDetail:
				build_item_detail_content();
				break;
			case RootDestination::StorageDetail:
				build_storage_detail_content();
				break;
			case RootDestination::ItemForm:
				build_item_form_content();
				break;
			case RootDestination::StorageForm:
				build_storage_form_content();
				break;
			case RootDestination::Add:
				build_add_content();
				break;
			case RootDestination::More:
				build_more_content();
				break;
			case RootDestination::PhotoViewer:
				build_photo_viewer_content();
				break;
			case RootDestination::BackupRecovery:
				build_backup_recovery_content();
				break;
		}
	}
	content->end_rebuild();
}

void AppShellComponent::build_catalog_content() {
	if (session.degraded()) {
		content->add_label(
			"Degraded load: some records or media need attention. "
			"Accepted records remain browsable.",
			70, warning_panel_colour(), true);
	}
	if (session.demo_catalog_active) {
		content->add_label(
			"Demo catalog is active. Existing canonical catalogs "
			"are never overwritten by debug seeding.",
			62, accent_colour().withAlpha(0.34f), true);
	}

	content->add_label(
		juce_text(active_filter_summary(catalog_filters, session.repository)),
		48, surface_colour(), false);
	if (catalog_filter_panel_visible)
		build_filter_panel();

	const std::string query = catalog_search_editor.getText().toStdString();
	catalog::CatalogSearchOptions options{
		.include_storage_results_for_empty_query = true};
	catalog::CatalogSearchResultSet results = catalog::search_catalog(
		session.search_index, query, catalog_filters, options);

	content->add_label(
		juce_text("Results: " + std::to_string(results.total_count)), 38,
		surface_colour(), true);

	if (session.repository.items.empty()
		&& session.repository.storages.empty()) {
		content->add_label(
			"Empty catalog. Add item and Add storage are available from Add; "
			"backup/import/recovery are available from More.",
			84, panel_colour(), true);
		return;
	}
	if (results.total_count == 0U) {
		content->add_label(
			"No results. Try clearing the query or filters, "
			"or include archived records.",
			72, panel_colour(), true);
		return;
	}

	content->add_label("Items", 36, panel_colour(), true);
	for (const catalog::SearchResult& result : results.item_results) {
		juce::Button& button =
			content->add_button(item_result_text(result, session), 78);
		core::StableIdentifier item_id = result.record_id;
		button.onClick = [this, item_id] { open_item_detail(item_id); };
	}

	if (!results.storage_results.empty()) {
		content->add_label("Storages", 36, panel_colour(), true);
		for (const catalog::SearchResult& result : results.storage_results) {
			juce::Button& button =
				content->add_button(storage_result_text(result, session), 78);
			core::StableIdentifier storage_id = result.record_id;
			button.onClick					  = [this, storage_id] {
				open_storage_detail(storage_id);
			};
		}
	}
}

void AppShellComponent::build_filter_panel() {
	content->add_label("Filter workflow", 38, accent_colour().withAlpha(0.35f),
					   true);

	content->add_label("Category multi-select", 32, panel_colour(), true);
	const std::vector<std::string> categories =
		distinct_categories(session.search_index);
	if (categories.empty())
		content->add_label("No category values yet.", 32);
	for (const std::string& category : categories) {
		juce::ToggleButton& toggle = content->add_toggle(
			juce_text("Category: " + category),
			contains_string(catalog_filter_draft.categories, category), 34);
		toggle.onClick = [this, category] {
			toggle_string(catalog_filter_draft.categories, category);
			refresh_content();
		};
	}

	content->add_label("Status multi-select", 32, panel_colour(), true);
	for (domain::ItemStatus status :
		 {domain::ItemStatus::Draft, domain::ItemStatus::Planned,
		  domain::ItemStatus::Listed, domain::ItemStatus::Sold,
		  domain::ItemStatus::Archived}) {
		juce::ToggleButton& toggle = content->add_toggle(
			juce_text("Status: " + status_text(status)),
			contains_status(catalog_filter_draft.statuses, status), 34);
		toggle.onClick = [this, status] {
			toggle_status(catalog_filter_draft.statuses, status);
			refresh_content();
		};
	}

	content->add_label("Storage selector", 32, panel_colour(), true);
	juce::Button& any_storage = content->add_button(
		catalog_filter_draft.storage_id
				|| catalog_filter_draft.storage_unassigned_only
			? "Any storage"
			: "* Any storage",
		34);
	any_storage.onClick = [this] {
		catalog_filter_draft.storage_id.reset();
		catalog_filter_draft.storage_unassigned_only = false;
		refresh_content();
	};
	juce::Button& unassigned = content->add_button(
		catalog_filter_draft.storage_unassigned_only ? "* Unassigned"
													 : "Unassigned",
		34);
	unassigned.onClick = [this] {
		catalog_filter_draft.storage_id.reset();
		catalog_filter_draft.storage_unassigned_only = true;
		refresh_content();
	};
	for (const catalog::StorageSearchDocument& storage :
		 session.search_index.storages) {
		std::string title = storage.projection.display_name;
		if (catalog_filter_draft.storage_id
			&& *catalog_filter_draft.storage_id == storage.projection.id) {
			title = "* " + title;
		}
		juce::Button& button = content->add_button(juce_text(title), 34);
		core::StableIdentifier storage_id = storage.projection.id;
		button.onClick					  = [this, storage_id] {
			catalog_filter_draft.storage_id				 = storage_id;
			catalog_filter_draft.storage_unassigned_only = false;
			catalog_filter_draft.include_nested_storage	 = true;
			refresh_content();
		};
	}

	juce::ToggleButton& nested =
		content->add_toggle("Include nested contents",
							catalog_filter_draft.include_nested_storage, 34);
	nested.setEnabled(catalog_filter_draft.storage_id.has_value());
	nested.onClick = [this] {
		catalog_filter_draft.include_nested_storage =
			!catalog_filter_draft.include_nested_storage;
		refresh_content();
	};

	content->add_label("Photo presence", 32, panel_colour(), true);
	for (catalog::SearchPhotoPresenceFilter filter :
		 {catalog::SearchPhotoPresenceFilter::Any,
		  catalog::SearchPhotoPresenceFilter::HasPhotos,
		  catalog::SearchPhotoPresenceFilter::NoPhotos,
		  catalog::SearchPhotoPresenceFilter::BrokenPhotos}) {
		std::string label = photo_filter_label(filter);
		if (catalog_filter_draft.photo_presence == filter)
			label = "* " + label;
		juce::Button& button = content->add_button(juce_text(label), 34);
		button.onClick		 = [this, filter] {
			catalog_filter_draft.photo_presence = filter;
			refresh_content();
		};
	}

	juce::ToggleButton& listed = content->add_toggle(
		"Listed shortcut", catalog_filter_draft.listed_only, 34);
	listed.onClick = [this] {
		catalog_filter_draft.listed_only = !catalog_filter_draft.listed_only;
		refresh_content();
	};
	juce::ToggleButton& sold = content->add_toggle(
		"Sold shortcut", catalog_filter_draft.sold_only, 34);
	sold.onClick = [this] {
		catalog_filter_draft.sold_only = !catalog_filter_draft.sold_only;
		refresh_content();
	};
	juce::ToggleButton& archived = content->add_toggle(
		"Include archived", catalog_filter_draft.include_archived, 34);
	archived.onClick = [this] {
		catalog_filter_draft.include_archived =
			!catalog_filter_draft.include_archived;
		refresh_content();
	};

	juce::Button& apply = content->add_button("Apply filters", 38);
	apply.onClick		= [this] {
		catalog_filters				 = catalog_filter_draft;
		catalog_filter_panel_visible = false;
		refresh_all();
	};
	juce::Button& clear = content->add_button("Clear filters", 38);
	clear.onClick		= [this] {
		reset_catalog_filters();
		refresh_all();
	};
}

void AppShellComponent::build_storages_content() {
	if (session.demo_catalog_active)
		content->add_label("Demo catalog is active.", 42,
						   accent_colour().withAlpha(0.34f), true);

	const std::string query = storage_search_editor.getText().toStdString();
	catalog::StorageSearchFilters filters;
	catalog::CatalogSearchResultSet results =
		catalog::search_storages(session.search_index, query, filters);
	content->add_label(
		juce_text("Storages: " + std::to_string(results.total_count)), 38,
		panel_colour(), true);
	if (results.storage_results.empty()) {
		content->add_label("No storage records yet.", 52, panel_colour(), true);
		juce::Button& add_storage = content->add_button("Add storage", 42);
		add_storage.onClick = [this] { open_new_storage_form(std::nullopt); };
		return;
	}
	for (const catalog::SearchResult& result : results.storage_results) {
		juce::Button& button =
			content->add_button(storage_result_text(result, session), 78);
		core::StableIdentifier storage_id = result.record_id;
		button.onClick					  = [this, storage_id] {
			open_storage_detail(storage_id);
		};
	}
}

void AppShellComponent::build_item_detail_content() {
	if (!selected_item_id) {
		content->add_label("No item selected.", 54, panel_colour(), true);
		return;
	}
	const persistence::ItemEnvelope* item =
		catalog::find_item_envelope(session.repository, *selected_item_id);
	const std::map<std::string, catalog::ItemProjection>::const_iterator
		projection =
			session.repository.item_projections.find(selected_item_id->value());
	if (item == nullptr
		|| projection == session.repository.item_projections.end()) {
		content->add_label(
			"Selected item is missing from the accepted catalog.", 64,
			warning_panel_colour(), true);
		return;
	}

	std::string header = photo_presence_label(projection->second.photo_presence)
						 + " " + item->record.display_name + " · "
						 + item->record.category + " · "
						 + status_text(item->record.status);
	if (!projection->second.storage_path_label.empty())
		header += " · " + projection->second.storage_path_label;
	if (projection->second.storage_archived)
		header += " · ⚠ archived storage";
	if (projection->second.broken_storage_reference)
		header += " · ⚠ broken storage";
	content->add_label(juce_text(header), 92, surface_colour(), true);

	juce::Button& edit = content->add_button("Edit item", 42);
	edit.onClick	   = [this, item_id = item->record.id] {
		open_existing_item_form(item_id);
	};
	juce::Button& change_storage = content->add_button(
		juce_text("Change storage: "
				  + storage_label(session.repository, item->record.storage_id)),
		42);
	change_storage.onClick = [this, item_id = item->record.id] {
		open_existing_item_form(item_id);
	};
	domain::PhotoOwner owner{.type = domain::PhotoOwnerType::Item,
							 .id   = item->record.id};
	juce::Button& add_photo = content->add_button("Add photos", 42);
	add_photo.onClick		= [this, owner] { request_add_photos(owner); };
	juce::Button& viewer = content->add_button("Open photo viewer/export", 42);
	viewer.setEnabled(projection->second.representative_photo_id.has_value());
	viewer.onClick = [this, owner,
					  photo_id = projection->second.representative_photo_id] {
		open_photo_viewer(owner, photo_id);
	};
	juce::Button& export_current =
		content->add_button("Export representative photo as JPEG", 42);
	export_current.setEnabled(
		projection->second.representative_usable_photo_id.has_value());
	export_current.onClick =
		[this, photo_id = projection->second.representative_usable_photo_id] {
		if (photo_id)
			request_export_photo(*photo_id);
	};
	if (item->record.storage_id
		&& !projection->second.broken_storage_reference) {
		juce::Button& storage_button = content->add_button(
			juce_text(
				"Open storage: "
				+ storage_label(session.repository, item->record.storage_id)),
			42);
		core::StableIdentifier storage_id = *item->record.storage_id;
		storage_button.onClick			  = [this, storage_id] {
			open_storage_detail(storage_id);
		};
	}

	content->add_label(
		juce_text(field_value_summary("Notes", item->record.notes)), 58,
		panel_colour());
	content->add_label(juce_text(tags_summary(item->record.tags)), 58,
					   panel_colour());
	content->add_label(juce_text(listing_summary(item->record.listing)), 58,
					   panel_colour(), !item->record.listing.empty());
	content->add_label(
		juce_text(
			finance_summary(item->record.acquisition, item->record.finance)),
		58, panel_colour(),
		!item->record.acquisition.empty() || !item->record.finance.empty());
	if (projection->second.photo_presence
		!= catalog::PhotoPresenceState::HasUsablePhotos) {
		content->add_label(
			"Recovery/photo warning: photo state needs attention or "
			"photo import is still pending.",
			64, warning_panel_colour(), true);
	}
}

void AppShellComponent::build_storage_detail_content() {
	if (!selected_storage_id) {
		content->add_label("No storage selected.", 54, panel_colour(), true);
		return;
	}

	const persistence::StorageEnvelope* storage =
		catalog::find_storage_envelope(session.repository,
									   *selected_storage_id);
	const std::map<std::string, catalog::StorageProjection>::const_iterator
		projection = session.repository.storage_projections.find(
			selected_storage_id->value());
	if (storage == nullptr
		|| projection == session.repository.storage_projections.end()) {
		content->add_label(
			"Selected storage is missing from the accepted catalog.", 64,
			warning_panel_colour(), true);
		return;
	}

	std::string header =
		storage->record.display_name + " · " + storage->record.storage_type;
	if (!projection->second.path_label.empty())
		header += " · " + projection->second.path_label;
	if (!storage->record.location.empty())
		header += " · " + storage->record.location;
	if (!storage->record.notes.empty())
		header += " · " + storage->record.notes;
	if (projection->second.parent_reference_state
		== domain::ReferenceState::Broken) {
		header += " · ⚠ broken parent";
	}
	content->add_label(juce_text(header), 84, surface_colour(), true);

	juce::ToggleButton& include_nested = content->add_toggle(
		"Include nested contents", storage_detail_include_nested, 34);
	include_nested.onClick = [this] {
		storage_detail_include_nested = !storage_detail_include_nested;
		refresh_content();
	};
	content->add_label(storage_detail_include_nested
						   ? "Showing nested contents by default."
						   : "Direct contents only mode.",
					   34, panel_colour());

	content->add_label("Child storages", 36, panel_colour(), true);
	std::vector<core::StableIdentifier> child_ids =
		projection->second.direct_child_storage_ids;
	if (storage_detail_include_nested) {
		child_ids.insert(
			child_ids.end(),
			projection->second.nested_descendant_storage_ids.begin(),
			projection->second.nested_descendant_storage_ids.end());
	}
	if (child_ids.empty())
		content->add_label("No child storages.", 34);
	for (const core::StableIdentifier& child_id : child_ids) {
		const persistence::StorageEnvelope* child =
			catalog::find_storage_envelope(session.repository, child_id);
		if (child == nullptr)
			continue;
		juce::Button& button = content->add_button(
			juce_text("[storage] " + child->record.display_name + " · "
					  + child->record.storage_type),
			54);
		button.onClick = [this, child_id] { open_storage_detail(child_id); };
	}

	content->add_label("Items inside", 36, panel_colour(), true);
	const std::set<std::string> accepted_storage_ids =
		storage_filter_id_set(session.repository, *selected_storage_id,
							  storage_detail_include_nested);
	std::size_t item_count = 0;
	for (const persistence::ItemEnvelope& item : session.repository.items) {
		if (!item.record.storage_id
			|| !accepted_storage_ids.contains(
				item.record.storage_id->value())) {
			continue;
		}
		const std::map<std::string, catalog::ItemProjection>::const_iterator
			found = session.repository.item_projections.find(
				item.record.id.value());
		if (found == session.repository.item_projections.end())
			continue;
		++item_count;
		std::string row = photo_presence_label(found->second.photo_presence)
						  + " " + item.record.display_name + " · "
						  + item.record.category + " · "
						  + status_text(item.record.status);
		if (!item.record.notes.empty())
			row += " · " + item.record.notes;
		juce::Button& button = content->add_button(juce_text(row), 62);
		core::StableIdentifier item_id = item.record.id;
		button.onClick = [this, item_id] { open_item_detail(item_id); };
	}
	if (item_count == 0U)
		content->add_label("No items in this mode.", 34);

	content->add_label("Actions", 36, panel_colour(), true);
	juce::Button& add_item = content->add_button("Add item here", 42);
	add_item.onClick	   = [this, storage_id = *selected_storage_id] {
		open_new_item_form(storage_id);
	};
	juce::Button& add_storage = content->add_button("Add nested storage", 42);
	add_storage.onClick		  = [this, storage_id = *selected_storage_id] {
		open_new_storage_form(storage_id);
	};
	domain::PhotoOwner owner{.type = domain::PhotoOwnerType::Storage,
							 .id   = *selected_storage_id};
	juce::Button& add_photo = content->add_button("Add photos", 42);
	add_photo.onClick		= [this, owner] { request_add_photos(owner); };
	juce::Button& viewer = content->add_button("Open storage photo viewer", 42);
	viewer.setEnabled(projection->second.representative_photo_id.has_value());
	viewer.onClick = [this, owner,
					  photo_id = projection->second.representative_photo_id] {
		open_photo_viewer(owner, photo_id);
	};
	juce::Button& edit = content->add_button("Edit storage", 42);
	edit.onClick	   = [this, storage_id = *selected_storage_id] {
		open_existing_storage_form(storage_id);
	};
}

namespace {
template<typename Content>
void add_status_rows(Content& content, domain::ItemStatus selected_status,
					 std::function<void(domain::ItemStatus)> choose_status) {
	content.add_inline_buttons(
		"Status",
		{InlineButtonRowComponent::Action{
			 .label = selected_status == domain::ItemStatus::Draft ? "* Draft"
																   : "Draft",
			 .handler =
				 [choose_status] { choose_status(domain::ItemStatus::Draft); }},
		 InlineButtonRowComponent::Action{
			 .label = selected_status == domain::ItemStatus::Planned ? "* Plan"
																	 : "Plan",
			 .handler =
				 [choose_status] {
		choose_status(domain::ItemStatus::Planned);
	}},
		 InlineButtonRowComponent::Action{
			 .label = selected_status == domain::ItemStatus::Listed ? "* List"
																	: "List",
			 .handler =
				 [choose_status] {
		choose_status(domain::ItemStatus::Listed);
	}},
		 InlineButtonRowComponent::Action{
			 .label = selected_status == domain::ItemStatus::Sold ? "* Sold"
																  : "Sold",
			 .handler =
				 [choose_status] { choose_status(domain::ItemStatus::Sold); }},
		 InlineButtonRowComponent::Action{
			 .label = selected_status == domain::ItemStatus::Archived ? "* Arch"
																	  : "Arch",
			 .handler =
				 [choose_status] {
		choose_status(domain::ItemStatus::Archived);
	}}},
		42);
}
template<typename Content>
void add_tag_rows(Content& content, std::vector<domain::TagRow>& tags,
				  std::function<void()> refresh) {
	content.add_label(juce_text(tag_row_count_summary(tags)), 38,
					  panel_colour(), true);
	if (tags.empty())
		content.add_label(
			"No tags yet. Add a row for brand, size, condition, or any custom "
			"fact.",
			50, panel_colour());

	for (std::size_t index = 0; index < tags.size(); ++index) {
		content.add_tag_editor_row(
			index, tags[index],
			[&tags](std::size_t changed_index, domain::TagRow changed_tag) {
			if (changed_index < tags.size())
				tags[changed_index] = std::move(changed_tag);
		}, [&tags, refresh](std::size_t removed_index) {
			if (removed_index >= tags.size())
				return;
			tags.erase(tags.begin()
					   + static_cast<std::ptrdiff_t>(removed_index));
			refresh();
		}, 52);
	}

	content.add_inline_buttons(
		"Tags",
		{InlineButtonRowComponent::Action{.label = "Add row",
										  .handler =
											  [&tags, refresh] {
		tags.push_back(domain::TagRow{});
		refresh();
	}},
		 InlineButtonRowComponent::Action{.label = "Clear all",
										  .handler =
											  [&tags, refresh] {
		tags.clear();
		refresh();
	},
										  .enabled = !tags.empty()}},
		40);
}
}	 // namespace

void AppShellComponent::build_item_form_content() {
	content->add_pending_photo_strip(item_form_pending_photos, [this] {
		request_add_pending_item_photos();
	}, [this] {
		cleanup_item_pending_photos();
		refresh_all();
	}, [this](std::size_t index) { remove_item_pending_photo(index); }, 104);

	content->add_editor_pair(item_name_editor, "Display name (required)",
							 item_category_editor, "Category (required)", 54);
	item_name_editor.onTextChange = [this] {
		item_form_draft.display_name = item_name_editor.getText().toStdString();
	};
	item_category_editor.onTextChange = [this] {
		item_form_draft.category = item_category_editor.getText().toStdString();
	};

	content->add_inline_buttons(
		juce_text(
			"Storage: "
			+ storage_label(session.repository, item_form_draft.storage_id)),
		{InlineButtonRowComponent::Action{
			 .label = item_storage_candidates_expanded ? "Hide choices"
					  : item_form_draft.storage_id	   ? "Change"
													   : "Choose",
			 .handler =
				 [this] {
		item_storage_candidates_expanded = !item_storage_candidates_expanded;
		refresh_all();
	}},
		 InlineButtonRowComponent::Action{
			 .label = "Clear",
			 .handler =
				 [this] {
		item_form_draft.storage_id.reset();
		item_storage_candidates_expanded = false;
		refresh_all();
	},
			 .enabled = item_form_draft.storage_id.has_value()}},
		42);

	if (item_storage_candidates_expanded) {
		std::vector<ButtonGridComponent::Action> storage_actions;
		storage_actions.push_back(ButtonGridComponent::Action{
			.label = "Unassigned storage", .handler = [this] {
			item_form_draft.storage_id.reset();
			item_storage_candidates_expanded = false;
			refresh_all();
		}});
		for (const persistence::StorageEnvelope& storage :
			 session.repository.storages) {
			const core::StableIdentifier storage_id = storage.record.id;
			storage_actions.push_back(ButtonGridComponent::Action{
				.label = juce_text(
					storage_choice_label(session.repository, storage)),
				.handler = [this, storage_id] {
				item_form_draft.storage_id		 = storage_id;
				item_storage_candidates_expanded = false;
				refresh_all();
			}});
		}
		const int storage_choices_height =
			ButtonGridComponent::preferred_height(
				static_cast<int>(storage_actions.size()), 2);
		content->add_button_grid("Storage choices", std::move(storage_actions),
								 2, storage_choices_height);
	}

	add_status_rows(*content, item_form_draft.status,
					[this](domain::ItemStatus status) {
		item_form_draft.status = status;
		refresh_all();
	});

	content->add_inline_buttons(
		juce_text(tag_row_count_summary(item_form_draft.tags)),
		{InlineButtonRowComponent::Action{.label = "Add row",
										  .handler =
											  [this] {
		item_form_draft.tags.push_back(domain::TagRow{});
		refresh_all();
	}},
		 InlineButtonRowComponent::Action{
			 .label = item_tag_candidates_expanded ? "Hide keys" : "Key hints",
			 .handler =
				 [this] {
		item_tag_candidates_expanded = !item_tag_candidates_expanded;
		refresh_all();
	}},
		 InlineButtonRowComponent::Action{
			 .label = "Clear",
			 .handler =
				 [this] {
		item_form_draft.tags.clear();
		refresh_all();
	},
			 .enabled = !item_form_draft.tags.empty()}},
		42);

	if (item_tag_candidates_expanded) {
		const TagKeyCandidateGroups groups =
			derive_tag_key_candidate_groups(session.repository);
		if (groups.item_keys.empty() && groups.storage_keys.empty()) {
			content->add_label("No reusable tag keys in this catalog yet.", 34,
							   panel_colour());
		}
		if (!groups.item_keys.empty()) {
			std::vector<ButtonGridComponent::Action> item_key_actions;
			for (const std::string& key : groups.item_keys) {
				item_key_actions.push_back(ButtonGridComponent::Action{
					.label = juce_text(key), .handler = [this, key] {
					apply_tag_key_candidate(item_form_draft.tags, key);
					refresh_all();
				}});
			}
			const int item_key_height = ButtonGridComponent::preferred_height(
				static_cast<int>(item_key_actions.size()), 3);
			content->add_button_grid("Item tag keys",
									 std::move(item_key_actions), 3,
									 item_key_height);
		}
		if (!groups.storage_keys.empty()) {
			std::vector<ButtonGridComponent::Action> storage_key_actions;
			for (const std::string& key : groups.storage_keys) {
				storage_key_actions.push_back(ButtonGridComponent::Action{
					.label = juce_text(key), .handler = [this, key] {
					apply_tag_key_candidate(item_form_draft.tags, key);
					refresh_all();
				}});
			}
			const int storage_key_height =
				ButtonGridComponent::preferred_height(
					static_cast<int>(storage_key_actions.size()), 3);
			content->add_button_grid("Storage tag keys",
									 std::move(storage_key_actions), 3,
									 storage_key_height);
		}
	}

	if (item_form_draft.tags.empty())
		content->add_label("No tags yet. Use Key hints or Add row.", 34,
						   panel_colour());
	for (std::size_t index = 0; index < item_form_draft.tags.size(); ++index) {
		content->add_tag_editor_row(
			index, item_form_draft.tags[index],
			[this](std::size_t changed_index, domain::TagRow changed_tag) {
			if (changed_index < item_form_draft.tags.size())
				item_form_draft.tags[changed_index] = std::move(changed_tag);
		}, [this](std::size_t removed_index) {
			if (removed_index >= item_form_draft.tags.size())
				return;
			item_form_draft.tags.erase(
				item_form_draft.tags.begin()
				+ static_cast<std::ptrdiff_t>(removed_index));
			refresh_all();
		}, 52);
	}

	content->add_editor(item_notes_editor, "Notes", 80, true).onTextChange =
		[this] {
		item_form_draft.notes = item_notes_editor.getText().toStdString();
	};

	content->add_label(juce_text(listing_summary(item_form_draft.listing)), 46,
					   panel_colour(), !item_form_draft.listing.empty());
	juce::Button& listing_toggle =
		content->add_button(item_listing_expanded ? "Collapse listing details"
												  : "Add listing details",
							38);
	listing_toggle.onClick = [this] {
		item_listing_expanded = !item_listing_expanded;
		refresh_all();
	};
	if (item_listing_expanded) {
		content->add_editor(item_listing_marketplace_editor, "Marketplace", 44)
			.onTextChange = [this] {
			item_form_draft.listing.marketplace =
				item_listing_marketplace_editor.getText().toStdString();
		};
		content->add_editor(item_listing_url_editor, "Listing URL", 44)
			.onTextChange = [this] {
			item_form_draft.listing.url =
				item_listing_url_editor.getText().toStdString();
		};
		content->add_editor(item_listing_note_editor, "Listing note", 70, true)
			.onTextChange = [this] {
			item_form_draft.listing.note =
				item_listing_note_editor.getText().toStdString();
		};
	}

	content->add_label(juce_text(finance_summary(item_form_draft.acquisition,
												 item_form_draft.finance)),
					   46, panel_colour(),
					   !item_form_draft.acquisition.empty()
						   || !item_form_draft.finance.empty());
	juce::Button& finance_toggle =
		content->add_button(item_finance_expanded ? "Collapse finance details"
												  : "Add finance details",
							38);
	finance_toggle.onClick = [this] {
		item_finance_expanded = !item_finance_expanded;
		refresh_all();
	};
	if (item_finance_expanded) {
		content
			->add_editor(item_acquisition_source_editor, "Acquisition source",
						 44)
			.onTextChange = [this] {
			item_form_draft.acquisition.source =
				item_acquisition_source_editor.getText().toStdString();
		};
		content->add_label(
			"Money fields stay omitted from this compact JUCE form pass; "
			"existing values are preserved when editing.",
			52, panel_colour());
	}

	if (item_form_mode == FormMode::Edit) {
		content->add_label("Edit-only actions", 34, panel_colour(), true);
		juce::Button& archive = content->add_button("Archive item", 42);
		archive.onClick		  = [this] {
			if (!item_form_draft.existing_id)
				return;
			EntityEditResult result = archive_item_in_session(
				EntityEditRequest{.current_session = session,
								  .identifiers	   = edit_identifiers,
								  .clock		   = edit_clock},
				*item_form_draft.existing_id);
			apply_entity_edit_result(std::move(result));
		};
		juce::Button& hard_delete = content->add_button(
			"Hard delete disabled until deletion sequence tests pass", 46);
		hard_delete.setEnabled(false);
	}
}
void AppShellComponent::build_storage_form_content() {
	content->add_editor_pair(storage_name_editor, "Display name (required)",
							 storage_type_editor, "Storage type (required)",
							 54);
	storage_name_editor.onTextChange = [this] {
		storage_form_draft.display_name =
			storage_name_editor.getText().toStdString();
	};
	storage_type_editor.onTextChange = [this] {
		storage_form_draft.storage_type =
			storage_type_editor.getText().toStdString();
	};

	content->add_inline_buttons(
		juce_text("Parent: "
				  + storage_label(session.repository,
								  storage_form_draft.parent_storage_id)),
		{InlineButtonRowComponent::Action{
			 .label = storage_parent_candidates_expanded	 ? "Hide choices"
					  : storage_form_draft.parent_storage_id ? "Change"
															 : "Choose",
			 .handler =
				 [this] {
		storage_parent_candidates_expanded =
			!storage_parent_candidates_expanded;
		refresh_all();
	}},
		 InlineButtonRowComponent::Action{
			 .label = "Clear",
			 .handler =
				 [this] {
		storage_form_draft.parent_storage_id.reset();
		storage_parent_candidates_expanded = false;
		refresh_all();
	},
			 .enabled = storage_form_draft.parent_storage_id.has_value()}},
		42);

	if (storage_parent_candidates_expanded) {
		std::vector<ButtonGridComponent::Action> parent_actions;
		parent_actions.push_back(ButtonGridComponent::Action{
			.label = "No parent storage", .handler = [this] {
			storage_form_draft.parent_storage_id.reset();
			storage_parent_candidates_expanded = false;
			refresh_all();
		}});
		for (const persistence::StorageEnvelope& storage :
			 session.repository.storages) {
			if (storage_form_draft.existing_id
				&& storage.record.id == *storage_form_draft.existing_id) {
				continue;
			}
			const core::StableIdentifier storage_id = storage.record.id;
			parent_actions.push_back(ButtonGridComponent::Action{
				.label = juce_text(
					storage_choice_label(session.repository, storage)),
				.handler = [this, storage_id] {
				storage_form_draft.parent_storage_id = storage_id;
				storage_parent_candidates_expanded	 = false;
				refresh_all();
			}});
		}
		const int parent_choices_height = ButtonGridComponent::preferred_height(
			static_cast<int>(parent_actions.size()), 2);
		content->add_button_grid("Parent choices", std::move(parent_actions), 2,
								 parent_choices_height);
	}

	content->add_editor(storage_location_editor, "Physical location", 46)
		.onTextChange = [this] {
		storage_form_draft.location =
			storage_location_editor.getText().toStdString();
	};

	content->add_inline_buttons(
		juce_text(tag_row_count_summary(storage_form_draft.tags)),
		{InlineButtonRowComponent::Action{.label = "Add row",
										  .handler =
											  [this] {
		storage_form_draft.tags.push_back(domain::TagRow{});
		refresh_all();
	}},
		 InlineButtonRowComponent::Action{
			 .label =
				 storage_tag_candidates_expanded ? "Hide keys" : "Key hints",
			 .handler =
				 [this] {
		storage_tag_candidates_expanded = !storage_tag_candidates_expanded;
		refresh_all();
	}},
		 InlineButtonRowComponent::Action{
			 .label = "Clear",
			 .handler =
				 [this] {
		storage_form_draft.tags.clear();
		refresh_all();
	},
			 .enabled = !storage_form_draft.tags.empty()}},
		42);

	if (storage_tag_candidates_expanded) {
		const TagKeyCandidateGroups groups =
			derive_tag_key_candidate_groups(session.repository);
		if (groups.item_keys.empty() && groups.storage_keys.empty()) {
			content->add_label("No reusable tag keys in this catalog yet.", 34,
							   panel_colour());
		}
		if (!groups.item_keys.empty()) {
			std::vector<ButtonGridComponent::Action> item_key_actions;
			for (const std::string& key : groups.item_keys) {
				item_key_actions.push_back(ButtonGridComponent::Action{
					.label = juce_text(key), .handler = [this, key] {
					apply_tag_key_candidate(storage_form_draft.tags, key);
					refresh_all();
				}});
			}
			const int storage_form_item_key_height =
				ButtonGridComponent::preferred_height(
					static_cast<int>(item_key_actions.size()), 3);
			content->add_button_grid("Item tag keys",
									 std::move(item_key_actions), 3,
									 storage_form_item_key_height);
		}
		if (!groups.storage_keys.empty()) {
			std::vector<ButtonGridComponent::Action> storage_key_actions;
			for (const std::string& key : groups.storage_keys) {
				storage_key_actions.push_back(ButtonGridComponent::Action{
					.label = juce_text(key), .handler = [this, key] {
					apply_tag_key_candidate(storage_form_draft.tags, key);
					refresh_all();
				}});
			}
			const int storage_form_storage_key_height =
				ButtonGridComponent::preferred_height(
					static_cast<int>(storage_key_actions.size()), 3);
			content->add_button_grid("Storage tag keys",
									 std::move(storage_key_actions), 3,
									 storage_form_storage_key_height);
		}
	}

	if (storage_form_draft.tags.empty())
		content->add_label("No tags yet. Use Key hints or Add row.", 34,
						   panel_colour());
	for (std::size_t index = 0; index < storage_form_draft.tags.size();
		 ++index) {
		content->add_tag_editor_row(
			index, storage_form_draft.tags[index],
			[this](std::size_t changed_index, domain::TagRow changed_tag) {
			if (changed_index < storage_form_draft.tags.size())
				storage_form_draft.tags[changed_index] = std::move(changed_tag);
		}, [this](std::size_t removed_index) {
			if (removed_index >= storage_form_draft.tags.size())
				return;
			storage_form_draft.tags.erase(
				storage_form_draft.tags.begin()
				+ static_cast<std::ptrdiff_t>(removed_index));
			refresh_all();
		}, 52);
	}

	content->add_editor(storage_notes_editor, "Notes", 80, true).onTextChange =
		[this] {
		storage_form_draft.notes = storage_notes_editor.getText().toStdString();
	};
	if (storage_form_mode == FormMode::Edit) {
		content->add_label("Edit-only actions", 34, panel_colour(), true);
		juce::Button& archive = content->add_button(
			storage_form_draft.lifecycle_status
					== domain::StorageLifecycleStatus::Archived
				? "Storage archived"
				: "Archive storage",
			42);
		archive.onClick = [this] {
			storage_form_draft.lifecycle_status =
				domain::StorageLifecycleStatus::Archived;
			storage_form_draft.archive_warning_acknowledged =
				storage_archive_warning_acknowledged;
			refresh_all();
		};
		juce::ToggleButton& acknowledge = content->add_toggle(
			"Confirm archive-with-contents warning if shown",
			storage_archive_warning_acknowledged, 38);
		acknowledge.onClick = [this] {
			storage_archive_warning_acknowledged =
				!storage_archive_warning_acknowledged;
			storage_form_draft.archive_warning_acknowledged =
				storage_archive_warning_acknowledged;
			refresh_all();
		};
		juce::Button& hard_delete = content->add_button(
			"Hard delete disabled until deletion sequence tests pass", 46);
		hard_delete.setEnabled(false);
	}
}

void AppShellComponent::build_photo_viewer_content() {
	if (!selected_photo_owner) {
		content->add_label("No photo owner selected.", 54, panel_colour(),
						   true);
		return;
	}
	const domain::PhotoOwner owner = *selected_photo_owner;
	const catalog::OwnerPhotoProjection* projection =
		owner_photo_projection(session.repository, owner);
	if (projection == nullptr || projection->ordered_photo_ids.empty()) {
		content->add_label(juce_text(owner_caption(session.repository, owner)),
						   50, surface_colour(), true);
		content->add_label(
			"No photo records for this owner yet. Add photos to import into "
			"app-private JPEG XL storage.",
			70, panel_colour(), true);
		juce::Button& add_photo = content->add_button("Add photos", 42);
		add_photo.onClick		= [this, owner] { request_add_photos(owner); };
		return;
	}

	if (!selected_photo_id
		|| !find_photo_index_in_order(projection->ordered_photo_ids,
									  *selected_photo_id)) {
		selected_photo_id = first_viewable_photo_id(session.repository, owner);
	}
	if (!selected_photo_id) {
		content->add_label("No selected photo.", 54, panel_colour(), true);
		return;
	}

	const persistence::PhotoEnvelope* photo =
		catalog::find_photo_envelope(session.repository, *selected_photo_id);
	if (photo == nullptr) {
		content->add_label(
			"Selected photo record is missing from the accepted catalog.", 64,
			warning_panel_colour(), true);
		return;
	}

	const std::optional<std::size_t> index = find_photo_index_in_order(
		projection->ordered_photo_ids, *selected_photo_id);
	const std::size_t position = index.has_value() ? *index + 1U : 1U;
	const std::size_t total	   = projection->ordered_photo_ids.size();
	content->add_label(juce_text(owner_caption(session.repository, owner)), 48,
					   surface_colour(), true);

	if (last_display_photo_id != selected_photo_id) {
		catalog::PhotoExportUseCase export_use_case{
			edit_identifiers, ui_operation_gate, internal_photo_codec,
			jpeg_export_service, document_export_service};
		platform::ProgressCollector display_progress;
		last_photo_display_result = export_use_case.load_photo_for_display(
			catalog::PhotoDisplayRequest{.current_state = session.repository,
										 .paths			= *session.paths,
										 .photo_id		= *selected_photo_id},
			display_progress, never_cancelled);
		last_display_photo_id = selected_photo_id;
	}

	juce::Image image;
	juce::String placeholder{"Loading preview placeholder"};
	if (last_photo_display_result.succeeded()
		&& last_photo_display_result.pixels.has_value()) {
		image = juce_image_from_pixels(*last_photo_display_result.pixels);
		placeholder =
			image.isValid() ? "" : "Decoded image cannot be displayed.";
	} else if (last_photo_display_result.placeholder.has_value()) {
		placeholder = juce_text(last_photo_display_result.placeholder->message);
	} else if (last_photo_display_result.was_user_cancelled()) {
		placeholder = "Photo display was cancelled.";
	} else {
		placeholder =
			"Photo preview placeholder. Full decode is only attempted "
			"for this viewer, not for result lists.";
	}
	content->add_image_panel(image,
							 juce_text(photo_summary(*photo, position, total)),
							 placeholder, 260);

	if (!last_photo_message.empty()) {
		content->add_label(juce_text(last_photo_message), 54,
						   accent_colour().withAlpha(0.34f), true);
	}
	if (!last_photo_diagnostics.empty()) {
		content->add_label(
			juce_text(core_diagnostic_summary(last_photo_diagnostics)), 76,
			warning_panel_colour(), true);
	}
	if (!last_photo_display_result.diagnostics.empty()) {
		content->add_label(juce_text(core_diagnostic_summary(
							   last_photo_display_result.diagnostics)),
						   76, warning_panel_colour(), true);
	}
	content->add_label(
		juce_text(progress_summary(last_progress_events.events())), 50,
		panel_colour());

	juce::Button& previous = content->add_button("Previous photo", 40);
	previous.setEnabled(total > 1U);
	previous.onClick = [this, owner, photo_id = *selected_photo_id] {
		selected_photo_id =
			adjacent_photo_id(session.repository, owner, photo_id, -1);
		last_display_photo_id.reset();
		refresh_all();
	};
	juce::Button& next = content->add_button("Next photo", 40);
	next.setEnabled(total > 1U);
	next.onClick = [this, owner, photo_id = *selected_photo_id] {
		selected_photo_id =
			adjacent_photo_id(session.repository, owner, photo_id, 1);
		last_display_photo_id.reset();
		refresh_all();
	};
	juce::Button& set_main = content->add_button(
		photo->record.is_main ? "Already main photo" : "Set as main", 42);
	set_main.setEnabled(!photo->record.is_main);
	set_main.onClick = [this, photo_id = *selected_photo_id] {
		EntityEditResult result = set_main_photo_in_session(
			EntityEditRequest{.current_session = session,
							  .identifiers	   = edit_identifiers,
							  .clock		   = edit_clock},
			photo_id);
		apply_photo_edit_result(std::move(result), photo_id);
	};
	juce::Button& export_button =
		content->add_button("Export current photo as JPEG", 42);
	export_button.setEnabled(last_photo_display_result.succeeded());
	export_button.onClick = [this, photo_id = *selected_photo_id] {
		request_export_photo(photo_id);
	};
	juce::Button& add_photo = content->add_button("Add more photos", 42);
	add_photo.onClick		= [this, owner] { request_add_photos(owner); };
}

void AppShellComponent::build_add_content() {
	content->add_label(
		"Create metadata first. Photo import and previews are available "
		"from item or storage details. Backup/import is in More.",
		70, panel_colour(), true);
	juce::Button& item	  = content->add_button("Add item", 52);
	item.onClick		  = [this] { open_new_item_form(std::nullopt); };
	juce::Button& storage = content->add_button("Add storage", 52);
	storage.onClick		  = [this] { open_new_storage_form(std::nullopt); };
}

void AppShellComponent::build_backup_recovery_content() {
	const CatalogRecoveryUiSummary summary = make_recovery_ui_summary(session);
	content->add_label(juce_text(summary.plain_summary_message), 86,
					   summary.fatal() || summary.degraded()
						   ? warning_panel_colour()
						   : panel_colour(),
					   true);
	content->add_label(juce_text(recovery_counts_summary(summary)), 72,
					   surface_colour(), true);
	content->add_label(juce_text(recovery_action_summary(summary.safe_actions)),
					   62, panel_colour());
	content->add_label(
		"ZIP exports are unencrypted and may contain photos, notes, tags, "
		"listing data, and finance values.",
		70, warning_panel_colour(), true);
	if (session.degraded()) {
		content->add_label(
			"Degraded normal backup preserves raw canonical metadata files and "
			"readable media. Export a diagnostic archive as a companion if "
			"manual repair is needed.",
			82, warning_panel_colour(), true);
	}
	if (session.fatal()) {
		content->add_label(
			"Fatal recovery never overwrites data automatically. Import backup "
			"still uses staging, validation, and explicit replacement "
			"confirmation.",
			82, warning_panel_colour(), true);
	}

	if (!last_backup_message.empty()) {
		content->add_label(juce_text(last_backup_message), 66,
						   accent_colour().withAlpha(0.34f), true);
	}
	if (has_diagnostics(last_backup_diagnostics)) {
		content->add_label(
			juce_text(core_diagnostic_summary(last_backup_diagnostics)), 86,
			warning_panel_colour(), true);
	}
	content->add_label(
		juce_text(progress_summary(last_progress_events.events())), 54,
		panel_colour());

	juce::Button& backup = content->add_button("Export normal backup ZIP", 46);
	backup.setEnabled(!session.fatal());
	backup.onClick = [this] { request_export_backup(); };
	juce::Button& diagnostic =
		content->add_button("Export diagnostic archive ZIP", 46);
	diagnostic.setEnabled(session.paths.has_value());
	diagnostic.onClick = [this] { request_export_diagnostic_archive(); };
	juce::Button& import =
		content->add_button("Import backup ZIP: select and validate", 46);
	import.setEnabled(session.paths.has_value());
	import.onClick = [this] { request_import_backup(); };

	if (pending_import_staging) {
		content->add_label(juce_text(import_validation_summary(
							   pending_import_staging->validation)),
						   76, surface_colour(), true);
		if (pending_import_staging->validation.explicit_warning_required()) {
			content->add_label(
				pending_import_degraded_acknowledged
					? "Degraded import warning is acknowledged. Confirming now "
					  "will replace the active catalog."
					: "Degraded staged import can replace current data only "
					  "after explicit warning acknowledgement.",
				78, warning_panel_colour(), true);
		}
		juce::Button& confirm = content->add_button(
			pending_import_staging->validation.explicit_warning_required()
					&& !pending_import_degraded_acknowledged
				? "Acknowledge degraded import warning"
				: "Confirm replacement with validated backup",
			48);
		confirm.onClick		= [this] { confirm_staged_backup_import(); };
		juce::Button& clear = content->add_button("Cancel staged import", 42);
		clear.onClick		= [this] {
			pending_import_staging.reset();
			pending_import_degraded_acknowledged = false;
			last_backup_message =
				"Validated staged import cleared. Active catalog unchanged.";
			refresh_all();
		};
	}

	if (!summary.technical_details.empty()) {
		content->add_label("Technical report", 38, panel_colour(), true);
		std::string details;
		const std::size_t max_entries =
			std::min<std::size_t>(summary.technical_details.size(), 6U);
		for (std::size_t index = 0; index < max_entries; ++index) {
			if (!details.empty())
				details += "\n";
			details += summary.technical_details[index];
		}
		content->add_label(juce_text(details), 124, panel_colour());
	}
}

void AppShellComponent::build_more_content() {
	content->add_label(
		"Maintenance hub for manual unencrypted ZIP backup, staged import, "
		"diagnostics, and recovery.",
		82, panel_colour(), true);
	juce::Button& maintenance =
		content->add_button("Open backup/import/recovery", 52);
	maintenance.onClick = [this] {
		select_root(RootDestination::BackupRecovery);
	};
	if (session.degraded()) {
		content->add_label(
			"Degraded load: valid records remain usable. Open recovery to "
			"review counts, export backup, or export diagnostic archive.",
			78, warning_panel_colour(), true);
	}
	content->add_label(
		"No cloud, accounts, marketplace automation, SQL, broad "
		"media permissions, or Java/Kotlin business logic is added.",
		82);
}
}	 // namespace shuba::ui
