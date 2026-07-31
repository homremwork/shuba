#include "UI/View/Primitives/Forms.hpp"

#include <catch2/catch_test_macros.hpp>

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

}	 // namespace
