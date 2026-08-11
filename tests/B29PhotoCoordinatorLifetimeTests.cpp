#include "Localization/Facade.hpp"
#include "Platform/LinuxFakes.hpp"
#include "UI/AppShellPhotoCoordinator.hpp"

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {
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
