#pragma once

#include "Catalog/PhotoExport.hpp"
#include "Catalog/Search.hpp"
#include "Core/Identifier.hpp"
#include "Domain/Domain.hpp"
#include "UI/Session/BackupRecoveryTypes.hpp"
#include "UI/Session/EntityEditTypes.hpp"
#include "UI/Session/PhotoSessionTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace shuba::ui {
enum class RootDestination : std::uint8_t {
	Catalog,
	Storages,
	Add,
	More,
	ItemDetail,
	StorageDetail,
	ItemForm,
	StorageForm,
	PhotoViewer,
	BackupRecovery,
};

enum class FormMode : std::uint8_t {
	Create,
	Edit,
};

struct AppShellManagedPhotoDeckState final {
	bool staged_selected{};
	std::size_t selected_index{};
	std::optional<std::size_t> staged_main_index;
};

struct AppShellRouteState final {
	RootDestination destination{RootDestination::Catalog};
	std::optional<core::StableIdentifier> selected_item_id;
	std::optional<core::StableIdentifier> selected_storage_id;
	std::optional<domain::PhotoOwner> selected_photo_owner;
	std::optional<core::StableIdentifier> selected_photo_id;
	std::optional<RootDestination> form_return_destination;
};

struct AppShellCatalogFilterState final {
	catalog::CatalogSearchFilters applied;
	catalog::CatalogSearchFilters draft;
	bool panel_visible{};
};

struct AppShellItemFormState final {
	ItemDraft draft;
	FormMode mode{FormMode::Create};
	std::vector<PendingPhotoSource> pending_photos;
	AppShellManagedPhotoDeckState photo_deck;
	bool storage_candidates_expanded{};
	bool tag_candidates_expanded{};
	bool listing_expanded{};
	bool finance_expanded{};
};

struct AppShellStorageFormState final {
	StorageDraft draft;
	FormMode mode{FormMode::Create};
	std::vector<PendingPhotoSource> pending_photos;
	AppShellManagedPhotoDeckState photo_deck;
	bool parent_candidates_expanded{};
	bool tag_candidates_expanded{};
	bool archive_warning_acknowledged{};
};

struct AppShellFeedbackState final {
	std::string edit_message;
	std::vector<EntityEditDiagnostic> edit_diagnostics;
	std::string photo_message;
	std::vector<core::Diagnostic> photo_diagnostics;
	std::string backup_message;
	std::vector<core::Diagnostic> backup_diagnostics;
};

struct AppShellBackupState final {
	std::optional<catalog::BackupImportStagingResult> pending_import_staging;
	bool pending_import_degraded_acknowledged{};
};

struct AppShellPhotoDisplayState final {
	catalog::PhotoDisplayResult result;
	std::optional<core::StableIdentifier> displayed_photo_id;
	std::optional<core::StableIdentifier> requested_display_photo_id;
	std::optional<core::StableIdentifier> pending_delete_photo_id;
	std::optional<core::StableIdentifier> viewer_transform_photo_id;
	std::uint64_t display_request_generation{};
	int viewer_rotation_quarter_turns{};
};

struct AppShellStorageDetailState final {
	bool include_nested{true};
};
}	 // namespace shuba::ui
