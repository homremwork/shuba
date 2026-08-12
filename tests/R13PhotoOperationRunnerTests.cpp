#include "Platform/LinuxFakes.hpp"
#include "UI/AppShellPhotoOperationRunner.hpp"
#include "UI/Session/CatalogStartupSession.hpp"
#include "UI/Session/EntityEditSession.hpp"

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
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace {
using namespace std::chrono_literals;

class TemporaryDirectory final {
public:
	explicit TemporaryDirectory(std::string leaf_name)
		: path_value(std::filesystem::temp_directory_path()
					 / std::move(leaf_name)) {
		std::error_code ignored;
		std::filesystem::remove_all(path_value, ignored);
		std::filesystem::create_directories(path_value);
	}

	~TemporaryDirectory() {
		std::error_code ignored;
		std::filesystem::remove_all(path_value, ignored);
	}

	[[nodiscard]] const std::filesystem::path& path() const noexcept {
		return path_value;
	}

private:
	std::filesystem::path path_value;
};

class BlockingPoint final {
public:
	void enter_and_wait(shuba::platform::CancellationToken& cancellation) {
		std::unique_lock<std::mutex> lock{mutex};
		entered = true;
		condition.notify_all();
		while (!released && !cancellation.cancellation_requested())
			condition.wait_for(lock, 1ms);
		cancelled = cancellation.cancellation_requested();
		finished  = true;
		condition.notify_all();
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

	[[nodiscard]] bool cancellation_observed() const {
		const std::lock_guard<std::mutex> lock{mutex};
		return cancelled;
	}

	[[nodiscard]] bool has_finished() const {
		const std::lock_guard<std::mutex> lock{mutex};
		return finished;
	}

private:
	mutable std::mutex mutex;
	std::condition_variable condition;
	bool entered{};
	bool released{};
	bool cancelled{};
	bool finished{};
};

class TestFingerprintService final
	: public shuba::platform::SourceByteFingerprintService {
public:
	[[nodiscard]] shuba::platform::PlatformValueResult<
		shuba::platform::SourceByteFingerprint>
	fingerprint_source_bytes(
		const shuba::platform::SourceByteFingerprintRequest&,
		const shuba::platform::PlatformOperationContext&,
		shuba::platform::ProgressSink&,
		shuba::platform::CancellationToken&) override {
		return shuba::platform::platform_value_success(
			shuba::platform::SourceByteFingerprint{.source_md5 = "r13"});
	}
};

class BlockingContentStagingService final
	: public shuba::platform::ContentStagingService {
public:
	explicit BlockingContentStagingService(
		std::shared_ptr<BlockingPoint> point_value,
		std::size_t progress_event_count_value = 0U)
		: point(std::move(point_value))
		, progress_event_count(progress_event_count_value) {}

	[[nodiscard]] shuba::platform::PlatformValueResult<
		shuba::platform::StagedContent>
	stage_content(const shuba::platform::ContentStagingRequest& request,
				  const shuba::platform::PlatformOperationContext& context,
				  shuba::platform::ProgressSink& progress,
				  shuba::platform::CancellationToken& cancellation) override {
		for (std::size_t index = 0U; index < progress_event_count; ++index) {
			progress.publish_progress(shuba::platform::ProgressEvent{
				.operation_id	= context.operation_id,
				.operation_type = context.operation_type,
				.phase			= "r13-progress-flood",
				.message_id		= shuba::platform::ProgressMessageId::Copying,
				.current_units	= static_cast<std::uint64_t>(index + 1U),
				.total_units = static_cast<std::uint64_t>(progress_event_count),
				.message	 = "R13 progress flood.",
				.cancellable = true});
		}
		point->enter_and_wait(cancellation);
		if (cancellation.cancellation_requested()) {
			return shuba::platform::platform_value_user_cancelled<
				shuba::platform::StagedContent>();
		}
		const std::filesystem::path destination =
			request.target_directory / request.target_file_name;
		std::filesystem::create_directories(destination.parent_path());
		std::ofstream output{destination, std::ios::binary | std::ios::trunc};
		output << "r13-staged";
		output.close();
		return shuba::platform::platform_value_success(
			shuba::platform::StagedContent{
				.staged_path  = destination,
				.display_name = request.source.display_name,
				.byte_count	  = std::uint64_t{10}});
	}

private:
	std::shared_ptr<BlockingPoint> point;
	std::size_t progress_event_count{};
};

class TestWorkerServiceFactory final
	: public shuba::ui::PhotoOperationWorkerServiceFactory {
public:
	explicit TestWorkerServiceFactory(
		std::shared_ptr<BlockingPoint> point_value)
		: point(std::move(point_value)) {}

	TestWorkerServiceFactory(std::shared_ptr<BlockingPoint> point_value,
							 std::size_t progress_event_count_value)
		: point(std::move(point_value))
		, progress_event_count(progress_event_count_value) {}

	[[nodiscard]] std::unique_ptr<shuba::platform::ContentStagingService>
	make_content_staging_service() const override {
		return std::make_unique<BlockingContentStagingService>(
			point, progress_event_count);
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
	std::size_t progress_event_count{};
};

class ImmediateWorkerServiceFactory final
	: public shuba::ui::PhotoOperationWorkerServiceFactory {
public:
	[[nodiscard]] std::unique_ptr<shuba::platform::ContentStagingService>
	make_content_staging_service() const override {
		return std::make_unique<
			shuba::platform::LinuxFakeContentStagingService>();
	}

	[[nodiscard]]
	std::unique_ptr<shuba::platform::SourceByteFingerprintService>
	make_source_fingerprint_service() const override {
		return std::make_unique<TestFingerprintService>();
	}

	[[nodiscard]] std::unique_ptr<shuba::platform::SourceImageDecodeService>
	make_source_decode_service() const override {
		std::unique_ptr<shuba::platform::SyntheticSourceImageDecodeService>
			decoder = std::make_unique<
				shuba::platform::SyntheticSourceImageDecodeService>();
		decoder->set_decoded_pixels(shuba::platform::ImagePixels{
			.width				= 2,
			.height				= 1,
			.format				= shuba::platform::PixelFormat::Rgba8,
			.bytes				= {8, 16, 24, 255, 32, 40, 48, 255},
			.source_description = "R13 synthetic source"});
		return decoder;
	}

	[[nodiscard]] std::unique_ptr<shuba::platform::InternalPhotoCodec>
	make_internal_photo_codec() const override {
		return std::make_unique<shuba::platform::MarkerInternalPhotoCodec>();
	}
};

[[nodiscard]] shuba::ui::CatalogSessionState make_session(
	TemporaryDirectory& temporary) {
	shuba::platform::LinuxFakePathProvider paths{temporary.path()};
	shuba::platform::ScriptedIdentifierSource identifiers;
	identifiers.script_stable_identifier("catalog-r13-runner");
	identifiers.script_operation_identifier("operation-r13-runner-init");
	shuba::core::ManualClock clock{shuba::core::EpochMilliseconds{1000}};
	return shuba::ui::load_catalog_session(
		shuba::ui::CatalogSessionLoadRequest{.path_provider = paths,
											 .identifiers	= identifiers,
											 .clock			= clock,
											 .debug_demo_seed_enabled = false});
}

[[nodiscard]] shuba::ui::PendingPhotoStagingRequest make_request(
	shuba::ui::CatalogSessionState session,
	shuba::platform::ScriptedIdentifierSource& identifiers,
	shuba::core::OperationGate& gate,
	shuba::platform::LinuxFakeContentStagingService& staging,
	TestFingerprintService& fingerprinting, std::string display_name) {
	return shuba::ui::PendingPhotoStagingRequest{
		.current_session	 = std::move(session),
		.identifiers		 = identifiers,
		.operation_gate		 = gate,
		.staging_service	 = staging,
		.fingerprint_service = fingerprinting,
		.sources			 = {shuba::platform::make_opaque_content_source(
			"content://r13/photo", std::move(display_name), 10U)}};
}

[[nodiscard]] shuba::ui::PendingPhotoSource make_ready_pending_source(
	const std::filesystem::path& path, std::string display_name) {
	return shuba::ui::PendingPhotoSource{
		.source_index  = 0U,
		.display_name  = display_name,
		.byte_count	   = std::filesystem::file_size(path),
		.status		   = shuba::ui::PendingPhotoStatus::Staged,
		.staged_source = shuba::platform::make_local_file_source(
			path, std::move(display_name)),
		.staged_path = path,
		.source_md5	 = "r13-ready"};
}

[[nodiscard]] shuba::core::StableIdentifier saved_id(
	const shuba::ui::EntityEditResult& result) {
	REQUIRE(result.saved_record_id.has_value());
	return *result.saved_record_id;
}

void pump_messages_until(const std::function<bool()>& predicate) {
	juce::MessageManager* manager = juce::MessageManager::getInstance();
	for (std::size_t attempt = 0U; attempt < 1000U && !predicate(); ++attempt)
		REQUIRE(manager->runDispatchLoopUntil(1));
	REQUIRE(predicate());
}
}	 // namespace

TEST_CASE(
	"R13 runner keeps the JUCE message thread responsive and serializes jobs",
	"[r13][photo-runner][responsiveness]") {
	juce::ScopedJuceInitialiser_GUI juce_initialiser;
	TemporaryDirectory temporary{"shuba-r13-runner-responsive"};
	shuba::ui::CatalogSessionState session = make_session(temporary);
	const std::size_t original_photo_count = session.repository.photos.size();
	const std::filesystem::path original_root =
		session.paths->active_catalog_root;
	std::shared_ptr<BlockingPoint> point = std::make_shared<BlockingPoint>();
	TestWorkerServiceFactory factory{point};
	shuba::core::OperationGate gate;
	shuba::platform::ScriptedIdentifierSource identifiers;
	shuba::platform::LinuxFakeContentStagingService staging;
	TestFingerprintService fingerprinting;
	std::atomic_bool sentinel{};
	std::atomic_bool completed{};
	std::optional<shuba::ui::PendingPhotoStagingResult> result;
	shuba::ui::AppShellPhotoOperationRunner runner{
		shuba::ui::AppShellPhotoOperationRunner::Dependencies{
			.operation_gate			= gate,
			.worker_service_factory = factory,
			.progress				= {},
			.failure				= {}}};

	const shuba::ui::AppShellPhotoOperationRunner::Submission submission =
		runner.submit_pending_staging(
			shuba::ui::PhotoOperationJobType::PendingItemStaging,
			make_request(session, identifiers, gate, staging, fingerprinting,
						 "original.jpg"),
			[&](shuba::ui::AppShellPhotoOperationRunner::Result
					operation_result) {
		result = std::get<shuba::ui::PendingPhotoStagingResult>(
			std::move(operation_result));
		completed.store(true, std::memory_order_release);
	});
	REQUIRE(submission.accepted);
	REQUIRE(submission.generation == 1U);
	point->wait_until_entered();
	REQUIRE(runner.active());
	REQUIRE_FALSE(completed.load(std::memory_order_acquire));
	REQUIRE(session.repository.photos.size() == original_photo_count);
	REQUIRE(session.paths->active_catalog_root == original_root);

	const shuba::ui::AppShellPhotoOperationRunner::Submission second =
		runner.submit_pending_staging(
			shuba::ui::PhotoOperationJobType::PendingItemStaging,
			make_request(session, identifiers, gate, staging, fingerprinting,
						 "second.jpg"),
			{});
	REQUIRE_FALSE(second.accepted);
	REQUIRE(juce::MessageManager::callAsync(
		[&sentinel] { sentinel.store(true, std::memory_order_release); }));
	pump_messages_until(
		[&sentinel] { return sentinel.load(std::memory_order_acquire); });
	REQUIRE_FALSE(completed.load(std::memory_order_acquire));

	point->release();
	pump_messages_until(
		[&completed] { return completed.load(std::memory_order_acquire); });
	REQUIRE(result.has_value());
	REQUIRE(result->succeeded());
	REQUIRE(result->sources.front().display_name == "original.jpg");
	REQUIRE_FALSE(gate.is_busy());
}

TEST_CASE("R13 runner coalesces a progress flood to the latest event",
		  "[r13][photo-runner][progress][coalescing]") {
	juce::ScopedJuceInitialiser_GUI juce_initialiser;
	TemporaryDirectory temporary{"shuba-r13-runner-progress-flood"};
	shuba::ui::CatalogSessionState session = make_session(temporary);
	std::shared_ptr<BlockingPoint> point   = std::make_shared<BlockingPoint>();
	constexpr std::size_t progress_event_count = 10000U;
	TestWorkerServiceFactory factory{point, progress_event_count};
	shuba::core::OperationGate gate;
	shuba::platform::ScriptedIdentifierSource identifiers;
	shuba::platform::LinuxFakeContentStagingService staging;
	TestFingerprintService fingerprinting;
	std::atomic_uint32_t delivery_count{};
	std::atomic_uint64_t latest_units{};
	std::atomic_bool sentinel{};
	std::atomic_bool completed{};
	shuba::ui::AppShellPhotoOperationRunner runner{
		shuba::ui::AppShellPhotoOperationRunner::Dependencies{
			.operation_gate			= gate,
			.worker_service_factory = factory,
			.progress =
				[&](std::uint64_t,
					const shuba::platform::ProgressEvent& event) {
		delivery_count.fetch_add(1U, std::memory_order_acq_rel);
		latest_units.store(event.current_units.value_or(0U),
						   std::memory_order_release);
	},
			.failure = {}}};

	const shuba::ui::AppShellPhotoOperationRunner::Submission submission =
		runner.submit_pending_staging(
			shuba::ui::PhotoOperationJobType::PendingItemStaging,
			make_request(session, identifiers, gate, staging, fingerprinting,
						 "flood.jpg"),
			[&](shuba::ui::AppShellPhotoOperationRunner::Result) {
		completed.store(true, std::memory_order_release);
	});
	REQUIRE(submission.accepted);
	point->wait_until_entered();
	REQUIRE(juce::MessageManager::callAsync(
		[&sentinel] { sentinel.store(true, std::memory_order_release); }));
	pump_messages_until(
		[&sentinel] { return sentinel.load(std::memory_order_acquire); });
	REQUIRE_FALSE(completed.load(std::memory_order_acquire));
	REQUIRE(delivery_count.load(std::memory_order_acquire) == 1U);
	REQUIRE(latest_units.load(std::memory_order_acquire)
			== progress_event_count);

	point->release();
	pump_messages_until(
		[&completed] { return completed.load(std::memory_order_acquire); });
	REQUIRE_FALSE(gate.is_busy());
}

TEST_CASE("R13 runner cancellation and destruction join the worker safely",
		  "[r13][photo-runner][lifetime]") {
	juce::ScopedJuceInitialiser_GUI juce_initialiser;
	TemporaryDirectory temporary{"shuba-r13-runner-lifetime"};
	shuba::ui::CatalogSessionState session = make_session(temporary);
	std::shared_ptr<BlockingPoint> point   = std::make_shared<BlockingPoint>();
	TestWorkerServiceFactory factory{point};
	shuba::core::OperationGate gate;
	shuba::platform::ScriptedIdentifierSource identifiers;
	shuba::platform::LinuxFakeContentStagingService staging;
	TestFingerprintService fingerprinting;
	std::atomic_bool completion_called{};
	std::unique_ptr<shuba::ui::AppShellPhotoOperationRunner> runner =
		std::make_unique<shuba::ui::AppShellPhotoOperationRunner>(
			shuba::ui::AppShellPhotoOperationRunner::Dependencies{
				.operation_gate			= gate,
				.worker_service_factory = factory,
				.progress				= {},
				.failure				= {}});

	const shuba::ui::AppShellPhotoOperationRunner::Submission submission =
		runner->submit_pending_staging(
			shuba::ui::PhotoOperationJobType::PendingStorageStaging,
			make_request(session, identifiers, gate, staging, fingerprinting,
						 "cancel.jpg"),
			[&](shuba::ui::AppShellPhotoOperationRunner::Result) {
		completion_called.store(true, std::memory_order_release);
	});
	REQUIRE(submission.accepted);
	point->wait_until_entered();
	runner->request_cancellation();
	runner.reset();
	REQUIRE(point->has_finished());
	REQUIRE(point->cancellation_observed());
	REQUIRE_FALSE(gate.is_busy());
	REQUIRE(juce::MessageManager::getInstance()->runDispatchLoopUntil(5));
	REQUIRE_FALSE(completion_called.load(std::memory_order_acquire));
}

TEST_CASE("R13 runner drops a completed result when destroyed before delivery",
		  "[r13][photo-runner][lifetime][result-delivery]") {
	juce::ScopedJuceInitialiser_GUI juce_initialiser;
	TemporaryDirectory temporary{"shuba-r13-runner-result-delivery"};
	shuba::ui::CatalogSessionState session = make_session(temporary);
	std::shared_ptr<BlockingPoint> point   = std::make_shared<BlockingPoint>();
	TestWorkerServiceFactory factory{point};
	shuba::core::OperationGate gate;
	shuba::platform::ScriptedIdentifierSource identifiers;
	shuba::platform::LinuxFakeContentStagingService staging;
	TestFingerprintService fingerprinting;
	std::atomic_bool completion_called{};
	std::unique_ptr<shuba::ui::AppShellPhotoOperationRunner> runner =
		std::make_unique<shuba::ui::AppShellPhotoOperationRunner>(
			shuba::ui::AppShellPhotoOperationRunner::Dependencies{
				.operation_gate			= gate,
				.worker_service_factory = factory,
				.progress				= {},
				.failure				= {}});

	const shuba::ui::AppShellPhotoOperationRunner::Submission submission =
		runner->submit_pending_staging(
			shuba::ui::PhotoOperationJobType::PendingItemStaging,
			make_request(session, identifiers, gate, staging, fingerprinting,
						 "completed-before-delivery.jpg"),
			[&](shuba::ui::AppShellPhotoOperationRunner::Result) {
		completion_called.store(true, std::memory_order_release);
	});
	REQUIRE(submission.accepted);
	point->wait_until_entered();
	point->release();
	pump_messages_until([&runner] { return runner->active(); });
	while (gate.is_busy())
		std::this_thread::yield();
	REQUIRE(runner->active());

	runner.reset();
	REQUIRE(point->has_finished());
	REQUIRE_FALSE(gate.is_busy());
	REQUIRE(juce::MessageManager::getInstance()->runDispatchLoopUntil(5));
	REQUIRE_FALSE(completion_called.load(std::memory_order_acquire));
}

TEST_CASE("R13 runner becomes idle after an empty completion is delivered",
		  "[r13][photo-runner][result-delivery]") {
	juce::ScopedJuceInitialiser_GUI juce_initialiser;
	TemporaryDirectory temporary{"shuba-r13-runner-empty-completion"};
	shuba::ui::CatalogSessionState session = make_session(temporary);
	std::shared_ptr<BlockingPoint> point   = std::make_shared<BlockingPoint>();
	TestWorkerServiceFactory factory{point};
	shuba::core::OperationGate gate;
	shuba::platform::ScriptedIdentifierSource identifiers;
	shuba::platform::LinuxFakeContentStagingService staging;
	TestFingerprintService fingerprinting;
	shuba::ui::AppShellPhotoOperationRunner runner{
		shuba::ui::AppShellPhotoOperationRunner::Dependencies{
			.operation_gate			= gate,
			.worker_service_factory = factory,
			.progress				= {},
			.failure				= {}}};

	const shuba::ui::AppShellPhotoOperationRunner::Submission submission =
		runner.submit_pending_staging(
			shuba::ui::PhotoOperationJobType::PendingStorageStaging,
			make_request(session, identifiers, gate, staging, fingerprinting,
						 "empty-completion.jpg"),
			{});
	REQUIRE(submission.accepted);
	point->wait_until_entered();
	point->release();
	pump_messages_until([&runner] { return !runner.active(); });
	REQUIRE_FALSE(gate.is_busy());
}

TEST_CASE("R13 runner executes every supported photo mutation from snapshots",
		  "[r13][photo-runner][workflows][snapshot]") {
	juce::ScopedJuceInitialiser_GUI juce_initialiser;
	TemporaryDirectory temporary{"shuba-r13-runner-workflows"};
	shuba::ui::CatalogSessionState session = make_session(temporary);
	ImmediateWorkerServiceFactory factory;
	shuba::core::OperationGate gate;
	shuba::platform::ScriptedIdentifierSource message_identifiers;
	shuba::core::ManualClock message_clock{
		shuba::core::EpochMilliseconds{1000}};
	shuba::platform::LinuxFakeContentStagingService message_staging;
	TestFingerprintService message_fingerprinting;
	shuba::platform::SyntheticSourceImageDecodeService message_decoder;
	shuba::platform::MarkerInternalPhotoCodec message_codec;
	shuba::ui::AppShellPhotoOperationRunner runner{
		shuba::ui::AppShellPhotoOperationRunner::Dependencies{
			.operation_gate			= gate,
			.worker_service_factory = factory,
			.progress				= {},
			.failure				= {}}};

	message_identifiers.script_stable_identifier("item-r13-direct-owner");
	message_identifiers.script_operation_identifier(
		"operation-r13-create-owner");
	shuba::ui::EntityEditResult item_saved = shuba::ui::save_item_draft(
		shuba::ui::EntityEditRequest{.current_session = session,
									 .identifiers	  = message_identifiers,
									 .clock			  = message_clock,
									 .create_previous_copy = false},
		shuba::ui::ItemDraft{.display_name		   = "Direct owner",
							 .category			   = "Testing",
							 .warning_acknowledged = true});
	REQUIRE(item_saved.succeeded());
	session = item_saved.session;

	const std::filesystem::path direct_source = temporary.path() / "direct.jpg";
	{
		std::ofstream output{direct_source, std::ios::binary | std::ios::trunc};
		output << "direct-source";
	}
	std::atomic_bool direct_completed{};
	std::optional<shuba::ui::PhotoImportSessionResult> direct_result;
	shuba::ui::PhotoImportSessionRequest direct_request{
		.current_session	 = session,
		.identifiers		 = message_identifiers,
		.clock				 = message_clock,
		.operation_gate		 = gate,
		.staging_service	 = message_staging,
		.fingerprint_service = message_fingerprinting,
		.decode_service		 = message_decoder,
		.photo_codec		 = message_codec,
		.owner =
			shuba::domain::PhotoOwner{
				.type = shuba::domain::PhotoOwnerType::Item,
				.id	  = saved_id(item_saved)},
		.sources = {shuba::platform::make_local_file_source(direct_source,
															"direct.jpg")},
		.create_previous_copy = false};
	REQUIRE(runner
				.submit_direct_import(
					direct_request,
					[&](shuba::ui::AppShellPhotoOperationRunner::Result
							operation_result) {
		direct_result = std::get<shuba::ui::PhotoImportSessionResult>(
			std::move(operation_result));
		direct_completed.store(true, std::memory_order_release);
	}).accepted);
	direct_request.sources.front().display_name = "mutated-after-submit.jpg";
	pump_messages_until([&direct_completed] {
		return direct_completed.load(std::memory_order_acquire);
	});
	REQUIRE(direct_result.has_value());
	REQUIRE(direct_result->succeeded());
	REQUIRE(direct_result->summary.photos.front().source_display_name
			== "direct.jpg");
	REQUIRE(direct_result->session.repository.photos.size() == 1U);
	session = direct_result->session;

	const std::filesystem::path item_pending =
		temporary.path() / "item-pending.jpg";
	{
		std::ofstream output{item_pending, std::ios::binary | std::ios::trunc};
		output << "item-pending-source";
	}
	std::atomic_bool item_completed{};
	std::optional<shuba::ui::ItemSaveWithPendingPhotosResult> item_result;
	shuba::ui::ItemSaveWithPendingPhotosRequest item_request{
		.current_session	 = session,
		.identifiers		 = message_identifiers,
		.clock				 = message_clock,
		.operation_gate		 = gate,
		.staging_service	 = message_staging,
		.fingerprint_service = message_fingerprinting,
		.decode_service		 = message_decoder,
		.photo_codec		 = message_codec,
		.draft			 = shuba::ui::ItemDraft{.display_name		  = "Runner item",
												.category			  = "Testing",
												.warning_acknowledged = true},
		.pending_sources = {make_ready_pending_source(item_pending,
													  "item-pending.jpg")},
		.create_previous_copy = false};
	REQUIRE(runner
				.submit_item_save(
					item_request,
					[&](shuba::ui::AppShellPhotoOperationRunner::Result
							operation_result) {
		item_result = std::get<shuba::ui::ItemSaveWithPendingPhotosResult>(
			std::move(operation_result));
		item_completed.store(true, std::memory_order_release);
	}).accepted);
	item_request.draft.display_name = "mutated item";
	pump_messages_until([&item_completed] {
		return item_completed.load(std::memory_order_acquire);
	});
	REQUIRE(item_result.has_value());
	REQUIRE(item_result->item_saved());
	REQUIRE(item_result->import_result.succeeded());
	REQUIRE(item_result->session.repository.items.back().record.display_name
			== "Runner item");
	REQUIRE_FALSE(std::filesystem::exists(item_pending));
	session = item_result->session;

	const std::filesystem::path storage_pending =
		temporary.path() / "storage-pending.jpg";
	{
		std::ofstream output{storage_pending,
							 std::ios::binary | std::ios::trunc};
		output << "storage-pending-source";
	}
	std::atomic_bool storage_completed{};
	std::optional<shuba::ui::StorageSaveWithPendingPhotosResult> storage_result;
	shuba::ui::StorageSaveWithPendingPhotosRequest storage_request{
		.current_session	 = session,
		.identifiers		 = message_identifiers,
		.clock				 = message_clock,
		.operation_gate		 = gate,
		.staging_service	 = message_staging,
		.fingerprint_service = message_fingerprinting,
		.decode_service		 = message_decoder,
		.photo_codec		 = message_codec,
		.draft = shuba::ui::StorageDraft{.display_name = "Runner shelf",
										 .storage_type = "Shelf"},
		.pending_sources = {make_ready_pending_source(storage_pending,
													  "storage-pending.jpg")},
		.create_previous_copy = false};
	REQUIRE(runner
				.submit_storage_save(
					storage_request,
					[&](shuba::ui::AppShellPhotoOperationRunner::Result
							operation_result) {
		storage_result =
			std::get<shuba::ui::StorageSaveWithPendingPhotosResult>(
				std::move(operation_result));
		storage_completed.store(true, std::memory_order_release);
	}).accepted);
	storage_request.draft.display_name = "mutated storage";
	pump_messages_until([&storage_completed] {
		return storage_completed.load(std::memory_order_acquire);
	});
	REQUIRE(storage_result.has_value());
	REQUIRE(storage_result->storage_saved());
	REQUIRE(storage_result->import_result.succeeded());
	REQUIRE(
		storage_result->session.repository.storages.back().record.display_name
		== "Runner shelf");
	REQUIRE(storage_result->session.repository.photos.size() == 3U);
	REQUIRE_FALSE(std::filesystem::exists(storage_pending));
	REQUIRE_FALSE(gate.is_busy());
}
