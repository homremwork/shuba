#include "UI/AppShellPreviewScheduler.hpp"

#include "Localization/Facade.hpp"

#include "Catalog/PhotoExport.hpp"
#include "UI/View/ScreenText.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace shuba::ui {
namespace {
[[nodiscard]] bool priority_before(ImagePreviewRequestPriority left,
								   ImagePreviewRequestPriority right) noexcept {
	return static_cast<std::uint8_t>(left) > static_cast<std::uint8_t>(right);
}

[[nodiscard]] juce::String placeholder_text(
	const localization::Localization& localization) {
	return juce_text(
		localization.text(localization::MessageId::WorkflowPreviewUnavailable));
}
}	 // namespace

class AppShellPreviewScheduler::Impl final {
public:
	explicit Impl(AppShellPreviewScheduler::Dependencies dependencies)
		: session(dependencies.session)
		, photo_display(dependencies.photo_display)
		, preview_cache(dependencies.preview_cache)
		, internal_photo_codec(dependencies.internal_photo_codec)
		, source_decode_service(dependencies.source_decode_service)
		, jpeg_export_service(dependencies.jpeg_export_service)
		, document_export_service(dependencies.document_export_service)
		, localization(dependencies.localization)
		, refresh_content_handler(std::move(dependencies.refresh_content)) {
		worker = std::thread([this] { worker_loop(); });
	}

	~Impl() {
		lifetime_token->alive.store(false, std::memory_order_release);
		{
			const std::lock_guard<std::mutex> lock{mutex};
			stopping = true;
			queued_preview_jobs.clear();
			queued_display_jobs.clear();
			pending_preview_jobs.clear();
			pending_display_photo_ids.clear();
		}
		condition.notify_all();
		if (worker.joinable())
			worker.join();
	}

	[[nodiscard]] std::optional<juce::String> failure_message(
		const ImagePreviewRequestIdentity& identity) const {
		const std::lock_guard<std::mutex> lock{mutex};
		const std::vector<PreviewFailure>::const_iterator found =
			std::ranges::find_if(preview_failures,
								 [&identity](const PreviewFailure& failure) {
			return failure.identity == identity;
		});
		return found == preview_failures.end()
				   ? std::nullopt
				   : std::optional<juce::String>{found->message};
	}

	void clear_failure(const ImagePreviewRequestIdentity& identity) {
		const std::lock_guard<std::mutex> lock{mutex};
		erase_preview_failure(identity);
	}

	void enqueue_internal_preview(core::StableIdentifier photo_id,
								  ImagePreviewSize target_size,
								  ImagePreviewRequestPriority priority) {
		if (!session.paths.has_value())
			return;
		ImagePreviewRequestIdentity identity =
			make_internal_photo_preview_identity(photo_id, target_size);
		if (preview_cache.contains(identity))
			return;
		{
			const std::lock_guard<std::mutex> lock{mutex};
			if (stopping)
				return;
			if (has_preview_failure(identity))
				return;
			if (contains_preview_identity(pending_preview_jobs, identity)) {
				raise_queued_preview_priority(identity, priority);
				return;
			}
			InternalPreviewJob job{.repository	= session.repository,
								   .paths		= *session.paths,
								   .photo_id	= std::move(photo_id),
								   .target_size = target_size,
								   .identity	= identity,
								   .priority	= priority,
								   .generation	= preview_generation};
			queued_preview_jobs.push_back(PreviewJob{std::move(job)});
			pending_preview_jobs.push_back(
				PendingPreviewJob{.identity	  = std::move(identity),
								  .generation = preview_generation});
		}
		condition.notify_one();
	}

