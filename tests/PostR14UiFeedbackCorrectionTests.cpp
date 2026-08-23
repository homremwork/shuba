#include "Localization/Facade.hpp"
#include "Platform/LinuxFakes.hpp"
#include "UI/AppShell/EditCoordinator.hpp"
#include "UI/AppShell/ScreenRenderer.hpp"
#include "UI/Session/CatalogStartupSession.hpp"
#include "UI/Session/EntityEditSession.hpp"

#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

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
			shuba::platform::SourceByteFingerprint{.source_md5 =
													   "post-r14-ready-photo"});
	}
};

class BlockingDecodeService final
	: public shuba::platform::SourceImageDecodeService {
public:
	explicit BlockingDecodeService(std::shared_ptr<BlockingPoint> point_value)
		: point(std::move(point_value)) {}

	[[nodiscard]] shuba::platform::PlatformValueResult<
		shuba::platform::ImagePixels>
	decode_source_image(
		const shuba::platform::SourceImageDecodeRequest&,
		const shuba::platform::PlatformOperationContext&,
		shuba::platform::ProgressSink&,
		shuba::platform::CancellationToken& cancellation) override {
		point->enter_and_wait(cancellation);
		if (cancellation.cancellation_requested()) {
			return shuba::platform::platform_value_user_cancelled<
				shuba::platform::ImagePixels>();
		}
		return shuba::platform::platform_value_success(
			shuba::platform::ImagePixels{
				.width				= 2,
				.height				= 1,
				.format				= shuba::platform::PixelFormat::Rgba8,
				.bytes				= {8, 16, 24, 255, 32, 40, 48, 255},
				.source_description = "post-R14 synthetic source"});
	}

private:
	std::shared_ptr<BlockingPoint> point;
};

class TestWorkerServiceFactory final
	: public shuba::ui::ShellOperationWorkerServiceFactory {
public:
	explicit TestWorkerServiceFactory(
		std::shared_ptr<BlockingPoint> point_value)
		: point(std::move(point_value)) {}

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
		return std::make_unique<BlockingDecodeService>(point);
	}

	[[nodiscard]] std::unique_ptr<shuba::platform::InternalPhotoCodec>
	make_internal_photo_codec() const override {
		return std::make_unique<shuba::platform::MarkerInternalPhotoCodec>();
	}

private:
	std::shared_ptr<BlockingPoint> point;
};

[[nodiscard]] bool has_edit_diagnostic(
	const shuba::ui::FeedbackState& feedback, std::string_view code) {
	for (const shuba::ui::EntityEditDiagnostic& diagnostic :
		 feedback.edit_diagnostics) {
		if (diagnostic.code == code)
			return true;
	}
	return false;
}

[[nodiscard]] shuba::core::StableIdentifier require_identifier(
	std::string text) {
	std::optional<shuba::core::StableIdentifier> identifier =
		shuba::core::StableIdentifier::try_create_file_safe(std::move(text));
	REQUIRE(identifier.has_value());
	return *identifier;
}

void pump_messages_until(const std::function<bool()>& predicate) {
	juce::MessageManager* manager = juce::MessageManager::getInstance();
	for (std::size_t attempt = 0U; attempt < 1000U && !predicate(); ++attempt)
		REQUIRE(manager->runDispatchLoopUntil(1));
	REQUIRE(predicate());
}

