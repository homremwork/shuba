#include "UI/View/Primitives/PhotoManagement.hpp"

#include "UI/View/Primitives/Palette.hpp"

#include <algorithm>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace shuba::ui {
namespace {
[[nodiscard]] std::string compact_byte_summary(
	const std::optional<std::uint64_t>& byte_count) {
	if (!byte_count.has_value())
		return "size unknown";
	if (*byte_count < 1024U)
		return std::to_string(*byte_count) + " B";
	if (*byte_count < 1024U * 1024U)
		return std::to_string(*byte_count / 1024U) + " KB";
	return std::to_string(*byte_count / (1024U * 1024U)) + " MB";
}

[[nodiscard]] std::string pending_photo_card_text(
	const PendingPhotoSource& source, std::size_t display_index) {
	std::string text = "#" + std::to_string(display_index) + " · "
					   + std::string{to_string(source.status)};
	if (!source.display_name.empty())
		text += " · " + source.display_name;
	text += " · " + compact_byte_summary(source.byte_count);
	if (!source.diagnostics.empty())
		text += " · " + std::to_string(source.diagnostics.size()) + " issue(s)";
	return text;
}

[[nodiscard]] juce::String current_photo_card_text(
	const CurrentPhotoCardEntry& entry, std::size_t display_index) {
	juce::String text =
		entry.title.isEmpty()
			? juce::String{"Photo "}
				  + juce::String(static_cast<int>(display_index))
			: entry.title;
	if (entry.is_main)
		text += " · main";
	return text;
}

[[nodiscard]] bool managed_deck_selects_staged(
	const ManagedPhotoDeckModel& model) noexcept {
	if (model.staged_selected && !model.staged_entries.empty())
		return true;
	if (!model.staged_selected && !model.current_entries.empty())
		return false;
	return model.current_entries.empty() && !model.staged_entries.empty();
}

[[nodiscard]] std::size_t managed_deck_selected_index(
	const ManagedPhotoDeckModel& model) noexcept {
	const bool staged_selected		 = managed_deck_selects_staged(model);
	const std::size_t selected_count = staged_selected
										   ? model.staged_entries.size()
										   : model.current_entries.size();
	if (selected_count == 0U)
		return 0U;
	return std::min(model.selected_index, selected_count - 1U);
}
}	 // namespace

class ManagedPhotoDeckSelectorComponent final : public juce::Component {
public:
	ManagedPhotoDeckSelectorComponent(
		std::span<const CurrentPhotoCardEntry> current_entries,
		std::span<const StagedPhotoCardEntry> staged_entries,
		bool staged_selected, std::size_t selected_index,
		std::function<void(std::size_t)> select_current_handler,
		std::function<void(std::size_t)> select_staged_handler)
		: current_count(current_entries.size())
		, staged_count(staged_entries.size())
		, selected_flat_index(staged_selected
								  ? current_entries.size() + selected_index
								  : selected_index)
		, select_current(std::move(select_current_handler))
		, select_staged(std::move(select_staged_handler)) {
		setOpaque(true);
		setBufferedToImage(true);
		const std::size_t total_count = current_count + staged_count;
		if (selected_flat_index >= total_count)
			selected_flat_index = 0U;
		setSize(1, 1);
	}

	void resized() override {}