	void enqueue_staged_preview(PendingPhotoSource source,
								ImagePreviewSize target_size,
								ImagePreviewRequestPriority priority) {
		ImagePreviewRequestIdentity identity =
			make_staged_photo_preview_identity(source, target_size);
		if (preview_cache.contains(identity))
			return;
		{
			const std::lock_guard<std::mutex> lock{mutex};
			if (stopping)
				return;
			if (has_preview_failure(identity))
				return;
			if (contains_preview_identity(pending_preview_jobs, identity)) {
				raise_queued_preview_priority(identity, priority);
				return;
			}
			StagedPreviewJob job{.source	  = std::move(source),
								 .target_size = target_size,
								 .identity	  = identity,
								 .priority	  = priority,
								 .generation  = preview_generation};
			queued_preview_jobs.push_back(PreviewJob{std::move(job)});
			pending_preview_jobs.push_back(
				PendingPreviewJob{.identity	  = std::move(identity),
								  .generation = preview_generation});
		}
		condition.notify_one();
	}

	void enqueue_display(core::StableIdentifier photo_id) {
		if (!session.paths.has_value())
			return;
		{
			const std::lock_guard<std::mutex> lock{mutex};
			if (stopping)
				return;
			const std::uint64_t generation =
				++photo_display.display_request_generation;
			photo_display.requested_display_photo_id = photo_id;
			DisplayJob job{.repository = session.repository,
						   .paths	   = *session.paths,
						   .photo_id   = std::move(photo_id),
						   .generation = generation};
			queued_display_jobs.push_back(std::move(job));
			pending_display_photo_ids.push_back(
				queued_display_jobs.back().photo_id);
		}
		condition.notify_one();
	}

	void cancel_display_requests() {
		const std::lock_guard<std::mutex> lock{mutex};
		queued_display_jobs.clear();
		pending_display_photo_ids.clear();
	}

	void invalidate_all() {
		{
			const std::lock_guard<std::mutex> lock{mutex};
			++preview_generation;
			++photo_display.display_request_generation;
			photo_display.requested_display_photo_id.reset();
			photo_display.displayed_photo_id.reset();
			queued_preview_jobs.clear();
			queued_display_jobs.clear();
			pending_preview_jobs.clear();
			pending_display_photo_ids.clear();
			preview_failures.clear();
		}
		preview_cache.clear();
	}

	void release_disposable_preview_memory() {
		{
			const std::lock_guard<std::mutex> lock{mutex};
			if (stopping)
				return;
			++preview_generation;
			queued_preview_jobs.clear();
			pending_preview_jobs.clear();
			preview_failures.clear();
		}
		preview_cache.clear();
	}

	void invalidate_internal_photo(const core::StableIdentifier& photo_id) {
		preview_cache.remove_internal_photo(photo_id);
		const std::lock_guard<std::mutex> lock{mutex};
		++preview_generation;
		if (photo_display.requested_display_photo_id == photo_id
			|| photo_display.displayed_photo_id == photo_id) {
			++photo_display.display_request_generation;
			photo_display.requested_display_photo_id.reset();
			photo_display.displayed_photo_id.reset();
		}
		std::erase_if(preview_failures,
					  [&photo_id](const PreviewFailure& failure) {
			return failure.identity.kind
					   == ImagePreviewRequestKind::InternalPhoto
				   && failure.identity.source_key == photo_id.value();
		});
		std::erase_if(queued_preview_jobs, [&photo_id](const PreviewJob& job) {
			return job.identity().kind == ImagePreviewRequestKind::InternalPhoto
				   && job.identity().source_key == photo_id.value();
		});
		std::erase_if(queued_display_jobs, [&photo_id](const DisplayJob& job) {
			return job.photo_id == photo_id;
		});
		std::erase_if(pending_preview_jobs,
					  [&photo_id](const PendingPreviewJob& pending) {
			return pending.identity.kind
					   == ImagePreviewRequestKind::InternalPhoto
				   && pending.identity.source_key == photo_id.value();
		});
		std::erase(pending_display_photo_ids, photo_id);
	}

