#pragma once

#include "Catalog/BackupArchive.hpp"
#include "Catalog/CatalogReplacement.hpp"
#include "Catalog/CatalogRepository.hpp"
#include "Catalog/PhotoImport.hpp"
#include "Catalog/Search.hpp"
#include "Core/Clock.hpp"
#include "Core/Identifier.hpp"
#include "Core/OperationGate.hpp"
#include "Core/Result.hpp"
#include "Persistence/JsonlCatalog.hpp"
#include "Platform/PlatformServices.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace shuba::ui {
inline constexpr auto debug_demo_marker_file_name =
	std::string_view{".debug-demo-catalog"};

enum class CatalogSessionStartupSource : std::uint8_t {
	ExistingCatalog,
	InitializedEmptyCatalog,
	SeededDemoCatalog,
	PathResolutionFailed,
	InitializationFailed,
	LoadFailed,
};

[[nodiscard]] std::string_view to_string(
	CatalogSessionStartupSource source) noexcept;

struct CatalogSessionState final {
	CatalogSessionStartupSource source{
		CatalogSessionStartupSource::PathResolutionFailed};
	std::optional<platform::AppPrivatePaths> paths;
	std::optional<core::StableIdentifier> catalog_id;
	bool existing_canonical_metadata{};
	bool initialized_empty_catalog{};
	bool demo_catalog_seeded{};
	bool demo_catalog_active{};
	persistence::CatalogLoadStatus load_status{
		persistence::CatalogLoadStatus::Fatal};
	std::vector<core::Diagnostic> startup_diagnostics;
	persistence::CatalogJsonlLoadResult load_result;
	catalog::CatalogRepositoryState repository;
	catalog::SearchIndex search_index;

	[[nodiscard]] bool ready_for_browsing() const noexcept;
	[[nodiscard]] bool degraded() const noexcept;
	[[nodiscard]] bool fatal() const noexcept;
};

struct CatalogSessionLoadRequest final {
	platform::AppPrivatePathProvider& path_provider;
	core::IdentifierSource& identifiers;
	core::Clock& clock;
	bool debug_demo_seed_enabled{};
};

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

struct PhotoImportSessionRequest final {
	CatalogSessionState current_session;
	core::IdentifierSource& identifiers;
	core::Clock& clock;
	core::OperationGate& operation_gate;
	platform::ContentStagingService& staging_service;
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

struct CatalogRecoveryUiSummary final {
	persistence::CatalogLoadStatus load_status{
		persistence::CatalogLoadStatus::Fatal};
	std::string plain_summary_message;
	std::uint64_t accepted_item_count{};
	std::uint64_t accepted_storage_count{};
	std::uint64_t accepted_photo_count{};
	std::uint64_t skipped_item_count{};
	std::uint64_t skipped_storage_count{};
	std::uint64_t skipped_photo_count{};
	std::uint64_t broken_reference_count{};
	std::uint64_t orphan_media_count{};
	std::vector<std::string> safe_actions;
	std::vector<std::string> technical_details;

	[[nodiscard]] bool fatal() const noexcept;
	[[nodiscard]] bool degraded() const noexcept;
};

struct BackupExportSessionRequest final {
	CatalogSessionState current_session;
	core::IdentifierSource& identifiers;
	const core::Clock& clock;
	core::OperationGate& operation_gate;
	platform::ZipArchiveService& zip_archive_service;
	platform::DocumentExportService& document_export_service;
	platform::ContentStagingService& content_staging_service;
	platform::DocumentDestinationDescriptor destination;
	bool keep_temp_zip{};
};

struct BackupExportSessionResult final {
	core::OperationResultCategory category{
		core::OperationResultCategory::InternalError};
	catalog::BackupExportResult export_result;
	std::vector<core::Diagnostic> diagnostics;
	bool unencrypted_zip_warning_required{true};
	bool degraded_backup_warning_required{};
	bool diagnostic_companion_recommended{};

