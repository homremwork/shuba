#include "UI/AppShell/ChromeComponent.hpp"
#include "UI/View/SafeArea.hpp"

#include "Localization/Facade.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {
void require_visible_chrome_inside(shuba::ui::ChromeComponent& chrome,
								   const juce::Rectangle<int>& safe_bounds) {
	for (int index = 0; index < chrome.getNumChildComponents(); ++index) {
		const juce::Component* child = chrome.getChildComponent(index);
		if (child->isVisible()) {
			CAPTURE(index, child->getName(), child->getBounds().toString());
			REQUIRE(safe_bounds.contains(child->getBounds()));
		}
	}
}
}	 // namespace

TEST_CASE("B23 fullscreen safe area applies top and bottom in portrait") {
	const juce::Rectangle<int> fullscreen_bounds{0, 0, 720, 1512};
	const shuba::ui::FullscreenSafeAreaInsets insets =
		shuba::ui::make_fullscreen_safe_area_insets(
			juce::BorderSize<int>{32, 0, 48, 0});

	const juce::Rectangle<int> content_bounds =
		shuba::ui::apply_fullscreen_safe_area(fullscreen_bounds, insets);

	REQUIRE(content_bounds == juce::Rectangle<int>{0, 32, 720, 1432});
}

TEST_CASE("B23 fullscreen safe area applies all reported landscape insets") {
	const juce::Rectangle<int> fullscreen_bounds{0, 0, 360, 780};
	const shuba::ui::FullscreenSafeAreaInsets insets =
		shuba::ui::make_fullscreen_safe_area_insets(
			juce::BorderSize<int>{24, 6, 96, 8});

	const juce::Rectangle<int> content_bounds =
		shuba::ui::apply_fullscreen_safe_area(fullscreen_bounds, insets);

	REQUIRE(content_bounds == juce::Rectangle<int>{6, 24, 346, 660});
}

TEST_CASE("B23 fullscreen safe area clamps oversized display insets") {
	const juce::Rectangle<int> compact_bounds{10, 20, 80, 40};
	const shuba::ui::FullscreenSafeAreaInsets insets{
		.top = 100, .left = 100, .bottom = 100, .right = 100};

	const juce::Rectangle<int> content_bounds =
		shuba::ui::apply_fullscreen_safe_area(compact_bounds, insets);

	REQUIRE(content_bounds == juce::Rectangle<int>{89, 59, 1, 1});
}

TEST_CASE("B23 fullscreen safe area handles bottom edge cases") {
	const juce::Rectangle<int> fullscreen_bounds{12, 18, 360, 780};

	SECTION("zero bottom inset preserves height") {
		const juce::Rectangle<int> content_bounds =
			shuba::ui::apply_fullscreen_safe_area(
				fullscreen_bounds,
				shuba::ui::FullscreenSafeAreaInsets{.top = 24});
		REQUIRE(content_bounds == juce::Rectangle<int>{12, 42, 360, 756});
	}

	SECTION("gesture inset is subtracted exactly") {
		const juce::Rectangle<int> content_bounds =
			shuba::ui::apply_fullscreen_safe_area(
				fullscreen_bounds,
				shuba::ui::FullscreenSafeAreaInsets{.bottom = 24});
		REQUIRE(content_bounds == juce::Rectangle<int>{12, 18, 360, 756});
	}

	SECTION("three-button inset is subtracted exactly") {
		const juce::Rectangle<int> content_bounds =
			shuba::ui::apply_fullscreen_safe_area(
				fullscreen_bounds,
				shuba::ui::FullscreenSafeAreaInsets{.bottom = 96});
		REQUIRE(content_bounds == juce::Rectangle<int>{12, 18, 360, 684});
	}

	SECTION("negative insets clamp to zero") {
		const juce::Rectangle<int> content_bounds =
			shuba::ui::apply_fullscreen_safe_area(
				fullscreen_bounds,
				shuba::ui::FullscreenSafeAreaInsets{
					.top = -1, .left = -2, .bottom = -3, .right = -4});
		REQUIRE(content_bounds == fullscreen_bounds);
	}

	SECTION("empty bounds remain empty") {
		const juce::Rectangle<int> empty_bounds{12, 18, 0, 780};
		REQUIRE(shuba::ui::apply_fullscreen_safe_area(
					empty_bounds,
					shuba::ui::FullscreenSafeAreaInsets{
						.top = 1, .left = 2, .bottom = 3, .right = 4})
				== empty_bounds);
	}
}

TEST_CASE("B23 shell chrome keeps bottom controls inside safe content") {
	juce::ScopedJuceInitialiser_GUI juce_initialiser;
	shuba::localization::Localization localization =
		shuba::localization::make_localization(
			shuba::localization::Language::English, {});
	shuba::ui::ChromeComponent chrome{{}, localization};

	for (const juce::Rectangle<int> safe_bounds :
		 {juce::Rectangle<int>{8, 24, 344, 660},
		  juce::Rectangle<int>{24, 8, 684, 328}}) {
		CAPTURE(safe_bounds.toString());
		chrome.update_model(shuba::ui::ChromeComponent::Model{
			.destination = shuba::ui::RootDestination::ItemDetail});
		const juce::Rectangle<int> detail_content_bounds =
			chrome.layout_shell(safe_bounds);
		require_visible_chrome_inside(chrome, safe_bounds);
		REQUIRE(detail_content_bounds.getY() == safe_bounds.getY() + 62);
		REQUIRE(detail_content_bounds.getBottom()
				== safe_bounds.getBottom() - 54);

		chrome.update_model(shuba::ui::ChromeComponent::Model{
			.destination = shuba::ui::RootDestination::PhotoViewer});
		const juce::Rectangle<int> viewer_content_bounds =
			chrome.layout_shell(safe_bounds);
		require_visible_chrome_inside(chrome, safe_bounds);
		REQUIRE(viewer_content_bounds.getY() == safe_bounds.getY() + 62);
		REQUIRE(viewer_content_bounds.getBottom() == safe_bounds.getBottom());

		chrome.update_model(shuba::ui::ChromeComponent::Model{
			.destination = shuba::ui::RootDestination::BackupRecovery});
		const juce::Rectangle<int> recovery_content_bounds =
			chrome.layout_shell(safe_bounds);
		require_visible_chrome_inside(chrome, safe_bounds);
		REQUIRE(recovery_content_bounds.getY() == safe_bounds.getY() + 62);
		REQUIRE(recovery_content_bounds.getBottom()
				== safe_bounds.getBottom() - 54);

		chrome.update_model(shuba::ui::ChromeComponent::Model{
			.destination = shuba::ui::RootDestination::ItemForm});
		const juce::Rectangle<int> form_content_bounds =
			chrome.layout_shell(safe_bounds);
		require_visible_chrome_inside(chrome, safe_bounds);
		REQUIRE(form_content_bounds.getY() == safe_bounds.getY() + 62);
		REQUIRE(form_content_bounds.getBottom()
				== safe_bounds.getBottom() - 58);

		chrome.update_model(shuba::ui::ChromeComponent::Model{
			.destination = shuba::ui::RootDestination::Storages});
		const juce::Rectangle<int> storage_content_bounds =
			chrome.layout_shell(safe_bounds);
		require_visible_chrome_inside(chrome, safe_bounds);
		REQUIRE(storage_content_bounds.getY() == safe_bounds.getY() + 106);
	}
}
