#include "UI/AppShell.hpp"
#include "UI/View/AppShellContentComponent.hpp"
#include "UI/View/Primitives/Palette.hpp"
#include "UI/View/ScreenText.hpp"

#include "UI/Session/BackupRecoverySession.hpp"
#include "UI/Session/CatalogStartupSession.hpp"
#include "UI/Session/EntityEditSession.hpp"
#include "UI/Session/PhotoSession.hpp"

#include "Domain/Domain.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
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
	const catalog::BrokenPhotoPlaceholder& placeholder) {
	return placeholder.message.empty()
			   ? juce::String{"Photo preview is unavailable."}
			   : juce_text(placeholder.message);
}
}	 // namespace

class AsyncImagePreviewScheduler final {
public:
	struct Dependencies final {
		CatalogSessionState& session;
		AppShellPhotoDisplayState& photo_display;
		ImagePreviewCache& preview_cache;
		platform::InternalPhotoCodec& internal_photo_codec;
		platform::SourceImageDecodeService& source_decode_service;
		platform::JpegExportService& jpeg_export_service;
		platform::DocumentExportService& document_export_service;
		std::function<void()> refresh_content;
	};

	explicit AsyncImagePreviewScheduler(Dependencies dependencies)
		: session(dependencies.session)
		, photo_display(dependencies.photo_display)
		, preview_cache(dependencies.preview_cache)
		, internal_photo_codec(dependencies.internal_photo_codec)
		, source_decode_service(dependencies.source_decode_service)
		, jpeg_export_service(dependencies.jpeg_export_service)
		, document_export_service(dependencies.document_export_service)
		, refresh_content_handler(std::move(dependencies.refresh_content)) {
		worker = std::thread([this] { worker_loop(); });
	}

	~AsyncImagePreviewScheduler() {
		lifetime_token->alive.store(false, std::memory_order_release);
		{
			const std::lock_guard<std::mutex> lock{mutex};
			stopping = true;
			queued_preview_jobs.clear();
			queued_display_jobs.clear();
			pending_preview_identities.clear();
			pending_display_photo_ids.clear();
		}
		condition.notify_all();
		if (worker.joinable())
			worker.join();
	}

