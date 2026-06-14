#include "Catalog/CatalogRepository.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace shuba::catalog {
namespace {
struct PhotoGroup final {
	domain::PhotoOwnerType owner_type{domain::PhotoOwnerType::Item};
	core::StableIdentifier owner_id;
	std::vector<const domain::PhotoRecord*> photos;
};

struct StoragePathBuildContext final {
	StoragePathBuildContext(
		const CatalogRepositoryState& state_value,
		const std::set<std::string>& cycle_storage_ids_value,
		std::map<std::string, std::vector<std::string>>& path_cache_value)
		: state(state_value)
		, cycle_storage_ids(cycle_storage_ids_value)
		, path_cache(path_cache_value) {}

	const CatalogRepositoryState& state;
	const std::set<std::string>& cycle_storage_ids;
	std::map<std::string, std::vector<std::string>>& path_cache;
};

[[nodiscard]] bool stable_id_less(
	const core::StableIdentifier& left,
	const core::StableIdentifier& right) noexcept {
	return left.value() < right.value();
}

[[nodiscard]] std::string owner_key(domain::PhotoOwnerType owner_type,
									const core::StableIdentifier& owner_id) {
	return std::string{domain::to_string(owner_type)} + ":" + owner_id.value();
}

[[nodiscard]] std::string media_file_name(std::string_view path) {
	const std::size_t slash = path.find_last_of("/\\");
	if (slash == std::string_view::npos)
		return std::string{path};

	return std::string{path.substr(slash + 1U)};
}

[[nodiscard]] bool ends_with(std::string_view text,
							 std::string_view suffix) noexcept {
	return text.size() >= suffix.size()
		   && text.substr(text.size() - suffix.size()) == suffix;
}

[[nodiscard]] std::string without_photo_extension(std::string_view file_name) {
	return std::string{file_name.substr(
		0, file_name.size() - persistence::photo_media_extension.size())};
}

void append_diagnostic(std::vector<DerivedDiagnostic>& diagnostics,
					   DerivedDiagnosticSeverity severity, std::string code,
					   std::string subject_id, std::string message,
					   std::string details = {}) {
	diagnostics.push_back(DerivedDiagnostic{.severity	= severity,
											.code		= std::move(code),
											.subject_id = std::move(subject_id),
											.message	= std::move(message),
											.details	= std::move(details)});
}

[[nodiscard]] bool is_broken_reference_code(std::string_view code) noexcept {
	return code == "broken_item_storage" || code == "broken_storage_parent"
		   || code == "storage_parent_cycle" || code == "missing_photo_owner"
		   || code == "missing_photo_media";
}

[[nodiscard]] const persistence::StorageEnvelope* storage_by_id(
	const CatalogRepositoryState& state, std::string_view id) {
	const std::map<std::string, std::size_t>::const_iterator found =
		state.storage_index_by_id.find(std::string{id});
	if (found == state.storage_index_by_id.end())
		return nullptr;

	return &state.storages[found->second];
}

[[nodiscard]] std::string join_path_segments(
	const std::vector<std::string>& segments) {
	std::string label;
	for (std::size_t index = 0; index < segments.size(); ++index) {
		if (index > 0)
			label += " / ";
		label += segments[index];
	}
	return label;
}

[[nodiscard]] std::vector<std::string> build_storage_path_segments(
	StoragePathBuildContext& context, const std::string& storage_id,
	std::set<std::string>& visiting_ids) {
	const std::map<std::string, std::vector<std::string>>::const_iterator
		cached = context.path_cache.find(storage_id);
	if (cached != context.path_cache.end())
		return cached->second;

	const persistence::StorageEnvelope* storage =
		storage_by_id(context.state, storage_id);
	if (storage == nullptr)
		return {};

	std::vector<std::string> segments;
	if (context.cycle_storage_ids.contains(storage_id)
		|| !storage->record.parent_storage_id
		|| visiting_ids.contains(storage_id)) {
		segments.push_back(storage->record.display_name);
		context.path_cache.emplace(storage_id, segments);
		return segments;
	}

	const std::string parent_id = storage->record.parent_storage_id->value();
	if (context.cycle_storage_ids.contains(parent_id)
		|| storage_by_id(context.state, parent_id) == nullptr) {
		segments.push_back(storage->record.display_name);
		context.path_cache.emplace(storage_id, segments);
		return segments;
	}

	visiting_ids.insert(storage_id);
	segments = build_storage_path_segments(context, parent_id, visiting_ids);
	visiting_ids.erase(storage_id);
	segments.push_back(storage->record.display_name);
	context.path_cache.emplace(storage_id, segments);
	return segments;
}

[[nodiscard]] std::set<std::string> detect_storage_cycle_ids(
	const CatalogRepositoryState& state) {
	std::map<std::string, std::string> existing_parent_by_storage_id;
	for (const persistence::StorageEnvelope& storage : state.storages) {
		if (storage.record.parent_storage_id
			&& state.storage_index_by_id.contains(
				storage.record.parent_storage_id->value())) {
			existing_parent_by_storage_id.emplace(
				storage.record.id.value(),
				storage.record.parent_storage_id->value());
		}
	}

	std::map<std::string, int> color_by_storage_id;
	for (const persistence::StorageEnvelope& storage : state.storages)
		color_by_storage_id.emplace(storage.record.id.value(), 0);

	std::vector<std::string> stack;
	std::set<std::string> cycle_storage_ids;

	std::function<void(const std::string&)> visit = [&](const std::string& id) {
		color_by_storage_id[id] = 1;
		stack.push_back(id);

		const std::map<std::string, std::string>::const_iterator parent =
			existing_parent_by_storage_id.find(id);
		if (parent != existing_parent_by_storage_id.end()) {
			const std::string& parent_id = parent->second;
			const int parent_color		 = color_by_storage_id[parent_id];
			if (parent_color == 0) {
				visit(parent_id);
			} else if (parent_color == 1) {
				const std::vector<std::string>::const_iterator
					first_cycle_entry =
						std::find(stack.cbegin(), stack.cend(), parent_id);
				cycle_storage_ids.insert(first_cycle_entry, stack.cend());
			}
		}

		stack.pop_back();
		color_by_storage_id[id] = 2;
	};

	for (const persistence::StorageEnvelope& storage : state.storages)
		if (color_by_storage_id[storage.record.id.value()] == 0)
			visit(storage.record.id.value());

	return cycle_storage_ids;
}

[[nodiscard]] PhotoPresenceState classify_photo_presence(
	std::size_t ordered_count, std::size_t usable_count) noexcept {
	if (ordered_count == 0U)
		return PhotoPresenceState::NoPhotoRecords;
	if (usable_count == 0U)
		return PhotoPresenceState::OnlyBrokenPhotos;
	if (usable_count == ordered_count)
		return PhotoPresenceState::HasUsablePhotos;
	return PhotoPresenceState::MixedUsableAndBrokenPhotos;
}

[[nodiscard]] PhotoPresenceState classify_photo_presence(
	const std::vector<core::StableIdentifier>& ordered_ids,
	const std::vector<core::StableIdentifier>& usable_ids) noexcept {
	return classify_photo_presence(ordered_ids.size(), usable_ids.size());
}

[[nodiscard]] std::optional<core::StableIdentifier> representative_from_ordered(
	const std::vector<const domain::PhotoRecord*>& ordered,
	const std::set<std::string>& excluded_photo_ids) {
	for (const domain::PhotoRecord* photo : ordered)
		if (photo->is_main && !excluded_photo_ids.contains(photo->id.value()))
			return photo->id;

	for (const domain::PhotoRecord* photo : ordered)
		if (!excluded_photo_ids.contains(photo->id.value()))
			return photo->id;

	return std::nullopt;
}

[[nodiscard]] OwnerPhotoProjection make_owner_photo_projection(
	const PhotoGroup& group, const std::set<std::string>& broken_photo_ids) {
	std::vector<const domain::PhotoRecord*> ordered = group.photos;
	std::sort(
		ordered.begin(), ordered.end(),
		[](const domain::PhotoRecord* left, const domain::PhotoRecord* right) {
		if (left->sort_order != right->sort_order)
			return left->sort_order < right->sort_order;
		if (left->timestamps.created_at != right->timestamps.created_at)
			return left->timestamps.created_at < right->timestamps.created_at;
		return left->id.value() < right->id.value();
	});

	std::vector<core::StableIdentifier> ordered_ids;
	std::vector<core::StableIdentifier> usable_ids;
	std::vector<core::StableIdentifier> broken_ids;
	for (const domain::PhotoRecord* photo : ordered) {
		ordered_ids.push_back(photo->id);
		if (broken_photo_ids.contains(photo->id.value()))
			broken_ids.push_back(photo->id);
		else
			usable_ids.push_back(photo->id);
	}

	std::set<std::int64_t> seen_sort_orders;
	std::size_t main_photo_count   = 0;
	bool has_duplicate_sort_orders = false;
	for (const domain::PhotoRecord* photo : ordered) {
		if (photo->is_main)
			++main_photo_count;
		if (!seen_sort_orders.insert(photo->sort_order).second)
			has_duplicate_sort_orders = true;
	}
	const PhotoPresenceState presence =
		classify_photo_presence(ordered_ids.size(), usable_ids.size());

	return OwnerPhotoProjection{
		.owner_type				 = group.owner_type,
		.owner_id				 = group.owner_id,
		.ordered_photo_ids		 = std::move(ordered_ids),
		.usable_photo_ids		 = std::move(usable_ids),
		.broken_photo_ids		 = std::move(broken_ids),
		.representative_photo_id = representative_from_ordered(ordered, {}),
		.representative_usable_photo_id =
			representative_from_ordered(ordered, broken_photo_ids),
		.presence				   = presence,
		.has_multiple_main_photos  = main_photo_count > 1U,
		.has_duplicate_sort_orders = has_duplicate_sort_orders};
}

[[nodiscard]] OwnerPhotoProjection make_empty_owner_photo_projection(
	domain::PhotoOwnerType owner_type, const core::StableIdentifier& owner_id) {
	return OwnerPhotoProjection{.owner_type = owner_type,
								.owner_id	= owner_id,
								.presence = PhotoPresenceState::NoPhotoRecords};
}

void append_sorted_id(std::vector<core::StableIdentifier>& ids,
					  const core::StableIdentifier& id) {
	ids.push_back(id);
	std::sort(ids.begin(), ids.end(), stable_id_less);
}

[[nodiscard]] std::vector<core::StableIdentifier> collect_nested_descendants(
	const std::map<std::string, StorageProjection>& projections,
	const core::StableIdentifier& storage_id) {
	std::vector<core::StableIdentifier> descendants;
	const std::map<std::string, StorageProjection>::const_iterator found =
		projections.find(storage_id.value());
	if (found == projections.end())
		return descendants;

	for (const core::StableIdentifier& child_id :
		 found->second.direct_child_storage_ids) {
		descendants.push_back(child_id);
		std::vector<core::StableIdentifier> child_descendants =
			collect_nested_descendants(projections, child_id);
		descendants.insert(descendants.end(), child_descendants.begin(),
						   child_descendants.end());
	}

	std::sort(descendants.begin(), descendants.end(), stable_id_less);
	return descendants;
}

[[nodiscard]] std::uint64_t nested_item_count_for_storage(
	const std::map<std::string, StorageProjection>& projections,
	const core::StableIdentifier& storage_id) {
	const std::map<std::string, StorageProjection>::const_iterator found =
		projections.find(storage_id.value());
	if (found == projections.end())
		return 0;

	std::uint64_t count = found->second.direct_item_count;
	for (const core::StableIdentifier& child_id :
		 found->second.direct_child_storage_ids) {
		count += nested_item_count_for_storage(projections, child_id);
	}
	return count;
}

void add_tag_key_hint(std::vector<std::string>& hints,
					  std::set<std::string>& seen_keys,
					  const domain::TagRow& tag) {
	if (!domain::is_tag_key_hint_candidate(tag))
		return;
	if (seen_keys.insert(tag.key).second)
		hints.push_back(tag.key);
}

[[nodiscard]] std::vector<std::string> make_tag_key_hints(
	const CatalogRepositoryState& state) {
	std::vector<std::string> hints;
	std::set<std::string> seen_keys;
	for (const persistence::ItemEnvelope& item : state.items)
		for (const domain::TagRow& tag : item.record.tags)
			add_tag_key_hint(hints, seen_keys, tag);
	for (const persistence::StorageEnvelope& storage : state.storages)
		for (const domain::TagRow& tag : storage.record.tags)
			add_tag_key_hint(hints, seen_keys, tag);
	return hints;
}
}	 // namespace