class CoordinatorHarness final {
public:
	explicit CoordinatorHarness(std::string temporary_name)
		: temporary(std::move(temporary_name))
		, path_provider(temporary.path())
		, clock(shuba::core::EpochMilliseconds{1000})
		, localization(shuba::localization::make_localization(
			  shuba::localization::Language::English, {}))
		, blocking_point(std::make_shared<BlockingPoint>())
		, worker_factory(blocking_point)
		, runner(shuba::ui::OperationRunner::Dependencies{
			  .operation_gate		  = operation_gate,
			  .worker_service_factory = worker_factory,
			  .progress				  = {},
			  .failure				  = {}}) {
		identifiers.script_stable_identifier("post-r14-catalog");
		identifiers.script_operation_identifier("post-r14-catalog-init");
		session = shuba::ui::load_catalog_session(
			shuba::ui::CatalogSessionLoadRequest{
				.path_provider			 = path_provider,
				.identifiers			 = identifiers,
				.clock					 = clock,
				.debug_demo_seed_enabled = false});
		REQUIRE(session.ready_for_browsing());

		coordinator = std::make_unique<shuba::ui::EditCoordinator>(
			shuba::ui::EditCoordinator::Dependencies{
				.session					= session,
				.route						= route,
				.item_form					= item_form,
				.storage_form				= storage_form,
				.feedback					= feedback,
				.identifiers				= identifiers,
				.clock						= clock,
				.operation_gate				= operation_gate,
				.content_staging_service	= staging_service,
				.source_fingerprint_service = fingerprint_service,
				.source_decode_service		= decode_service,
				.internal_photo_codec		= photo_codec,
				.shell_operation_runner		= runner,
				.shell_operation_state		= operation_state,
				.localization				= localization,
				.editors =
					shuba::ui::EditCoordinator::Editors{
						.item_name_editor	  = item_name_editor,
						.item_category_editor = item_category_editor,
						.item_notes_editor	  = item_notes_editor,
						.item_listing_marketplace_editor =
							item_listing_marketplace_editor,
						.item_listing_url_editor  = item_listing_url_editor,
						.item_listing_note_editor = item_listing_note_editor,
						.item_acquisition_source_editor =
							item_acquisition_source_editor,
						.storage_name_editor	 = storage_name_editor,
						.storage_type_editor	 = storage_type_editor,
						.storage_location_editor = storage_location_editor,
						.storage_notes_editor	 = storage_notes_editor},
				.cleanup_item_pending_photos	= {},
				.cleanup_storage_pending_photos = {},
				.invalidate_all_previews		= {},
				.refresh_all					= [this] { ++refresh_count; },
				.refresh_content				= [this] { ++refresh_count; },
				.begin_shell_operation =
					[this](shuba::ui::ShellOperationJobType job_type,
						   std::uint64_t generation) {
			operation_state.state	 = shuba::ui::ShellOperationState::Running;
			operation_state.job_type = job_type;
			operation_state.generation = generation;
		},
				.complete_shell_operation = [this] {
			operation_state.state = shuba::ui::ShellOperationState::Idle;
			operation_state.job_type.reset();
			++completion_count;
		}});
	}

	~CoordinatorHarness() {
		coordinator.reset();
		runner.stop();
	}

	[[nodiscard]] shuba::core::StableIdentifier create_storage() {
		identifiers.script_stable_identifier("post-r14-storage");
		identifiers.script_operation_identifier("post-r14-storage-save");
		shuba::ui::EntityEditResult result = shuba::ui::save_storage_draft(
			shuba::ui::EntityEditRequest{.current_session	   = session,
										 .identifiers		   = identifiers,
										 .clock				   = clock,
										 .create_previous_copy = false},
			shuba::ui::StorageDraft{.display_name				  = "Shelf",
									.storage_type				  = "shelf",
									.archive_warning_acknowledged = true});
		REQUIRE(result.succeeded());
		REQUIRE(result.saved_record_id.has_value());
		session = std::move(result.session);
		return *result.saved_record_id;
	}

	void attach_ready_pending_photo() {
		const std::filesystem::path staged_path =
			session.paths->staged_content_root / "post-r14-ready.jpg";
		std::filesystem::create_directories(staged_path.parent_path());
		std::ofstream output{staged_path, std::ios::binary | std::ios::trunc};
		output << "post-r14-ready-source";
		output.close();
		item_form.pending_photos = {shuba::ui::PendingPhotoSource{
			.source_index  = 0U,
			.display_name  = "post-r14-ready.jpg",
			.byte_count	   = std::filesystem::file_size(staged_path),
			.status		   = shuba::ui::PendingPhotoStatus::Staged,
			.staged_source = shuba::platform::make_local_file_source(
				staged_path, "post-r14-ready.jpg"),
			.staged_path = staged_path,
			.source_md5	 = "post-r14-ready-photo"}};
	}

	void set_required_item_fields() {
		item_name_editor.setText("Camera", juce::dontSendNotification);
		item_category_editor.setText("Electronics", juce::dontSendNotification);
	}

