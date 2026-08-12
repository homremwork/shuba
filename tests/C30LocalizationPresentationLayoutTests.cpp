#include "UI/View/Primitives/Forms.hpp"
#include "UI/View/Primitives/PhotoManagement.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
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
	shuba::ui::PhotoOperationProgressComponent progress{[&] {
		cancellation_count.fetch_add(1U, std::memory_order_acq_rel);
	}};
	progress.setBounds(0, 0, 448, 82);
	progress.update_model(shuba::ui::PhotoOperationProgressModel{
		.heading = "Storing photos",
		.summary = "Copying content - 1 / 10000",
		.cancel_label = "Cancel",
		.active = true,
		.cancellation_available = true});
	REQUIRE(progress.isVisible());
	REQUIRE(progress.getNumChildComponents() == 3);
	juce::Component* const first_heading = progress.getChildComponent(0);
	juce::Component* const first_summary = progress.getChildComponent(1);
	juce::Component* const first_cancel = progress.getChildComponent(2);
	REQUIRE(first_cancel->isVisible());

	for (std::size_t index = 0U; index < 10000U; ++index) {
		progress.update_model(shuba::ui::PhotoOperationProgressModel{
			.heading = "Storing photos",
			.summary = "Copying content - "
					   + juce::String(static_cast<int>(index + 1U))
					   + " / 10000",
			.cancel_label = "Cancel",
			.active = true,
			.cancellation_available = true});
	}
	REQUIRE(progress.getNumChildComponents() == 3);
	REQUIRE(progress.getChildComponent(0) == first_heading);
	REQUIRE(progress.getChildComponent(1) == first_summary);
	REQUIRE(progress.getChildComponent(2) == first_cancel);
	REQUIRE(progress.isVisible());
	REQUIRE(first_cancel->isVisible());

	juce::Button* const cancel_button = dynamic_cast<juce::Button*>(first_cancel);
	REQUIRE(cancel_button != nullptr);
	REQUIRE(static_cast<bool>(cancel_button->onClick));
	cancel_button->onClick();
	REQUIRE(cancellation_count.load(std::memory_order_acquire) == 1U);
	progress.update_model(shuba::ui::PhotoOperationProgressModel{});
	REQUIRE_FALSE(progress.isVisible());
}

}  // namespace