	void invalidate_staged_photo(const std::filesystem::path& staged_path) {
		preview_cache.remove_staged_photo(staged_path);
		const ImagePreviewRequestIdentity path_identity =
			make_staged_photo_preview_identity(
				staged_path,
				ImagePreviewSize{.max_width = 1U, .max_height = 1U});
		const std::string source_key = path_identity.source_key;
		const std::lock_guard<std::mutex> lock{mutex};
		++preview_generation;
		std::erase_if(preview_failures,
					  [&source_key](const PreviewFailure& failure) {
			return failure.identity.kind == ImagePreviewRequestKind::StagedPhoto
				   && failure.identity.source_key == source_key;
		});
		std::erase_if(queued_preview_jobs,
					  [&source_key](const PreviewJob& job) {
			return job.identity().kind == ImagePreviewRequestKind::StagedPhoto
				   && job.identity().source_key == source_key;
		});
		std::erase_if(pending_preview_jobs,
					  [&source_key](const PendingPreviewJob& pending) {
			return pending.identity.kind == ImagePreviewRequestKind::StagedPhoto
				   && pending.identity.source_key == source_key;
		});
	}

private:
	struct LifetimeToken final {
		std::atomic_bool alive{true};
	};

	class WorkerIdentifierSource final : public core::IdentifierSource {
	public:
		[[nodiscard]] core::StableIdentifier next_stable_identifier() override {
			return random_identifiers.next_stable_identifier();
		}

		[[nodiscard]] core::OperationIdentifier next_operation_identifier()
			override {
			return random_identifiers.next_operation_identifier();
		}

	private:
		core::RandomIdentifierSource random_identifiers;
	};

	struct PendingPreviewJob final {
		ImagePreviewRequestIdentity identity;
		std::uint64_t generation{};
	};

	struct InternalPreviewJob final {
		catalog::CatalogRepositoryState repository;
		platform::AppPrivatePaths paths;
		core::StableIdentifier photo_id;
		ImagePreviewSize target_size;
		ImagePreviewRequestIdentity identity;
		ImagePreviewRequestPriority priority{
			ImagePreviewRequestPriority::Normal};
		std::uint64_t generation{};
	};

	struct StagedPreviewJob final {
		PendingPhotoSource source;
		ImagePreviewSize target_size;
		ImagePreviewRequestIdentity identity;
		ImagePreviewRequestPriority priority{
			ImagePreviewRequestPriority::Normal};
		std::uint64_t generation{};
	};

	struct PreviewJob final {
		std::variant<InternalPreviewJob, StagedPreviewJob> value;

		[[nodiscard]] const ImagePreviewRequestIdentity& identity() const {
			return std::visit(
				[](const auto& job) -> const ImagePreviewRequestIdentity& {
				return job.identity;
			}, value);
		}

		[[nodiscard]] ImagePreviewRequestPriority priority() const noexcept {
			return std::visit(
				[](const auto& job) noexcept { return job.priority; }, value);
		}

		void raise_priority(ImagePreviewRequestPriority priority_value) {
			std::visit([priority_value](auto& job) {
				if (priority_before(priority_value, job.priority))
					job.priority = priority_value;
			}, value);
		}
	};

	struct DisplayJob final {
		catalog::CatalogRepositoryState repository;
		platform::AppPrivatePaths paths;
		core::StableIdentifier photo_id;
		std::uint64_t generation{};
	};

	struct PreviewResult final {
		ImagePreviewRequestIdentity identity;
		std::optional<platform::ImagePixels> pixels;
		std::optional<catalog::BrokenPhotoPlaceholder> placeholder;
		std::vector<core::Diagnostic> diagnostics;
		bool succeeded{};
		bool cancelled{};
		std::uint64_t generation{};
	};

	struct DisplayResult final {
		core::StableIdentifier photo_id;
		catalog::PhotoDisplayResult result;
		std::uint64_t generation{};
	};

	struct PreviewFailure final {
		ImagePreviewRequestIdentity identity;
		juce::String message;
	};

