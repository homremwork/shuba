#include "UI/AppShellPhotoOperationRunner.hpp"

#include "Core/Identifier.hpp"
#include "UI/Session/PhotoSession.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <condition_variable>
#include <concepts>
#include <filesystem>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace shuba::ui {
bool AppShellPhotoOperationState::active() const noexcept {
	return state != PhotoOperationState::Idle;
}

namespace {
class WorkerCancellationToken final : public platform::CancellationToken {
public:
	[[nodiscard]] bool cancellation_requested() const noexcept override {
		return requested.load(std::memory_order_acquire);
	}

	void request_cancellation() noexcept {
		requested.store(true, std::memory_order_release);
	}

	void reset() noexcept { requested.store(false, std::memory_order_release); }

private:
	std::atomic_bool requested{};
};

class WorkerProgressSink final : public platform::ProgressSink {
public:
	explicit WorkerProgressSink(
		std::function<void(const platform::ProgressEvent&)> handler_value)
		: handler(std::move(handler_value)) {}

	void publish_progress(platform::ProgressEvent event) override {
		if (handler)
			handler(event);
	}

private:
	std::function<void(const platform::ProgressEvent&)> handler;
};

class WorkerIdentifierSource final : public core::IdentifierSource {
public:
	[[nodiscard]] core::StableIdentifier next_stable_identifier() override {
		return identifiers.next_stable_identifier();
	}

	[[nodiscard]] core::OperationIdentifier next_operation_identifier()
		override {
		return identifiers.next_operation_identifier();
	}

private:
	core::RandomIdentifierSource identifiers;
};

struct PendingStagingJob final {
	PhotoOperationJobType type{PhotoOperationJobType::PendingItemStaging};
	CatalogSessionState current_session;
	std::vector<platform::ContentSourceDescriptor> sources;
	std::vector<PendingPhotoSource> existing_pending_sources;
	std::optional<domain::PhotoOwner> existing_owner;
};

struct DirectImportJob final {
	CatalogSessionState current_session;
	domain::PhotoOwner owner;
	std::vector<platform::ContentSourceDescriptor> sources;
	std::optional<std::filesystem::path> active_catalog_root_override;
	bool create_previous_copy{true};
};

struct ItemSaveJob final {
	CatalogSessionState current_session;
	ItemDraft draft;
	std::vector<PendingPhotoSource> pending_sources;
	std::optional<std::size_t> main_pending_source_index;
	std::optional<std::filesystem::path> active_catalog_root_override;
	bool create_previous_copy{true};
};

struct StorageSaveJob final {
	CatalogSessionState current_session;
	StorageDraft draft;
	std::vector<PendingPhotoSource> pending_sources;
	std::optional<std::size_t> main_pending_source_index;
	std::optional<std::filesystem::path> active_catalog_root_override;
	bool create_previous_copy{true};
};

struct LifetimeToken final {
	std::atomic_bool alive{true};
};
}  // namespace

class AppShellPhotoOperationRunner::Impl final {
public:
	explicit Impl(AppShellPhotoOperationRunner::Dependencies dependencies)
		: operation_gate(dependencies.operation_gate)
		, worker_service_factory(dependencies.worker_service_factory)
		, progress_handler(std::move(dependencies.progress))
		, failure_handler(std::move(dependencies.failure))
		, worker([this] { worker_loop(); }) {}

	~Impl() { stop(); }

	[[nodiscard]] Submission submit_pending_staging(
		PhotoOperationJobType job_type, const PendingPhotoStagingRequest& request,
		Completion completion) {
		if (job_type != PhotoOperationJobType::PendingItemStaging
			&& job_type != PhotoOperationJobType::PendingStorageStaging) {
			return {};
		}
		return submit(Job{.type = job_type,
						  .value = PendingStagingJob{
							  .type = job_type,
							  .current_session = request.current_session,
							  .sources = request.sources,
							  .existing_pending_sources = request.existing_pending_sources,
							  .existing_owner = request.existing_owner},
						  .completion = std::move(completion)});
	}

	[[nodiscard]] Submission submit_direct_import(
		const PhotoImportSessionRequest& request, Completion completion) {
		return submit(Job{.type = PhotoOperationJobType::DirectImport,
						  .value = DirectImportJob{
							  .current_session = request.current_session,
							  .owner = request.owner,
							  .sources = request.sources,
							  .active_catalog_root_override =
								  request.active_catalog_root_override,
							  .create_previous_copy = request.create_previous_copy},
						  .completion = std::move(completion)});
	}

