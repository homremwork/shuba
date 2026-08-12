#include "Localization/Facade.hpp"
#include "Platform/LinuxFakes.hpp"
#include "UI/AppShellPhotoCoordinator.hpp"

#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
using namespace std::chrono_literals;

class BlockingPoint final {
public:
	void enter_and_wait(shuba::platform::CancellationToken& cancellation) {
		std::unique_lock<std::mutex> lock{mutex};
		entered = true;
		condition.notify_all();
		while (!released && !cancellation.cancellation_requested())
			condition.wait_for(lock, 1ms);
	}

	void wait_until_entered() {
		std::unique_lock<std::mutex> lock{mutex};
		REQUIRE(condition.wait_for(lock, 5s, [this] { return entered; }));
	}

	void release() {
		const std::lock_guard<std::mutex> lock{mutex};
		released = true;
		condition.notify_all();
	}

private:
	std::mutex mutex;
	std::condition_variable condition;
	bool entered{};
	bool released{};
};

class DeferredPhotoSelectionService final
	: public shuba::platform::PhotoSelectionService {
public:
	[[nodiscard]] shuba::core::OperationResult request_photo_selection(
		const shuba::platform::PhotoSelectionRequest& request,
		shuba::platform::PhotoSelectionCompletion completion) override {
		last_request	   = request;
		completion_handler = std::move(completion);
		return shuba::core::OperationResult::success();
	}

	[[nodiscard]] bool has_pending_completion() const noexcept {
		return static_cast<bool>(completion_handler);
	}

	void complete_with_cancellation() {
		REQUIRE(completion_handler);
		shuba::platform::PhotoSelectionCompletion completion =
			std::move(completion_handler);
		completion(shuba::platform::platform_value_user_cancelled<
				   std::vector<shuba::platform::ContentSourceDescriptor>>());
	}

	void complete_with_sources(
		std::vector<shuba::platform::ContentSourceDescriptor> sources) {
		REQUIRE(completion_handler);
		shuba::platform::PhotoSelectionCompletion completion =
			std::move(completion_handler);
		completion(shuba::platform::platform_value_success(std::move(sources)));
	}

private:
	shuba::platform::PhotoSelectionRequest last_request;
	shuba::platform::PhotoSelectionCompletion completion_handler;
};

class DeferredDocumentExportService final
	: public shuba::platform::DocumentExportService {
public:
	[[nodiscard]] shuba::core::OperationResult
	request_export_destination_selection(
		const shuba::platform::DocumentExportRequest& request,
		shuba::platform::DocumentExportDestinationCompletion completion)
		override {
		last_request	   = request;
		completion_handler = std::move(completion);
		return shuba::core::OperationResult::success();
	}

	[[nodiscard]] shuba::core::OperationResult copy_file_to_destination(
		const shuba::platform::DocumentCopyRequest& request,
		const shuba::platform::PlatformOperationContext& context,
		shuba::platform::ProgressSink& progress_sink,
		shuba::platform::CancellationToken& cancellation_token) override {
		(void)request;
		(void)context;
		(void)progress_sink;
		(void)cancellation_token;
		return shuba::core::OperationResult::success();
	}

	[[nodiscard]] bool has_pending_completion() const noexcept {
		return static_cast<bool>(completion_handler);
	}

	void complete_with_cancellation() {
		REQUIRE(completion_handler);
		shuba::platform::DocumentExportDestinationCompletion completion =
			std::move(completion_handler);
		completion(shuba::platform::platform_value_user_cancelled<
				   shuba::platform::DocumentDestinationDescriptor>());
	}

private:
	shuba::platform::DocumentExportRequest last_request;
	shuba::platform::DocumentExportDestinationCompletion completion_handler;
};