	[[nodiscard]] static bool contains_preview_identity(
		const std::vector<PendingPreviewJob>& pending_jobs,
		const ImagePreviewRequestIdentity& identity) {
		return std::ranges::find_if(
				   pending_jobs, [&identity](const PendingPreviewJob& pending) {
			return pending.identity == identity;
		}) != pending_jobs.end();
	}

	void erase_pending_preview_job(const ImagePreviewRequestIdentity& identity,
								   std::uint64_t generation) {
		std::erase_if(
			pending_preview_jobs,
			[&identity, generation](const PendingPreviewJob& pending) {
			return pending.identity == identity
				   && pending.generation == generation;
		});
	}

	[[nodiscard]] bool has_preview_failure(
		const ImagePreviewRequestIdentity& identity) const {
		return std::ranges::find_if(preview_failures,
									[&identity](const PreviewFailure& failure) {
			return failure.identity == identity;
		}) != preview_failures.end();
	}

	void erase_preview_failure(const ImagePreviewRequestIdentity& identity) {
		std::erase_if(preview_failures,
					  [&identity](const PreviewFailure& failure) {
			return failure.identity == identity;
		});
	}

	void put_preview_failure(ImagePreviewRequestIdentity identity,
							 juce::String message) {
		erase_preview_failure(identity);
		preview_failures.push_back(PreviewFailure{
			.identity = std::move(identity), .message = std::move(message)});
	}

	void raise_queued_preview_priority(
		const ImagePreviewRequestIdentity& identity,
		ImagePreviewRequestPriority priority) {
		std::vector<PreviewJob>::iterator found = std::ranges::find_if(
			queued_preview_jobs, [&identity](const PreviewJob& job) {
			return job.identity() == identity;
		});
		if (found != queued_preview_jobs.end())
			found->raise_priority(priority);
	}

	[[nodiscard]] bool stale_generation(std::uint64_t generation) const {
		return generation != preview_generation;
	}

	void worker_loop() {
		for (;;) {
			std::optional<DisplayJob> display_job;
			std::optional<PreviewJob> preview_job;
			{
				std::unique_lock<std::mutex> lock{mutex};
				condition.wait(lock, [this] {
					return stopping || !queued_display_jobs.empty()
						   || !queued_preview_jobs.empty();
				});
				if (stopping)
					return;
				if (!queued_display_jobs.empty()) {
					display_job = std::move(queued_display_jobs.front());
					queued_display_jobs.erase(queued_display_jobs.begin());
				} else if (!queued_preview_jobs.empty()) {
					std::vector<PreviewJob>::iterator best =
						std::ranges::max_element(queued_preview_jobs,
												 [](const PreviewJob& left,
													const PreviewJob& right) {
						return priority_before(right.priority(),
											   left.priority());
					});
					preview_job = std::move(*best);
					queued_preview_jobs.erase(best);
				}
			}

			if (display_job.has_value()) {
				process_display_job(std::move(*display_job));
				continue;
			}

			if (preview_job.has_value()) {
				process_preview_job(std::move(*preview_job));
				continue;
			}
		}
	}

	void process_preview_job(PreviewJob job) {
		std::visit([this](auto& concrete_job) {
			PreviewResult result = load_preview(concrete_job);
			post_preview_result(std::move(result));
		}, job.value);
	}