	[[nodiscard]] Submission submit_item_save(
		const ItemSaveWithPendingPhotosRequest& request, Completion completion) {
		return submit(Job{.type = PhotoOperationJobType::ItemSaveWithPendingPhotos,
						  .value = ItemSaveJob{
							  .current_session = request.current_session,
							  .draft = request.draft,
							  .pending_sources = request.pending_sources,
							  .main_pending_source_index =
								  request.main_pending_source_index,
							  .active_catalog_root_override =
								  request.active_catalog_root_override,
							  .create_previous_copy = request.create_previous_copy},
						  .completion = std::move(completion)});
	}

	[[nodiscard]] Submission submit_storage_save(
		const StorageSaveWithPendingPhotosRequest& request, Completion completion) {
		return submit(Job{
			.type = PhotoOperationJobType::StorageSaveWithPendingPhotos,
			.value = StorageSaveJob{
				.current_session = request.current_session,
				.draft = request.draft,
				.pending_sources = request.pending_sources,
				.main_pending_source_index = request.main_pending_source_index,
				.active_catalog_root_override = request.active_catalog_root_override,
				.create_previous_copy = request.create_previous_copy},
			.completion = std::move(completion)});
	}

	[[nodiscard]] bool active() const {
		const std::lock_guard<std::mutex> lock{mutex};
		return active_job || queued_job.has_value() || result_pending;
	}

	void request_cancellation() {
		cancellation.request_cancellation();
	}

	void completion_applied() {
		const std::lock_guard<std::mutex> lock{mutex};
		result_pending = false;
	}

	void stop() {
		{
			const std::lock_guard<std::mutex> lock{mutex};
			if (stopping)
				return;
			stopping = true;
			queued_job.reset();
		}
		lifetime_token->alive.store(false, std::memory_order_release);
		{
			const std::lock_guard<std::mutex> lock{progress_mutex};
			latest_progress.reset();
		}
		cancellation.request_cancellation();
		condition.notify_all();
		if (worker.joinable())
			worker.join();
	}

private:
	struct Job final {
		PhotoOperationJobType type{PhotoOperationJobType::DirectImport};
		std::uint64_t generation{};
		std::variant<PendingStagingJob, DirectImportJob, ItemSaveJob,
					 StorageSaveJob>
			value;
		Completion completion;
	};

	[[nodiscard]] Submission submit(Job job) {
		{
			const std::lock_guard<std::mutex> lock{mutex};
			if (stopping || active_job || queued_job.has_value() || result_pending)
				return {};
			job.generation = ++next_generation;
			const std::uint64_t generation = job.generation;
			queued_job = std::move(job);
			condition.notify_one();
			return Submission{.accepted = true, .generation = generation};
		}
	}

	void worker_loop() {
		for (;;) {
			std::optional<Job> job;
			{
				std::unique_lock<std::mutex> lock{mutex};
				condition.wait(lock, [this] {
					return stopping || queued_job.has_value();
				});
				if (stopping)
					return;
				job = std::move(queued_job);
				queued_job.reset();
				active_job = true;
			}

			cancellation.reset();
			Completion completion = std::move(job->completion);
			std::optional<Result> result;
			std::string failure;
			try {
				result = execute(std::move(*job));
			} catch (const std::exception& exception) {
				failure = exception.what();
			} catch (...) {
				failure = "Unknown photo operation worker failure.";
			}
			{
				const std::lock_guard<std::mutex> lock{mutex};
				active_job = false;
			}
			if (result.has_value()) {
				{
					const std::lock_guard<std::mutex> lock{mutex};
					result_pending = true;
				}
				post_completion(std::move(completion), std::move(*result));
			} else {
				post_failure(std::move(failure));
			}
		}
	}

