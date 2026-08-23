#pragma once

#include "Catalog/PhotoExport.hpp"
#include "Core/Clock.hpp"
#include "Core/OperationGate.hpp"
#include "Platform/PlatformServices.hpp"
#include "UI/Session/BackupRecoveryTypes.hpp"
#include "UI/Session/PhotoSessionTypes.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <variant>

namespace shuba::ui {
enum class ShellOperationJobType : std::uint8_t {
	PendingItemStaging,
	PendingStorageStaging,
	DirectImport,
	ItemSaveWithPendingPhotos,
	StorageSaveWithPendingPhotos,
	JpegExport,
	BackupExport,
	DiagnosticArchiveExport,
	BackupImportStaging,
	BackupImportReplacement,
};

enum class ShellOperationState : std::uint8_t {
	Idle,
	Running,
	CancellationRequested,
	Applying,
};

struct AppShellOperationState final {
	ShellOperationState state{ShellOperationState::Idle};
	std::optional<ShellOperationJobType> job_type;
	std::optional<core::OperationIdentifier> operation_id;
	std::optional<platform::ProgressEvent> latest_progress;
	std::uint64_t generation{};

	[[nodiscard]] bool active() const noexcept;
};

class ShellOperationWorkerServiceFactory {
public:
	ShellOperationWorkerServiceFactory() = default;
	ShellOperationWorkerServiceFactory(
		const ShellOperationWorkerServiceFactory&) = delete;
	ShellOperationWorkerServiceFactory& operator=(
		const ShellOperationWorkerServiceFactory&) = delete;
	ShellOperationWorkerServiceFactory(
		ShellOperationWorkerServiceFactory&&) noexcept = default;
	ShellOperationWorkerServiceFactory& operator=(
		ShellOperationWorkerServiceFactory&&) noexcept = default;
	virtual ~ShellOperationWorkerServiceFactory()	   = default;

	[[nodiscard]] virtual std::unique_ptr<platform::ContentStagingService>
	make_content_staging_service() const = 0;
	[[nodiscard]] virtual std::unique_ptr<
		platform::SourceByteFingerprintService>
	make_source_fingerprint_service() const = 0;
	[[nodiscard]] virtual std::unique_ptr<platform::SourceImageDecodeService>
	make_source_decode_service() const = 0;
	[[nodiscard]] virtual std::unique_ptr<platform::InternalPhotoCodec>
	make_internal_photo_codec() const = 0;
	[[nodiscard]] virtual std::unique_ptr<platform::JpegExportService>
	make_jpeg_export_service() const {
		return nullptr;
	}
	[[nodiscard]] virtual std::unique_ptr<platform::ZipArchiveService>
	make_zip_archive_service() const {
		return nullptr;
	}
	[[nodiscard]] virtual std::unique_ptr<platform::DocumentExportService>
	make_document_export_service() const {
		return nullptr;
	}
};

class AppShellOperationRunner final {
public:
	using Result = std::variant<
		PendingPhotoStagingResult, PhotoImportSessionResult,
		ItemSaveWithPendingPhotosResult, StorageSaveWithPendingPhotosResult,
		catalog::PhotoExportResult, BackupExportSessionResult,
		BackupImportStagingSessionResult, BackupImportReplacementSessionResult>;
	struct CompletionResult final {
		ShellOperationJobType job_type;
		std::uint64_t generation{};
		Result value;
	};
	using Completion = std::function<void(CompletionResult)>;
	using Failure =
		std::function<void(ShellOperationJobType, std::uint64_t, std::string)>;
	struct Submission final {
		bool accepted{};
		std::uint64_t generation{};
	};

	struct Dependencies final {
		core::OperationGate& operation_gate;
		const ShellOperationWorkerServiceFactory& worker_service_factory;
		std::function<void(std::uint64_t, const platform::ProgressEvent&)>
			progress;
		Failure failure;
	};

	explicit AppShellOperationRunner(Dependencies dependencies);
	~AppShellOperationRunner();

	AppShellOperationRunner(const AppShellOperationRunner&)			   = delete;
	AppShellOperationRunner& operator=(const AppShellOperationRunner&) = delete;
	AppShellOperationRunner(AppShellOperationRunner&&) noexcept		   = delete;
	AppShellOperationRunner& operator=(AppShellOperationRunner&&) noexcept =
		delete;

	[[nodiscard]] Submission submit_pending_staging(
		ShellOperationJobType job_type,
		const PendingPhotoStagingRequest& request, Completion completion);
	[[nodiscard]] Submission submit_direct_import(
		const PhotoImportSessionRequest& request, Completion completion);
	[[nodiscard]] Submission submit_item_save(
		const ItemSaveWithPendingPhotosRequest& request, Completion completion);
	[[nodiscard]] Submission submit_storage_save(
		const StorageSaveWithPendingPhotosRequest& request,
		Completion completion);
	[[nodiscard]] Submission submit_jpeg_export(
		const catalog::PhotoExportRequest& request, Completion completion);
	[[nodiscard]] Submission submit_backup_export(
		const BackupExportSessionRequest& request, bool diagnostic_archive,
		Completion completion);
	[[nodiscard]] Submission submit_backup_import_staging(
		const BackupImportStagingSessionRequest& request,
		Completion completion);
	[[nodiscard]] Submission submit_backup_import_replacement(
		const BackupImportReplacementSessionRequest& request,
		Completion completion);
	[[nodiscard]] bool active() const;
	void request_cancellation();
	void stop();

private:
	class Impl;
	std::unique_ptr<Impl> impl;
};
}	 // namespace shuba::ui