	[[nodiscard]] bool succeeded() const noexcept;
	[[nodiscard]] bool was_user_cancelled() const noexcept;
	[[nodiscard]] bool failed() const noexcept;
};

struct BackupImportStagingSessionRequest final {
	CatalogSessionState current_session;
	core::IdentifierSource& identifiers;
	const core::Clock& clock;
	core::OperationGate& operation_gate;
	platform::ZipArchiveService& zip_archive_service;
	platform::DocumentExportService& document_export_service;
	platform::ContentStagingService& content_staging_service;
	platform::ContentSourceDescriptor source;
	bool keep_staged_zip{};
	bool keep_extracted_catalog{true};
};

struct BackupImportStagingSessionResult final {
	core::OperationResultCategory category{
		core::OperationResultCategory::InternalError};
	catalog::BackupImportStagingResult staging_result;
	std::vector<core::Diagnostic> diagnostics;
	bool unencrypted_zip_warning_required{true};
	bool import_validation_ready{};
	bool degraded_import_confirmation_required{};

	[[nodiscard]] bool succeeded() const noexcept;
	[[nodiscard]] bool was_user_cancelled() const noexcept;
	[[nodiscard]] bool failed() const noexcept;
};

struct BackupImportReplacementSessionRequest final {
	CatalogSessionState current_session;
	core::IdentifierSource& identifiers;
	const core::Clock& clock;
	core::OperationGate& operation_gate;
	std::filesystem::path staged_catalog_root;
	bool replacement_confirmed{};
	bool degraded_import_confirmed{};
	catalog::CatalogReplacementFaultMode fault_mode{
		catalog::CatalogReplacementFaultMode::None};
};

struct BackupImportReplacementSessionResult final {
	core::OperationResultCategory category{
		core::OperationResultCategory::InternalError};
	catalog::CatalogReplacementResult replacement_result;
	CatalogSessionState session;
	std::vector<core::Diagnostic> diagnostics;
	bool fatal_recovery_required{};

	[[nodiscard]] bool succeeded() const noexcept;
	[[nodiscard]] bool was_user_cancelled() const noexcept;
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

[[nodiscard]] CatalogSessionState load_catalog_session(
	const CatalogSessionLoadRequest& request);
[[nodiscard]] CatalogRecoveryUiSummary make_recovery_ui_summary(
	const CatalogSessionState& session);
[[nodiscard]] EntityEditResult save_item_draft(const EntityEditRequest& request,
											   const ItemDraft& draft);
[[nodiscard]] EntityEditResult save_storage_draft(
	const EntityEditRequest& request, const StorageDraft& draft);
[[nodiscard]] EntityEditResult archive_item_in_session(
	const EntityEditRequest& request, const core::StableIdentifier& item_id);
[[nodiscard]] EntityEditResult archive_storage_in_session(
	const EntityEditRequest& request, const core::StableIdentifier& storage_id,
	bool archive_warning_acknowledged);
[[nodiscard]] EntityEditResult set_main_photo_in_session(
	const EntityEditRequest& request, const core::StableIdentifier& photo_id);
[[nodiscard]] PhotoImportSessionResult import_photos_into_session(
	const PhotoImportSessionRequest& request,
	platform::ProgressSink& progress_sink,
	platform::CancellationToken& cancellation_token);
[[nodiscard]] BackupExportSessionResult export_backup_from_session(
	const BackupExportSessionRequest& request,
	platform::ProgressSink& progress_sink,
	platform::CancellationToken& cancellation_token);
[[nodiscard]] BackupExportSessionResult export_diagnostic_archive_from_session(
	const BackupExportSessionRequest& request,
	platform::ProgressSink& progress_sink,
	platform::CancellationToken& cancellation_token);
[[nodiscard]] BackupImportStagingSessionResult stage_backup_import_for_session(
	const BackupImportStagingSessionRequest& request,
	platform::ProgressSink& progress_sink,
	platform::CancellationToken& cancellation_token);
[[nodiscard]] BackupImportReplacementSessionResult
replace_session_with_staged_import(
	const BackupImportReplacementSessionRequest& request,
	platform::ProgressSink& progress_sink,
	platform::CancellationToken& cancellation_token);
[[nodiscard]] bool hard_delete_enabled_for_owner(
	const CatalogSessionState& session, const core::StableIdentifier& owner_id,
	domain::PhotoOwnerType owner_type,
	bool multi_file_deletion_sequence_tests_proven = false);
}	 // namespace shuba::ui
