#pragma once

#include "Core/Identifier.hpp"
#include "JuceHeader.h"
#include "UI/Session/PhotoSessionTypes.hpp"
#include "UI/View/Primitives/Previews.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

namespace shuba::localization {
class Localization;
}

namespace shuba::ui {
struct StagedPhotoCardEntry final {
	PendingPhotoSource source;
	juce::Image image;
	juce::String placeholder;
	PreviewImageVisualState state{PreviewImageVisualState::Staged};
	bool main_after_save{};
	bool can_set_main_after_save{};
};

struct CurrentPhotoCardEntry final {
	core::StableIdentifier photo_id;
	juce::Image image;
	juce::String title;
	juce::String placeholder;
	PreviewImageVisualState state{PreviewImageVisualState::Loading};
	bool is_main{};
	bool delete_confirmation_requested{};
};

struct ManagedPhotoDeckModel final {
	std::vector<CurrentPhotoCardEntry> current_entries;
	std::vector<StagedPhotoCardEntry> staged_entries;
	bool staged_selected{};
	std::size_t selected_index{};
};

struct ManagedPhotoDeckHandlers final {
	std::function<void(std::size_t)> select_current;
	std::function<void(std::size_t)> select_staged;
	std::function<void()> add_staged;
	std::function<void()> clear_staged;
	std::function<void(std::size_t)> remove_staged;
	std::function<void(core::StableIdentifier)> set_main_current;
	std::function<void(std::size_t)> set_main_staged;
	std::function<void(core::StableIdentifier)> request_delete_current;
	std::function<void(core::StableIdentifier)> confirm_delete_current;
	std::function<void()> cancel_delete_current;
};

class ManagedPhotoDeckSelectorComponent;

class ManagedPhotoDeckComponent final : public juce::Component {
public:
	ManagedPhotoDeckComponent(ManagedPhotoDeckModel model_value,
							  ManagedPhotoDeckHandlers handlers_value,
							  localization::Localization& localization_value);
	~ManagedPhotoDeckComponent() override;

	void resized() override;
	void paint(juce::Graphics& graphics) override;

private:
	struct Layout final {
		juce::Rectangle<int> header;
		juce::Rectangle<int> image;
		juce::Rectangle<int> caption;
		juce::Rectangle<int> controls;
		juce::Rectangle<int> secondary_controls;
		juce::Rectangle<int> selector;
	};

	[[nodiscard]] Layout calculate_layout() const;
	[[nodiscard]] bool selected_staged() const noexcept;
	[[nodiscard]] std::size_t selected_index() const noexcept;
	[[nodiscard]] bool has_photo_entries() const noexcept;
	[[nodiscard]] const CurrentPhotoCardEntry* selected_current_entry() const;
	[[nodiscard]] const StagedPhotoCardEntry* selected_staged_entry() const;
	void refresh_button_state();
	void select_relative(int direction);

	ManagedPhotoDeckModel model;
	ManagedPhotoDeckHandlers handlers;
	localization::Localization& localization;
	juce::TextButton current_button;
	juce::TextButton staged_button;
	juce::TextButton add_button;
	juce::TextButton clear_button;
	juce::TextButton previous_button;
	juce::TextButton next_button;
	juce::TextButton set_main_button;
	juce::TextButton delete_button;
	juce::TextButton cancel_delete_button;
	juce::TextButton remove_staged_button;
	std::unique_ptr<ManagedPhotoDeckSelectorComponent> selector;
};
}	 // namespace shuba::ui