	void paint(juce::Graphics& graphics) override {
		graphics.fillAll(panel_colour());
		const std::size_t total_count = current_count + staged_count;
		if (total_count == 0U)
			return;

		juce::Rectangle<int> area	   = getLocalBounds().reduced(8, 6);
		juce::Rectangle<int> text_area = area.removeFromTop(22);
		graphics.setColour(text_colour());
		graphics.setFont(juce::FontOptions(13.0f, juce::Font::bold));
		graphics.drawFittedText(summary_text(), text_area,
								juce::Justification::centredLeft, 1, 0.90f);

		juce::Rectangle<int> rail = area.removeFromTop(18).reduced(0, 6);
		graphics.setColour(outline_colour().withAlpha(0.52f));
		graphics.fillRoundedRectangle(rail.toFloat(), 4.0f);
		const int segment_width =
			std::max(1, rail.getWidth() / static_cast<int>(total_count));
		for (std::size_t index = 0; index < total_count; ++index) {
			juce::Rectangle<int> segment = rail;
			segment.setX(rail.getX() + static_cast<int>(index) * segment_width);
			segment.setWidth(index + 1U == total_count
								 ? rail.getRight() - segment.getX()
								 : segment_width);
			if (index < current_count)
				graphics.setColour(accent_colour().withAlpha(0.42f));
			else
				graphics.setColour(warning_panel_colour().brighter(0.32f));
			graphics.fillRoundedRectangle(segment.reduced(1).toFloat(), 3.0f);
		}
		if (selected_flat_index < total_count) {
			const int marker_width = std::max(8, segment_width);
			const int marker_x	   = std::min(
				rail.getRight() - marker_width,
				rail.getX()
					+ static_cast<int>(selected_flat_index) * segment_width);
			graphics.setColour(text_colour());
			graphics.fillRoundedRectangle(
				juce::Rectangle<int>{marker_x, rail.getY() - 4, marker_width,
									 rail.getHeight() + 8}
					.toFloat(),
				4.0f);
		}
	}

	void mouseUp(const juce::MouseEvent& event) override {
		const std::size_t total_count = current_count + staged_count;
		if (total_count == 0U)
			return;
		juce::Rectangle<int> area = getLocalBounds().reduced(8, 6);
		area.removeFromTop(22);
		juce::Rectangle<int> rail = area.removeFromTop(18).reduced(0, 6);
		if (!rail.contains(event.getPosition()))
			return;
		const int relative_x = std::clamp(event.x - rail.getX(), 0,
										  std::max(0, rail.getWidth() - 1));
		const std::size_t flat_index = std::min(
			total_count - 1U,
			static_cast<std::size_t>(relative_x) * total_count
				/ static_cast<std::size_t>(std::max(1, rail.getWidth())));
		select_flat_index(flat_index);
	}

private:
	[[nodiscard]] juce::String summary_text() const {
		juce::String text;
		if (selected_flat_index < current_count) {
			text = juce::String{"Current "}
				   + juce::String(static_cast<int>(selected_flat_index + 1U))
				   + "/" + juce::String(static_cast<int>(current_count));
		} else {
			const std::size_t staged_index =
				selected_flat_index - current_count;
			text = juce::String{"Staged "}
				   + juce::String(static_cast<int>(staged_index + 1U)) + "/"
				   + juce::String(static_cast<int>(staged_count));
		}
		text += juce::String{" · total "}
				+ juce::String(static_cast<int>(current_count + staged_count));
		return text;
	}

	void select_flat_index(std::size_t flat_index) {
		if (flat_index < current_count) {
			if (select_current)
				select_current(flat_index);
		} else if (select_staged) {
			select_staged(flat_index - current_count);
		}
	}

	std::size_t current_count{};
	std::size_t staged_count{};
	std::size_t selected_flat_index{};
	std::function<void(std::size_t)> select_current;
	std::function<void(std::size_t)> select_staged;
};