	[[nodiscard]] Result execute(Job job) {
		WorkerIdentifierSource identifiers;
		core::SystemClock clock;
		WorkerProgressSink progress{
			[this, generation = job.generation](const platform::ProgressEvent& event) {
				publish_progress(generation, event);
			}};
		std::unique_ptr<platform::ContentStagingService> staging =
			worker_service_factory.make_content_staging_service();
		std::unique_ptr<platform::SourceByteFingerprintService> fingerprinting =
			worker_service_factory.make_source_fingerprint_service();
		std::unique_ptr<platform::SourceImageDecodeService> decoder =
			worker_service_factory.make_source_decode_service();
		std::unique_ptr<platform::InternalPhotoCodec> codec =
			worker_service_factory.make_internal_photo_codec();
		if (staging == nullptr || fingerprinting == nullptr || decoder == nullptr
			|| codec == nullptr) {
			throw std::runtime_error{
				"Photo operation worker service factory returned a null service."};
		}

		return std::visit(
			[this, &identifiers, &clock, &progress, &staging, &fingerprinting,
			 &decoder, &codec](const auto& concrete_job) -> Result {
				using JobType = std::decay_t<decltype(concrete_job)>;
				if constexpr (std::same_as<JobType, PendingStagingJob>) {
					return stage_pending_photos_for_session(
						PendingPhotoStagingRequest{
							.current_session = concrete_job.current_session,
							.identifiers = identifiers,
							.operation_gate = operation_gate,
							.staging_service = *staging,
							.fingerprint_service = *fingerprinting,
							.sources = concrete_job.sources,
							.existing_pending_sources =
								concrete_job.existing_pending_sources,
							.existing_owner = concrete_job.existing_owner},
						progress, cancellation);
				} else if constexpr (std::same_as<JobType, DirectImportJob>) {
					return import_photos_into_session(
						PhotoImportSessionRequest{
							.current_session = concrete_job.current_session,
							.identifiers = identifiers,
							.clock = clock,
							.operation_gate = operation_gate,
							.staging_service = *staging,
							.fingerprint_service = *fingerprinting,
							.decode_service = *decoder,
							.photo_codec = *codec,
							.owner = concrete_job.owner,
							.sources = concrete_job.sources,
							.active_catalog_root_override =
								concrete_job.active_catalog_root_override,
							.create_previous_copy = concrete_job.create_previous_copy},
						progress, cancellation);
				} else if constexpr (std::same_as<JobType, ItemSaveJob>) {
					return save_item_draft_and_import_pending_photos(
						ItemSaveWithPendingPhotosRequest{
							.current_session = concrete_job.current_session,
							.identifiers = identifiers,
							.clock = clock,
							.operation_gate = operation_gate,
							.staging_service = *staging,
							.fingerprint_service = *fingerprinting,
							.decode_service = *decoder,
							.photo_codec = *codec,
							.draft = concrete_job.draft,
							.pending_sources = concrete_job.pending_sources,
							.main_pending_source_index =
								concrete_job.main_pending_source_index,
							.active_catalog_root_override =
								concrete_job.active_catalog_root_override,
							.create_previous_copy = concrete_job.create_previous_copy},
						progress, cancellation);
				} else {
					return save_storage_draft_and_import_pending_photos(
						StorageSaveWithPendingPhotosRequest{
							.current_session = concrete_job.current_session,
							.identifiers = identifiers,
							.clock = clock,
							.operation_gate = operation_gate,
							.staging_service = *staging,
							.fingerprint_service = *fingerprinting,
							.decode_service = *decoder,
							.photo_codec = *codec,
							.draft = concrete_job.draft,
							.pending_sources = concrete_job.pending_sources,
							.main_pending_source_index =
								concrete_job.main_pending_source_index,
							.active_catalog_root_override =
								concrete_job.active_catalog_root_override,
							.create_previous_copy = concrete_job.create_previous_copy},
						progress, cancellation);
				}
			},
			job.value);
	}

	struct PendingProgress final {
		std::uint64_t generation{};
		std::uint64_t revision{};
		platform::ProgressEvent event;
	};

	void publish_progress(std::uint64_t generation,
					  const platform::ProgressEvent& event) {
		bool schedule_delivery{};
		{
			const std::lock_guard<std::mutex> lock{progress_mutex};
			if (!lifetime_token->alive.load(std::memory_order_acquire))
				return;
			latest_progress = PendingProgress{.generation = generation,
									  .revision = ++progress_revision,
									  .event = event};
			if (!progress_delivery_queued) {
				progress_delivery_queued = true;
				schedule_delivery = true;
			}
		}
		if (schedule_delivery)
			post_progress_delivery();
	}

	void post_progress_delivery() {
		const std::weak_ptr<LifetimeToken> lifetime = lifetime_token;
		Impl* const owner = this;
		const bool posted = juce::MessageManager::callAsync([lifetime, owner] {
			const std::shared_ptr<LifetimeToken> token = lifetime.lock();
			if (token == nullptr
				|| !token->alive.load(std::memory_order_acquire)) {
				return;
			}
			owner->deliver_latest_progress();
		});
		if (posted)
			return;

		const std::lock_guard<std::mutex> lock{progress_mutex};
		progress_delivery_queued = false;
	}