class TestFingerprintService final
	: public shuba::platform::SourceByteFingerprintService {
public:
	[[nodiscard]] shuba::platform::PlatformValueResult<
		shuba::platform::SourceByteFingerprint>
	fingerprint_source_bytes(
		const shuba::platform::SourceByteFingerprintRequest& request,
		const shuba::platform::PlatformOperationContext& context,
		shuba::platform::ProgressSink& progress_sink,
		shuba::platform::CancellationToken& cancellation_token) override {
		(void)request;
		(void)context;
		(void)progress_sink;
		(void)cancellation_token;
		return shuba::platform::platform_value_success(
			shuba::platform::SourceByteFingerprint{.source_md5 = "test"});
	}
};

class TestPhotoOperationWorkerServiceFactory final
	: public shuba::ui::PhotoOperationWorkerServiceFactory {
public:
	[[nodiscard]] std::unique_ptr<shuba::platform::ContentStagingService>
	make_content_staging_service() const override {
		return std::make_unique<shuba::platform::LinuxFakeContentStagingService>();
	}

	[[nodiscard]]
	std::unique_ptr<shuba::platform::SourceByteFingerprintService>
	make_source_fingerprint_service() const override {
		return std::make_unique<TestFingerprintService>();
	}

	[[nodiscard]] std::unique_ptr<shuba::platform::SourceImageDecodeService>
	make_source_decode_service() const override {
		return std::make_unique<
			shuba::platform::SyntheticSourceImageDecodeService>();
	}

	[[nodiscard]] std::unique_ptr<shuba::platform::InternalPhotoCodec>
	make_internal_photo_codec() const override {
		return std::make_unique<shuba::platform::MarkerInternalPhotoCodec>();
	}
};

class BlockingContentStagingService final
	: public shuba::platform::ContentStagingService {
public:
	explicit BlockingContentStagingService(
		std::shared_ptr<BlockingPoint> point_value)
		: point(std::move(point_value)) {}

	[[nodiscard]] shuba::platform::PlatformValueResult<
		shuba::platform::StagedContent>
	stage_content(const shuba::platform::ContentStagingRequest& request,
				  const shuba::platform::PlatformOperationContext&,
				  shuba::platform::ProgressSink&,
				  shuba::platform::CancellationToken& cancellation) override {
		point->enter_and_wait(cancellation);
		if (cancellation.cancellation_requested())
			return shuba::platform::platform_value_user_cancelled<
				shuba::platform::StagedContent>();
		return shuba::platform::platform_value_success(
			shuba::platform::StagedContent{
				.staged_path = request.target_directory / request.target_file_name,
				.display_name = "provider-photo.jpg",
				.byte_count = std::uint64_t{42}});
	}

private:
	std::shared_ptr<BlockingPoint> point;
};

class BlockingPhotoOperationWorkerServiceFactory final
	: public shuba::ui::PhotoOperationWorkerServiceFactory {
public:
	explicit BlockingPhotoOperationWorkerServiceFactory(
		std::shared_ptr<BlockingPoint> point_value)
		: point(std::move(point_value)) {}

	[[nodiscard]] std::unique_ptr<shuba::platform::ContentStagingService>
	make_content_staging_service() const override {
		return std::make_unique<BlockingContentStagingService>(point);
	}

	[[nodiscard]]
	std::unique_ptr<shuba::platform::SourceByteFingerprintService>
	make_source_fingerprint_service() const override {
		return std::make_unique<TestFingerprintService>();
	}

	[[nodiscard]] std::unique_ptr<shuba::platform::SourceImageDecodeService>
	make_source_decode_service() const override {
		return std::make_unique<
			shuba::platform::SyntheticSourceImageDecodeService>();
	}

	[[nodiscard]] std::unique_ptr<shuba::platform::InternalPhotoCodec>
	make_internal_photo_codec() const override {
		return std::make_unique<shuba::platform::MarkerInternalPhotoCodec>();
	}

private:
	std::shared_ptr<BlockingPoint> point;
};

[[nodiscard]] shuba::core::StableIdentifier require_identifier(
	std::string value) {
	std::optional<shuba::core::StableIdentifier> identifier =
		shuba::core::StableIdentifier::try_create_file_safe(std::move(value));
	REQUIRE(identifier.has_value());
	return std::move(*identifier);
}