	TemporaryDirectory temporary;
	shuba::platform::LinuxFakePathProvider path_provider;
	shuba::platform::ScriptedIdentifierSource identifiers;
	shuba::core::ManualClock clock;
	shuba::localization::Localization localization;
	shuba::ui::CatalogSessionState session;
	shuba::ui::RouteState route;
	shuba::ui::ItemFormState item_form;
	shuba::ui::StorageFormState storage_form;
	shuba::ui::FeedbackState feedback;
	shuba::core::OperationGate operation_gate;
	shuba::platform::LinuxFakeContentStagingService staging_service;
	TestFingerprintService fingerprint_service;
	shuba::platform::SyntheticSourceImageDecodeService decode_service;
	shuba::platform::MarkerInternalPhotoCodec photo_codec;
	shuba::ui::OperationState operation_state;
	std::shared_ptr<BlockingPoint> blocking_point;
	TestWorkerServiceFactory worker_factory;
	shuba::ui::OperationRunner runner;
	juce::TextEditor item_name_editor;
	juce::TextEditor item_category_editor;
	juce::TextEditor item_notes_editor;
	juce::TextEditor item_listing_marketplace_editor;
	juce::TextEditor item_listing_url_editor;
	juce::TextEditor item_listing_note_editor;
	juce::TextEditor item_acquisition_source_editor;
	juce::TextEditor storage_name_editor;
	juce::TextEditor storage_type_editor;
	juce::TextEditor storage_location_editor;
	juce::TextEditor storage_notes_editor;
	std::unique_ptr<shuba::ui::EditCoordinator> coordinator;
	std::uint32_t refresh_count{};
	std::uint32_t completion_count{};
};
}	 // namespace

TEST_CASE("Post-R14 item detail exposes distinct edit and storage routes",
		  "[post-r14][item-detail][actions]") {
	const shuba::core::StableIdentifier item_id =
		require_identifier("post-r14-action-item");
	const shuba::core::StableIdentifier storage_id =
		require_identifier("post-r14-action-storage");
	const shuba::persistence::ItemEnvelope item{
		.record = shuba::domain::ItemRecord{.id			  = item_id,
											.display_name = "Camera",
											.category	  = "Electronics",
											.storage_id	  = storage_id}};
	const shuba::persistence::StorageEnvelope storage{
		.record =
			shuba::domain::StorageRecord{.id		   = storage_id,
										 .display_name = "Long Shelf Полка",
										 .storage_type = "shelf"}};
	shuba::catalog::CatalogRepositoryState repository;
	repository.storages.push_back(storage);
	repository.storage_index_by_id.emplace(storage_id.value(), 0U);
	const shuba::catalog::ItemProjection resolved_projection{
		.id						 = item_id,
		.storage_reference_state = shuba::domain::ReferenceState::Resolved,
		.storage_id				 = storage_id};

	SECTION("resolved assignment has one action per destination") {
		const std::vector<shuba::ui::ItemDetailAction> actions =
			shuba::ui::item_detail_actions(item, resolved_projection,
										   repository);
		REQUIRE(actions
				== std::vector<shuba::ui::ItemDetailAction>{
					shuba::ui::ItemDetailAction{
						.kind = shuba::ui::ItemDetailActionKind::EditItem,
						.destination_id = item_id},
					shuba::ui::ItemDetailAction{
						.kind = shuba::ui::ItemDetailActionKind::OpenStorage,
						.destination_id = storage_id}});
	}

	SECTION("broken assignment has no invalid open action") {
		shuba::catalog::ItemProjection projection = resolved_projection;
		projection.broken_storage_reference		  = true;
		const std::vector<shuba::ui::ItemDetailAction> actions =
			shuba::ui::item_detail_actions(item, projection, repository);
		REQUIRE(actions.size() == 1U);
		REQUIRE(actions.front().kind
				== shuba::ui::ItemDetailActionKind::EditItem);
	}

	SECTION("unassigned item has no open action") {
		shuba::persistence::ItemEnvelope unassigned = item;
		unassigned.record.storage_id.reset();
		const std::vector<shuba::ui::ItemDetailAction> actions =
			shuba::ui::item_detail_actions(
				unassigned, shuba::catalog::ItemProjection{.id = item_id},
				repository);
		REQUIRE(actions.size() == 1U);
		REQUIRE(actions.front().destination_id == item_id);
	}
}

