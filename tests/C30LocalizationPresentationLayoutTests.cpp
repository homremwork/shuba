#include "Localization/EmbeddedCatalog.hpp"
#include "Localization/Facade.hpp"
#include "UI/View/Primitives/Forms.hpp"
#include "UI/View/Primitives/Palette.hpp"
#include "UI/View/Primitives/PhotoManagement.hpp"
#include "UI/View/Primitives/Previews.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

namespace {
using shuba::ui::DirectChoiceGridComponent;

TEST_CASE("C5 lays out five direct Russian status choices in readable rows",
		  "[c5][localization][layout][forms]") {
	const juce::Rectangle<int> selector_bounds{
		0, 0, 448, DirectChoiceGridComponent::preferred_height()};
	const std::vector<juce::Rectangle<int>> buttons =
		DirectChoiceGridComponent::choice_bounds(selector_bounds, true, 5U);
	REQUIRE(buttons.size() == 5U);

	for (const juce::Rectangle<int>& button : buttons) {
		REQUIRE(button.getWidth() >= 70);
		REQUIRE(button.getHeight() == 32);
		REQUIRE(selector_bounds.contains(button));
	}
	REQUIRE(buttons[0].getY() == buttons[1].getY());
	REQUIRE(buttons[1].getY() == buttons[2].getY());
	REQUIRE(buttons[3].getY() == buttons[4].getY());
	REQUIRE(buttons[3].getY() > buttons[0].getY());
	for (std::size_t left{}; left < buttons.size(); ++left)
		for (std::size_t right = left + 1U; right < buttons.size(); ++right)
			REQUIRE_FALSE(buttons[left].intersects(buttons[right]));
}

TEST_CASE("R13 photo operation progress surface updates without reconstruction",
		  "[r13][ui][progress][stable-surface]") {
	juce::ScopedJuceInitialiser_GUI juce_initialiser;
	std::atomic_uint32_t cancellation_count{};
	shuba::ui::ShellOperationProgressComponent progress{
		[&] { cancellation_count.fetch_add(1U, std::memory_order_acq_rel); }};
	progress.setBounds(0, 0, 448, 82);
	progress.update_model(shuba::ui::ShellOperationProgressModel{
		.heading				= "Storing photos",
		.summary				= "Copying content - 1 / 10000",
		.cancel_label			= "Cancel",
		.active					= true,
		.cancellation_available = true});
	REQUIRE(progress.isVisible());
	REQUIRE(progress.getNumChildComponents() == 3);
	juce::Component* const first_heading = progress.getChildComponent(0);
	juce::Component* const first_summary = progress.getChildComponent(1);
	juce::Component* const first_cancel	 = progress.getChildComponent(2);
	REQUIRE(first_cancel->isVisible());

	for (std::size_t index = 0U; index < 10000U; ++index) {
		progress.update_model(shuba::ui::ShellOperationProgressModel{
			.heading	  = "Storing photos",
			.summary	  = "Copying content - "
							+ juce::String(static_cast<int>(index + 1U))
							+ " / 10000",
			.cancel_label = "Cancel",
			.active		  = true,
			.cancellation_available = true});
	}
	REQUIRE(progress.getNumChildComponents() == 3);
	REQUIRE(progress.getChildComponent(0) == first_heading);
	REQUIRE(progress.getChildComponent(1) == first_summary);
	REQUIRE(progress.getChildComponent(2) == first_cancel);
	REQUIRE(progress.isVisible());
	REQUIRE(first_cancel->isVisible());

	juce::Button* const cancel_button =
		dynamic_cast<juce::Button*>(first_cancel);
	REQUIRE(cancel_button != nullptr);
	REQUIRE(static_cast<bool>(cancel_button->onClick));
	cancel_button->onClick();
	REQUIRE(cancellation_count.load(std::memory_order_acquire) == 1U);
	progress.update_model(shuba::ui::ShellOperationProgressModel{});
	REQUIRE_FALSE(progress.isVisible());
}

TEST_CASE("JI3 preview badges use shaped localized text widths within bounds",
		  "[ji3][localization][layout][preview-badge]") {
	juce::ScopedJuceInitialiser_GUI juce_initialiser;
	const shuba::localization::Localization english_localization =
		shuba::localization::make_localization(
			shuba::localization::Language::English, {});
	const shuba::localization::Localization russian_localization =
		shuba::localization::make_localization(
			shuba::localization::Language::Russian,
			shuba::localization::embedded_russian_catalog());
	const juce::FontOptions badge_font{11.5f, juce::Font::bold};
	const juce::String english_text =
		shuba::ui::juce_text(english_localization.text(
			shuba::localization::MessageId::PreviewStateStaged));
	const juce::String russian_text =
		shuba::ui::juce_text(russian_localization.text(
			shuba::localization::MessageId::PreviewStateStaged));
	const juce::String long_english_text{"Staged preview label"};
	const juce::String long_russian_text =
		shuba::ui::juce_text("Подготовленный предпросмотр");
	const float english_width =
		shuba::ui::shaped_text_width(english_text, badge_font);
	const float russian_width =
		shuba::ui::shaped_text_width(russian_text, badge_font);
	const float long_english_width =
		shuba::ui::shaped_text_width(long_english_text, badge_font);
	const float long_russian_width =
		shuba::ui::shaped_text_width(long_russian_text, badge_font);
	REQUIRE(english_width > 0.0f);
	REQUIRE(russian_width > 0.0f);
	REQUIRE(long_english_width > english_width);
	REQUIRE(long_russian_width > russian_width);

	const int wide_container_width = 448;
	const int expected_english_width =
		static_cast<int>(std::ceil(long_english_width)) + 18;
	const int expected_russian_width =
		static_cast<int>(std::ceil(long_russian_width)) + 18;
	REQUIRE(
		shuba::ui::preview_badge_width(long_english_text, wide_container_width)
		== std::max(68, expected_english_width));
	REQUIRE(
		shuba::ui::preview_badge_width(long_russian_text, wide_container_width)
		== std::max(68, expected_russian_width));

	const int narrow_container_width = 40;
	const int narrow_badge_width	 = shuba::ui::preview_badge_width(
		long_russian_text, narrow_container_width);
	REQUIRE(narrow_badge_width == narrow_container_width - 12);
	const juce::Rectangle<int> container{0, 0, narrow_container_width, 25};
	juce::Rectangle<int> badge_source = container;
	const juce::Rectangle<int> narrow_badge =
		badge_source.removeFromRight(narrow_badge_width).reduced(4, 3);
	REQUIRE(container.contains(narrow_badge));
}

}	 // namespace