struct CoordinatorHarness final {
	shuba::ui::CatalogSessionState session;
	shuba::ui::AppShellRouteState route;
	shuba::ui::AppShellItemFormState item_form;
	shuba::ui::AppShellStorageFormState storage_form;
	shuba::ui::AppShellFeedbackState feedback;
	shuba::ui::AppShellPhotoDisplayState photo_display;
	shuba::ui::ImagePreviewCache preview_cache;
	shuba::platform::ScriptedIdentifierSource identifiers;
	shuba::core::ManualClock clock{shuba::core::EpochMilliseconds{1000}};
	shuba::core::OperationGate operation_gate;
	TestPhotoOperationWorkerServiceFactory worker_service_factory;
	shuba::ui::AppShellPhotoOperationState photo_operation_state;
	shuba::ui::AppShellPhotoOperationRunner photo_operation_runner{
		shuba::ui::AppShellPhotoOperationRunner::Dependencies{
			.operation_gate = operation_gate,
			.worker_service_factory = worker_service_factory,
			.progress = {},
			.failure = {}}};
	DeferredPhotoSelectionService photo_selection;
	DeferredDocumentExportService document_export;
	shuba::platform::LinuxFakeContentStagingService content_staging;
	TestFingerprintService fingerprinting;
	shuba::platform::SyntheticSourceImageDecodeService source_decoder;
	shuba::platform::MarkerJpegExportService jpeg_export;
	shuba::platform::MarkerInternalPhotoCodec internal_photo_codec;
	shuba::platform::ProgressCollector progress;
	shuba::platform::NeverCancelledToken cancellation;
	shuba::localization::Localization localization =
		shuba::localization::make_localization(
			shuba::localization::Language::English, {});
	std::uint32_t refresh_count{};

	[[nodiscard]] shuba::ui::AppShellPhotoCoordinator::Dependencies
	dependencies() {
		return shuba::ui::AppShellPhotoCoordinator::Dependencies{
			.session						   = session,
			.route							   = route,
			.item_form						   = item_form,
			.storage_form					   = storage_form,
			.feedback						   = feedback,
			.photo_display					   = photo_display,
			.preview_cache					   = preview_cache,
			.identifiers					   = identifiers,
			.clock							   = clock,
			.operation_gate					   = operation_gate,
			.photo_selection_service		   = photo_selection,
			.document_export_service		   = document_export,
			.content_staging_service		   = content_staging,
			.source_fingerprint_service		   = fingerprinting,
			.source_decode_service			   = source_decoder,
			.jpeg_export_service			   = jpeg_export,
			.internal_photo_codec			   = internal_photo_codec,
			.progress_events				   = progress,
			.cancellation_token				   = cancellation,
			.photo_operation_runner		   = photo_operation_runner,
			.photo_operation_state			   = photo_operation_state,
			.localization					   = localization,
			.invalidate_all_previews		   = {},
			.invalidate_internal_photo_preview = {},
			.invalidate_staged_photo_preview   = {},
			.refresh_all					   = [this] { ++refresh_count; },
			.begin_photo_operation			   = {},
			.complete_photo_operation		   = {}};
	}
};
}	 // namespace

TEST_CASE(
	"B29 ignores deferred photo-picker completion after coordinator "
	"destruction",
	"[b29][photo-coordinator][lifetime]") {
	CoordinatorHarness harness;
	{
		shuba::ui::AppShellPhotoCoordinator coordinator{harness.dependencies()};
		coordinator.request_add_photos(shuba::domain::PhotoOwner{
			.type = shuba::domain::PhotoOwnerType::Item,
			.id	  = require_identifier("owner-b29-picker")});
		REQUIRE(harness.photo_selection.has_pending_completion());
		REQUIRE(harness.feedback.photo_message == "Select photos to import.");
	}

	const std::uint32_t refreshes_before_completion = harness.refresh_count;
	harness.photo_selection.complete_with_cancellation();
	REQUIRE(harness.refresh_count == refreshes_before_completion);
	REQUIRE(harness.feedback.photo_message == "Select photos to import.");
}

