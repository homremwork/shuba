#pragma once

#include "UI/AppShell/State.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <string>

namespace shuba::localization {
class Localization;
}

namespace shuba::ui {
class ChromeComponent final : public juce::Component {
public:
	struct Callbacks final {
		std::function<void()> catalog_search_changed;
		std::function<void()> storage_search_changed;
		std::function<void()> catalog_clear;
		std::function<void()> catalog_filter;
		std::function<void()> catalog_apply_filters;
		std::function<void()> catalog_clear_filters;
		std::function<void()> catalog_close_filters;
		std::function<void()> storage_clear;
		std::function<void()> form_cancel;
		std::function<void()> form_save;
		std::function<void()> select_catalog;
		std::function<void()> select_storages;
		std::function<void()> select_add;
		std::function<void()> select_more;
	};

	struct Model final {
		RootDestination destination{RootDestination::Catalog};
		FormMode item_form_mode{FormMode::Create};
		FormMode storage_form_mode{FormMode::Create};
		juce::String title;
		juce::String status;
		juce::String catalog_draft_result_count;
		bool session_fatal{};
		bool shell_operation_active{};
		bool catalog_filters_active{};
		bool catalog_filter_panel_visible{};
	};

	ChromeComponent(Callbacks callbacks,
							localization::Localization& localization_value);

	void update_model(const Model& model);
	[[nodiscard]] juce::Rectangle<int> layout_shell(
		juce::Rectangle<int> bounds);
	[[nodiscard]] std::string catalog_query() const;
	[[nodiscard]] std::string storage_query() const;
	void clear_catalog_query_without_notification();
	void clear_storage_query_without_notification();
	void release_catalog_search_focus();

	void paint(juce::Graphics& graphics) override;

private:
	Callbacks callbacks;
	localization::Localization& localization;
	Model current_model;
	juce::Label title_label;
	juce::Label status_label;
	juce::Label catalog_draft_count_label;
	juce::TextEditor catalog_search_editor;
	juce::TextEditor storage_search_editor;
	juce::TextButton catalog_clear_button{"Clear"};
	juce::TextButton catalog_filter_button{"Filters"};
	juce::TextButton catalog_apply_filters_button{"Apply"};
	juce::TextButton catalog_clear_filters_button{"Clear filters"};
	juce::TextButton catalog_close_filters_button{"Close"};
	juce::TextButton storage_clear_button{"Clear"};
	juce::TextButton form_cancel_button{"Cancel"};
	juce::TextButton form_save_button{"Save"};
	juce::TextButton catalog_nav_button{"Catalog"};
	juce::TextButton storages_nav_button{"Storages"};
	juce::TextButton add_nav_button{"Add"};
	juce::TextButton more_nav_button{"More"};
};
}	 // namespace shuba::ui