ManagedPhotoDeckComponent::ManagedPhotoDeckComponent(
	ManagedPhotoDeckModel model_value, ManagedPhotoDeckHandlers handlers_value)
	: model(std::move(model_value))
	, handlers(std::move(handlers_value))
	, selector(std::make_unique<ManagedPhotoDeckSelectorComponent>(
		  model.current_entries, model.staged_entries, selected_staged(),
		  selected_index(), handlers.select_current, handlers.select_staged)) {
	setOpaque(true);
	setBufferedToImage(true);
	style_text_button(current_button);
	style_text_button(staged_button);
	style_text_button(add_button);
	style_text_button(clear_button);
	style_text_button(previous_button);
	style_text_button(next_button);
	style_text_button(set_main_button);
	style_text_button(delete_button);
	style_text_button(cancel_delete_button);
	style_text_button(remove_staged_button);

	current_button.onClick = [this] {
		if (handlers.select_current)
			handlers.select_current(0U);
	};
	staged_button.onClick = [this] {
		if (handlers.select_staged)
			handlers.select_staged(0U);
	};
	add_button.onClick = [this] {
		if (handlers.add_staged)
			handlers.add_staged();
	};
	clear_button.onClick = [this] {
		if (handlers.clear_staged)
			handlers.clear_staged();
	};
	previous_button.onClick = [this] { select_relative(-1); };
	next_button.onClick		= [this] { select_relative(1); };
	set_main_button.onClick = [this] {
		const CurrentPhotoCardEntry* current_entry = selected_current_entry();
		if (current_entry != nullptr && handlers.set_main_current) {
			handlers.set_main_current(current_entry->photo_id);
			return;
		}
		const StagedPhotoCardEntry* staged_entry = selected_staged_entry();
		if (staged_entry != nullptr && handlers.set_main_staged)
			handlers.set_main_staged(selected_index());
	};
	delete_button.onClick = [this] {
		const CurrentPhotoCardEntry* entry = selected_current_entry();
		if (entry == nullptr)
			return;
		if (entry->delete_confirmation_requested) {
			if (handlers.confirm_delete_current)
				handlers.confirm_delete_current(entry->photo_id);
		} else if (handlers.request_delete_current) {
			handlers.request_delete_current(entry->photo_id);
		}
	};
	cancel_delete_button.onClick = [this] {
		if (handlers.cancel_delete_current)
			handlers.cancel_delete_current();
	};
	remove_staged_button.onClick = [this] {
		if (handlers.remove_staged && selected_staged_entry() != nullptr)
			handlers.remove_staged(selected_index());
	};

	addAndMakeVisible(current_button);
	addAndMakeVisible(staged_button);
	addAndMakeVisible(add_button);
	addAndMakeVisible(clear_button);
	addAndMakeVisible(previous_button);
	addAndMakeVisible(next_button);
	addAndMakeVisible(set_main_button);
	addAndMakeVisible(delete_button);
	addAndMakeVisible(cancel_delete_button);
	addAndMakeVisible(remove_staged_button);
	addAndMakeVisible(*selector);
	refresh_button_state();
}

ManagedPhotoDeckComponent::~ManagedPhotoDeckComponent() {}

ManagedPhotoDeckComponent::Layout ManagedPhotoDeckComponent::calculate_layout()
	const {
	Layout layout;
	juce::Rectangle<int> area = getLocalBounds().reduced(10, 8);
	layout.header			  = area.removeFromTop(32);
	if (!has_photo_entries()) {
		area.removeFromTop(6);
		layout.caption = area;
		return layout;
	}

	area.removeFromTop(6);
	layout.selector = area.removeFromBottom(52);
	area.removeFromBottom(4);
	layout.secondary_controls = area.removeFromBottom(32);
	area.removeFromBottom(4);
	layout.controls = area.removeFromBottom(32);
	area.removeFromBottom(6);
	layout.caption = area.removeFromBottom(38);
	layout.image   = area;
	return layout;
}

bool ManagedPhotoDeckComponent::selected_staged() const noexcept {
	return managed_deck_selects_staged(model);
}

std::size_t ManagedPhotoDeckComponent::selected_index() const noexcept {
	return managed_deck_selected_index(model);
}

bool ManagedPhotoDeckComponent::has_photo_entries() const noexcept {
	return !model.current_entries.empty() || !model.staged_entries.empty();
}

const CurrentPhotoCardEntry* ManagedPhotoDeckComponent::selected_current_entry()
	const {
	if (selected_staged() || model.current_entries.empty())
		return nullptr;
	const std::size_t index = selected_index();
	if (index >= model.current_entries.size())
		return nullptr;
	return &model.current_entries[index];
}

const StagedPhotoCardEntry* ManagedPhotoDeckComponent::selected_staged_entry()
	const {
	if (!selected_staged() || model.staged_entries.empty())
		return nullptr;
	const std::size_t index = selected_index();
	if (index >= model.staged_entries.size())
		return nullptr;
	return &model.staged_entries[index];
}