TEST_CASE(
	"B29 ignores deferred JPEG-destination completion after coordinator "
	"destruction",
	"[b29][photo-coordinator][lifetime]") {
	CoordinatorHarness harness;
	const shuba::core::StableIdentifier photo_id =
		require_identifier("photo-b29-export");
	{
		shuba::ui::AppShellPhotoCoordinator coordinator{harness.dependencies()};
		coordinator.request_export_photo(photo_id);
		REQUIRE(harness.document_export.has_pending_completion());
	}

	const std::uint32_t refreshes_before_completion = harness.refresh_count;
	harness.document_export.complete_with_cancellation();
	REQUIRE(harness.refresh_count == refreshes_before_completion);
	REQUIRE(harness.feedback.photo_message.empty());
}

TEST_CASE("R13 picker completion submits shallow sources before worker staging",
		  "[r13][photo-coordinator][picker][responsiveness]") {
	juce::ScopedJuceInitialiser_GUI juce_initialiser;
	CoordinatorHarness harness;
	const std::filesystem::path staging_root =
		std::filesystem::temp_directory_path() / "shuba-r13-picker-boundary";
	harness.session.paths = shuba::platform::AppPrivatePaths{
		.app_private_root = staging_root,
		.active_catalog_root = staging_root / "active",
		.operation_tmp_root = staging_root / "operations",
		.staged_content_root = staging_root / "staged",
		.export_tmp_root = staging_root / "exports",
		.media_root = staging_root / "active" / "media"};
	std::shared_ptr<BlockingPoint> point = std::make_shared<BlockingPoint>();
	BlockingPhotoOperationWorkerServiceFactory factory{point};
	shuba::core::OperationGate gate;
	shuba::ui::AppShellPhotoOperationState operation_state;
	std::atomic_bool completed{};
	shuba::ui::AppShellPhotoOperationRunner runner{
		shuba::ui::AppShellPhotoOperationRunner::Dependencies{
			.operation_gate = gate,
			.worker_service_factory = factory,
			.progress = {},
			.failure = {}}};
	shuba::ui::AppShellPhotoCoordinator coordinator{
		shuba::ui::AppShellPhotoCoordinator::Dependencies{
			.session = harness.session,
			.route = harness.route,
			.item_form = harness.item_form,
			.storage_form = harness.storage_form,
			.feedback = harness.feedback,
			.photo_display = harness.photo_display,
			.preview_cache = harness.preview_cache,
			.identifiers = harness.identifiers,
			.clock = harness.clock,
			.operation_gate = gate,
			.photo_selection_service = harness.photo_selection,
			.document_export_service = harness.document_export,
			.content_staging_service = harness.content_staging,
			.source_fingerprint_service = harness.fingerprinting,
			.source_decode_service = harness.source_decoder,
			.jpeg_export_service = harness.jpeg_export,
			.internal_photo_codec = harness.internal_photo_codec,
			.progress_events = harness.progress,
			.cancellation_token = harness.cancellation,
			.photo_operation_runner = runner,
			.photo_operation_state = operation_state,
			.localization = harness.localization,
			.invalidate_all_previews = {},
			.invalidate_internal_photo_preview = {},
			.invalidate_staged_photo_preview = {},
			.refresh_all = [&harness] { ++harness.refresh_count; },
			.begin_photo_operation =
				[&](shuba::ui::PhotoOperationJobType type,
					std::uint64_t generation) {
					operation_state.state = shuba::ui::PhotoOperationState::Running;
					operation_state.job_type = type;
					operation_state.generation = generation;
				},
			.complete_photo_operation = [&] {
				operation_state.state = shuba::ui::PhotoOperationState::Idle;
				completed.store(true, std::memory_order_release);
			}}};

	coordinator.request_add_pending_item_photos();
	REQUIRE(harness.photo_selection.has_pending_completion());
	harness.photo_selection.complete_with_sources(
		{shuba::platform::make_opaque_content_source(
			"content://provider/images/42", "selected-photo")});
	REQUIRE(operation_state.active());
	point->wait_until_entered();
	std::atomic_bool sentinel{};
	REQUIRE(juce::MessageManager::callAsync(
		[&sentinel] { sentinel.store(true, std::memory_order_release); }));
	for (std::size_t attempt = 0U;
		 attempt < 1000U && !sentinel.load(std::memory_order_acquire); ++attempt) {
		REQUIRE(juce::MessageManager::getInstance()->runDispatchLoopUntil(1));
	}
	REQUIRE(sentinel.load(std::memory_order_acquire));
	REQUIRE_FALSE(completed.load(std::memory_order_acquire));

	point->release();
	for (std::size_t attempt = 0U;
		 attempt < 1000U && !completed.load(std::memory_order_acquire); ++attempt) {
		REQUIRE(juce::MessageManager::getInstance()->runDispatchLoopUntil(1));
	}
	REQUIRE(completed.load(std::memory_order_acquire));
	REQUIRE_FALSE(gate.is_busy());
}