TEST_CASE("Post-R14 assigned pending-photo item save owns current feedback",
		  "[post-r14][edit-coordinator][warning-lifecycle]") {
	juce::ScopedJuceInitialiser_GUI juce_initialiser;
	CoordinatorHarness harness{"shuba-post-r14-assigned-warning"};
	const shuba::core::StableIdentifier storage_id = harness.create_storage();
	harness.route.destination = shuba::ui::RootDestination::StorageDetail;
	harness.route.selected_storage_id = storage_id;
	harness.feedback.edit_message	  = "Confirm warning and save again.";
	harness.feedback.edit_diagnostics = {shuba::ui::EntityEditDiagnostic{
		.severity = shuba::core::DiagnosticSeverity::RecoverableWarning,
		.code	  = "item_saved_without_storage",
		.message  = "stale no-storage warning"}};

	harness.coordinator->open_new_item_form(storage_id);
	REQUIRE(harness.route.destination == shuba::ui::RootDestination::ItemForm);
	REQUIRE(harness.item_form.draft.storage_id == storage_id);
	REQUIRE(harness.feedback.edit_message.empty());
	REQUIRE(harness.feedback.edit_diagnostics.empty());
	const std::uint32_t refreshes_after_open = harness.refresh_count;
	REQUIRE(refreshes_after_open > 0U);
	REQUIRE(harness.item_form.draft.storage_id == storage_id);

	harness.set_required_item_fields();
	harness.attach_ready_pending_photo();
	harness.coordinator->save_item_form();
	REQUIRE(harness.operation_state.active());
	REQUIRE(harness.item_form.draft.storage_id == storage_id);
	harness.item_form.draft.storage_id.reset();
	harness.blocking_point->wait_until_entered();
	REQUIRE_FALSE(
		has_edit_diagnostic(harness.feedback, "item_saved_without_storage"));

	harness.blocking_point->release();
	pump_messages_until([&harness] { return harness.completion_count == 1U; });
	REQUIRE_FALSE(harness.operation_state.active());
	REQUIRE(harness.route.destination
			== shuba::ui::RootDestination::ItemDetail);
	REQUIRE(harness.session.repository.items.size() == 1U);
	REQUIRE(harness.session.repository.items.front().record.storage_id
			== storage_id);
	REQUIRE_FALSE(
		has_edit_diagnostic(harness.feedback, "item_saved_without_storage"));
}

TEST_CASE(
	"Post-R14 unassigned item retains its current acknowledgement warning",
	"[post-r14][edit-coordinator][warning-lifecycle]") {
	juce::ScopedJuceInitialiser_GUI juce_initialiser;
	CoordinatorHarness harness{"shuba-post-r14-unassigned-warning"};
	harness.route.destination = shuba::ui::RootDestination::Add;
	harness.coordinator->open_new_item_form(std::nullopt);
	harness.set_required_item_fields();
	harness.attach_ready_pending_photo();

	harness.coordinator->save_item_form();
	pump_messages_until([&harness] { return harness.completion_count == 1U; });
	REQUIRE(harness.route.destination == shuba::ui::RootDestination::ItemForm);
	REQUIRE(harness.item_form.draft.warning_acknowledged);
	REQUIRE(
		has_edit_diagnostic(harness.feedback, "item_saved_without_storage"));

	harness.coordinator->save_item_form();
	REQUIRE(harness.operation_state.active());
	harness.blocking_point->wait_until_entered();
	REQUIRE(
		has_edit_diagnostic(harness.feedback, "item_saved_without_storage"));

	harness.blocking_point->release();
	pump_messages_until([&harness] { return harness.completion_count == 2U; });
	REQUIRE(harness.route.destination
			== shuba::ui::RootDestination::ItemDetail);
	REQUIRE(harness.session.repository.items.size() == 1U);
	REQUIRE_FALSE(
		harness.session.repository.items.front().record.storage_id.has_value());
	REQUIRE_FALSE(
		has_edit_diagnostic(harness.feedback, "item_saved_without_storage"));
}