void ManagedPhotoDeckComponent::refresh_button_state() {
	current_button.setButtonText(
		juce::String{"Current "}
		+ juce::String(static_cast<int>(model.current_entries.size())));
	staged_button.setButtonText(
		juce::String{"Staged "}
		+ juce::String(static_cast<int>(model.staged_entries.size())));
	current_button.setEnabled(!model.current_entries.empty()
							  && selected_staged());
	staged_button.setEnabled(!model.staged_entries.empty()
							 && !selected_staged());
	clear_button.setEnabled(!model.staged_entries.empty()
							&& static_cast<bool>(handlers.clear_staged));

	const std::size_t total_count =
		model.current_entries.size() + model.staged_entries.size();
	const bool can_step = total_count > 1U;
	previous_button.setEnabled(can_step);
	next_button.setEnabled(can_step);

	const CurrentPhotoCardEntry* current = selected_current_entry();
	const StagedPhotoCardEntry* staged	 = selected_staged_entry();
	set_main_button.setVisible(current != nullptr
							   || (staged != nullptr
								   && staged->can_set_main_after_save));
	delete_button.setVisible(current != nullptr);
	cancel_delete_button.setVisible(current != nullptr
									&& current->delete_confirmation_requested);
	remove_staged_button.setVisible(staged != nullptr);
	if (current != nullptr) {
		set_main_button.setButtonText(current->is_main ? "Main" : "Set main");
		set_main_button.setEnabled(
			!current->is_main && static_cast<bool>(handlers.set_main_current));
		delete_button.setButtonText(current->delete_confirmation_requested
										? "Confirm delete"
										: "Delete");
		delete_button.setEnabled(
			current->delete_confirmation_requested
				? static_cast<bool>(handlers.confirm_delete_current)
				: static_cast<bool>(handlers.request_delete_current));
	} else {
		if (staged != nullptr && staged->can_set_main_after_save) {
			set_main_button.setButtonText(staged->main_after_save
										  ? "Main after save"
										  : "Set main");
			set_main_button.setEnabled(!staged->main_after_save
									  && static_cast<bool>(
										  handlers.set_main_staged));
		} else {
			set_main_button.setEnabled(false);
		}
		delete_button.setEnabled(false);
	}
	remove_staged_button.setEnabled(
		staged != nullptr && static_cast<bool>(handlers.remove_staged));
}

void ManagedPhotoDeckComponent::select_relative(int direction) {
	const std::size_t current_count = model.current_entries.size();
	const std::size_t staged_count	= model.staged_entries.size();
	const std::size_t total_count	= current_count + staged_count;
	if (total_count == 0U)
		return;

	std::size_t flat_index =
		selected_staged() ? current_count + selected_index() : selected_index();
	if (flat_index >= total_count)
		flat_index = 0U;
	if (direction < 0)
		flat_index = flat_index == 0U ? total_count - 1U : flat_index - 1U;
	else
		flat_index = (flat_index + 1U) % total_count;

	if (flat_index < current_count) {
		if (handlers.select_current)
			handlers.select_current(flat_index);
	} else {
		if (handlers.select_staged)
			handlers.select_staged(flat_index - current_count);
	}
}

void ManagedPhotoDeckComponent::resized() {
	const Layout layout			= calculate_layout();
	juce::Rectangle<int> header = layout.header;
	clear_button.setBounds(header.removeFromRight(78).reduced(2));
	header.removeFromRight(6);
	add_button.setBounds(header.removeFromRight(104).reduced(2));
	header.removeFromRight(8);
	current_button.setBounds(header.removeFromLeft(116).reduced(2));
	header.removeFromLeft(6);
	staged_button.setBounds(header.removeFromLeft(116).reduced(2));
	if (!has_photo_entries()) {
		previous_button.setBounds(0, 0, 0, 0);
		next_button.setBounds(0, 0, 0, 0);
		set_main_button.setBounds(0, 0, 0, 0);
		delete_button.setBounds(0, 0, 0, 0);
		cancel_delete_button.setBounds(0, 0, 0, 0);
		remove_staged_button.setBounds(0, 0, 0, 0);
		selector->setBounds(0, 0, 0, 0);
		return;
	}

	juce::Rectangle<int> controls = layout.controls;
	const int side_width = std::max(82, std::min(112, controls.getWidth() / 4));
	previous_button.setBounds(controls.removeFromLeft(side_width).reduced(2));
	next_button.setBounds(controls.removeFromRight(side_width).reduced(2));
	juce::Rectangle<int> primary_action = controls.withSizeKeepingCentre(
		std::max(1, std::min(168, controls.getWidth())),
		std::max(1, controls.getHeight()));
	set_main_button.setBounds(primary_action.reduced(2));
	remove_staged_button.setBounds(0, 0, 0, 0);

	juce::Rectangle<int> secondary = layout.secondary_controls;
	if (remove_staged_button.isVisible()) {
		remove_staged_button.setBounds(
			secondary
				.withSizeKeepingCentre(
					std::max(1, std::min(220, secondary.getWidth())),
					std::max(1, secondary.getHeight()))
				.reduced(2));
		delete_button.setBounds(0, 0, 0, 0);
		cancel_delete_button.setBounds(0, 0, 0, 0);
	} else if (cancel_delete_button.isVisible()) {
		delete_button.setBounds(
			secondary.removeFromLeft(std::max(1, secondary.getWidth() / 2))
				.reduced(2));
		cancel_delete_button.setBounds(secondary.reduced(2));
	} else {
		delete_button.setBounds(
			secondary
				.withSizeKeepingCentre(
					std::max(1, std::min(220, secondary.getWidth())),
					std::max(1, secondary.getHeight()))
				.reduced(2));
		cancel_delete_button.setBounds(0, 0, 0, 0);
	}
	if (!delete_button.isVisible())
		delete_button.setBounds(0, 0, 0, 0);

	selector->setBounds(layout.selector);
	if (selector) {
		selector->setSize(std::max(1, layout.selector.getWidth()),
						  std::max(1, layout.selector.getHeight()));
	}
}