	void deliver_latest_progress() {
		std::optional<PendingProgress> delivery;
		{
			const std::lock_guard<std::mutex> lock{progress_mutex};
			delivery = latest_progress;
		}

		if (delivery.has_value() && progress_handler) {
			progress_handler(delivery->generation, delivery->event);
		}

		bool reschedule{};
		{
			const std::lock_guard<std::mutex> lock{progress_mutex};
			if (delivery.has_value() && latest_progress.has_value()
				&& latest_progress->revision != delivery->revision) {
				reschedule = true;
			} else {
				latest_progress.reset();
				progress_delivery_queued = false;
			}
		}
		if (reschedule)
			post_progress_delivery();
	}

	void post_failure(std::string failure) {
		const std::weak_ptr<LifetimeToken> lifetime = lifetime_token;
		Failure handler = failure_handler;
		{
			const std::lock_guard<std::mutex> lock{mutex};
			result_pending = true;
		}
		Impl* const owner = this;
		const bool posted = juce::MessageManager::callAsync(
			[lifetime, owner, handler = std::move(handler),
			 failure = std::move(failure)]() mutable {
				const std::shared_ptr<LifetimeToken> token = lifetime.lock();
				if (token == nullptr
					|| !token->alive.load(std::memory_order_acquire))
					return;
				if (handler)
					handler(std::move(failure));
				owner->completion_applied();
			});
		if (!posted)
			completion_applied();
	}

	void post_completion(Completion completion, Result result) {
		const std::weak_ptr<LifetimeToken> lifetime = lifetime_token;
		Impl* const owner = this;
		const bool posted = juce::MessageManager::callAsync(
			[lifetime, owner, completion = std::move(completion),
			 result = std::move(result)]() mutable {
				const std::shared_ptr<LifetimeToken> token = lifetime.lock();
				if (token == nullptr
					|| !token->alive.load(std::memory_order_acquire))
					return;
				if (completion)
					completion(std::move(result));
				owner->completion_applied();
			});
		if (!posted)
			completion_applied();
	}

	core::OperationGate& operation_gate;
	const PhotoOperationWorkerServiceFactory& worker_service_factory;
	std::function<void(std::uint64_t, const platform::ProgressEvent&)>
		progress_handler;
	Failure failure_handler;
	WorkerCancellationToken cancellation;
	mutable std::mutex mutex;
	std::condition_variable condition;
	std::optional<Job> queued_job;
	std::mutex progress_mutex;
	std::optional<PendingProgress> latest_progress;
	std::shared_ptr<LifetimeToken> lifetime_token{
		std::make_shared<LifetimeToken>()};
	std::thread worker;
	std::uint64_t next_generation{};
	std::uint64_t progress_revision{};
	bool active_job{};
	bool result_pending{};
	bool progress_delivery_queued{};
	bool stopping{};
};

AppShellPhotoOperationRunner::AppShellPhotoOperationRunner(Dependencies dependencies)
	: impl(std::make_unique<Impl>(std::move(dependencies))) {}

AppShellPhotoOperationRunner::~AppShellPhotoOperationRunner() = default;

AppShellPhotoOperationRunner::Submission
AppShellPhotoOperationRunner::submit_pending_staging(
	PhotoOperationJobType job_type, const PendingPhotoStagingRequest& request,
	Completion completion) {
	return impl->submit_pending_staging(job_type, request, std::move(completion));
}

AppShellPhotoOperationRunner::Submission
AppShellPhotoOperationRunner::submit_direct_import(
	const PhotoImportSessionRequest& request, Completion completion) {
	return impl->submit_direct_import(request, std::move(completion));
}

AppShellPhotoOperationRunner::Submission
AppShellPhotoOperationRunner::submit_item_save(
	const ItemSaveWithPendingPhotosRequest& request, Completion completion) {
	return impl->submit_item_save(request, std::move(completion));
}

AppShellPhotoOperationRunner::Submission
AppShellPhotoOperationRunner::submit_storage_save(
	const StorageSaveWithPendingPhotosRequest& request, Completion completion) {
	return impl->submit_storage_save(request, std::move(completion));
}

bool AppShellPhotoOperationRunner::active() const { return impl->active(); }

void AppShellPhotoOperationRunner::request_cancellation() {
	impl->request_cancellation();
}

void AppShellPhotoOperationRunner::stop() { impl->stop(); }
}  // namespace shuba::ui
