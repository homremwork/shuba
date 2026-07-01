#pragma once

#include "Catalog/PhotoImport.hpp"
#include "Core/Clock.hpp"
#include "Core/Identifier.hpp"
#include "Core/OperationGate.hpp"
#include "Platform/PlatformServices.hpp"
#include "UI/Session/EntityEditTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace shuba::ui {
struct PhotoImportSessionRequest final {
	CatalogSessionState current_session;
	core::IdentifierSource& identifiers;
	core::Clock& clock;
	core::OperationGate& operation_gate;
	platform::ContentStagingService& staging_service;
	platform::SourceByteFingerprintService& fingerprint_service;
	platform::SourceImageDecodeService& decode_service;
	platform::InternalPhotoCodec& photo_codec;
	domain::PhotoOwner owner;
	std::vector<platform::ContentSourceDescriptor> sources;
	std::optional<std::filesystem::path> active_catalog_root_override;
	bool create_previous_copy{true};
};

struct PhotoImportSessionResult final {
	core::OperationResultCategory category{
		core::OperationResultCategory::Success};
	std::vector<EntityEditDiagnostic> diagnostics;
	CatalogSessionState session;
	catalog::PhotoImportSummary summary;
	std::vector<core::StableIdentifier> imported_photo_ids;
	bool metadata_changed{};

	[[nodiscard]] bool succeeded() const noexcept;
	[[nodiscard]] bool was_user_cancelled() const noexcept;
	[[nodiscard]] bool failed() const noexcept;
	[[nodiscard]] bool has_partial_failures() const noexcept;
};

enum class PendingPhotoStatus : std::uint8_t {
	Selected,
	Staged,
	Failed,
	Cancelled,
	Removed,
	Consumed,
};

[[nodiscard]] std::string_view to_string(PendingPhotoStatus status) noexcept;

struct PendingPhotoSource final {
	std::size_t source_index{};
	std::string display_name;
	std::optional<std::uint64_t> byte_count;
	PendingPhotoStatus status{PendingPhotoStatus::Selected};
	std::optional<platform::ContentSourceDescriptor> staged_source;
	std::optional<std::filesystem::path> staged_path;
	std::string source_md5;
	std::vector<core::Diagnostic> diagnostics;

	[[nodiscard]] bool ready_for_import() const noexcept;
};

struct PendingPhotoStagingRequest final {
	CatalogSessionState current_session;
	core::IdentifierSource& identifiers;
	core::OperationGate& operation_gate;
	platform::ContentStagingService& staging_service;
	platform::SourceByteFingerprintService& fingerprint_service;
	std::vector<platform::ContentSourceDescriptor> sources;
	std::vector<PendingPhotoSource> existing_pending_sources;
	std::optional<domain::PhotoOwner> existing_owner;
};

struct PendingPhotoStagingResult final {
	core::OperationResultCategory category{
		core::OperationResultCategory::Success};
	std::vector<core::Diagnostic> diagnostics;
	std::vector<PendingPhotoSource> sources;
	std::uint64_t staged_count{};
	std::uint64_t failure_count{};
	std::uint64_t cancelled_count{};

	[[nodiscard]] bool succeeded() const noexcept;
	[[nodiscard]] bool was_user_cancelled() const noexcept;
	[[nodiscard]] bool failed() const noexcept;
	[[nodiscard]] bool has_partial_failures() const noexcept;
};

struct PendingPhotoCleanupResult final {
	core::OperationResultCategory category{
		core::OperationResultCategory::Success};
	std::vector<core::Diagnostic> diagnostics;
	std::uint64_t cleanup_attempt_count{};
	std::uint64_t removed_count{};
	std::uint64_t failure_count{};

	[[nodiscard]] bool succeeded() const noexcept;
	[[nodiscard]] bool failed() const noexcept;
};

struct ItemSaveWithPendingPhotosRequest final {
	CatalogSessionState current_session;
	core::IdentifierSource& identifiers;
	core::Clock& clock;
	core::OperationGate& operation_gate;
	platform::ContentStagingService& staging_service;
	platform::SourceByteFingerprintService& fingerprint_service;
	platform::SourceImageDecodeService& decode_service;
	platform::InternalPhotoCodec& photo_codec;
	ItemDraft draft;
	std::vector<PendingPhotoSource> pending_sources;
	std::optional<std::size_t> main_pending_source_index;
	std::optional<std::filesystem::path> active_catalog_root_override;
	bool create_previous_copy{true};
};

struct ItemSaveWithPendingPhotosResult final {
	EntityEditResult save_result;
	PhotoImportSessionResult import_result;
	PendingPhotoCleanupResult cleanup_result;
	CatalogSessionState session;
	std::vector<PendingPhotoSource> pending_sources;
	EntityEditResult main_selection_result;
	std::optional<core::StableIdentifier> main_selected_photo_id;
	bool import_attempted{};
	bool cleanup_attempted{};
	bool main_selection_attempted{};

	[[nodiscard]] bool item_saved() const noexcept;
	[[nodiscard]] bool warning_acknowledgement_required() const noexcept;
	[[nodiscard]] bool import_failed() const noexcept;
};

struct StorageSaveWithPendingPhotosRequest final {
	CatalogSessionState current_session;
	core::IdentifierSource& identifiers;
	core::Clock& clock;
	core::OperationGate& operation_gate;
	platform::ContentStagingService& staging_service;
	platform::SourceByteFingerprintService& fingerprint_service;
	platform::SourceImageDecodeService& decode_service;
	platform::InternalPhotoCodec& photo_codec;
	StorageDraft draft;
	std::vector<PendingPhotoSource> pending_sources;
	std::optional<std::size_t> main_pending_source_index;
	std::optional<std::filesystem::path> active_catalog_root_override;
	bool create_previous_copy{true};
};

struct StorageSaveWithPendingPhotosResult final {
	EntityEditResult save_result;
	PhotoImportSessionResult import_result;
	PendingPhotoCleanupResult cleanup_result;
	CatalogSessionState session;
	std::vector<PendingPhotoSource> pending_sources;
	EntityEditResult main_selection_result;
	std::optional<core::StableIdentifier> main_selected_photo_id;
	bool import_attempted{};
	bool cleanup_attempted{};
	bool main_selection_attempted{};

	[[nodiscard]] bool storage_saved() const noexcept;
	[[nodiscard]] bool warning_acknowledgement_required() const noexcept;
	[[nodiscard]] bool import_failed() const noexcept;
};
}	 // namespace shuba::ui