	[[nodiscard]] PreviewResult load_preview(InternalPreviewJob& job) {
		WorkerIdentifierSource identifiers;
		core::OperationGate gate;
		platform::ProgressCollector progress;
		platform::NeverCancelledToken cancellation;
		ImagePreviewCache worker_cache{ImagePreviewCacheSettings{
			.maximum_entries = 1U,
			.maximum_pixel_bytes =
				preview_cache.settings().maximum_pixel_bytes}};
		catalog::PhotoExportUseCase export_use_case{
			identifiers, gate, internal_photo_codec, jpeg_export_service,
			document_export_service};
		InternalPhotoPreviewLoadResult loaded = load_internal_photo_preview(
			InternalPhotoPreviewLoadRequest{.current_state = job.repository,
											.paths		   = job.paths,
											.photo_id	   = job.photo_id,
											.target_size   = job.target_size},
			worker_cache, export_use_case, progress, cancellation);
		return PreviewResult{.identity	  = std::move(job.identity),
							 .pixels	  = std::move(loaded.pixels),
							 .placeholder = std::move(loaded.placeholder),
							 .diagnostics = std::move(loaded.diagnostics),
							 .succeeded	  = loaded.succeeded(),
							 .cancelled	  = loaded.was_user_cancelled(),
							 .generation  = job.generation};
	}

	[[nodiscard]] PreviewResult load_preview(StagedPreviewJob& job) {
		WorkerIdentifierSource identifiers;
		platform::ProgressCollector progress;
		platform::NeverCancelledToken cancellation;
		ImagePreviewCache worker_cache{ImagePreviewCacheSettings{
			.maximum_entries = 1U,
			.maximum_pixel_bytes =
				preview_cache.settings().maximum_pixel_bytes}};
		StagedPhotoPreviewLoadResult loaded = load_staged_photo_preview(
			StagedPhotoPreviewLoadRequest{.source	   = job.source,
										  .identifiers = identifiers,
										  .target_size = job.target_size},
			worker_cache, source_decode_service, progress, cancellation);
		return PreviewResult{.identity	  = std::move(job.identity),
							 .pixels	  = std::move(loaded.pixels),
							 .placeholder = std::move(loaded.placeholder),
							 .diagnostics = std::move(loaded.diagnostics),
							 .succeeded	  = loaded.succeeded(),
							 .cancelled	  = loaded.was_user_cancelled(),
							 .generation  = job.generation};
	}

	void process_display_job(DisplayJob job) {
		WorkerIdentifierSource identifiers;
		core::OperationGate gate;
		platform::ProgressCollector progress;
		platform::NeverCancelledToken cancellation;
		catalog::PhotoExportUseCase export_use_case{
			identifiers, gate, internal_photo_codec, jpeg_export_service,
			document_export_service};
		catalog::PhotoDisplayResult result =
			export_use_case.load_photo_for_display(
				catalog::PhotoDisplayRequest{.current_state = job.repository,
											 .paths			= job.paths,
											 .photo_id		= job.photo_id},
				progress, cancellation);
		post_display_result(DisplayResult{.photo_id	  = std::move(job.photo_id),
										  .result	  = std::move(result),
										  .generation = job.generation});
	}

	void post_preview_result(PreviewResult result) {
		std::weak_ptr<LifetimeToken> lifetime = lifetime_token;
		Impl* scheduler						  = this;
		juce::MessageManager::callAsync(
			[lifetime, scheduler, result = std::move(result)]() mutable {
			std::shared_ptr<LifetimeToken> token = lifetime.lock();
			if (token == nullptr
				|| !token->alive.load(std::memory_order_acquire)) {
				return;
			}
			scheduler->apply_preview_result(std::move(result));
		});
	}

	void post_display_result(DisplayResult result) {
		std::weak_ptr<LifetimeToken> lifetime = lifetime_token;
		Impl* scheduler						  = this;
		juce::MessageManager::callAsync(
			[lifetime, scheduler, result = std::move(result)]() mutable {
			std::shared_ptr<LifetimeToken> token = lifetime.lock();
			if (token == nullptr
				|| !token->alive.load(std::memory_order_acquire)) {
				return;
			}
			scheduler->apply_display_result(std::move(result));
		});
	}

