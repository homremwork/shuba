#pragma once

#include "Core/Clock.hpp"
#include "Core/Identifier.hpp"
#include "Core/Result.hpp"
#include "Domain/Domain.hpp"
#include "UI/Session/CatalogSessionState.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace shuba::ui {
struct EntityEditDiagnostic final {
	core::DiagnosticSeverity severity{
		core::DiagnosticSeverity::RecoverableWarning};
	std::string code;
	std::string message;
	std::string technical_details;

	friend bool operator==(const EntityEditDiagnostic&,
						   const EntityEditDiagnostic&) = default;
};

struct EntityEditResult final {
	core::OperationResultCategory category{
		core::OperationResultCategory::Success};
	std::vector<EntityEditDiagnostic> diagnostics;
	CatalogSessionState session;
	std::optional<core::StableIdentifier> saved_record_id;
	bool metadata_changed{};
	bool warning_acknowledgement_required{};

	[[nodiscard]] bool succeeded() const noexcept;
	[[nodiscard]] bool failed() const noexcept;
};

struct ItemDraft final {
	std::optional<core::StableIdentifier> existing_id;
	std::optional<core::StableIdentifier> reserved_new_id;
	std::string display_name;
	std::string category;
	std::optional<core::StableIdentifier> storage_id;
	std::vector<domain::TagRow> tags;
	std::string notes;
	domain::ItemStatus status{domain::ItemStatus::Draft};
	domain::ListingData listing;
	domain::AcquisitionData acquisition;
	domain::FinanceData finance;
	bool warning_acknowledged{};
	bool pending_photo_import_planned{};
};

struct StorageDraft final {
	std::optional<core::StableIdentifier> existing_id;
	std::optional<core::StableIdentifier> reserved_new_id;
	std::string display_name;
	std::string storage_type;
	std::optional<core::StableIdentifier> parent_storage_id;
	std::string location;
	std::vector<domain::TagRow> tags;
	std::string notes;
	domain::StorageLifecycleStatus lifecycle_status{
		domain::StorageLifecycleStatus::Active};
	bool archive_warning_acknowledged{};
};

struct EntityEditRequest final {
	CatalogSessionState current_session;
	core::IdentifierSource& identifiers;
	core::Clock& clock;
	std::optional<std::filesystem::path> active_catalog_root_override;
	bool create_previous_copy{true};
};
}	 // namespace shuba::ui
