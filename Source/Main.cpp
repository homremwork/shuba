#include "Core/Clock.hpp"
#include "Core/Identifier.hpp"
#include "Platform/JuceAndroidServices.hpp"
#include "UI/AppShell.hpp"
#include "UI/CatalogSession.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <utility>

namespace {
[[nodiscard]] bool debug_demo_seed_enabled() noexcept {
#if defined(NDEBUG)
	return false;
#else
	return true;
#endif
}

[[nodiscard]] shuba::ui::CatalogSessionState make_catalog_session() {
	shuba::platform::JuceAndroidPathProvider path_provider;
	shuba::core::RandomIdentifierSource identifiers;
	shuba::core::SystemClock clock;
	return shuba::ui::load_catalog_session(shuba::ui::CatalogSessionLoadRequest{
		.path_provider			 = path_provider,
		.identifiers			 = identifiers,
		.clock					 = clock,
		.debug_demo_seed_enabled = debug_demo_seed_enabled()});
}

class MainWindow final : public juce::DocumentWindow {
public:
	explicit MainWindow(juce::String name)
		: DocumentWindow(std::move(name), juce::Colours::black,
						 DocumentWindow::allButtons) {
		setUsingNativeTitleBar(true);
		setContentOwned(
			new shuba::ui::AppShellComponent(make_catalog_session()), true);
#if JUCE_ANDROID
		setFullScreen(true);
#else
		centreWithSize(getWidth(), getHeight());
#endif
		setVisible(true);
	}

	void closeButtonPressed() override {
		juce::JUCEApplication::getInstance()->systemRequestedQuit();
	}
};

class ShubaApplication final : public juce::JUCEApplication {
public:
	const juce::String getApplicationName() override { return "Shuba"; }

	const juce::String getApplicationVersion() override { return "0.1.0"; }

	bool moreThanOneInstanceAllowed() override { return false; }

	void initialise(const juce::String&) override {
		main_window = std::make_unique<MainWindow>(getApplicationName());
	}

	void shutdown() override { main_window = nullptr; }

	void systemRequestedQuit() override { quit(); }

private:
	std::unique_ptr<MainWindow> main_window;
};
}	 // namespace

START_JUCE_APPLICATION(ShubaApplication)
