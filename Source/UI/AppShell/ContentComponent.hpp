#pragma once

#include "Domain/Domain.hpp"
#include "UI/View/Primitives/Forms.hpp"
#include "UI/View/Primitives/Palette.hpp"
#include "UI/View/Primitives/PhotoManagement.hpp"
#include "UI/View/Primitives/Previews.hpp"
#include "UI/View/Primitives/Rows.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

namespace shuba::ui {
class ContentComponent final : public juce::Component {
public:
	ContentComponent();
	~ContentComponent() override;

	void clear_rows();
	void begin_rebuild() noexcept;
	void end_rebuild();

	TextRowComponent& add_label(juce::String text, int height,
								juce::Colour colour = panel_colour(),
								bool bold			= false);
	juce::Button& add_button(const juce::String& text, int height);
	ImagePanelComponent& add_image_panel(juce::Image image,
										 juce::String caption,
										 juce::String placeholder, int height);
	PhotoCarouselComponent& add_photo_carousel(
		std::vector<PhotoCarouselSlide> slides, std::size_t selected_index,
		localization::Localization& localization,
		std::function<void(std::size_t)> select_handler,
		std::function<void()> activate_handler, int height);
	PhotoViewerImageComponent& add_photo_viewer_image(
		PhotoViewerImageModel model, PhotoViewerImageHandlers handlers,
		localization::Localization& localization, int height);
	PreviewCardButtonComponent& add_preview_card(
		PreviewCardContent content, localization::Localization& localization,
		int height);
	CompactStorageStripComponent& add_compact_storage_strip(
		std::vector<CompactStorageCardContent> cards,
		localization::Localization& localization, int height);
	InlineButtonRowComponent& add_inline_buttons(
		juce::String title,
		std::vector<InlineButtonRowComponent::Action> actions, int height);
	DirectChoiceGridComponent& add_direct_choice_grid(
		juce::String title,
		std::vector<DirectChoiceGridComponent::Choice> choices,
		int height = DirectChoiceGridComponent::preferred_height());
	ButtonGridComponent& add_button_grid(
		juce::String title, std::vector<ButtonGridComponent::Action> actions,
		int column_count, int height);
	ChipGridComponent& add_chip_grid(
		juce::String title, std::vector<ChipGridComponent::Action> actions,
		int column_count, int height);
	EditorPairComponent& add_editor_pair(juce::TextEditor& first_editor,
										 const juce::String& first_placeholder,
										 juce::TextEditor& second_editor,
										 const juce::String& second_placeholder,
										 int height);
	ManagedPhotoDeckComponent& add_managed_photo_deck(
		ManagedPhotoDeckModel model, ManagedPhotoDeckHandlers handlers,
		localization::Localization& localization, int height);
	TagRowEditorComponent& add_tag_editor_row(
		std::size_t row_index, const domain::TagRow& tag,
		localization::Localization& localization,
		std::function<void(std::size_t, domain::TagRow)> change_handler,
		std::function<void(std::size_t)> remove_handler, int height,
		bool enabled = true);
	juce::ToggleButton& add_toggle(const juce::String& text, bool state,
								   int height);
	juce::TextEditor& add_editor(juce::TextEditor& editor,
								 const juce::String& placeholder, int height,
								 bool multiline = false);

	void resized() override;
	void paint(juce::Graphics& graphics) override;
	void set_viewport_height_hint(int height) noexcept;
	[[nodiscard]] int viewport_height_hint() const noexcept;

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
	int viewport_height_hint_value{620};
};
}	 // namespace shuba::ui