std::string_view to_string(PhotoPresenceState state) noexcept {
	switch (state) {
		case PhotoPresenceState::HasUsablePhotos:
			return "has usable photos";
		case PhotoPresenceState::NoPhotoRecords:
			return "no photo records";
		case PhotoPresenceState::OnlyBrokenPhotos:
			return "only broken photos";
		case PhotoPresenceState::MixedUsableAndBrokenPhotos:
			return "mixed usable and broken photos";
	}

	return "unknown photo presence state";
}

std::string_view to_string(DerivedDiagnosticSeverity severity) noexcept {
	switch (severity) {
		case DerivedDiagnosticSeverity::Info:
			return "info";
		case DerivedDiagnosticSeverity::Warning:
			return "warning";
		case DerivedDiagnosticSeverity::Error:
			return "error";
	}

	return "unknown derived diagnostic severity";
}

std::string expected_photo_media_file_name(
	const core::StableIdentifier& photo_id) {
	return photo_id.value() + std::string{persistence::photo_media_extension};
}

std::string expected_photo_media_relative_path(
	const core::StableIdentifier& photo_id) {
	return std::string{persistence::photo_media_directory_path} + "/"
		   + expected_photo_media_file_name(photo_id);
}

CatalogRepositoryInput make_catalog_repository_input(
	const persistence::CatalogJsonlLoadResult& load_result,
	CatalogMediaSnapshot media) {
	return CatalogRepositoryInput{.items	= load_result.items.records,
								  .storages = load_result.storages.records,
								  .photos	= load_result.photos.records,
								  .media	= std::move(media)};
}

