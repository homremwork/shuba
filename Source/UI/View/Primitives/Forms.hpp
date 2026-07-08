#pragma once

#include "Domain/Domain.hpp"
#include "JuceHeader.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

namespace shuba::ui {
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

class ChipGridComponent final : public juce::Component {
public:
	struct Action final {
		juce::String label;
		bool selected{};
		bool enabled{true};
		std::function<void()> handler;
	};

	ChipGridComponent(juce::String title_value,
					  std::vector<Action> actions_value,
					  int column_count_value);

	[[nodiscard]] static int preferred_height(int action_count,
											  int column_count_value,
											  bool has_title = true) noexcept;

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
		std::size_t row_index_value, const domain::TagRow& tag_value,
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
						const juce::String& first_placeholder,
						const juce::String& second_placeholder);
	~EditorPairComponent() override;

	void resized() override;
	void paint(juce::Graphics& graphics) override;

private:
	static void prepare_editor(juce::TextEditor& editor,
							   const juce::String& placeholder);

	juce::TextEditor& first_editor;
	juce::TextEditor& second_editor;
};
}	 // namespace shuba::ui
