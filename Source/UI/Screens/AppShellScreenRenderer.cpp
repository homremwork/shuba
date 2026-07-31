#include "UI/Screens/AppShellScreenRenderer.hpp"

#include "Localization/Facade.hpp"
#include "UI/Session/PhotoSession.hpp"
#include "UI/View/AppShellContentComponent.hpp"
#include "UI/View/ScreenText.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

namespace shuba::ui {
namespace {
constexpr ImagePreviewSize list_preview_target_size{.max_width	= 96U,
													.max_height = 96U};
constexpr ImagePreviewSize compact_storage_preview_target_size{
	.max_width = 128U, .max_height = 128U};
constexpr ImagePreviewSize detail_preview_target_size{.max_width  = 640U,
													  .max_height = 420U};
constexpr ImagePreviewSize edit_photo_deck_preview_target_size{
	.max_width = 640U, .max_height = 420U};
constexpr int detail_carousel_height						   = 312;
constexpr int compact_photo_deck_height						   = 86;
constexpr int populated_photo_deck_height					   = 410;
constexpr std::size_t maximum_immediate_current_photo_previews = 3U;
void append_summary_part(std::string& text, std::string_view part) {
	if (part.empty())
		return;
	if (!text.empty())
		text += " · ";
	text += part;
}

[[nodiscard]] juce::String representative_broken_placeholder(
	catalog::PhotoPresenceState presence) {
	if (presence == catalog::PhotoPresenceState::OnlyBrokenPhotos)
		return "Photo media needs attention.";
	if (presence == catalog::PhotoPresenceState::MixedUsableAndBrokenPhotos)
		return "Representative photo needs attention.";
	return "Photo preview is unavailable.";
}
}	 // namespace
AppShellScreenRenderer::AppShellScreenRenderer(Dependencies dependencies)
	: session(dependencies.session)
	, route(dependencies.route)
	, catalog_filter_state(dependencies.catalog_filter_state)
	, item_form(dependencies.item_form)
	, storage_form(dependencies.storage_form)
	, feedback(dependencies.feedback)
	, backup(dependencies.backup)
	, photo_display(dependencies.photo_display)
	, storage_detail(dependencies.storage_detail)
	, preview_cache(dependencies.preview_cache)
	, edit_identifiers(dependencies.edit_identifiers)
	, edit_clock(dependencies.edit_clock)
	, ui_operation_gate(dependencies.ui_operation_gate)
	, internal_photo_codec(dependencies.internal_photo_codec)
	, source_decode_service(dependencies.source_decode_service)
	, jpeg_export_service(dependencies.jpeg_export_service)
	, document_export_service(dependencies.document_export_service)
	, last_progress_events(dependencies.last_progress_events)
	, never_cancelled(dependencies.never_cancelled)
	, localization(dependencies.localization)
	, content(&dependencies.content)
	, item_name_editor(dependencies.editors.item_name_editor)
	, item_category_editor(dependencies.editors.item_category_editor)
	, item_notes_editor(dependencies.editors.item_notes_editor)
	, item_listing_marketplace_editor(
		  dependencies.editors.item_listing_marketplace_editor)
	, item_listing_url_editor(dependencies.editors.item_listing_url_editor)
	, item_listing_note_editor(dependencies.editors.item_listing_note_editor)
	, item_acquisition_source_editor(
		  dependencies.editors.item_acquisition_source_editor)
	, storage_name_editor(dependencies.editors.storage_name_editor)
	, storage_type_editor(dependencies.editors.storage_type_editor)
	, storage_location_editor(dependencies.editors.storage_location_editor)
	, storage_notes_editor(dependencies.editors.storage_notes_editor)
	, catalog_query_provider(std::move(dependencies.queries.catalog_query))
	, storage_query_provider(std::move(dependencies.queries.storage_query))
	, select_root_handler(std::move(dependencies.actions.select_root))
	, open_item_detail_handler(std::move(dependencies.actions.open_item_detail))
	, open_storage_detail_handler(
		  std::move(dependencies.actions.open_storage_detail))
	, open_photo_viewer_handler(
		  std::move(dependencies.actions.open_photo_viewer))
	, open_new_item_form_handler(
		  std::move(dependencies.actions.open_new_item_form))
	, open_existing_item_form_handler(
		  std::move(dependencies.actions.open_existing_item_form))
	, open_new_storage_form_handler(
		  std::move(dependencies.actions.open_new_storage_form))
	, open_existing_storage_form_handler(
		  std::move(dependencies.actions.open_existing_storage_form))
	, request_add_photos_handler(
		  std::move(dependencies.actions.request_add_photos))
	, request_add_pending_item_photos_handler(
		  std::move(dependencies.actions.request_add_pending_item_photos))
	, request_add_pending_storage_photos_handler(
		  std::move(dependencies.actions.request_add_pending_storage_photos))
	, request_export_photo_handler(
		  std::move(dependencies.actions.request_export_photo))
	, request_export_backup_handler(
		  std::move(dependencies.actions.request_export_backup))
	, request_export_diagnostic_archive_handler(
		  std::move(dependencies.actions.request_export_diagnostic_archive))
	, request_import_backup_handler(
		  std::move(dependencies.actions.request_import_backup))
	, retry_normal_startup_handler(
		  std::move(dependencies.actions.retry_normal_startup))
	, confirm_staged_backup_import_handler(
		  std::move(dependencies.actions.confirm_staged_backup_import))
	, cleanup_item_pending_photos_handler(
		  std::move(dependencies.actions.cleanup_item_pending_photos))
	, cleanup_storage_pending_photos_handler(
		  std::move(dependencies.actions.cleanup_storage_pending_photos))
	, remove_item_pending_photo_handler(
		  std::move(dependencies.actions.remove_item_pending_photo))
	, remove_storage_pending_photo_handler(
		  std::move(dependencies.actions.remove_storage_pending_photo))
	, set_item_pending_photo_as_main_handler(
		  std::move(dependencies.actions.set_item_pending_photo_as_main))
	, set_storage_pending_photo_as_main_handler(
		  std::move(dependencies.actions.set_storage_pending_photo_as_main))
	, request_delete_photo_handler(
		  std::move(dependencies.actions.request_delete_photo))
	, confirm_delete_photo_handler(
		  std::move(dependencies.actions.confirm_delete_photo))
	, cancel_delete_photo_handler(
		  std::move(dependencies.actions.cancel_delete_photo))
	, apply_catalog_filters_handler(
		  std::move(dependencies.actions.apply_catalog_filters))
	, reset_catalog_filters_handler(
		  std::move(dependencies.actions.reset_catalog_filters))
	, apply_entity_edit_result_handler(
		  std::move(dependencies.actions.apply_entity_edit_result))
	, apply_photo_edit_result_handler(
		  std::move(dependencies.actions.apply_photo_edit_result))
	, request_internal_preview_handler(
		  std::move(dependencies.actions.request_internal_preview))
	, request_staged_preview_handler(
		  std::move(dependencies.actions.request_staged_preview))
	, preview_failure_message_handler(
		  std::move(dependencies.actions.preview_failure_message))
	, request_photo_display_handler(
		  std::move(dependencies.actions.request_photo_display))
	, refresh_all_handler(std::move(dependencies.actions.refresh_all))
	, refresh_content_handler(std::move(dependencies.actions.refresh_content)) {
}

std::string AppShellScreenRenderer::catalog_query() const {
	return catalog_query_provider ? catalog_query_provider() : std::string{};
}

std::string AppShellScreenRenderer::storage_query() const {
	return storage_query_provider ? storage_query_provider() : std::string{};
}

AppShellScreenRenderer::PreviewCardBuildResult
AppShellScreenRenderer::build_item_result_preview_card(
	const catalog::SearchResult& result,
	ImagePreviewRequestPriority preview_priority) {
	PreviewCardBuildResult card;
	card.content.title =
		result.display_title.empty()
			? juce_text(localization.text(
				  localization::MessageId::CatalogPreviewUntitledItem))
			: juce_text(result.display_title);

	const persistence::ItemEnvelope* item =
		catalog::find_item_envelope(session.repository, result.record_id);
	const std::string photo_state =
		localization.photo_presence_label(result.photo_presence);
	const std::string status =
		result.item_status.has_value()
			? localization.item_status_label(*result.item_status)
			: std::string{};
	const std::string details  = first_note_or_tag_summary(item);
	const std::string warnings = warning_summary(result.warnings, localization);
	card.content.subtitle =
		juce_text(localization.item_result_card(localization::ItemResultFields{
			.title =
				result.display_title.empty()
					? std::string_view{localization.text(
						  localization::MessageId::CatalogPreviewUntitledItem)}
					: std::string_view{result.display_title},
			.photo_state = photo_state,
			.category	 = result.category,
			.status		 = status,
			.location	 = result.location_text,
			.details	 = details,
			.warnings	 = warnings}));
	card.content.metadata = juce_text(photo_state);

	apply_representative_preview(result, preview_priority, card);
	return card;
}

AppShellScreenRenderer::PreviewCardBuildResult
AppShellScreenRenderer::build_storage_result_preview_card(
	const catalog::SearchResult& result,
	ImagePreviewRequestPriority preview_priority) {
	PreviewCardBuildResult card;
	card.content.title =
		result.display_title.empty()
			? juce_text(localization.text(
				  localization::MessageId::CatalogPreviewDefaultStorage))
			: juce_text(result.display_title);

	const persistence::StorageEnvelope* storage =
		catalog::find_storage_envelope(session.repository, result.record_id);
	const std::string lifecycle = result.storage_lifecycle_status.has_value()
									  ? localization.storage_lifecycle_label(
											*result.storage_lifecycle_status)
									  : std::string{};
	const std::string details	= first_storage_note_or_tag_summary(storage);
	const std::string warnings = warning_summary(result.warnings, localization);
	card.content.subtitle	   = juce_text(
		localization.storage_result_card(localization::StorageResultFields{
			.title			 = result.display_title.empty()
								   ? std::string_view{localization.text(
										 localization::MessageId::
											 CatalogPreviewDefaultStorage)}
								   : std::string_view{result.display_title},
			.type			 = result.storage_type,
			.lifecycle		 = lifecycle,
			.location		 = result.location_text,
			.direct_children = result.direct_child_count,
			.direct_items	 = result.direct_item_count,
			.nested_items	 = result.nested_item_count,
			.details		 = details,
			.warnings		 = warnings}));
	card.content.metadata =
		juce_text(localization.photo_presence_label(result.photo_presence));

	apply_representative_preview(result, preview_priority, card);
	return card;
}

CompactStorageCardContent
AppShellScreenRenderer::build_storage_result_compact_card(
	const catalog::SearchResult& result,
	ImagePreviewRequestPriority preview_priority) {
	CompactStorageCardContent card;
	card.name =
		result.display_title.empty()
			? juce_text(localization.text(
				  localization::MessageId::CatalogPreviewDefaultStorage))
			: juce_text(result.display_title);
	card.item_count =
		juce_text(localization.item_count(result.nested_item_count));
	apply_representative_preview(result, preview_priority, card);
	return card;
}

AppShellScreenRenderer::PreviewCardBuildResult
AppShellScreenRenderer::build_item_preview_card(
	const persistence::ItemEnvelope& item,
	const catalog::ItemProjection& projection,
	ImagePreviewRequestPriority preview_priority) {
	PreviewCardBuildResult card;
	card.content.title =
		item.record.display_name.empty()
			? juce_text(localization.text(
				  localization::MessageId::CatalogPreviewUntitledItem))
			: juce_text(item.record.display_name);

	std::string subtitle;
	append_summary_part(subtitle, item.record.category);
	append_summary_part(subtitle,
						status_text(item.record.status, localization));
	append_summary_part(subtitle, projection.storage_path_label);
	append_summary_part(subtitle, first_note_or_tag_summary(&item));
	card.content.subtitle = juce_text(subtitle);

	std::string metadata =
		photo_presence_label(projection.photo_presence, localization);
	if (projection.storage_archived)
		append_summary_part(
			metadata, "⚠ "
						  + localization.catalog_warning_label(
							  localization::CatalogWarning::ArchivedStorage));
	if (projection.broken_storage_reference)
		append_summary_part(
			metadata, "⚠ "
						  + localization.catalog_warning_label(
							  localization::CatalogWarning::BrokenStorage));
	card.content.metadata = juce_text(metadata);

	apply_representative_preview(
		projection.photo_presence, projection.representative_photo_id,
		projection.representative_usable_photo_id, preview_priority, card);
	return card;
}

AppShellScreenRenderer::PreviewCardBuildResult
AppShellScreenRenderer::build_storage_preview_card(
	const persistence::StorageEnvelope& storage,
	const catalog::StorageProjection& projection,
	ImagePreviewRequestPriority preview_priority) {
	PreviewCardBuildResult card;
	card.content.title =
		storage.record.display_name.empty()
			? juce_text(localization.text(
				  localization::MessageId::CatalogPreviewDefaultStorage))
			: juce_text(storage.record.display_name);

	std::string subtitle;
	const std::string lifecycle =
		storage_lifecycle_text(storage.record.lifecycle_status, localization);
	const std::string details = first_storage_note_or_tag_summary(&storage);
	const std::string warnings =
		projection.parent_reference_state == domain::ReferenceState::Broken
			? localization.catalog_warning_label(
				  localization::CatalogWarning::BrokenParent)
			: std::string{};
	const std::string location = !projection.path_label.empty()
									 ? projection.path_label
									 : storage.record.location;
	subtitle =
		localization.storage_result_card(localization::StorageResultFields{
			.title	   = storage.record.display_name.empty()
							 ? std::string_view{localization.text(
								   localization::MessageId::
									   CatalogPreviewDefaultStorage)}
							 : std::string_view{storage.record.display_name},
			.type	   = storage.record.storage_type,
			.lifecycle = lifecycle,
			.location  = location,
			.direct_children = projection.direct_child_storage_ids.size(),
			.direct_items	 = projection.direct_item_count,
			.nested_items	 = projection.nested_item_count,
			.details		 = details,
			.warnings		 = warnings});
	card.content.subtitle = juce_text(subtitle);

	card.content.metadata = juce_text(
		photo_presence_label(projection.photo_presence, localization));

	apply_representative_preview(
		projection.photo_presence, projection.representative_photo_id,
		projection.representative_usable_photo_id, preview_priority, card);
	return card;
}

CompactStorageCardContent AppShellScreenRenderer::build_storage_compact_card(
	const persistence::StorageEnvelope& storage,
	const catalog::StorageProjection& projection,
	ImagePreviewRequestPriority preview_priority) {
	CompactStorageCardContent card;
	card.name =
		storage.record.display_name.empty()
			? juce_text(localization.text(
				  localization::MessageId::CatalogPreviewDefaultStorage))
			: juce_text(storage.record.display_name);
	card.item_count =
		juce_text(localization.item_count(projection.nested_item_count));
	apply_representative_preview(
		projection.photo_presence, projection.representative_photo_id,
		projection.representative_usable_photo_id, preview_priority, card);
	return card;
}

void AppShellScreenRenderer::apply_representative_preview(
	const catalog::SearchResult& result,
	ImagePreviewRequestPriority preview_priority,
	PreviewCardBuildResult& card) {
	apply_representative_preview(
		result.photo_presence, result.representative_photo_id,
		result.representative_usable_photo_id, preview_priority, card);
}

void AppShellScreenRenderer::apply_representative_preview(
	const catalog::SearchResult& result,
	ImagePreviewRequestPriority preview_priority,
	CompactStorageCardContent& card) {
	apply_representative_preview(
		result.photo_presence, result.representative_photo_id,
		result.representative_usable_photo_id, preview_priority, card);
}

void AppShellScreenRenderer::apply_representative_preview(
	catalog::PhotoPresenceState photo_presence,
	const std::optional<core::StableIdentifier>& representative_photo_id,
	const std::optional<core::StableIdentifier>& representative_usable_photo_id,
	ImagePreviewRequestPriority preview_priority,
	PreviewCardBuildResult& card) {
	if (!representative_photo_id.has_value()) {
		card.content.state = PreviewImageVisualState::Empty;
		return;
	}

	if (!representative_usable_photo_id.has_value()) {
		card.content.state = PreviewImageVisualState::Broken;
		card.content.placeholder =
			representative_broken_placeholder(photo_presence);
		return;
	}

	const core::StableIdentifier& photo_id = *representative_usable_photo_id;
	const ImagePreviewRenderState preview  = load_internal_preview_image(
		photo_id, list_preview_target_size, preview_priority);
	card.content.image		 = preview.image;
	card.content.state		 = preview.state;
	card.content.placeholder = preview.placeholder;
}

void AppShellScreenRenderer::apply_representative_preview(
	catalog::PhotoPresenceState photo_presence,
	const std::optional<core::StableIdentifier>& representative_photo_id,
	const std::optional<core::StableIdentifier>& representative_usable_photo_id,
	ImagePreviewRequestPriority preview_priority,
	CompactStorageCardContent& card) {
	if (!representative_photo_id.has_value()) {
		card.state = PreviewImageVisualState::Empty;
		return;
	}

	if (!representative_usable_photo_id.has_value()) {
		card.state		 = PreviewImageVisualState::Broken;
		card.placeholder = representative_broken_placeholder(photo_presence);
		return;
	}

	const core::StableIdentifier& photo_id = *representative_usable_photo_id;
	const ImagePreviewRenderState preview  = load_internal_preview_image(
		photo_id, compact_storage_preview_target_size, preview_priority);
	card.image		 = preview.image;
	card.state		 = preview.state;
	card.placeholder = preview.placeholder;
}

AppShellScreenRenderer::ImagePreviewRenderState
AppShellScreenRenderer::load_internal_preview_image(
	const core::StableIdentifier& photo_id, ImagePreviewSize target_size,
	ImagePreviewRequestPriority priority) {
	ImagePreviewRenderState state;
	const ImagePreviewRequestIdentity identity =
		make_internal_photo_preview_identity(photo_id, target_size);
	const platform::ImagePixels* cached_pixels = preview_cache.find(identity);
	if (cached_pixels != nullptr) {
		state.image = juce_image_from_pixels(*cached_pixels);
		state.state = state.image.isValid() ? PreviewImageVisualState::Loaded
											: PreviewImageVisualState::Broken;
		state.placeholder = state.image.isValid()
								? juce::String{}
								: "Cached preview could not be displayed.";
		return state;
	}

	if (preview_failure_message_handler) {
		std::optional<juce::String> failure =
			preview_failure_message_handler(identity);
		if (failure.has_value()) {
			state.state		  = PreviewImageVisualState::Broken;
			state.placeholder = *failure;
			return state;
		}
	}

	if (!session.paths.has_value()) {
		state.state		  = PreviewImageVisualState::Broken;
		state.placeholder = "App-private media paths are unavailable.";
		return state;
	}

	if (request_internal_preview_handler)
		request_internal_preview_handler(photo_id, target_size, priority);
	state.state		  = PreviewImageVisualState::Loading;
	state.placeholder = "Preview loading in background.";
	return state;
}

AppShellScreenRenderer::ImagePreviewRenderState
AppShellScreenRenderer::load_staged_preview_image(
	const PendingPhotoSource& source, ImagePreviewSize target_size,
	ImagePreviewRequestPriority priority) {
	ImagePreviewRenderState state;
	state.state = PreviewImageVisualState::Staged;
	if (!source.ready_for_import() || !source.staged_path.has_value()) {
		state.state		  = PreviewImageVisualState::Broken;
		state.placeholder = "Staged photo is not ready for preview.";
		return state;
	}

	const ImagePreviewRequestIdentity identity =
		make_staged_photo_preview_identity(source, target_size);
	const platform::ImagePixels* cached_pixels = preview_cache.find(identity);
	if (cached_pixels != nullptr) {
		state.image = juce_image_from_pixels(*cached_pixels);
		state.state = state.image.isValid() ? PreviewImageVisualState::Staged
											: PreviewImageVisualState::Broken;
		state.placeholder = state.image.isValid()
								? juce::String{}
								: "Cached preview could not be displayed.";
		return state;
	}
	if (preview_failure_message_handler) {
		std::optional<juce::String> failure =
			preview_failure_message_handler(identity);
		if (failure.has_value()) {
			state.state		  = PreviewImageVisualState::Broken;
			state.placeholder = *failure;
			return state;
		}
	}

	if (request_staged_preview_handler)
		request_staged_preview_handler(source, target_size, priority);
	state.state		  = PreviewImageVisualState::Loading;
	state.placeholder = "Staged preview loading in background.";
	return state;
}

std::vector<StagedPhotoCardEntry>
AppShellScreenRenderer::build_staged_photo_card_entries(
	std::span<const PendingPhotoSource> sources, ImagePreviewSize target_size,
	std::optional<std::size_t> immediate_preview_index,
	bool load_default_previews) {
	std::vector<StagedPhotoCardEntry> entries;
	entries.reserve(sources.size());
	for (std::size_t index = 0; index < sources.size(); ++index) {
		const PendingPhotoSource& source = sources[index];
		StagedPhotoCardEntry entry;
		entry.source		   = source;
		const bool should_load = immediate_preview_index.has_value()
									 ? *immediate_preview_index == index
									 : load_default_previews;
		if (should_load) {
			const ImagePreviewRenderState preview = load_staged_preview_image(
				source, target_size, ImagePreviewRequestPriority::High);
			entry.image		  = preview.image;
			entry.placeholder = preview.placeholder;
			entry.state		  = preview.state;
		} else {
			entry.state		  = PreviewImageVisualState::Loading;
			entry.placeholder = "Select this staged photo to load preview.";
		}
		entries.push_back(std::move(entry));
	}
	return entries;
}

std::vector<CurrentPhotoCardEntry>
AppShellScreenRenderer::build_current_photo_card_entries(
	const domain::PhotoOwner& owner, ImagePreviewSize target_size,
	std::optional<std::size_t> immediate_preview_index,
	bool load_default_previews) {
	std::vector<CurrentPhotoCardEntry> entries;
	const catalog::OwnerPhotoProjection* projection =
		owner_photo_projection(session.repository, owner);
	if (projection == nullptr || projection->ordered_photo_ids.empty())
		return entries;

	entries.reserve(projection->ordered_photo_ids.size());
	std::size_t decode_candidate_count{};
	for (std::size_t photo_index = 0U;
		 photo_index < projection->ordered_photo_ids.size(); ++photo_index) {
		const core::StableIdentifier& photo_id =
			projection->ordered_photo_ids[photo_index];
		const persistence::PhotoEnvelope* photo =
			catalog::find_photo_envelope(session.repository, photo_id);
		CurrentPhotoCardEntry entry{.photo_id = photo_id};
		entry.title	  = photo == nullptr
							? juce::String{"Missing photo record"}
							: juce_text(photo_summary(
								  *photo, photo_index + 1U,
								  projection->ordered_photo_ids.size()));
		entry.is_main = photo != nullptr && photo->record.is_main;
		entry.delete_confirmation_requested =
			photo_display.pending_delete_photo_id.has_value()
			&& *photo_display.pending_delete_photo_id == photo_id;
		const bool usable_photo =
			std::ranges::find(projection->usable_photo_ids, photo_id)
			!= projection->usable_photo_ids.end();
		if (!usable_photo) {
			entry.state		  = PreviewImageVisualState::Broken;
			entry.placeholder = "Photo media needs attention.";
			entries.push_back(std::move(entry));
			continue;
		}

		ImagePreviewRequestPriority priority{ImagePreviewRequestPriority::Low};
		if (immediate_preview_index.has_value()
			&& *immediate_preview_index == photo_index) {
			priority = ImagePreviewRequestPriority::High;
		} else if (load_default_previews
				   && decode_candidate_count
						  < maximum_immediate_current_photo_previews) {
			priority = ImagePreviewRequestPriority::Normal;
		}
		if (!immediate_preview_index.has_value())
			++decode_candidate_count;
		ImagePreviewRenderState preview =
			load_internal_preview_image(photo_id, target_size, priority);
		entry.image		  = preview.image;
		entry.placeholder = preview.placeholder;
		entry.state		  = preview.state;
		entries.push_back(std::move(entry));
	}
	return entries;
}

std::size_t AppShellScreenRenderer::current_photo_count_for_owner(
	const std::optional<domain::PhotoOwner>& owner) const {
	if (!owner.has_value())
		return 0U;
	const catalog::OwnerPhotoProjection* projection =
		owner_photo_projection(session.repository, *owner);
	return projection == nullptr ? 0U : projection->ordered_photo_ids.size();
}

void AppShellScreenRenderer::add_photo_management_deck(
	std::optional<domain::PhotoOwner> owner,
	std::span<const PendingPhotoSource> pending_sources,
	AppShellManagedPhotoDeckState& deck_state,
	std::function<void()> add_staged_handler,
	std::function<void()> clear_staged_handler,
	std::function<void(std::size_t)> remove_staged_handler,
	std::function<void(std::size_t)> set_main_staged_handler) {
	const std::size_t current_count = current_photo_count_for_owner(owner);
	const std::size_t staged_count	= pending_sources.size();
	if (deck_state.staged_main_index.has_value()
		&& (*deck_state.staged_main_index >= staged_count
			|| !pending_sources[*deck_state.staged_main_index]
					.ready_for_import())) {
		deck_state.staged_main_index.reset();
	}
	bool staged_selected = deck_state.staged_selected;
	if (!staged_selected && current_count == 0U && staged_count > 0U)
		staged_selected = true;
	if (staged_selected && staged_count == 0U && current_count > 0U)
		staged_selected = false;
	const std::size_t selected_count =
		staged_selected ? staged_count : current_count;
	const std::size_t selected_index =
		selected_count == 0U
			? 0U
			: std::min(deck_state.selected_index, selected_count - 1U);
	deck_state.staged_selected = staged_selected;
	deck_state.selected_index  = selected_index;

	std::vector<CurrentPhotoCardEntry> current_entries;
	if (owner.has_value()) {
		current_entries = build_current_photo_card_entries(
			*owner, edit_photo_deck_preview_target_size,
			!staged_selected && current_count > 0U
				? std::optional<std::size_t>{selected_index}
				: std::nullopt,
			false);
	}
	std::vector<StagedPhotoCardEntry> staged_entries =
		build_staged_photo_card_entries(
			pending_sources, edit_photo_deck_preview_target_size,
			staged_selected && staged_count > 0U
				? std::optional<std::size_t>{selected_index}
				: std::nullopt,
			false);
	for (std::size_t index = 0; index < staged_entries.size(); ++index) {
		StagedPhotoCardEntry& entry = staged_entries[index];
		entry.main_after_save		= deck_state.staged_main_index.has_value()
									  && *deck_state.staged_main_index == index;
		entry.can_set_main_after_save =
			owner.has_value() && entry.source.ready_for_import();
	}

	ManagedPhotoDeckModel model{.current_entries = std::move(current_entries),
								.staged_entries	 = std::move(staged_entries),
								.staged_selected = staged_selected,
								.selected_index	 = selected_index};
	ManagedPhotoDeckHandlers handlers{
		.select_current =
			[this, &deck_state](std::size_t index) {
		deck_state.staged_selected = false;
		deck_state.selected_index  = index;
		refresh_content();
	},
		.select_staged =
			[this, &deck_state](std::size_t index) {
		deck_state.staged_selected = true;
		deck_state.selected_index  = index;
		refresh_content();
	},
		.add_staged	   = std::move(add_staged_handler),
		.clear_staged  = std::move(clear_staged_handler),
		.remove_staged = std::move(remove_staged_handler),
		.set_main_current =
			[this, &deck_state](const core::StableIdentifier& photo_id) {
		deck_state.staged_main_index.reset();
		EntityEditResult result = set_main_photo_in_session(
			EntityEditRequest{.current_session = session,
							  .identifiers	   = edit_identifiers,
							  .clock		   = edit_clock},
			photo_id);
		apply_photo_edit_result(std::move(result), photo_id);
	},
		.set_main_staged =
			[this, &deck_state,
			 set_main_staged_handler = std::move(set_main_staged_handler)](
				std::size_t index) mutable {
		deck_state.staged_main_index = index;
		if (set_main_staged_handler)
			set_main_staged_handler(index);
		else
			refresh_content();
	},
		.request_delete_current =
			[this](const core::StableIdentifier& photo_id) {
		request_delete_photo(photo_id);
	},
		.confirm_delete_current =
			[this](const core::StableIdentifier& photo_id) {
		confirm_delete_photo(photo_id);
	},
		.cancel_delete_current = [this] { cancel_delete_photo(); }};

	const bool has_any_photos = current_count > 0U || staged_count > 0U;
	content->add_managed_photo_deck(std::move(model), std::move(handlers),
									localization,
									has_any_photos ? populated_photo_deck_height
												   : compact_photo_deck_height);
}

std::optional<core::StableIdentifier>
AppShellScreenRenderer::selected_photo_id_for_owner(
	const domain::PhotoOwner& owner) const {
	const catalog::OwnerPhotoProjection* projection =
		owner_photo_projection(session.repository, owner);
	if (projection == nullptr || projection->ordered_photo_ids.empty())
		return std::nullopt;

	if (route.selected_photo_owner.has_value()
		&& *route.selected_photo_owner == owner && route.selected_photo_id
		&& find_photo_index_in_order(projection->ordered_photo_ids,
									 *route.selected_photo_id)) {
		return route.selected_photo_id;
	}

	return first_viewable_photo_id(session.repository, owner);
}

std::optional<core::StableIdentifier>
AppShellScreenRenderer::selected_usable_photo_id_for_owner(
	const domain::PhotoOwner& owner) const {
	const catalog::OwnerPhotoProjection* projection =
		owner_photo_projection(session.repository, owner);
	if (projection == nullptr)
		return std::nullopt;

	std::optional<core::StableIdentifier> selected_photo_id =
		selected_photo_id_for_owner(owner);
	if (selected_photo_id.has_value()
		&& std::ranges::find(projection->usable_photo_ids, *selected_photo_id)
			   != projection->usable_photo_ids.end()) {
		return selected_photo_id;
	}

	return projection->representative_usable_photo_id;
}

void AppShellScreenRenderer::add_owner_photo_carousel(
	const domain::PhotoOwner& owner, catalog::PhotoPresenceState photo_presence,
	juce::String empty_title, juce::String empty_caption) {
	const catalog::OwnerPhotoProjection* projection =
		owner_photo_projection(session.repository, owner);
	if (projection == nullptr || projection->ordered_photo_ids.empty()) {
		PhotoCarouselSlide slide;
		slide.title	  = std::move(empty_title);
		slide.caption = std::move(empty_caption);
		slide.placeholder =
			localization.text(localization::MessageId::PreviewPlaceholderEmpty);
		slide.state = PreviewImageVisualState::Empty;
		slide.action_label =
			juce_text(localization.text(localization::MessageId::PhotoAdd));
		slide.action_handler = [this, owner] { request_add_photos(owner); };
		std::vector<PhotoCarouselSlide> slides;
		slides.push_back(std::move(slide));
		content->add_photo_carousel(std::move(slides), 0U, localization, {},
									[this, owner] {
			request_add_photos(owner);
		}, detail_carousel_height);
		return;
	}

	const std::optional<core::StableIdentifier> selected_photo_id =
		selected_photo_id_for_owner(owner);
	const std::optional<std::size_t> selected_index =
		selected_photo_id.has_value()
			? find_photo_index_in_order(projection->ordered_photo_ids,
										*selected_photo_id)
			: std::nullopt;
	const std::size_t carousel_index = selected_index.value_or(0U);
	std::vector<PhotoCarouselSlide> slides;
	slides.reserve(projection->ordered_photo_ids.size());
	const std::string owner_text = owner_caption(session.repository, owner);
	for (std::size_t slide_index = 0U;
		 slide_index < projection->ordered_photo_ids.size(); ++slide_index) {
		const core::StableIdentifier& photo_id =
			projection->ordered_photo_ids[slide_index];
		const persistence::PhotoEnvelope* photo =
			catalog::find_photo_envelope(session.repository, photo_id);
		PhotoCarouselSlide slide;
		if (photo != nullptr) {
			slide.title =
				juce_text(photo_summary(*photo, slide_index + 1U,
										projection->ordered_photo_ids.size()));
		} else {
			slide.title = "Missing photo record";
		}
		slide.caption =
			juce_text(owner_text
					  + " · Tap preview to open viewer. Swipe or use buttons.");
		slide.placeholder = "Preview loads when this photo is selected.";
		slide.state		  = PreviewImageVisualState::Loading;
		const bool usable_photo =
			std::ranges::find(projection->usable_photo_ids, photo_id)
			!= projection->usable_photo_ids.end();
		if (!usable_photo) {
			slide.state = PreviewImageVisualState::Broken;
			slide.placeholder =
				representative_broken_placeholder(photo_presence);
		} else if (slide_index == carousel_index) {
			ImagePreviewRenderState preview = load_internal_preview_image(
				photo_id, detail_preview_target_size,
				ImagePreviewRequestPriority::High);
			slide.image		  = preview.image;
			slide.placeholder = preview.placeholder;
			slide.state		  = preview.state;
		} else {
			const bool adjacent =
				projection->ordered_photo_ids.size() > 1U
				&& (slide_index + 1U == carousel_index
					|| slide_index == carousel_index + 1U
					|| (carousel_index == 0U
						&& slide_index + 1U
							   == projection->ordered_photo_ids.size())
					|| (slide_index == 0U
						&& carousel_index + 1U
							   == projection->ordered_photo_ids.size()));
			if (adjacent) {
				ImagePreviewRenderState preview = load_internal_preview_image(
					photo_id, detail_preview_target_size,
					ImagePreviewRequestPriority::Normal);
				slide.image		  = preview.image;
				slide.placeholder = preview.placeholder;
				slide.state		  = preview.state;
			}
		}
		slides.push_back(std::move(slide));
	}

	content->add_photo_carousel(
		std::move(slides), carousel_index, localization,
		[this, owner](std::size_t selected_index_value) {
		const catalog::OwnerPhotoProjection* current_projection =
			owner_photo_projection(session.repository, owner);
		if (current_projection == nullptr
			|| selected_index_value
				   >= current_projection->ordered_photo_ids.size()) {
			return;
		}
		route.selected_photo_owner = owner;
		route.selected_photo_id =
			current_projection->ordered_photo_ids[selected_index_value];
		photo_display.displayed_photo_id.reset();
		refresh_all();
	}, [this, owner] {
		open_photo_viewer(owner, selected_photo_id_for_owner(owner));
	}, detail_carousel_height);
}

void AppShellScreenRenderer::select_root(RootDestination destination) {
	if (select_root_handler)
		select_root_handler(destination);
}

void AppShellScreenRenderer::open_item_detail(core::StableIdentifier item_id) {
	if (open_item_detail_handler)
		open_item_detail_handler(std::move(item_id));
}

void AppShellScreenRenderer::open_storage_detail(
	core::StableIdentifier storage_id) {
	if (open_storage_detail_handler)
		open_storage_detail_handler(std::move(storage_id));
}

void AppShellScreenRenderer::open_photo_viewer(
	const domain::PhotoOwner& owner,
	const std::optional<core::StableIdentifier>& requested_photo_id) {
	if (open_photo_viewer_handler)
		open_photo_viewer_handler(owner, requested_photo_id);
}

void AppShellScreenRenderer::open_new_item_form(
	std::optional<core::StableIdentifier> storage_id) {
	if (open_new_item_form_handler)
		open_new_item_form_handler(std::move(storage_id));
}

void AppShellScreenRenderer::open_existing_item_form(
	core::StableIdentifier item_id) {
	if (open_existing_item_form_handler)
		open_existing_item_form_handler(std::move(item_id));
}

void AppShellScreenRenderer::open_new_storage_form(
	std::optional<core::StableIdentifier> parent_id) {
	if (open_new_storage_form_handler)
		open_new_storage_form_handler(std::move(parent_id));
}

void AppShellScreenRenderer::open_existing_storage_form(
	core::StableIdentifier storage_id) {
	if (open_existing_storage_form_handler)
		open_existing_storage_form_handler(std::move(storage_id));
}

void AppShellScreenRenderer::request_add_photos(
	const domain::PhotoOwner& owner) {
	if (request_add_photos_handler)
		request_add_photos_handler(owner);
}

void AppShellScreenRenderer::request_add_pending_item_photos() {
	if (request_add_pending_item_photos_handler)
		request_add_pending_item_photos_handler();
}

void AppShellScreenRenderer::request_add_pending_storage_photos() {
	if (request_add_pending_storage_photos_handler)
		request_add_pending_storage_photos_handler();
}

void AppShellScreenRenderer::request_export_photo(
	const core::StableIdentifier& photo_id) {
	if (request_export_photo_handler)
		request_export_photo_handler(photo_id);
}

void AppShellScreenRenderer::request_export_backup() {
	if (request_export_backup_handler)
		request_export_backup_handler();
}

void AppShellScreenRenderer::request_export_diagnostic_archive() {
	if (request_export_diagnostic_archive_handler)
		request_export_diagnostic_archive_handler();
}

void AppShellScreenRenderer::request_import_backup() {
	if (request_import_backup_handler)
		request_import_backup_handler();
}

void AppShellScreenRenderer::retry_normal_startup() {
	if (retry_normal_startup_handler)
		retry_normal_startup_handler();
}

void AppShellScreenRenderer::confirm_staged_backup_import() {
	if (confirm_staged_backup_import_handler)
		confirm_staged_backup_import_handler();
}

void AppShellScreenRenderer::cleanup_item_pending_photos() {
	if (cleanup_item_pending_photos_handler)
		cleanup_item_pending_photos_handler();
}

void AppShellScreenRenderer::cleanup_storage_pending_photos() {
	if (cleanup_storage_pending_photos_handler)
		cleanup_storage_pending_photos_handler();
}

void AppShellScreenRenderer::remove_item_pending_photo(
	std::size_t pending_photo_index) {
	if (remove_item_pending_photo_handler)
		remove_item_pending_photo_handler(pending_photo_index);
}

void AppShellScreenRenderer::remove_storage_pending_photo(
	std::size_t pending_photo_index) {
	if (remove_storage_pending_photo_handler)
		remove_storage_pending_photo_handler(pending_photo_index);
}

void AppShellScreenRenderer::set_item_pending_photo_as_main(
	std::size_t pending_photo_index) {
	if (set_item_pending_photo_as_main_handler)
		set_item_pending_photo_as_main_handler(pending_photo_index);
}

void AppShellScreenRenderer::set_storage_pending_photo_as_main(
	std::size_t pending_photo_index) {
	if (set_storage_pending_photo_as_main_handler)
		set_storage_pending_photo_as_main_handler(pending_photo_index);
}

void AppShellScreenRenderer::request_delete_photo(
	const core::StableIdentifier& photo_id) {
	if (request_delete_photo_handler)
		request_delete_photo_handler(photo_id);
}

void AppShellScreenRenderer::confirm_delete_photo(
	const core::StableIdentifier& photo_id) {
	if (confirm_delete_photo_handler)
		confirm_delete_photo_handler(photo_id);
}

void AppShellScreenRenderer::cancel_delete_photo() {
	if (cancel_delete_photo_handler)
		cancel_delete_photo_handler();
}

void AppShellScreenRenderer::apply_catalog_filters() {
	if (apply_catalog_filters_handler)
		apply_catalog_filters_handler();
}

void AppShellScreenRenderer::reset_catalog_filters() {
	if (reset_catalog_filters_handler)
		reset_catalog_filters_handler();
}

void AppShellScreenRenderer::apply_entity_edit_result(EntityEditResult result) {
	if (apply_entity_edit_result_handler)
		apply_entity_edit_result_handler(std::move(result));
}

void AppShellScreenRenderer::apply_photo_edit_result(
	EntityEditResult result, const core::StableIdentifier& selected_photo_id) {
	if (apply_photo_edit_result_handler)
		apply_photo_edit_result_handler(std::move(result), selected_photo_id);
}

void AppShellScreenRenderer::refresh_all() {
	if (refresh_all_handler)
		refresh_all_handler();
}

void AppShellScreenRenderer::refresh_content() {
	if (refresh_content_handler)
		refresh_content_handler();
}
}	 // namespace shuba::ui