CatalogRepositoryState build_catalog_repository(CatalogRepositoryInput input) {
	CatalogRepositoryState state{.items	   = std::move(input.items),
								 .storages = std::move(input.storages),
								 .photos   = std::move(input.photos)};

	for (std::size_t index = 0; index < state.items.size(); ++index)
		state.item_index_by_id.emplace(state.items[index].record.id.value(),
									   index);
	for (std::size_t index = 0; index < state.storages.size(); ++index)
		state.storage_index_by_id.emplace(
			state.storages[index].record.id.value(), index);
	for (std::size_t index = 0; index < state.photos.size(); ++index)
		state.photo_index_by_id.emplace(state.photos[index].record.id.value(),
										index);

	std::set<std::string> readable_media_file_names;
	if (!input.media.complete_scan_available)
		for (const persistence::PhotoEnvelope& photo : state.photos)
			readable_media_file_names.insert(
				expected_photo_media_file_name(photo.record.id));

	for (const std::string& media_file :
		 input.media.readable_photo_media_files) {
		const std::string file_name = media_file_name(media_file);
		if (!ends_with(file_name, persistence::photo_media_extension)) {
			state.unexpected_photo_media_files.push_back(media_file);
			append_diagnostic(
				state.diagnostics, DerivedDiagnosticSeverity::Warning,
				"unexpected_photo_media_file", media_file,
				"Photo media directory contains an ignored non-JPEG XL file.");
			continue;
		}
		readable_media_file_names.insert(file_name);
	}

	for (const std::string& file_name : readable_media_file_names) {
		const std::string photo_id = without_photo_extension(file_name);
		if (!state.photo_index_by_id.contains(photo_id)) {
			state.orphan_photo_media_files.push_back(file_name);
			append_diagnostic(
				state.diagnostics, DerivedDiagnosticSeverity::Warning,
				"orphan_photo_media", photo_id,
				"Readable JPEG XL media file has no accepted photo record.",
				file_name);
		}
	}

	std::set<std::string> broken_photo_ids;
	for (const persistence::PhotoEnvelope& photo : state.photos) {
		const std::string expected_file =
			expected_photo_media_file_name(photo.record.id);
		if (!readable_media_file_names.contains(expected_file)) {
			broken_photo_ids.insert(photo.record.id.value());
			append_diagnostic(
				state.diagnostics, DerivedDiagnosticSeverity::Warning,
				"missing_photo_media", photo.record.id.value(),
				"Accepted photo record is missing its expected media file.",
				expected_photo_media_relative_path(photo.record.id));
		}
	}

	std::map<std::string, PhotoGroup> item_photo_groups;
	std::map<std::string, PhotoGroup> storage_photo_groups;
	for (const persistence::ItemEnvelope& item : state.items) {
		item_photo_groups.emplace(
			item.record.id.value(),
			PhotoGroup{.owner_type = domain::PhotoOwnerType::Item,
					   .owner_id   = item.record.id});
	}
	for (const persistence::StorageEnvelope& storage : state.storages) {
		storage_photo_groups.emplace(
			storage.record.id.value(),
			PhotoGroup{.owner_type = domain::PhotoOwnerType::Storage,
					   .owner_id   = storage.record.id});
	}

	for (const persistence::PhotoEnvelope& photo : state.photos) {
		if (photo.record.owner_type == domain::PhotoOwnerType::Item) {
			std::map<std::string, PhotoGroup>::iterator group =
				item_photo_groups.find(photo.record.owner_id.value());
			if (group != item_photo_groups.end()) {
				group->second.photos.push_back(&photo.record);
			} else {
				append_diagnostic(
					state.diagnostics, DerivedDiagnosticSeverity::Warning,
					"missing_photo_owner", photo.record.id.value(),
					"Photo owner item does not exist; photo is not attached.",
					photo.record.owner_id.value());
			}
		} else {
			std::map<std::string, PhotoGroup>::iterator group =
				storage_photo_groups.find(photo.record.owner_id.value());
			if (group != storage_photo_groups.end()) {
				group->second.photos.push_back(&photo.record);
			} else {
				append_diagnostic(
					state.diagnostics, DerivedDiagnosticSeverity::Warning,
					"missing_photo_owner", photo.record.id.value(),
					"Photo owner storage does not exist; photo is not "
					"attached.",
					photo.record.owner_id.value());
			}
		}
	}

	for (const std::pair<const std::string, PhotoGroup>& entry :
		 item_photo_groups) {
		OwnerPhotoProjection projection =
			make_owner_photo_projection(entry.second, broken_photo_ids);
		if (projection.has_multiple_main_photos)
			append_diagnostic(state.diagnostics,
							  DerivedDiagnosticSeverity::Warning,
							  "multiple_main_photos", entry.first,
							  "Owner has multiple main photos; representative "
							  "selection is deterministic.");
		if (projection.has_duplicate_sort_orders)
			append_diagnostic(state.diagnostics,
							  DerivedDiagnosticSeverity::Warning,
							  "duplicate_photo_sort_order", entry.first,
							  "Owner has duplicate photo sort orders; ordering "
							  "uses timestamp and id tie-breakers.");
		state.item_photo_projections.emplace(entry.first,
											 std::move(projection));
	}
	for (const std::pair<const std::string, PhotoGroup>& entry :
		 storage_photo_groups) {
		OwnerPhotoProjection projection =
			make_owner_photo_projection(entry.second, broken_photo_ids);
		if (projection.has_multiple_main_photos)
			append_diagnostic(state.diagnostics,
							  DerivedDiagnosticSeverity::Warning,
							  "multiple_main_photos", entry.first,
							  "Owner has multiple main photos; representative "
							  "selection is deterministic.");
		if (projection.has_duplicate_sort_orders)
			append_diagnostic(state.diagnostics,
							  DerivedDiagnosticSeverity::Warning,
							  "duplicate_photo_sort_order", entry.first,
							  "Owner has duplicate photo sort orders; ordering "
							  "uses timestamp and id tie-breakers.");
		state.storage_photo_projections.emplace(entry.first,
												std::move(projection));
	}

	const std::set<std::string> cycle_storage_ids =
		detect_storage_cycle_ids(state);
	for (const std::string& storage_id : cycle_storage_ids) {
		append_diagnostic(
			state.diagnostics, DerivedDiagnosticSeverity::Warning,
			"storage_parent_cycle", storage_id,
			"Storage parent cycle prevents normal path derivation.");
	}

	std::map<std::string, std::vector<std::string>> path_cache;
	StoragePathBuildContext path_context{state, cycle_storage_ids, path_cache};
	for (const persistence::StorageEnvelope& storage : state.storages) {
		domain::ReferenceState parent_state = domain::ReferenceState::Absent;
		if (storage.record.parent_storage_id) {
			parent_state = state.storage_index_by_id.contains(
							   storage.record.parent_storage_id->value())
							   ? domain::ReferenceState::Resolved
							   : domain::ReferenceState::Broken;
			if (parent_state == domain::ReferenceState::Broken) {
				append_diagnostic(
					state.diagnostics, DerivedDiagnosticSeverity::Warning,
					"broken_storage_parent", storage.record.id.value(),
					"Storage parent reference points to a missing storage.",
					storage.record.parent_storage_id->value());
			}
		}

		std::set<std::string> visiting_path_ids;
		std::vector<std::string> path_segments = build_storage_path_segments(
			path_context, storage.record.id.value(), visiting_path_ids);
		const std::map<std::string, OwnerPhotoProjection>::const_iterator
			photos =
				state.storage_photo_projections.find(storage.record.id.value());
		OwnerPhotoProjection photo_projection =
			photos == state.storage_photo_projections.end()
				? make_empty_owner_photo_projection(
					  domain::PhotoOwnerType::Storage, storage.record.id)
				: photos->second;

		state.storage_projections.emplace(
			storage.record.id.value(),
			StorageProjection{
				.id						= storage.record.id,
				.parent_reference_state = parent_state,
				.parent_storage_id		= storage.record.parent_storage_id,
				.path_segments			= path_segments,
				.path_label				= join_path_segments(path_segments),
				.photo_presence			= photo_projection.presence,
				.representative_photo_id =
					photo_projection.representative_photo_id,
				.representative_usable_photo_id =
					photo_projection.representative_usable_photo_id,
				.has_parent_cycle =
					cycle_storage_ids.contains(storage.record.id.value()),
				.visible_by_default = !domain::is_archived(storage.record)});
	}

	for (const persistence::StorageEnvelope& storage : state.storages) {
		if (!storage.record.parent_storage_id)
			continue;
		if (!state.storage_index_by_id.contains(
				storage.record.parent_storage_id->value()))
			continue;
		if (cycle_storage_ids.contains(storage.record.id.value())
			|| cycle_storage_ids.contains(
				storage.record.parent_storage_id->value()))
			continue;

		std::map<std::string, StorageProjection>::iterator parent =
			state.storage_projections.find(
				storage.record.parent_storage_id->value());
		if (parent != state.storage_projections.end())
			append_sorted_id(parent->second.direct_child_storage_ids,
							 storage.record.id);
	}

	for (const persistence::ItemEnvelope& item : state.items) {
		domain::ReferenceState storage_state = domain::ReferenceState::Absent;
		std::string storage_display_name;
		std::string storage_path_label;
		bool storage_archived = false;
		if (item.record.storage_id) {
			const persistence::StorageEnvelope* storage =
				storage_by_id(state, item.record.storage_id->value());
			if (storage == nullptr) {
				storage_state = domain::ReferenceState::Broken;
				append_diagnostic(
					state.diagnostics, DerivedDiagnosticSeverity::Warning,
					"broken_item_storage", item.record.id.value(),
					"Item storage reference points to a missing storage.",
					item.record.storage_id->value());
			} else {
				storage_state		 = domain::ReferenceState::Resolved;
				storage_display_name = storage->record.display_name;
				storage_archived	 = domain::is_archived(storage->record);
				const std::map<std::string, StorageProjection>::const_iterator
					storage_projection = state.storage_projections.find(
						storage->record.id.value());
				if (storage_projection != state.storage_projections.end())
					storage_path_label = storage_projection->second.path_label;
				std::map<std::string, StorageProjection>::iterator
					mutable_storage = state.storage_projections.find(
						storage->record.id.value());
				if (mutable_storage != state.storage_projections.end())
					++mutable_storage->second.direct_item_count;
			}
		}

		const std::map<std::string, OwnerPhotoProjection>::const_iterator
			photos = state.item_photo_projections.find(item.record.id.value());
		OwnerPhotoProjection photo_projection =
			photos == state.item_photo_projections.end()
				? make_empty_owner_photo_projection(
					  domain::PhotoOwnerType::Item, item.record.id)
				: photos->second;

		state.item_projections.emplace(
			item.record.id.value(),
			ItemProjection{
				.id						 = item.record.id,
				.storage_reference_state = storage_state,
				.storage_id				 = item.record.storage_id,
				.storage_display_name	 = std::move(storage_display_name),
				.storage_path_label		 = std::move(storage_path_label),
				.storage_archived		 = storage_archived,
				.broken_storage_reference =
					storage_state == domain::ReferenceState::Broken,
				.photo_presence = photo_projection.presence,
				.representative_photo_id =
					photo_projection.representative_photo_id,
				.representative_usable_photo_id =
					photo_projection.representative_usable_photo_id,
				.visible_by_default = !domain::is_archived(item.record)});
	}

	for (std::pair<const std::string, StorageProjection>& entry :
		 state.storage_projections) {
		entry.second.nested_descendant_storage_ids = collect_nested_descendants(
			state.storage_projections, entry.second.id);
		entry.second.nested_item_count = nested_item_count_for_storage(
			state.storage_projections, entry.second.id);
	}

	for (const std::pair<const std::string, ItemProjection>& entry :
		 state.item_projections) {
		const persistence::ItemEnvelope* item =
			find_item_envelope(state, entry.second.id);
		if (item == nullptr)
			continue;
		const std::map<std::string, OwnerPhotoProjection>::const_iterator
			photos = state.item_photo_projections.find(entry.first);
		const bool has_broken_photos =
			photos != state.item_photo_projections.end()
			&& !photos->second.broken_photo_ids.empty();
		state.search_projection.items.push_back(ItemSearchProjection{
			.id						  = entry.second.id,
			.display_name			  = item->record.display_name,
			.category				  = item->record.category,
			.status					  = item->record.status,
			.storage_id				  = item->record.storage_id,
			.storage_display_name	  = entry.second.storage_display_name,
			.storage_path_label		  = entry.second.storage_path_label,
			.storage_archived		  = entry.second.storage_archived,
			.broken_storage_reference = entry.second.broken_storage_reference,
			.photo_presence			  = entry.second.photo_presence,
			.has_broken_photos		  = has_broken_photos,
			.representative_photo_id  = entry.second.representative_photo_id,
			.representative_usable_photo_id =
				entry.second.representative_usable_photo_id,
			.visible_by_default = entry.second.visible_by_default});
	}

	for (const std::pair<const std::string, StorageProjection>& entry :
		 state.storage_projections) {
		const persistence::StorageEnvelope* storage =
			find_storage_envelope(state, entry.second.id);
		if (storage == nullptr)
			continue;
		const std::map<std::string, OwnerPhotoProjection>::const_iterator
			photos = state.storage_photo_projections.find(entry.first);
		const bool has_broken_photos =
			photos != state.storage_photo_projections.end()
			&& !photos->second.broken_photo_ids.empty();
		state.search_projection.storages.push_back(StorageSearchProjection{
			.id					= entry.second.id,
			.display_name		= storage->record.display_name,
			.storage_type		= storage->record.storage_type,
			.lifecycle_status	= storage->record.lifecycle_status,
			.parent_path_label	= entry.second.path_label,
			.direct_child_count = entry.second.direct_child_storage_ids.size(),
			.direct_item_count	= entry.second.direct_item_count,
			.nested_item_count	= entry.second.nested_item_count,
			.photo_presence		= entry.second.photo_presence,
			.has_broken_photos	= has_broken_photos,
			.representative_photo_id = entry.second.representative_photo_id,
			.representative_usable_photo_id =
				entry.second.representative_usable_photo_id,
			.visible_by_default = entry.second.visible_by_default});
	}
	state.search_projection.tag_key_hints = make_tag_key_hints(state);

	std::uint64_t broken_reference_count = 0;
	for (const DerivedDiagnostic& diagnostic : state.diagnostics)
		if (is_broken_reference_code(diagnostic.code))
			++broken_reference_count;
	state.recovery_summary = DerivedRecoverySummary{
		.accepted_item_count	= state.items.size(),
		.accepted_storage_count = state.storages.size(),
		.accepted_photo_count	= state.photos.size(),
		.broken_reference_count = broken_reference_count,
		.orphan_media_count		= state.orphan_photo_media_files.size(),
		.diagnostic_count		= state.diagnostics.size()};

	return state;
}

const persistence::ItemEnvelope* find_item_envelope(
	const CatalogRepositoryState& state, const core::StableIdentifier& id) {
	const std::map<std::string, std::size_t>::const_iterator found =
		state.item_index_by_id.find(id.value());
	if (found == state.item_index_by_id.end())
		return nullptr;
	return &state.items[found->second];
}

const persistence::StorageEnvelope* find_storage_envelope(
	const CatalogRepositoryState& state, const core::StableIdentifier& id) {
	const std::map<std::string, std::size_t>::const_iterator found =
		state.storage_index_by_id.find(id.value());
	if (found == state.storage_index_by_id.end())
		return nullptr;
	return &state.storages[found->second];
}

const persistence::PhotoEnvelope* find_photo_envelope(
	const CatalogRepositoryState& state, const core::StableIdentifier& id) {
	const std::map<std::string, std::size_t>::const_iterator found =
		state.photo_index_by_id.find(id.value());
	if (found == state.photo_index_by_id.end())
		return nullptr;
	return &state.photos[found->second];
}
}	 // namespace shuba::catalog