	AsyncImagePreviewScheduler(const AsyncImagePreviewScheduler&) = delete;
	AsyncImagePreviewScheduler& operator=(const AsyncImagePreviewScheduler&) =
		delete;
	AsyncImagePreviewScheduler(AsyncImagePreviewScheduler&&) noexcept = delete;
	AsyncImagePreviewScheduler& operator=(
		AsyncImagePreviewScheduler&&) noexcept = delete;

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
			if (contains_preview_identity(pending_preview_identities,
										  identity)) {
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
			pending_preview_identities.push_back(std::move(identity));
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
			if (contains_preview_identity(pending_preview_identities,
										  identity)) {
				raise_queued_preview_priority(identity, priority);
				return;
			}
			StagedPreviewJob job{.source	  = std::move(source),
								 .target_size = target_size,
								 .identity	  = identity,
								 .priority	  = priority,
								 .generation  = preview_generation};
			queued_preview_jobs.push_back(PreviewJob{std::move(job)});
			pending_preview_identities.push_back(std::move(identity));
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
			pending_preview_identities.clear();
			pending_display_photo_ids.clear();
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
		std::erase_if(pending_preview_identities,
					  [&photo_id](const ImagePreviewRequestIdentity& identity) {
			return identity.kind == ImagePreviewRequestKind::InternalPhoto
				   && identity.source_key == photo_id.value();
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
		std::erase_if(
			pending_preview_identities,
			[&source_key](const ImagePreviewRequestIdentity& identity) {
			return identity.kind == ImagePreviewRequestKind::StagedPhoto
				   && identity.source_key == source_key;
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
		const std::vector<ImagePreviewRequestIdentity>& identities,
		const ImagePreviewRequestIdentity& identity) {
		return std::ranges::find(identities, identity) != identities.end();
	}

	[[nodiscard]] static bool contains_photo_id(
		const std::vector<core::StableIdentifier>& photo_ids,
		const core::StableIdentifier& photo_id) {
		return std::ranges::find(photo_ids, photo_id) != photo_ids.end();
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
		AsyncImagePreviewScheduler* scheduler = this;
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
		AsyncImagePreviewScheduler* scheduler = this;
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
			std::erase(pending_preview_identities, result.identity);
			if (stopping || stale_generation(result.generation))
				return;
		}
		if (result.succeeded && result.pixels.has_value()) {
			preview_cache.put(std::move(result.identity),
							  std::move(*result.pixels));
		} else if (!result.cancelled) {
			juce::String message{"Photo preview is unavailable."};
			if (result.placeholder.has_value())
				message = placeholder_text(*result.placeholder);
			else if (!result.diagnostics.empty())
				message = juce_text(result.diagnostics.front().message);
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
	std::function<void()> refresh_content_handler;

	mutable std::mutex mutex;
	std::condition_variable condition;
	std::vector<PreviewJob> queued_preview_jobs;
	std::vector<DisplayJob> queued_display_jobs;
	std::vector<ImagePreviewRequestIdentity> pending_preview_identities;
	std::vector<core::StableIdentifier> pending_display_photo_ids;
	std::vector<PreviewFailure> preview_failures;
	std::shared_ptr<LifetimeToken> lifetime_token{
		std::make_shared<LifetimeToken>()};
	std::thread worker;
	std::uint64_t preview_generation{};
	bool stopping{};
};

core::StableIdentifier ShellIdentifierSource::next_stable_identifier() {
	return random_identifiers.next_stable_identifier();
}

core::OperationIdentifier ShellIdentifierSource::next_operation_identifier() {
	return random_identifiers.next_operation_identifier();
}

core::EpochMilliseconds ShellClock::now() const {
	return core::SystemClock{}.now();
}

AppShellComponent::AppShellComponent(CatalogSessionState session_state,
									 PlatformServices platform_services)
	: session(std::move(session_state))
	, internal_photo_codec(platform_services.internal_photo_codec)
	, content(std::make_unique<AppShellContentComponent>()) {
	setOpaque(true);
	setSize(480, 720);

	chrome = std::make_unique<AppShellChromeComponent>(
		AppShellChromeComponent::Callbacks{.catalog_search_changed = [this] {
		schedule_content_refresh();
	}, .storage_search_changed = [this] {
		schedule_content_refresh();
	}, .catalog_clear = [this] {
		chrome->clear_catalog_query_without_notification();
		refresh_content();
	}, .catalog_filter = [this] {
		catalog_filter_state.draft = catalog_filter_state.applied;
		catalog_filter_state.panel_visible =
			!catalog_filter_state.panel_visible;
		refresh_all();
	}, .catalog_clear_filters = [this] {
		reset_catalog_filters();
		refresh_all();
	}, .storage_clear = [this] {
		chrome->clear_storage_query_without_notification();
		refresh_content();
	}, .back = [this] {
		if (route.destination == RootDestination::ItemDetail) {
			select_root(RootDestination::Catalog);
		} else if (route.destination == RootDestination::PhotoViewer
				   && route.selected_photo_owner
				   && route.selected_photo_owner->type
						  == domain::PhotoOwnerType::Item) {
			route.selected_item_id = route.selected_photo_owner->id;
			select_root(RootDestination::ItemDetail);
		} else if (route.destination == RootDestination::PhotoViewer
				   && route.selected_photo_owner
				   && route.selected_photo_owner->type
						  == domain::PhotoOwnerType::Storage) {
			route.selected_storage_id = route.selected_photo_owner->id;
			select_root(RootDestination::StorageDetail);
		} else if (route.destination == RootDestination::ItemForm
				   || route.destination == RootDestination::StorageForm) {
			select_root(route.form_return_destination.value_or(
				RootDestination::Catalog));
		} else if (route.destination == RootDestination::BackupRecovery) {
			select_root(RootDestination::More);
		} else {
			select_root(RootDestination::Storages);
		}
	}, .form_cancel = [this] {
		select_root(
			route.form_return_destination.value_or(RootDestination::Catalog));
	}, .form_save = [this] {
		if (route.destination == RootDestination::ItemForm)
			save_item_form();
		else if (route.destination == RootDestination::StorageForm)
			save_storage_form();
	}, .select_catalog = [this] {
		select_root(RootDestination::Catalog);
	}, .select_storages = [this] {
		select_root(RootDestination::Storages);
	}, .select_add = [this] {
		select_root(RootDestination::Add);
	}, .select_more = [this] { select_root(RootDestination::More); }});
	addAndMakeVisible(*chrome);

	photo_coordinator = std::make_unique<AppShellPhotoCoordinator>(
		AppShellPhotoCoordinator::Dependencies{
			.session					= session,
			.route						= route,
			.item_form					= item_form,
			.storage_form				= storage_form,
			.feedback					= feedback,
			.photo_display				= photo_display,
			.preview_cache				= preview_cache,
			.identifiers				= edit_identifiers,
			.clock						= edit_clock,
			.operation_gate				= ui_operation_gate,
			.photo_selection_service	= photo_selection_service,
			.document_export_service	= document_export_service,
			.content_staging_service	= content_staging_service,
			.source_fingerprint_service = source_fingerprint_service,
			.source_decode_service		= source_decode_service,
			.jpeg_export_service		= jpeg_export_service,
			.internal_photo_codec		= internal_photo_codec,
			.progress_events			= last_progress_events,
			.cancellation_token			= never_cancelled,
			.invalidate_all_previews	= [this] { invalidate_all_previews(); },
			.invalidate_internal_photo_preview =
				[this](const core::StableIdentifier& photo_id) {
		invalidate_internal_photo_preview(photo_id);
	},
			.invalidate_staged_photo_preview =
				[this](const std::filesystem::path& staged_path) {
		invalidate_staged_photo_preview(staged_path);
	},
			.refresh_all = [this] { refresh_all(); }});

	preview_scheduler = std::make_unique<AsyncImagePreviewScheduler>(
		AsyncImagePreviewScheduler::Dependencies{
			.session				 = session,
			.photo_display			 = photo_display,
			.preview_cache			 = preview_cache,
			.internal_photo_codec	 = internal_photo_codec,
			.source_decode_service	 = source_decode_service,
			.jpeg_export_service	 = jpeg_export_service,
			.document_export_service = document_export_service,
			.refresh_content		 = [this] { refresh_content(); }});

	screen_renderer = std::make_unique<AppShellScreenRenderer>(
		AppShellScreenRenderer::Dependencies{
			.session = session,
			.route = route,
			.catalog_filter_state = catalog_filter_state,
			.item_form = item_form,
			.storage_form = storage_form,
			.feedback = feedback,
			.backup = backup,
			.photo_display = photo_display,
			.storage_detail = storage_detail,
			.preview_cache = preview_cache,
			.edit_identifiers = edit_identifiers,
			.edit_clock = edit_clock,
			.ui_operation_gate = ui_operation_gate,
			.internal_photo_codec = internal_photo_codec,
			.source_decode_service = source_decode_service,
			.jpeg_export_service = jpeg_export_service,
			.document_export_service = document_export_service,
			.last_progress_events = last_progress_events,
			.never_cancelled = never_cancelled,
			.content = *content,
			.editors = AppShellScreenRenderer::Editors{
				.item_name_editor = item_name_editor,
				.item_category_editor = item_category_editor,
				.item_notes_editor = item_notes_editor,
				.item_listing_marketplace_editor =
					item_listing_marketplace_editor,
				.item_listing_url_editor = item_listing_url_editor,
				.item_listing_note_editor = item_listing_note_editor,
				.item_acquisition_source_editor =
					item_acquisition_source_editor,
				.storage_name_editor = storage_name_editor,
				.storage_type_editor = storage_type_editor,
				.storage_location_editor = storage_location_editor,
				.storage_notes_editor = storage_notes_editor},
			.queries = AppShellScreenRenderer::Queries{
				.catalog_query = [this] {
					return chrome != nullptr ? chrome->catalog_query()
									 : std::string{};
				},
				.storage_query = [this] {
					return chrome != nullptr ? chrome->storage_query()
									 : std::string{};
				}},
			.actions = AppShellScreenRenderer::Actions{
				.select_root = [this](RootDestination destination_value) {
					select_root(destination_value);
				},
				.open_item_detail = [this](core::StableIdentifier item_id) {
					open_item_detail(std::move(item_id));
				},
				.open_storage_detail =
					[this](core::StableIdentifier storage_id) {
						open_storage_detail(std::move(storage_id));
					},
				.open_photo_viewer =
					[this](domain::PhotoOwner owner,
						   std::optional<core::StableIdentifier> photo_id) {
						open_photo_viewer(std::move(owner), std::move(photo_id));
					},
				.open_new_item_form =
					[this](std::optional<core::StableIdentifier> storage_id) {
						open_new_item_form(std::move(storage_id));
					},
				.open_existing_item_form =
					[this](core::StableIdentifier item_id) {
						open_existing_item_form(std::move(item_id));
					},
				.open_new_storage_form =
					[this](std::optional<core::StableIdentifier> parent_id) {
						open_new_storage_form(std::move(parent_id));
					},
				.open_existing_storage_form =
					[this](core::StableIdentifier storage_id) {
						open_existing_storage_form(std::move(storage_id));
					},
				.request_add_photos = [this](domain::PhotoOwner owner) {
					request_add_photos(std::move(owner));
				},
				.request_add_pending_item_photos = [this] {
					request_add_pending_item_photos();
				},
				.request_add_pending_storage_photos = [this] {
					request_add_pending_storage_photos();
				},
				.request_export_photo =
					[this](core::StableIdentifier photo_id) {
						request_export_photo(std::move(photo_id));
					},
				.request_export_backup = [this] { request_export_backup(); },
				.request_export_diagnostic_archive = [this] {
					request_export_diagnostic_archive();
				},
				.request_import_backup = [this] { request_import_backup(); },
				.confirm_staged_backup_import = [this] {
					confirm_staged_backup_import();
				},
				.cleanup_item_pending_photos = [this] {
					cleanup_item_pending_photos();
				},
				.cleanup_storage_pending_photos = [this] {
					cleanup_storage_pending_photos();
				},
				.remove_item_pending_photo = [this](std::size_t index) {
					remove_item_pending_photo(index);
				},
				.remove_storage_pending_photo = [this](std::size_t index) {
					remove_storage_pending_photo(index);
				},
				.set_item_pending_photo_as_main = [this](std::size_t index) {
					set_item_pending_photo_as_main(index);
				},
				.set_storage_pending_photo_as_main = [this](std::size_t index) {
					set_storage_pending_photo_as_main(index);
				},
				.request_delete_photo =
					[this](core::StableIdentifier photo_id) {
						request_delete_photo_confirmation(std::move(photo_id));
					},
				.confirm_delete_photo =
					[this](core::StableIdentifier photo_id) {
						confirm_delete_photo(std::move(photo_id));
					},
				.cancel_delete_photo = [this] {
					cancel_delete_photo_confirmation();
				},
				.reset_catalog_filters = [this] { reset_catalog_filters(); },
				.apply_entity_edit_result = [this](EntityEditResult result) {
					apply_entity_edit_result(std::move(result));
				},
				.apply_photo_edit_result =
					[this](EntityEditResult result,
						   core::StableIdentifier selected_photo_id) {
						apply_photo_edit_result(std::move(result),
											std::move(selected_photo_id));
					},
				.request_internal_preview =
					[this](core::StableIdentifier photo_id,
						   ImagePreviewSize target_size,
						   ImagePreviewRequestPriority priority) {
						request_internal_preview_async(
							std::move(photo_id), target_size, priority);
					},
				.request_staged_preview =
					[this](PendingPhotoSource source,
						   ImagePreviewSize target_size,
						   ImagePreviewRequestPriority priority) {
						request_staged_preview_async(
							std::move(source), target_size, priority);
					},
				.preview_failure_message =
					[this](const ImagePreviewRequestIdentity& identity) {
						return preview_failure_message(identity);
					},
				.request_photo_display = [this](core::StableIdentifier photo_id) {
					request_photo_display_async(std::move(photo_id));
				},
				.refresh_all = [this] { refresh_all(); },
				.refresh_content = [this] { refresh_content(); }}});

	viewport.setViewedComponent(content.get(), false);
	viewport.setScrollBarsShown(true, false);
	viewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::nonHover);
	addAndMakeVisible(viewport);

	refresh_all();
}

AppShellComponent::~AppShellComponent() {
	stopTimer();
	preview_scheduler.reset();
}

void AppShellComponent::paint(juce::Graphics& graphics) {
	graphics.fillAll(background_colour());
}

void AppShellComponent::resized() {
	juce::Rectangle<int> bounds = getLocalBounds().reduced(10);
	if (chrome != nullptr) {
		chrome->setBounds(getLocalBounds());
		bounds = chrome->layout_shell(bounds);
	}
	viewport.setBounds(bounds);
	if (content) {
		content->set_viewport_height_hint(bounds.getHeight());
		content->setSize(viewport.getWidth(), content->getHeight());
	}
}

void AppShellComponent::build_catalog_content() {
	if (screen_renderer != nullptr)
		screen_renderer->build_catalog_content();
}

void AppShellComponent::build_filter_panel() {
	if (screen_renderer != nullptr)
		screen_renderer->build_filter_panel();
}

void AppShellComponent::build_storages_content() {
	if (screen_renderer != nullptr)
		screen_renderer->build_storages_content();
}

void AppShellComponent::build_item_detail_content() {
	if (screen_renderer != nullptr)
		screen_renderer->build_item_detail_content();
}

void AppShellComponent::build_storage_detail_content() {
	if (screen_renderer != nullptr)
		screen_renderer->build_storage_detail_content();
}

void AppShellComponent::build_item_form_content() {
	if (screen_renderer != nullptr)
		screen_renderer->build_item_form_content();
}

void AppShellComponent::build_storage_form_content() {
	if (screen_renderer != nullptr)
		screen_renderer->build_storage_form_content();
}

void AppShellComponent::build_photo_viewer_content() {
	if (screen_renderer != nullptr)
		screen_renderer->build_photo_viewer_content();
}

void AppShellComponent::build_backup_recovery_content() {
	if (screen_renderer != nullptr)
		screen_renderer->build_backup_recovery_content();
}

void AppShellComponent::build_add_content() {
	if (screen_renderer != nullptr)
		screen_renderer->build_add_content();
}

void AppShellComponent::build_more_content() {
	if (screen_renderer != nullptr)
		screen_renderer->build_more_content();
}

void AppShellComponent::select_root(RootDestination destination_value) {
	const RootDestination previous_destination = route.destination;
	if (previous_destination == RootDestination::ItemForm
		&& destination_value != RootDestination::ItemForm) {
		cleanup_item_pending_photos();
	}
	if (previous_destination == RootDestination::StorageForm
		&& destination_value != RootDestination::StorageForm) {
		cleanup_storage_pending_photos();
	}
	route.destination = destination_value;
	if (route.destination != RootDestination::ItemDetail
		&& route.destination != RootDestination::ItemForm
		&& !(route.destination == RootDestination::PhotoViewer
			 && route.selected_photo_owner
			 && route.selected_photo_owner->type
					== domain::PhotoOwnerType::Item)) {
		route.selected_item_id.reset();
	}
	if (route.destination != RootDestination::StorageDetail
		&& route.destination != RootDestination::StorageForm
		&& route.destination != RootDestination::ItemForm
		&& !(route.destination == RootDestination::PhotoViewer
			 && route.selected_photo_owner
			 && route.selected_photo_owner->type
					== domain::PhotoOwnerType::Storage)) {
		route.selected_storage_id.reset();
	}
	if (route.destination != RootDestination::PhotoViewer) {
		route.selected_photo_owner.reset();
		route.selected_photo_id.reset();
		photo_display.displayed_photo_id.reset();
		photo_display.requested_display_photo_id.reset();
		++photo_display.display_request_generation;
		photo_display.viewer_transform_photo_id.reset();
		photo_display.viewer_rotation_quarter_turns = 0;
		photo_display.result = catalog::PhotoDisplayResult{};
	}
	if (destination_value != previous_destination)
		photo_display.pending_delete_photo_id.reset();
	if (route.destination != RootDestination::ItemForm
		&& route.destination != RootDestination::StorageForm) {
		route.form_return_destination.reset();
	}
	if (route.destination != RootDestination::BackupRecovery) {
		backup.pending_import_staging.reset();
		backup.pending_import_degraded_acknowledged = false;
	}
	refresh_all();
}

void AppShellComponent::open_item_detail(core::StableIdentifier item_id) {
	route.selected_item_id = std::move(item_id);
	route.destination	   = RootDestination::ItemDetail;
	refresh_all();
}

void AppShellComponent::open_storage_detail(core::StableIdentifier storage_id) {
	route.selected_storage_id	  = std::move(storage_id);
	route.destination			  = RootDestination::StorageDetail;
	storage_detail.include_nested = true;
	refresh_all();
}

void AppShellComponent::open_photo_viewer(
	domain::PhotoOwner owner,
	std::optional<core::StableIdentifier> requested_photo_id) {
	route.selected_photo_owner = owner;
	route.selected_photo_id =
		requested_photo_id.has_value()
			? requested_photo_id
			: first_viewable_photo_id(session.repository, owner);
	if (owner.type == domain::PhotoOwnerType::Item)
		route.selected_item_id = owner.id;
	else
		route.selected_storage_id = owner.id;
	feedback.photo_message.clear();
	feedback.photo_diagnostics.clear();
	photo_display.pending_delete_photo_id.reset();
	photo_display.displayed_photo_id.reset();
	photo_display.requested_display_photo_id.reset();
	++photo_display.display_request_generation;
	photo_display.viewer_transform_photo_id.reset();
	photo_display.viewer_rotation_quarter_turns = 0;
	photo_display.result						= catalog::PhotoDisplayResult{};
	route.destination							= RootDestination::PhotoViewer;
	refresh_all();
}

void AppShellComponent::open_new_item_form(
	std::optional<core::StableIdentifier> storage_id) {
	reset_item_form();
	item_form.mode				  = FormMode::Create;
	item_form.draft.storage_id	  = std::move(storage_id);
	route.form_return_destination = route.selected_storage_id
										? RootDestination::StorageDetail
										: RootDestination::Add;
	route.destination			  = RootDestination::ItemForm;
	refresh_all();
}

void AppShellComponent::open_existing_item_form(
	core::StableIdentifier item_id) {
	const persistence::ItemEnvelope* item =
		catalog::find_item_envelope(session.repository, item_id);
	if (item == nullptr)
		return;
	load_item_form_from_record(*item);
	route.selected_item_id		  = std::move(item_id);
	item_form.mode				  = FormMode::Edit;
	route.form_return_destination = RootDestination::ItemDetail;
	route.destination			  = RootDestination::ItemForm;
	refresh_all();
}

void AppShellComponent::open_new_storage_form(
	std::optional<core::StableIdentifier> parent_id) {
	reset_storage_form();
	storage_form.mode					 = FormMode::Create;
	storage_form.draft.parent_storage_id = std::move(parent_id);
	route.form_return_destination		 = route.selected_storage_id
											   ? RootDestination::StorageDetail
											   : RootDestination::Add;
	route.destination					 = RootDestination::StorageForm;
	refresh_all();
}

void AppShellComponent::open_existing_storage_form(
	core::StableIdentifier storage_id) {
	const persistence::StorageEnvelope* storage =
		catalog::find_storage_envelope(session.repository, storage_id);
	if (storage == nullptr)
		return;
	load_storage_form_from_record(*storage);
	route.selected_storage_id	  = std::move(storage_id);
	storage_form.mode			  = FormMode::Edit;
	route.form_return_destination = RootDestination::StorageDetail;
	route.destination			  = RootDestination::StorageForm;
	refresh_all();
}

void AppShellComponent::request_add_photos(domain::PhotoOwner owner) {
	if (photo_coordinator != nullptr)
		photo_coordinator->request_add_photos(std::move(owner));
}

void AppShellComponent::request_add_pending_item_photos() {
	if (photo_coordinator != nullptr)
		photo_coordinator->request_add_pending_item_photos();
}

void AppShellComponent::request_add_pending_storage_photos() {
	if (photo_coordinator != nullptr)
		photo_coordinator->request_add_pending_storage_photos();
}

void AppShellComponent::set_item_pending_photo_as_main(
	std::size_t pending_photo_index) {
	set_pending_photo_as_main(item_form.photo_deck, item_form.pending_photos,
						  pending_photo_index);
}

void AppShellComponent::set_storage_pending_photo_as_main(
	std::size_t pending_photo_index) {
	set_pending_photo_as_main(storage_form.photo_deck,
						  storage_form.pending_photos, pending_photo_index);
}

void AppShellComponent::request_export_photo(core::StableIdentifier photo_id) {
	if (photo_coordinator != nullptr)
		photo_coordinator->request_export_photo(std::move(photo_id));
}

void AppShellComponent::request_delete_photo_confirmation(
	core::StableIdentifier photo_id) {
	if (photo_coordinator != nullptr)
		photo_coordinator->request_delete_photo_confirmation(
			std::move(photo_id));
}

void AppShellComponent::confirm_delete_photo(core::StableIdentifier photo_id) {
	if (photo_coordinator != nullptr)
		photo_coordinator->confirm_delete_photo(std::move(photo_id));
}

void AppShellComponent::cancel_delete_photo_confirmation() {
	if (photo_coordinator != nullptr)
		photo_coordinator->cancel_delete_photo_confirmation();
}

void AppShellComponent::apply_photo_edit_result(
	EntityEditResult result, core::StableIdentifier selected_photo_id_value) {
	if (photo_coordinator != nullptr) {
		photo_coordinator->apply_photo_edit_result(
			std::move(result), std::move(selected_photo_id_value));
	}
}

void AppShellComponent::cleanup_item_pending_photos() {
	if (photo_coordinator != nullptr)
		photo_coordinator->cleanup_item_pending_photos();
}

void AppShellComponent::cleanup_storage_pending_photos() {
	if (photo_coordinator != nullptr)
		photo_coordinator->cleanup_storage_pending_photos();
}

void AppShellComponent::remove_item_pending_photo(
	std::size_t pending_photo_index) {
	if (photo_coordinator != nullptr)
		photo_coordinator->remove_item_pending_photo(pending_photo_index);
}

void AppShellComponent::remove_storage_pending_photo(
	std::size_t pending_photo_index) {
	if (photo_coordinator != nullptr)
		photo_coordinator->remove_storage_pending_photo(pending_photo_index);
}

void AppShellComponent::set_pending_photo_as_main(
	AppShellManagedPhotoDeckState& photo_deck,
	std::vector<PendingPhotoSource>& pending_photos,
	std::size_t pending_photo_index) {
	if (pending_photo_index >= pending_photos.size()
		|| !pending_photos[pending_photo_index].ready_for_import()) {
		photo_deck.staged_main_index.reset();
		feedback.photo_message =
			"Selected staged photo is not ready to become main.";
		refresh_content();
		return;
	}

	photo_deck.staged_main_index = pending_photo_index;
	photo_deck.staged_selected	= true;
	photo_deck.selected_index	= pending_photo_index;
	feedback.photo_message =
		"Staged photo will become main after the edit is saved.";
	refresh_content();
}

void AppShellComponent::reset_catalog_filters() {
	catalog_filter_state.applied	   = catalog::CatalogSearchFilters{};
	catalog_filter_state.draft		   = catalog_filter_state.applied;
	catalog_filter_state.panel_visible = false;
}

void AppShellComponent::reset_item_form() {
	cleanup_item_pending_photos();
	item_form.draft						  = ItemDraft{};
	item_form.mode						  = FormMode::Create;
	item_form.storage_candidates_expanded = false;
	item_form.tag_candidates_expanded	  = false;
	item_form.listing_expanded			  = false;
	item_form.finance_expanded			  = false;
	item_form.photo_deck				  = AppShellManagedPhotoDeckState{};
	for (juce::TextEditor* editor :
		 {&item_name_editor, &item_category_editor, &item_notes_editor,
		  &item_listing_marketplace_editor, &item_listing_url_editor,
		  &item_listing_note_editor, &item_acquisition_source_editor}) {
		editor->setText(juce::String{}, juce::dontSendNotification);
	}
}

void AppShellComponent::reset_storage_form() {
	cleanup_storage_pending_photos();
	storage_form.draft						  = StorageDraft{};
	storage_form.mode						  = FormMode::Create;
	storage_form.parent_candidates_expanded	  = false;
	storage_form.tag_candidates_expanded	  = false;
	storage_form.archive_warning_acknowledged = false;
	storage_form.photo_deck					  = AppShellManagedPhotoDeckState{};
	for (juce::TextEditor* editor :
		 {&storage_name_editor, &storage_type_editor, &storage_location_editor,
		  &storage_notes_editor}) {
		editor->setText(juce::String{}, juce::dontSendNotification);
	}
}

void AppShellComponent::load_item_form_from_record(
	const persistence::ItemEnvelope& item) {
	item_form.draft = ItemDraft{.existing_id  = item.record.id,
								.display_name = item.record.display_name,
								.category	  = item.record.category,
								.storage_id	  = item.record.storage_id,
								.tags		  = item.record.tags,
								.notes		  = item.record.notes,
								.status		  = item.record.status,
								.listing	  = item.record.listing,
								.acquisition  = item.record.acquisition,
								.finance	  = item.record.finance,
								.warning_acknowledged = true};
	item_name_editor.setText(juce_text(item.record.display_name),
							 juce::dontSendNotification);
	item_category_editor.setText(juce_text(item.record.category),
								 juce::dontSendNotification);
	item_notes_editor.setText(juce_text(item.record.notes),
							  juce::dontSendNotification);
	item_listing_marketplace_editor.setText(
		juce_text(item.record.listing.marketplace), juce::dontSendNotification);
	item_listing_url_editor.setText(juce_text(item.record.listing.url),
									juce::dontSendNotification);
	item_listing_note_editor.setText(juce_text(item.record.listing.note),
									 juce::dontSendNotification);
	item_acquisition_source_editor.setText(
		juce_text(item.record.acquisition.source), juce::dontSendNotification);
	item_form.listing_expanded = !item.record.listing.empty();
	item_form.finance_expanded =
		!item.record.acquisition.empty() || !item.record.finance.empty();
	item_form.storage_candidates_expanded = false;
	item_form.tag_candidates_expanded	  = false;
	item_form.photo_deck				  = AppShellManagedPhotoDeckState{};
}

void AppShellComponent::load_storage_form_from_record(
	const persistence::StorageEnvelope& storage) {
	storage_form.draft =
		StorageDraft{.existing_id		= storage.record.id,
					 .display_name		= storage.record.display_name,
					 .storage_type		= storage.record.storage_type,
					 .parent_storage_id = storage.record.parent_storage_id,
					 .location			= storage.record.location,
					 .tags				= storage.record.tags,
					 .notes				= storage.record.notes,
					 .lifecycle_status	= storage.record.lifecycle_status,
					 .archive_warning_acknowledged = true};
	storage_name_editor.setText(juce_text(storage.record.display_name),
								juce::dontSendNotification);
	storage_type_editor.setText(juce_text(storage.record.storage_type),
								juce::dontSendNotification);
	storage_location_editor.setText(juce_text(storage.record.location),
									juce::dontSendNotification);
	storage_notes_editor.setText(juce_text(storage.record.notes),
								 juce::dontSendNotification);
	storage_form.parent_candidates_expanded	  = false;
	storage_form.tag_candidates_expanded	  = false;
	storage_form.archive_warning_acknowledged = false;
	storage_form.photo_deck					  = AppShellManagedPhotoDeckState{};
}

void AppShellComponent::save_item_form() {
	item_form.draft.display_name = item_name_editor.getText().toStdString();
	item_form.draft.category	 = item_category_editor.getText().toStdString();
	item_form.draft.notes		 = item_notes_editor.getText().toStdString();
	item_form.draft.listing.marketplace =
		item_listing_marketplace_editor.getText().toStdString();
	item_form.draft.listing.url =
		item_listing_url_editor.getText().toStdString();
	item_form.draft.listing.note =
		item_listing_note_editor.getText().toStdString();
	item_form.draft.acquisition.source =
		item_acquisition_source_editor.getText().toStdString();
	item_form.draft.pending_photo_import_planned =
		has_ready_pending_photo(item_form.pending_photos);

	ItemSaveWithPendingPhotosResult result =
		save_item_draft_and_import_pending_photos(
			ItemSaveWithPendingPhotosRequest{
				.current_session	 = session,
				.identifiers		 = edit_identifiers,
				.clock				 = edit_clock,
				.operation_gate		 = ui_operation_gate,
				.staging_service	 = content_staging_service,
				.fingerprint_service = source_fingerprint_service,
				.decode_service		 = source_decode_service,
				.photo_codec		 = internal_photo_codec,
				.draft				 = item_form.draft,
				.pending_sources	 = item_form.pending_photos,
				.main_pending_source_index =
					item_form.photo_deck.staged_main_index},
			last_progress_events, never_cancelled);
	if (result.warning_acknowledgement_required())
		item_form.draft.warning_acknowledged = true;
	if (result.warning_acknowledgement_required()
		&& result.save_result.saved_record_id) {
		item_form.draft.reserved_new_id = result.save_result.saved_record_id;
	}
	if (result.item_saved())
		item_form.draft.existing_id = result.save_result.saved_record_id;
	apply_item_save_with_pending_photos_result(std::move(result));
}

void AppShellComponent::save_storage_form() {
	storage_form.draft.display_name =
		storage_name_editor.getText().toStdString();
	storage_form.draft.storage_type =
		storage_type_editor.getText().toStdString();
	storage_form.draft.location =
		storage_location_editor.getText().toStdString();
	storage_form.draft.notes = storage_notes_editor.getText().toStdString();
	storage_form.draft.archive_warning_acknowledged =
		storage_form.archive_warning_acknowledged;
	StorageSaveWithPendingPhotosResult result =
		save_storage_draft_and_import_pending_photos(
			StorageSaveWithPendingPhotosRequest{
				.current_session	 = session,
				.identifiers		 = edit_identifiers,
				.clock				 = edit_clock,
				.operation_gate		 = ui_operation_gate,
				.staging_service	 = content_staging_service,
				.fingerprint_service = source_fingerprint_service,
				.decode_service		 = source_decode_service,
				.photo_codec		 = internal_photo_codec,
				.draft				 = storage_form.draft,
				.pending_sources	 = storage_form.pending_photos,
				.main_pending_source_index =
					storage_form.photo_deck.staged_main_index},
			last_progress_events, never_cancelled);
	if (result.warning_acknowledgement_required())
		storage_form.archive_warning_acknowledged = true;
	if (result.warning_acknowledgement_required()
		&& result.save_result.saved_record_id) {
		storage_form.draft.reserved_new_id = result.save_result.saved_record_id;
	}
	if (result.storage_saved())
		storage_form.draft.existing_id = result.save_result.saved_record_id;
	apply_storage_save_with_pending_photos_result(std::move(result));
}

void AppShellComponent::apply_item_save_with_pending_photos_result(
	ItemSaveWithPendingPhotosResult result) {
	feedback.edit_diagnostics = result.save_result.diagnostics;
	item_form.pending_photos  = std::move(result.pending_sources);
	std::erase_if(item_form.pending_photos,
				  [](const PendingPhotoSource& source) {
		return !source.staged_path.has_value();
	});
	if (result.warning_acknowledgement_required()) {
		feedback.edit_message = "Confirm warning and save again.";
		refresh_all();
		return;
	}
	if (result.save_result.failed()) {
		feedback.edit_message = "Save failed.";
		refresh_all();
		return;
	}

	feedback.photo_diagnostics.clear();
	if (result.import_attempted) {
		for (const EntityEditDiagnostic& diagnostic :
			 result.import_result.diagnostics) {
			feedback.photo_diagnostics.push_back(core::Diagnostic{
				.severity		   = diagnostic.severity,
				.code			   = diagnostic.code,
				.message		   = diagnostic.message,
				.technical_details = diagnostic.technical_details});
		}
		feedback.photo_diagnostics.insert(
			feedback.photo_diagnostics.end(),
			result.cleanup_result.diagnostics.begin(),
			result.cleanup_result.diagnostics.end());
		if (result.import_result.succeeded()) {
			feedback.photo_message =
				"Pending photo import completed: "
				+ std::to_string(result.import_result.summary.success_count)
				+ " imported, "
				+ std::to_string(result.import_result.summary.failure_count)
				+ " failed.";
		} else if (result.import_result.was_user_cancelled()) {
			feedback.photo_message =
				"Item saved, pending photo import cancelled.";
		} else {
			feedback.photo_message =
				"Item saved, but pending photo import failed.";
		}
		if (result.cleanup_attempted && result.cleanup_result.failed())
			feedback.photo_message +=
				" Pending source cleanup needs attention.";
		if (result.main_selected_photo_id.has_value())
			route.selected_photo_id = result.main_selected_photo_id;
		else if (!result.import_result.imported_photo_ids.empty())
			route.selected_photo_id =
				result.import_result.imported_photo_ids.front();
		if (result.main_selection_attempted) {
			for (const EntityEditDiagnostic& diagnostic :
				 result.main_selection_result.diagnostics) {
				feedback.photo_diagnostics.push_back(core::Diagnostic{
					.severity		 = diagnostic.severity,
					.code			 = diagnostic.code,
					.message		 = diagnostic.message,
					.technical_details = diagnostic.technical_details});
			}
			feedback.photo_message += result.main_selected_photo_id.has_value()
									  ? " Main staged photo applied."
									  : " Main staged photo was not applied.";
		}
	} else if (!item_form.pending_photos.empty()) {
		feedback.photo_message =
			"Item saved, but no staged pending photos were ready.";
	}

	session = std::move(result.session);
	invalidate_all_previews();
	item_form.photo_deck.staged_main_index.reset();
	feedback.edit_message =
		result.save_result.metadata_changed ? "Saved." : "No changes.";
	feedback.edit_diagnostics.clear();
	if (item_form.draft.existing_id) {
		route.selected_item_id = *item_form.draft.existing_id;
		route.destination	   = RootDestination::ItemDetail;
	} else {
		route.destination =
			route.form_return_destination.value_or(RootDestination::Catalog);
	}
	refresh_all();
}

void AppShellComponent::apply_storage_save_with_pending_photos_result(
	StorageSaveWithPendingPhotosResult result) {
	feedback.edit_diagnostics	= result.save_result.diagnostics;
	storage_form.pending_photos = std::move(result.pending_sources);
	std::erase_if(storage_form.pending_photos,
				  [](const PendingPhotoSource& source) {
		return !source.staged_path.has_value();
	});
	if (result.warning_acknowledgement_required()) {
		feedback.edit_message = "Confirm warning and save again.";
		refresh_all();
		return;
	}
	if (result.save_result.failed()) {
		feedback.edit_message = "Save failed.";
		refresh_all();
		return;
	}

	feedback.photo_diagnostics.clear();
	if (result.import_attempted) {
		for (const EntityEditDiagnostic& diagnostic :
			 result.import_result.diagnostics) {
			feedback.photo_diagnostics.push_back(core::Diagnostic{
				.severity		   = diagnostic.severity,
				.code			   = diagnostic.code,
				.message		   = diagnostic.message,
				.technical_details = diagnostic.technical_details});
		}
		feedback.photo_diagnostics.insert(
			feedback.photo_diagnostics.end(),
			result.cleanup_result.diagnostics.begin(),
			result.cleanup_result.diagnostics.end());
		if (result.import_result.succeeded()) {
			feedback.photo_message =
				"Pending storage photo import completed: "
				+ std::to_string(result.import_result.summary.success_count)
				+ " imported, "
				+ std::to_string(result.import_result.summary.failure_count)
				+ " failed.";
		} else if (result.import_result.was_user_cancelled()) {
			feedback.photo_message =
				"Storage saved, pending photo import cancelled.";
		} else {
			feedback.photo_message =
				"Storage saved, but pending photo import failed.";
		}
		if (result.cleanup_attempted && result.cleanup_result.failed())
			feedback.photo_message +=
				" Pending source cleanup needs attention.";
		if (result.main_selected_photo_id.has_value())
			route.selected_photo_id = result.main_selected_photo_id;
		else if (!result.import_result.imported_photo_ids.empty())
			route.selected_photo_id =
				result.import_result.imported_photo_ids.front();
		if (result.main_selection_attempted) {
			for (const EntityEditDiagnostic& diagnostic :
				 result.main_selection_result.diagnostics) {
				feedback.photo_diagnostics.push_back(core::Diagnostic{
					.severity		 = diagnostic.severity,
					.code			 = diagnostic.code,
					.message		 = diagnostic.message,
					.technical_details = diagnostic.technical_details});
			}
			feedback.photo_message += result.main_selected_photo_id.has_value()
									  ? " Main staged photo applied."
									  : " Main staged photo was not applied.";
		}
	} else if (!storage_form.pending_photos.empty()) {
		feedback.photo_message =
			"Storage saved, but no staged pending photos were ready.";
	}

	session = std::move(result.session);
	invalidate_all_previews();
	storage_form.photo_deck.staged_main_index.reset();
	feedback.edit_message =
		result.save_result.metadata_changed ? "Saved." : "No changes.";
	feedback.edit_diagnostics.clear();
	if (storage_form.draft.existing_id) {
		route.selected_storage_id = *storage_form.draft.existing_id;
		route.destination		  = RootDestination::StorageDetail;
	} else {
		route.destination =
			route.form_return_destination.value_or(RootDestination::Catalog);
	}
	refresh_all();
}

void AppShellComponent::apply_entity_edit_result(EntityEditResult result) {
	feedback.edit_diagnostics = std::move(result.diagnostics);
	if (result.warning_acknowledgement_required) {
		feedback.edit_message = "Confirm warning and save again.";
		refresh_all();
		return;
	}
	if (result.failed()) {
		feedback.edit_message = "Save failed.";
		refresh_all();
		return;
	}
	const RootDestination completed_destination = route.destination;
	const std::optional<core::StableIdentifier> saved_item_id =
		item_form.draft.existing_id;
	const std::optional<core::StableIdentifier> saved_storage_id =
		storage_form.draft.existing_id;
	session = std::move(result.session);
	invalidate_all_previews();
	feedback.edit_message = result.metadata_changed ? "Saved." : "No changes.";
	feedback.edit_diagnostics.clear();
	if (completed_destination == RootDestination::ItemForm && saved_item_id) {
		route.selected_item_id = *saved_item_id;
		route.destination	   = RootDestination::ItemDetail;
	} else if (completed_destination == RootDestination::StorageForm
			   && saved_storage_id) {
		route.selected_storage_id = *saved_storage_id;
		route.destination		  = RootDestination::StorageDetail;
	} else {
		route.destination =
			route.form_return_destination.value_or(RootDestination::Catalog);
	}
	refresh_all();
}

void AppShellComponent::request_internal_preview_async(
	core::StableIdentifier photo_id, ImagePreviewSize target_size,
	ImagePreviewRequestPriority priority) {
	if (preview_scheduler != nullptr)
		preview_scheduler->enqueue_internal_preview(std::move(photo_id),
													target_size, priority);
}

void AppShellComponent::request_staged_preview_async(
	PendingPhotoSource source, ImagePreviewSize target_size,
	ImagePreviewRequestPriority priority) {
	if (preview_scheduler != nullptr)
		preview_scheduler->enqueue_staged_preview(std::move(source),
												  target_size, priority);
}

std::optional<juce::String> AppShellComponent::preview_failure_message(
	const ImagePreviewRequestIdentity& identity) const {
	return preview_scheduler == nullptr
			   ? std::nullopt
			   : preview_scheduler->failure_message(identity);
}

void AppShellComponent::request_photo_display_async(
	core::StableIdentifier photo_id) {
	if (preview_scheduler != nullptr)
		preview_scheduler->enqueue_display(std::move(photo_id));
}

void AppShellComponent::invalidate_preview_failure(
	const ImagePreviewRequestIdentity& identity) {
	if (preview_scheduler != nullptr)
		preview_scheduler->clear_failure(identity);
}

void AppShellComponent::invalidate_all_previews() {
	if (preview_scheduler != nullptr)
		preview_scheduler->invalidate_all();
	else
		preview_cache.clear();
}

void AppShellComponent::invalidate_internal_photo_preview(
	const core::StableIdentifier& photo_id) {
	if (preview_scheduler != nullptr)
		preview_scheduler->invalidate_internal_photo(photo_id);
	else
		preview_cache.remove_internal_photo(photo_id);
}

void AppShellComponent::invalidate_staged_photo_preview(
	const std::filesystem::path& staged_path) {
	if (preview_scheduler != nullptr)
		preview_scheduler->invalidate_staged_photo(staged_path);
	else
		preview_cache.remove_staged_photo(staged_path);
}

void AppShellComponent::schedule_content_refresh() {
	startTimer(85);
}

void AppShellComponent::timerCallback() {
	stopTimer();
	refresh_content();
	resized();
	repaint();
}

void AppShellComponent::refresh_all() {
	stopTimer();
	refresh_controls();
	refresh_content();
	resized();
	repaint();
}

void AppShellComponent::refresh_controls() {
	juce::String title;
	switch (route.destination) {
		case RootDestination::Catalog:
			title = "Catalog";
			break;
		case RootDestination::Storages:
			title = "Storages";
			break;
		case RootDestination::Add:
			title = "Add";
			break;
		case RootDestination::More:
			title = "More";
			break;
		case RootDestination::ItemDetail:
			title = "Item detail";
			break;
		case RootDestination::StorageDetail:
			title = "Storage detail";
			break;
		case RootDestination::ItemForm:
			title =
				item_form.mode == FormMode::Create ? "Add item" : "Edit item";
			break;
		case RootDestination::StorageForm:
			title = storage_form.mode == FormMode::Create ? "Add storage"
														  : "Edit storage";
			break;
		case RootDestination::PhotoViewer:
			title = "Photo viewer";
			break;
		case RootDestination::BackupRecovery:
			title = session.fatal() ? "Fatal recovery" : "Backup and recovery";
			break;
	}
	if (session.fatal()) {
		route.destination = RootDestination::BackupRecovery;
		title			  = "Fatal recovery";
	}

	std::string status =
		"Load: " + std::string{persistence::to_string(session.load_status)};
	status += " · " + std::string{to_string(session.source)};
	status += " · items=" + std::to_string(session.repository.items.size());
	status +=
		" · storages=" + std::to_string(session.repository.storages.size());
	if (session.demo_catalog_active)
		status += " · demo catalog";
	if (chrome != nullptr) {
		chrome->update_model(AppShellChromeComponent::Model{
			.destination	   = route.destination,
			.item_form_mode	   = item_form.mode,
			.storage_form_mode = storage_form.mode,
			.title			   = title,
			.status			   = juce_text(status),
			.session_fatal	   = session.fatal(),
			.catalog_filters_active =
				has_catalog_filters(catalog_filter_state.applied)});
	}
}

void AppShellComponent::refresh_content() {
	content->begin_rebuild();
	content->clear_rows();
	if (!session.ready_for_browsing() && session.fatal()) {
		build_backup_recovery_content();
	} else if (!session.ready_for_browsing()) {
		content->add_label(
			"Catalog could not be loaded and app-private paths are "
			"unavailable. "
			"Review technical diagnostics before retrying.",
			86, warning_panel_colour(), true);
	} else {
		if (!feedback.edit_message.empty()) {
			content->add_label(juce_text(feedback.edit_message), 42,
							   accent_colour().withAlpha(0.34f), true);
		}
		if (!feedback.edit_diagnostics.empty()) {
			content->add_label(
				juce_text(diagnostic_summary(feedback.edit_diagnostics)), 76,
				warning_panel_colour(), true);
		}
		if (route.destination != RootDestination::PhotoViewer
			&& !feedback.photo_message.empty()) {
			content->add_label(juce_text(feedback.photo_message), 54,
							   accent_colour().withAlpha(0.34f), true);
		}
		if (route.destination != RootDestination::PhotoViewer
			&& !feedback.photo_diagnostics.empty()) {
			content->add_label(
				juce_text(core_diagnostic_summary(feedback.photo_diagnostics)),
				76, warning_panel_colour(), true);
		}
		if (route.destination != RootDestination::BackupRecovery
			&& !feedback.backup_message.empty()) {
			content->add_label(juce_text(feedback.backup_message), 62,
							   accent_colour().withAlpha(0.34f), true);
		}
		if (route.destination != RootDestination::BackupRecovery
			&& !feedback.backup_diagnostics.empty()) {
			content->add_label(
				juce_text(core_diagnostic_summary(feedback.backup_diagnostics)),
				76, warning_panel_colour(), true);
		}
		switch (route.destination) {
			case RootDestination::Catalog:
				build_catalog_content();
				break;
			case RootDestination::Storages:
				build_storages_content();
				break;
			case RootDestination::ItemDetail:
				build_item_detail_content();
				break;
			case RootDestination::StorageDetail:
				build_storage_detail_content();
				break;
			case RootDestination::ItemForm:
				build_item_form_content();
				break;
			case RootDestination::StorageForm:
				build_storage_form_content();
				break;
			case RootDestination::Add:
				build_add_content();
				break;
			case RootDestination::More:
				build_more_content();
				break;
			case RootDestination::PhotoViewer:
				build_photo_viewer_content();
				break;
			case RootDestination::BackupRecovery:
				build_backup_recovery_content();
				break;
		}
	}
	content->end_rebuild();
}

}	 // namespace shuba::ui
