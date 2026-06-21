#pragma once

#include "Domain/Domain.hpp"
#include "UI/Session/PhotoSessionTypes.hpp"
#include "UI/View/UiPrimitives.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

namespace shuba::ui {
class AppShellContentComponent final : public juce::Component {
public:
	AppShellContentComponent();
	~AppShellContentComponent() override;

	void clear_rows();
	void begin_rebuild() noexcept;
	void end_rebuild();

	TextRowComponent& add_label(juce::String text, int height,
								juce::Colour colour = panel_colour(),
								bool bold			= false);
	juce::Button& add_button(juce::String text, int height);
	ImagePanelComponent& add_image_panel(juce::Image image,
										 juce::String caption,
										 juce::String placeholder, int height);
	InlineButtonRowComponent& add_inline_buttons(
		juce::String title,
		std::vector<InlineButtonRowComponent::Action> actions, int height);
	ButtonGridComponent& add_button_grid(
		juce::String title, std::vector<ButtonGridComponent::Action> actions,
		int column_count, int height);
	EditorPairComponent& add_editor_pair(juce::TextEditor& first_editor,
										 juce::String first_placeholder,
										 juce::TextEditor& second_editor,
										 juce::String second_placeholder,
										 int height);
	PendingPhotoStripComponent& add_pending_photo_strip(
		std::vector<PendingPhotoSource> sources,
		std::function<void()> add_handler, std::function<void()> clear_handler,
		std::function<void(std::size_t)> remove_handler, int height);
	TagRowEditorComponent& add_tag_editor_row(
		std::size_t row_index, domain::TagRow tag,
		std::function<void(std::size_t, domain::TagRow)> change_handler,
		std::function<void(std::size_t)> remove_handler, int height);
	juce::ToggleButton& add_toggle(juce::String text, bool state, int height);
	juce::TextEditor& add_editor(juce::TextEditor& editor,
								 juce::String placeholder, int height,
								 bool multiline = false);

	void resized() override;
	void paint(juce::Graphics& graphics) override;

private:
	struct Row final {
		std::unique_ptr<juce::Component> component;
		int height{};
		bool owned{true};
	};

	void add_row(std::unique_ptr<juce::Component> component, int height,
				 bool owned = true);

	std::vector<Row> rows;
	bool rebuilding{};
};
}	 // namespace shuba::ui
