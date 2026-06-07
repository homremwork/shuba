#include <juce_gui_basics/juce_gui_basics.h>
#include <jxl/decode.h>
#include <glaze/glaze.hpp>

#include <string>

struct GlazeSmokeRecord {
	int id{};
	std::string name;
};

namespace {
constexpr int COMPILED_CPP_STANDARD = __cplusplus;

juce::String describe_cpp_standard() {
	if constexpr (COMPILED_CPP_STANDARD > 202002L)
		return "C++23 or newer";

	return "unexpected pre-C++23 standard";
}

bool run_glaze_smoke() {
	const GlazeSmokeRecord input{.id = 7, .name = "b01"};
	const auto encoded = glz::write_json(input);

	if (!encoded)
		return false;

	GlazeSmokeRecord output{};
	const auto error = glz::read_json(output, encoded.value());

	return !error && output.id == input.id && output.name == input.name;
}

uint32_t get_jxl_decoder_version() {
	return JxlDecoderVersion();
}

class MainComponent final : public juce::Component {
public:
	MainComponent() { setSize(480, 720); }

	void paint(juce::Graphics& GraphicsContext) override {
		GraphicsContext.fillAll(juce::Colour::fromRGB(18, 24, 32));
		GraphicsContext.setColour(juce::Colours::white);
		GraphicsContext.setFont(juce::FontOptions(24.0f, juce::Font::bold));
		GraphicsContext.drawFittedText(
			"Shuba B01", getLocalBounds().reduced(24).removeFromTop(80),
			juce::Justification::centred, 1);

		GraphicsContext.setFont(juce::FontOptions(16.0f));
		GraphicsContext.setColour(juce::Colour::fromRGB(210, 220, 235));

		auto body = getLocalBounds().reduced(24);
		body.removeFromTop(120);

		const auto text =
			juce::String("Minimal JUCE Android launch proof\n") + "JUCE "
			+ juce::String(JUCE_MAJOR_VERSION) + "."
			+ juce::String(JUCE_MINOR_VERSION) + "."
			+ juce::String(JUCE_BUILDNUMBER) + "\n" + describe_cpp_standard()
			+ " (__cplusplus=" + juce::String(COMPILED_CPP_STANDARD) + ")\n"
			+ "Glaze smoke: " + (run_glaze_smoke() ? "pass" : "fail") + "\n"
			+ "libjxl decoder version: "
			+ juce::String(static_cast<int>(get_jxl_decoder_version()));

		GraphicsContext.drawFittedText(text, body,
									   juce::Justification::centredTop, 6);
	}
};

class MainWindow final : public juce::DocumentWindow {
public:
	MainWindow(juce::String Name)
		: DocumentWindow(std::move(Name), juce::Colours::black,
						 DocumentWindow::allButtons) {
		setUsingNativeTitleBar(true);
		setContentOwned(new MainComponent(), true);
		centreWithSize(getWidth(), getHeight());
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
