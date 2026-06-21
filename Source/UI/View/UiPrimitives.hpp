#pragma once

#include "Domain/Domain.hpp"
#include "JuceHeader.h"
#include "UI/Session/PhotoSessionTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
	RowButtonComponent(juce::String text_value, juce::Colour background_value);

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
	explicit TouchSafeToggleButton(juce::String text_value);

private:
	void mouseDown(const juce::MouseEvent& event) override;
	void mouseDrag(const juce::MouseEvent& event) override;
	void mouseUp(const juce::MouseEvent& event) override;

	TouchScrollActivationGuard touch_activation_guard;
};

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

class InlineButtonRowComponent final : public juce::Component {
public:
	struct Action final {
		juce::String label;
		std::function<void()> handler;
		bool enabled{true};
	};

	InlineButtonRowComponent(juce::String title_value,
							 std::vector<Action> actions_value);

	void resized() override;
	void paint(juce::Graphics& graphics) override;

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
						int column_count_value);

	[[nodiscard]] static int preferred_height(int action_count,
											  int column_count_value) noexcept;

	void resized() override;
	void paint(juce::Graphics& graphics) override;

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
		std::function<void(std::size_t)> remove_handler);

	void resized() override;
	void paint(juce::Graphics& graphics) override;

private:
	void publish_change();

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
						juce::String second_placeholder);
	~EditorPairComponent() override;

	void resized() override;
	void paint(juce::Graphics& graphics) override;

private:
	static void prepare_editor(juce::TextEditor& editor,
							   juce::String placeholder);

	juce::TextEditor& first_editor;
	juce::TextEditor& second_editor;
};

class PendingPhotoCardsComponent;

class PendingPhotoStripComponent final : public juce::Component {
public:
	PendingPhotoStripComponent(std::vector<PendingPhotoSource> sources_value,
							   std::function<void()> add_handler,
							   std::function<void()> clear_handler,
							   std::function<void(std::size_t)> remove_handler);
	~PendingPhotoStripComponent() override;

	void resized() override;
	void paint(juce::Graphics& graphics) override;

private:
	std::vector<PendingPhotoSource> sources;
	juce::TextButton add_button{"Add photos"};
	juce::TextButton clear_button{"Clear"};
	juce::Viewport card_viewport;
	std::unique_ptr<PendingPhotoCardsComponent> cards;
};
}	 // namespace shuba::ui
