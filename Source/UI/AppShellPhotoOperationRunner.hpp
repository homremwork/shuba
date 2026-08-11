#pragma once

#include "Core/Clock.hpp"
#include "Core/OperationGate.hpp"
#include "Platform/PlatformServices.hpp"
#include "UI/Session/PhotoSessionTypes.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <variant>

namespace shuba::ui {
enum class PhotoOperationJobType : std::uint8_t {
	PendingItemStaging,
	PendingStorageStaging,
	DirectImport,
	ItemSaveWithPendingPhotos,
	StorageSaveWithPendingPhotos,
};

enum class PhotoOperationState : std::uint8_t {
	Idle,
	Running,
	CancellationRequested,
	Applying,
};

struct AppShellPhotoOperationState final {
	PhotoOperationState state{PhotoOperationState::Idle};
	std::optional<PhotoOperationJobType> job_type;
	std::optional<core::OperationIdentifier> operation_id;
	std::optional<platform::ProgressEvent> latest_progress;
	std::uint64_t generation{};

	[[nodiscard]] bool active() const noexcept;
};

class PhotoOperationWorkerServiceFactory {
public:
	PhotoOperationWorkerServiceFactory() = default;
	PhotoOperationWorkerServiceFactory(const PhotoOperationWorkerServiceFactory&) =
		delete;
	PhotoOperationWorkerServiceFactory& operator=(
		const PhotoOperationWorkerServiceFactory&) = delete;
	PhotoOperationWorkerServiceFactory(PhotoOperationWorkerServiceFactory&&)
		noexcept = default;
	PhotoOperationWorkerServiceFactory& operator=(
		PhotoOperationWorkerServiceFactory&&) noexcept = default;
	virtual ~PhotoOperationWorkerServiceFactory() = default;

	[[nodiscard]] virtual std::unique_ptr<platform::ContentStagingService>
	make_content_staging_service() const = 0;
	[[nodiscard]] virtual std::unique_ptr<platform::SourceByteFingerprintService>
	make_source_fingerprint_service() const = 0;
	[[nodiscard]] virtual std::unique_ptr<platform::SourceImageDecodeService>
	make_source_decode_service() const = 0;
	[[nodiscard]] virtual std::unique_ptr<platform::InternalPhotoCodec>
	make_internal_photo_codec() const = 0;
};

class AppShellPhotoOperationRunner final {
public:
	using Result = std::variant<PendingPhotoStagingResult,
							PhotoImportSessionResult,
							ItemSaveWithPendingPhotosResult,
							StorageSaveWithPendingPhotosResult>;
	using Completion = std::function<void(Result)>;
	using Failure = std::function<void(std::string)>;
	struct Submission final {
		bool accepted{};
		std::uint64_t generation{};
	};

	struct Dependencies final {
		core::OperationGate& operation_gate;
		const PhotoOperationWorkerServiceFactory& worker_service_factory;
		std::function<void(std::uint64_t, const platform::ProgressEvent&)>
			progress;
		Failure failure;
	};

	explicit AppShellPhotoOperationRunner(Dependencies dependencies);
	~AppShellPhotoOperationRunner();

	AppShellPhotoOperationRunner(const AppShellPhotoOperationRunner&) = delete;
	AppShellPhotoOperationRunner& operator=(
		const AppShellPhotoOperationRunner&) = delete;
	AppShellPhotoOperationRunner(AppShellPhotoOperationRunner&&) noexcept = delete;
	AppShellPhotoOperationRunner& operator=(AppShellPhotoOperationRunner&&) noexcept =
		delete;

	[[nodiscard]] Submission submit_pending_staging(
		PhotoOperationJobType job_type, const PendingPhotoStagingRequest& request,
		Completion completion);
	[[nodiscard]] Submission submit_direct_import(
		const PhotoImportSessionRequest& request, Completion completion);
	[[nodiscard]] Submission submit_item_save(
		const ItemSaveWithPendingPhotosRequest& request, Completion completion);
	[[nodiscard]] Submission submit_storage_save(
		const StorageSaveWithPendingPhotosRequest& request, Completion completion);
	[[nodiscard]] bool active() const;
	void request_cancellation();
	void stop();

private:
	class Impl;
	std::unique_ptr<Impl> impl;
};
}  // namespace shuba::ui