void ManagedPhotoDeckComponent::paint(juce::Graphics& graphics) {
	graphics.fillAll(background_colour());
	draw_card_background(graphics, getLocalBounds(), panel_colour(), false);
	const Layout layout = calculate_layout();

	juce::Image image;
	juce::String title;
	juce::String caption;
	juce::String placeholder;
	PreviewImageVisualState state = PreviewImageVisualState::Empty;

	if (const CurrentPhotoCardEntry* current = selected_current_entry()) {
		image = current->image;
		title = current_photo_card_text(*current, selected_index() + 1U);
		caption =
			juce::String{"Stored photo "}
			+ juce::String(static_cast<int>(selected_index() + 1U)) + "/"
			+ juce::String(static_cast<int>(model.current_entries.size()));
		placeholder = current->placeholder;
		state		= current->state;
	} else if (const StagedPhotoCardEntry* staged = selected_staged_entry()) {
		image = staged->image;
		title = juce_text(
			pending_photo_card_text(staged->source, selected_index() + 1U));
		caption = juce::String{"Staged photo "}
				  + juce::String(static_cast<int>(selected_index() + 1U)) + "/"
				  + juce::String(static_cast<int>(model.staged_entries.size()));
		placeholder = staged->placeholder;
		state		= staged->state;
	} else if (selected_staged()) {
		title		= "No staged photos";
		caption		= "Use Add photos to stage images before saving.";
		placeholder = "No staged photos yet.";
	} else {
		title		= "No current photos";
		caption		= "Stored owner photos will appear here.";
		placeholder = "No current owner photos yet.";
	}
	if (!has_photo_entries()) {
		juce::Rectangle<int> caption_area = layout.caption.reduced(4, 0);
		graphics.setColour(text_colour());
		graphics.setFont(juce::FontOptions(15.0f, juce::Font::bold));
		graphics.drawFittedText(title, caption_area.removeFromTop(22),
								juce::Justification::centredLeft, 1, 0.90f);
		graphics.setColour(muted_text_colour());
		graphics.setFont(juce::FontOptions(12.5f, juce::Font::plain));
		graphics.drawFittedText(caption, caption_area,
								juce::Justification::centredLeft, 2, 0.90f);
		return;
	}

	draw_preview_image_slot(graphics, layout.image, image, placeholder, state,
							false);
	juce::Rectangle<int> caption_area = layout.caption.reduced(4, 0);
	graphics.setColour(text_colour());
	graphics.setFont(juce::FontOptions(15.0f, juce::Font::bold));
	graphics.drawFittedText(title, caption_area.removeFromTop(20),
							juce::Justification::centredLeft, 1, 0.90f);
	graphics.setColour(muted_text_colour());
	graphics.setFont(juce::FontOptions(12.5f, juce::Font::plain));
	graphics.drawFittedText(caption, caption_area,
							juce::Justification::centredLeft, 1, 0.90f);
}
}	 // namespace shuba::ui