TEST_CASE("R13 item and storage picker paths publish staged results and restore idle",
		  "[r13][photo-coordinator][picker][forms]") {
	juce::ScopedJuceInitialiser_GUI juce_initialiser;
	for (const bool item_target : {true, false}) {
		CoordinatorHarness harness;
		const std::filesystem::path root =
			std::filesystem::temp_directory_path()
			/ (item_target ? "shuba-r13-item-form" : "shuba-r13-storage-form");
		std::filesystem::create_directories(root / "source");
		const std::filesystem::path source = root / "source" / "photo.jpg";
		{
			std::ofstream output{source, std::ios::binary | std::ios::trunc};
			output << "r13-form-source";
		}
		harness.session.paths = shuba::platform::AppPrivatePaths{
			.app_private_root = root,
			.active_catalog_root = root / "active",
			.operation_tmp_root = root / "operations",
			.staged_content_root = root / "staged",
			.export_tmp_root = root / "exports",
			.media_root = root / "active" / "media"};
		shuba::ui::AppShellPhotoOperationState operation_state;
		std::atomic_bool completed{};
		std::atomic_uint32_t invalidation_count{};
		shuba::ui::AppShellPhotoCoordinator::Dependencies dependencies =
			harness.dependencies();
		dependencies.photo_operation_state = operation_state;
		dependencies.invalidate_all_previews = [&] {
			invalidation_count.fetch_add(1U, std::memory_order_acq_rel);
		};
		dependencies.begin_photo_operation =
			[&](shuba::ui::PhotoOperationJobType type, std::uint64_t generation) {
				operation_state.state = shuba::ui::PhotoOperationState::Running;
				operation_state.job_type = type;
				operation_state.generation = generation;
			};
		dependencies.complete_photo_operation = [&] {
			operation_state.state = shuba::ui::PhotoOperationState::Idle;
			completed.store(true, std::memory_order_release);
		};
		shuba::ui::AppShellPhotoCoordinator coordinator{std::move(dependencies)};

		if (item_target)
			coordinator.request_add_pending_item_photos();
		else
			coordinator.request_add_pending_storage_photos();
		harness.photo_selection.complete_with_sources(
			{shuba::platform::make_local_file_source(source, "photo.jpg")});
		for (std::size_t attempt = 0U;
			 attempt < 1000U && !completed.load(std::memory_order_acquire);
			 ++attempt) {
			REQUIRE(juce::MessageManager::getInstance()->runDispatchLoopUntil(1));
		}
		REQUIRE(completed.load(std::memory_order_acquire));
		REQUIRE(operation_state.state == shuba::ui::PhotoOperationState::Idle);
		const std::vector<shuba::ui::PendingPhotoSource>& pending =
			item_target ? harness.item_form.pending_photos
						: harness.storage_form.pending_photos;
		REQUIRE(pending.size() == 1U);
		REQUIRE(pending.front().ready_for_import());
		REQUIRE(harness.refresh_count >= 1U);
		REQUIRE(invalidation_count.load(std::memory_order_acquire) == 0U);
		REQUIRE_FALSE(harness.operation_gate.is_busy());
		std::error_code ignored;
		std::filesystem::remove_all(root, ignored);
	}
}
