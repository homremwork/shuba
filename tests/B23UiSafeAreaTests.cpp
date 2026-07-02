#include "UI/View/SafeArea.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("B23 fullscreen safe area keeps Android status bar clear") {
	const juce::Rectangle<int> fullscreen_bounds{0, 0, 720, 1512};
	const shuba::ui::FullscreenSafeAreaInsets insets =
		shuba::ui::make_fullscreen_safe_area_insets(
			juce::BorderSize<int>{32, 0, 48, 0});

	const juce::Rectangle<int> content_bounds =
		shuba::ui::apply_fullscreen_safe_area(fullscreen_bounds, insets);

	REQUIRE(content_bounds == juce::Rectangle<int>{0, 32, 720, 1480});
}

TEST_CASE("B23 fullscreen safe area preserves bottom fullscreen layout") {
	const juce::Rectangle<int> fullscreen_bounds{0, 0, 360, 780};
	const shuba::ui::FullscreenSafeAreaInsets insets =
		shuba::ui::make_fullscreen_safe_area_insets(
			juce::BorderSize<int>{24, 6, 96, 8});

	const juce::Rectangle<int> content_bounds =
		shuba::ui::apply_fullscreen_safe_area(fullscreen_bounds, insets);

	REQUIRE(content_bounds == juce::Rectangle<int>{6, 24, 346, 756});
}

TEST_CASE("B23 fullscreen safe area clamps oversized display insets") {
	const juce::Rectangle<int> compact_bounds{10, 20, 80, 40};
	const shuba::ui::FullscreenSafeAreaInsets insets{.top = 100,
												 .left = 100,
												 .right = 100};

	const juce::Rectangle<int> content_bounds =
		shuba::ui::apply_fullscreen_safe_area(compact_bounds, insets);

	REQUIRE(content_bounds == juce::Rectangle<int>{89, 59, 1, 1});
}