	void apply_preview_result(PreviewResult result) {
		{
			const std::lock_guard<std::mutex> lock{mutex};
			erase_pending_preview_job(result.identity, result.generation);
			if (stopping || stale_generation(result.generation))
				return;
		}
		if (result.succeeded && result.pixels.has_value()) {
			preview_cache.put(std::move(result.identity),
							  std::move(*result.pixels));
		} else if (!result.cancelled) {
			juce::String message = placeholder_text(localization);
			const std::lock_guard<std::mutex> lock{mutex};
			put_preview_failure(std::move(result.identity), std::move(message));
		}
		if (refresh_content_handler)
			refresh_content_handler();
	}

	void apply_display_result(DisplayResult result) {
		{
			const std::lock_guard<std::mutex> lock{mutex};
			std::erase(pending_display_photo_ids, result.photo_id);
			if (stopping)
				return;
		}
		if (photo_display.requested_display_photo_id == result.photo_id
			&& photo_display.display_request_generation == result.generation) {
			photo_display.result			 = std::move(result.result);
			photo_display.displayed_photo_id = result.photo_id;
			if (refresh_content_handler)
				refresh_content_handler();
		}
	}

	CatalogSessionState& session;
	AppShellPhotoDisplayState& photo_display;
	ImagePreviewCache& preview_cache;
	platform::InternalPhotoCodec& internal_photo_codec;
	platform::SourceImageDecodeService& source_decode_service;
	platform::JpegExportService& jpeg_export_service;
	platform::DocumentExportService& document_export_service;
	localization::Localization& localization;
	std::function<void()> refresh_content_handler;

	mutable std::mutex mutex;
	std::condition_variable condition;
	std::vector<PreviewJob> queued_preview_jobs;
	std::vector<DisplayJob> queued_display_jobs;
	std::vector<PendingPreviewJob> pending_preview_jobs;
	std::vector<core::StableIdentifier> pending_display_photo_ids;
	std::vector<PreviewFailure> preview_failures;
	std::shared_ptr<LifetimeToken> lifetime_token{
		std::make_shared<LifetimeToken>()};
	std::thread worker;
	std::uint64_t preview_generation{};
	bool stopping{};
};

AppShellPreviewScheduler::AppShellPreviewScheduler(Dependencies dependencies)
	: impl(std::make_unique<Impl>(std::move(dependencies))) {}

AppShellPreviewScheduler::~AppShellPreviewScheduler() = default;

std::optional<juce::String> AppShellPreviewScheduler::failure_message(
	const ImagePreviewRequestIdentity& identity) const {
	return impl->failure_message(identity);
}

void AppShellPreviewScheduler::clear_failure(
	const ImagePreviewRequestIdentity& identity) {
	impl->clear_failure(identity);
}

void AppShellPreviewScheduler::enqueue_internal_preview(
	core::StableIdentifier photo_id, ImagePreviewSize target_size,
	ImagePreviewRequestPriority priority) {
	impl->enqueue_internal_preview(std::move(photo_id), target_size, priority);
}

void AppShellPreviewScheduler::enqueue_staged_preview(
	PendingPhotoSource source, ImagePreviewSize target_size,
	ImagePreviewRequestPriority priority) {
	impl->enqueue_staged_preview(std::move(source), target_size, priority);
}

void AppShellPreviewScheduler::enqueue_display(
	core::StableIdentifier photo_id) {
	impl->enqueue_display(std::move(photo_id));
}

void AppShellPreviewScheduler::cancel_display_requests() {
	impl->cancel_display_requests();
}

void AppShellPreviewScheduler::invalidate_all() {
	impl->invalidate_all();
}

void AppShellPreviewScheduler::release_disposable_preview_memory() {
	impl->release_disposable_preview_memory();
}

void AppShellPreviewScheduler::invalidate_internal_photo(
	const core::StableIdentifier& photo_id) {
	impl->invalidate_internal_photo(photo_id);
}

void AppShellPreviewScheduler::invalidate_staged_photo(
	const std::filesystem::path& staged_path) {
	impl->invalidate_staged_photo(staged_path);
}
}	 // namespace shuba::ui
