#include "Catalog/CatalogRepository.hpp"
#include "Localization/Facade.hpp"
#include "Platform/LinuxFakes.hpp"
#include "UI/AppShellPreviewScheduler.hpp"
#include "UI/Session/ImagePreviewSession.hpp"
#include "UI/View/Primitives/PhotoManagement.hpp"
#include "UI/View/Primitives/PhotoViewerPinchGesture.hpp"
#include "UI/View/ScreenText.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
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
#include <vector>

namespace {
class TemporaryDirectory final {
public:
	explicit TemporaryDirectory(std::string leaf_prefix)
		: path_value(std::filesystem::temp_directory_path()
					 / (std::move(leaf_prefix) + "-"
						+ std::to_string(std::chrono::steady_clock::now()
											 .time_since_epoch()
											 .count()))) {
		std::error_code ignored;
		std::filesystem::remove_all(path_value, ignored);
		std::filesystem::create_directories(path_value);
	}

	TemporaryDirectory(const TemporaryDirectory&)				 = delete;
	TemporaryDirectory& operator=(const TemporaryDirectory&)	 = delete;
	TemporaryDirectory(TemporaryDirectory&&) noexcept			 = delete;
	TemporaryDirectory& operator=(TemporaryDirectory&&) noexcept = delete;

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

[[nodiscard]] shuba::core::StableIdentifier make_id(std::string text) {
	std::optional<shuba::core::StableIdentifier> identifier =
		shuba::core::StableIdentifier::try_create_file_safe(std::move(text));
	REQUIRE(identifier.has_value());
	return *identifier;
}

[[nodiscard]] shuba::platform::ImagePixels make_pixels(
	std::uint32_t width, std::uint32_t height, std::uint8_t first_value = 1U) {
	shuba::platform::ImagePixels pixels{
		.width				  = width,
		.height				  = height,
		.format				  = shuba::platform::PixelFormat::Rgba8,
		.source_description	  = "test pixels",
		.elapsed_milliseconds = 17U};
	const std::uint64_t byte_count =
		static_cast<std::uint64_t>(width) * height * 4U;
	pixels.bytes.reserve(static_cast<std::size_t>(byte_count));
	for (std::uint64_t index = 0U; index < byte_count; ++index)
		pixels.bytes.push_back(static_cast<std::uint8_t>(first_value + index));
	return pixels;
}

class ContentRefreshCoalescer final : private juce::Timer {
public:
	explicit ContentRefreshCoalescer(
		int debounce_milliseconds, std::function<void()> refresh_content_value)
		: debounce_interval_milliseconds(debounce_milliseconds)
		, refresh_content(std::move(refresh_content_value)) {}

	~ContentRefreshCoalescer() override { stopTimer(); }

	void request_refresh() {
		++request_count_value;
		startTimer(debounce_interval_milliseconds);
	}

	[[nodiscard]] std::uint32_t request_count() const noexcept {
		return request_count_value;
	}

	[[nodiscard]] std::uint32_t delivery_count() const noexcept {
		return delivery_count_value;
	}

private:
	void timerCallback() override {
		stopTimer();
		++delivery_count_value;
		if (refresh_content)
			refresh_content();
	}

	int debounce_interval_milliseconds;
	std::function<void()> refresh_content;
	std::uint32_t request_count_value{};
	std::uint32_t delivery_count_value{};
};

class BlockingSourceImageDecodeService final
	: public shuba::platform::SourceImageDecodeService {
public:
	explicit BlockingSourceImageDecodeService(
		shuba::platform::ImagePixels decoded_pixels_value)
		: decoded_pixels(std::move(decoded_pixels_value)) {}

	void wait_until_started() {
		std::unique_lock<std::mutex> lock{mutex};
		REQUIRE(condition.wait_for(lock, std::chrono::seconds{5},
								   [this] { return started; }));
	}

	void release() {
		const std::lock_guard<std::mutex> lock{mutex};
		released = true;
		condition.notify_all();
	}

	void wait_until_completed(std::size_t expected_count) {
		std::unique_lock<std::mutex> lock{mutex};
		REQUIRE(condition.wait_for(lock, std::chrono::seconds{5},
								   [this, expected_count] {
			return completed_count >= expected_count;
		}));
	}

	[[nodiscard]] shuba::platform::PlatformValueResult<
		shuba::platform::ImagePixels>
	decode_source_image(
		const shuba::platform::SourceImageDecodeRequest& request,
		const shuba::platform::PlatformOperationContext& context,
		shuba::platform::ProgressSink& progress_sink,
		shuba::platform::CancellationToken& cancellation_token) override {
		(void)request;
		(void)context;
		(void)progress_sink;
		{
			std::unique_lock<std::mutex> lock{mutex};
			started = true;
			condition.notify_all();
			condition.wait(lock, [this] { return released; });
		}
		if (cancellation_token.cancellation_requested()) {
			const std::lock_guard<std::mutex> lock{mutex};
			++completed_count;
			condition.notify_all();
			return shuba::platform::platform_value_user_cancelled<
				shuba::platform::ImagePixels>();
		}
		{
			const std::lock_guard<std::mutex> lock{mutex};
			++completed_count;
			condition.notify_all();
		}
		return shuba::platform::platform_value_success(decoded_pixels);
	}

private:
	shuba::platform::ImagePixels decoded_pixels;
	std::mutex mutex;
	std::condition_variable condition;
	bool started{};
	bool released{};
	std::size_t completed_count{};
};

void pump_messages_until(const std::function<bool()>& predicate) {
	juce::MessageManager* const manager = juce::MessageManager::getInstance();
	for (std::size_t attempt = 0U; attempt < 1000U && !predicate(); ++attempt)
		REQUIRE(manager->runDispatchLoopUntil(1));
	REQUIRE(predicate());
}

[[nodiscard]] shuba::ui::ImagePreviewRequestIdentity identity(
	std::string text,
	shuba::ui::ImagePreviewSize size = {.max_width = 64U, .max_height = 64U}) {
	return shuba::ui::make_internal_photo_preview_identity(
		make_id(std::move(text)), size);
}

[[nodiscard]] shuba::domain::RecordTimestamps make_timestamps(
	std::int64_t created_at, std::int64_t updated_at) {
	return shuba::domain::RecordTimestamps{
		.created_at = shuba::core::EpochMilliseconds{created_at},
		.updated_at = shuba::core::EpochMilliseconds{updated_at}};
}

[[nodiscard]] shuba::persistence::ItemEnvelope make_item(std::string id) {
	return shuba::persistence::ItemEnvelope{
		.record =
			shuba::domain::ItemRecord{.id			= make_id(std::move(id)),
									  .display_name = "Preview Owner",
									  .category		= "other",
									  .timestamps	= make_timestamps(1, 2)}};
}

[[nodiscard]] shuba::persistence::PhotoEnvelope make_photo(
	std::string id, const shuba::core::StableIdentifier& owner_id) {
	return shuba::persistence::PhotoEnvelope{
		.record = shuba::domain::PhotoRecord{
			.id				  = make_id(std::move(id)),
			.owner_type		  = shuba::domain::PhotoOwnerType::Item,
			.owner_id		  = owner_id,
			.media_format	  = shuba::domain::PhotoMediaFormat::JpegXl,
			.sort_order		  = 1000,
			.is_main		  = true,
			.width			  = 4,
			.height			  = 2,
			.encoded_bytes	  = std::uint64_t{15},
			.source_mime_type = "image/jpeg",
			.timestamps		  = make_timestamps(10, 10)}};
}

void write_text(const std::filesystem::path& path, std::string_view text) {
	std::filesystem::create_directories(path.parent_path());
	std::ofstream output{path, std::ios::binary | std::ios::trunc};
	REQUIRE(output.good());
	output << text;
}

[[nodiscard]] shuba::ui::PendingPhotoSource make_pending_source(
	const std::filesystem::path& staged_path, std::string display_name,
	std::uint64_t byte_count) {
	shuba::platform::ContentSourceDescriptor descriptor =
		shuba::platform::make_local_file_source(staged_path, display_name);
	descriptor.byte_count = byte_count;
	descriptor.transient  = true;
	return shuba::ui::PendingPhotoSource{
		.source_index  = 0U,
		.display_name  = std::move(display_name),
		.byte_count	   = byte_count,
		.status		   = shuba::ui::PendingPhotoStatus::Staged,
		.staged_source = std::move(descriptor),
		.staged_path   = staged_path};
}

[[nodiscard]] shuba::catalog::CatalogRepositoryState state_with_photo(
	const shuba::persistence::ItemEnvelope& item,
	const shuba::persistence::PhotoEnvelope& photo,
	std::vector<std::string> readable_media) {
	return shuba::catalog::build_catalog_repository(
		shuba::catalog::CatalogRepositoryInput{
			.items	= {item},
			.photos = {photo},
			.media	= shuba::catalog::CatalogMediaSnapshot{
				.complete_scan_available	= true,
				.readable_photo_media_files = std::move(readable_media)}});
}

struct PreviewHarness final {
	TemporaryDirectory temporary{"shuba-b22-preview"};
	shuba::platform::LinuxFakePathProvider path_provider{temporary.path()};
	shuba::platform::AppPrivatePaths paths{
		*path_provider.resolve_app_private_paths().value};
	shuba::platform::MarkerInternalPhotoCodec codec;
	shuba::platform::SyntheticSourceImageDecodeService source_decoder;
	shuba::platform::MarkerJpegExportService jpeg;
	shuba::platform::LinuxFakeDocumentExportService document_export;
	shuba::core::OperationGate gate;
	shuba::platform::ScriptedIdentifierSource identifiers;
	shuba::platform::ProgressCollector progress;
	shuba::platform::ManualCancellationToken cancellation;

	[[nodiscard]] shuba::catalog::PhotoExportUseCase use_case(
		shuba::platform::JpegExportService& jpeg_service) {
		return shuba::catalog::PhotoExportUseCase{
			identifiers, gate, codec, jpeg_service, document_export};
	}
};
}	 // namespace

TEST_CASE("B22 preview scaler bounds RGBA pixels without metadata effects",
		  "[b22][image-preview]") {
	const shuba::platform::ImagePixels source = make_pixels(4U, 2U);
	std::optional<shuba::platform::ImagePixels> scaled =
		shuba::ui::scale_image_pixels_for_preview(
			source,
			shuba::ui::ImagePreviewSize{.max_width = 2U, .max_height = 2U});
	REQUIRE(scaled.has_value());
	REQUIRE(scaled->width == 2U);
	REQUIRE(scaled->height == 1U);
	REQUIRE(scaled->bytes.size() == 8U);
	REQUIRE(scaled->bytes[0] == source.bytes[0]);
	REQUIRE(scaled->bytes[4] == source.bytes[8]);

	std::optional<shuba::platform::ImagePixels> unchanged =
		shuba::ui::scale_image_pixels_for_preview(
			source,
			shuba::ui::ImagePreviewSize{.max_width = 8U, .max_height = 8U});
	REQUIRE(unchanged.has_value());
	REQUIRE(unchanged->width == source.width);
	REQUIRE(unchanged->height == source.height);
	REQUIRE(unchanged->bytes == source.bytes);

	shuba::platform::ImagePixels invalid = source;
	invalid.bytes.pop_back();
	REQUIRE_FALSE(shuba::ui::scale_image_pixels_for_preview(
					  invalid, shuba::ui::ImagePreviewSize{.max_width  = 2U,
														   .max_height = 2U})
					  .has_value());
	REQUIRE_FALSE(shuba::ui::scale_image_pixels_for_preview(
					  source, shuba::ui::ImagePreviewSize{.max_width  = 0U,
														  .max_height = 2U})
					  .has_value());

	shuba::catalog::CatalogRepositoryState repository;
	repository.photos.push_back(shuba::persistence::PhotoEnvelope{
		.record = shuba::domain::PhotoRecord{
			.id = make_id("photo-owned"), .owner_id = make_id("item-owned")}});
	const std::vector<shuba::persistence::PhotoEnvelope> before_photos =
		repository.photos;
	(void)shuba::ui::scale_image_pixels_for_preview(
		source, shuba::ui::ImagePreviewSize{.max_width = 2U, .max_height = 2U});
	REQUIRE(repository.photos == before_photos);
}

TEST_CASE("B22 pixel-to-JUCE conversion preserves straight RGB at full opacity",
		  "[b22][image-preview][pixels]") {
	shuba::platform::ImagePixels pixels{
		.width				= 2U,
		.height				= 1U,
		.format				= shuba::platform::PixelFormat::Rgba8,
		.bytes				= {250U, 200U, 150U, 0U, 12U, 34U, 56U, 128U},
		.source_description = "straight rgba test pixels"};

	juce::Image image = shuba::ui::juce_image_from_pixels(pixels);
	REQUIRE(image.isValid());
	REQUIRE(image.getFormat() == juce::Image::RGB);

	const juce::Colour first_pixel = image.getPixelAt(0, 0);
	REQUIRE(first_pixel.isOpaque());
	REQUIRE(first_pixel.getRed() == 250U);
	REQUIRE(first_pixel.getGreen() == 200U);
	REQUIRE(first_pixel.getBlue() == 150U);

	const juce::Colour second_pixel = image.getPixelAt(1, 0);
	REQUIRE(second_pixel.isOpaque());
	REQUIRE(second_pixel.getRed() == 12U);
	REQUIRE(second_pixel.getGreen() == 34U);
	REQUIRE(second_pixel.getBlue() == 56U);
}

TEST_CASE(
	"JI.5 pinch gesture tracks two touch sources and safely rejects invalid "
	"streams",
	"[ji5][b22][image-preview][pinch]") {
	shuba::ui::PhotoViewerPinchGesture gesture;
	const juce::Point<float> first_start{20.0f, 40.0f};
	const juce::Point<float> second_start{80.0f, 40.0f};

	gesture.begin_touch(-1, first_start);
	REQUIRE_FALSE(gesture.active());
	REQUIRE_FALSE(gesture.tracks_touch(-1));
	REQUIRE_FALSE(gesture.update_touch(-1, first_start).has_value());
	gesture.end_touch(-1);
	REQUIRE_FALSE(gesture.single_touch().has_value());

	gesture.begin_touch(4, first_start);
	gesture.begin_touch(4, juce::Point<float>{30.0f, 40.0f});
	REQUIRE_FALSE(gesture.active());
	gesture.begin_touch(9, second_start);
	REQUIRE(gesture.active());
	REQUIRE(gesture.tracks_touch(4));
	REQUIRE(gesture.tracks_touch(9));

	const std::optional<shuba::ui::PhotoViewerPinchUpdate> first_update =
		gesture.update_touch(9, juce::Point<float>{90.0f, 40.0f});
	REQUIRE(first_update.has_value());
	REQUIRE(first_update->scale_factor == Catch::Approx{1.2f});
	REQUIRE(first_update->midpoint.x == Catch::Approx{60.0f});
	REQUIRE(first_update->midpoint.y == Catch::Approx{40.0f});

	gesture.begin_touch(12, juce::Point<float>{130.0f, 40.0f});
	REQUIRE_FALSE(gesture.tracks_touch(12));
	REQUIRE_FALSE(gesture.update_touch(12, juce::Point<float>{140.0f, 40.0f})
					  .has_value());

	gesture.end_touch(4);
	const std::optional<shuba::ui::PhotoViewerPinchTouch> remaining_touch =
		gesture.single_touch();
	REQUIRE(remaining_touch.has_value());
	REQUIRE(remaining_touch->index == 9);
	REQUIRE_FALSE(gesture.active());
	REQUIRE(gesture.tracks_touch(9));
	REQUIRE_FALSE(
		gesture.update_touch(9, juce::Point<float>{95.0f, 40.0f}).has_value());
	gesture.end_touch(9);
	REQUIRE_FALSE(gesture.tracks_touch(9));
	REQUIRE_FALSE(gesture.single_touch().has_value());
	gesture.end_touch(9);
	REQUIRE_FALSE(gesture.single_touch().has_value());
}

TEST_CASE(
	"JI.5 pinch transform preserves midpoint when unclamped and bounds image "
	"edges",
	"[ji5][b22][image-preview][pinch]") {
	const shuba::ui::PhotoViewerPinchGeometry spacious_geometry{
		.slot_centre	= {100.0f, 100.0f},
		.slot			= {0.0f, 0.0f, 200.0f, 200.0f},
		.unzoomed_image = {0.0f, 0.0f, 400.0f, 400.0f}};
	const shuba::ui::PhotoViewerPinchTransform midpoint_preserved =
		shuba::ui::apply_photo_viewer_pinch_update(
			shuba::ui::PhotoViewerPinchTransform{.zoom_scale = 1.0f,
												 .pan_offset = {0.0f, 0.0f}},
			shuba::ui::PhotoViewerPinchUpdate{.scale_factor = 2.0f,
											  .midpoint		= {125.0f, 75.0f}},
			spacious_geometry, 1.0f, 4.0f);
	REQUIRE(midpoint_preserved.zoom_scale == Catch::Approx{2.0f});
	REQUIRE(midpoint_preserved.pan_offset.x == Catch::Approx{-25.0f});
	REQUIRE(midpoint_preserved.pan_offset.y == Catch::Approx{25.0f});

	const shuba::ui::PhotoViewerPinchGeometry constrained_geometry{
		.slot_centre	= {100.0f, 100.0f},
		.slot			= {0.0f, 0.0f, 200.0f, 200.0f},
		.unzoomed_image = {0.0f, 0.0f, 100.0f, 100.0f}};
	const shuba::ui::PhotoViewerPinchTransform edge_clamped =
		shuba::ui::apply_photo_viewer_pinch_update(
			shuba::ui::PhotoViewerPinchTransform{.zoom_scale = 1.0f,
												 .pan_offset = {0.0f, 0.0f}},
			shuba::ui::PhotoViewerPinchUpdate{.scale_factor = 4.0f,
											  .midpoint = {300.0f, -100.0f}},
			constrained_geometry, 1.0f, 4.0f);
	REQUIRE(edge_clamped.zoom_scale == Catch::Approx{4.0f});
	REQUIRE(edge_clamped.pan_offset.x == Catch::Approx{-100.0f});
	REQUIRE(edge_clamped.pan_offset.y == Catch::Approx{100.0f});

	const shuba::ui::PhotoViewerPinchTransform zoom_bounded =
		shuba::ui::apply_photo_viewer_pinch_update(
			shuba::ui::PhotoViewerPinchTransform{.zoom_scale = 3.5f,
												 .pan_offset = {0.0f, 0.0f}},
			shuba::ui::PhotoViewerPinchUpdate{.scale_factor = 2.0f,
											  .midpoint		= {100.0f, 100.0f}},
			spacious_geometry, 1.0f, 4.0f);
	REQUIRE(zoom_bounded.zoom_scale == Catch::Approx{4.0f});

	const shuba::ui::PhotoViewerPinchTransform minimum_bounded =
		shuba::ui::apply_photo_viewer_pinch_update(
			shuba::ui::PhotoViewerPinchTransform{.zoom_scale = 1.2f,
												 .pan_offset = {0.0f, 0.0f}},
			shuba::ui::PhotoViewerPinchUpdate{.scale_factor = 0.1f,
											  .midpoint		= {100.0f, 100.0f}},
			spacious_geometry, 1.0f, 4.0f);
	REQUIRE(minimum_bounded.zoom_scale == Catch::Approx{1.0f});
}

TEST_CASE(
	"JI.5 image-slot gesture boundary leaves viewer metadata drags to the "
	"viewport",
	"[ji5][b22][image-preview][pinch]") {
	const juce::Rectangle<float> image_slot{12.0f, 16.0f, 300.0f, 220.0f};
	REQUIRE(shuba::ui::photo_viewer_gesture_starts_in_image_slot(
		image_slot, juce::Point<float>{12.0f, 16.0f}));
	REQUIRE(shuba::ui::photo_viewer_gesture_starts_in_image_slot(
		image_slot, juce::Point<float>{311.0f, 235.0f}));
	REQUIRE_FALSE(shuba::ui::photo_viewer_gesture_starts_in_image_slot(
		image_slot, juce::Point<float>{160.0f, 246.0f}));
	REQUIRE_FALSE(shuba::ui::photo_viewer_gesture_starts_in_image_slot(
		juce::Rectangle<float>{}, juce::Point<float>{12.0f, 16.0f}));

	REQUIRE_FALSE(
		shuba::ui::photo_viewer_blocks_viewport_drag(true, false, false));
	REQUIRE(shuba::ui::photo_viewer_blocks_viewport_drag(true, true, false));
	REQUIRE(shuba::ui::photo_viewer_blocks_viewport_drag(false, false, true));
	REQUIRE_FALSE(
		shuba::ui::photo_viewer_blocks_viewport_drag(false, false, false));
}

TEST_CASE(
	"B22 managed photo deck constructs and resizes with stored and staged "
	"photos",
	"[b22][image-preview][ui]") {
	std::vector<shuba::ui::CurrentPhotoCardEntry> current_entries;
	current_entries.push_back(shuba::ui::CurrentPhotoCardEntry{
		.photo_id	 = make_id("current-photo-main"),
		.image		 = juce::Image{juce::Image::RGB, 8, 6, true},
		.title		 = "Main photo",
		.placeholder = "Main preview",
		.state		 = shuba::ui::PreviewImageVisualState::Loaded,
		.is_main	 = true,
		.delete_confirmation_requested = false});
	current_entries.push_back(shuba::ui::CurrentPhotoCardEntry{
		.photo_id	 = make_id("current-photo-delete"),
		.image		 = juce::Image{juce::Image::RGB, 8, 6, true},
		.title		 = "Delete candidate",
		.placeholder = "Delete preview",
		.state		 = shuba::ui::PreviewImageVisualState::Loaded,
		.is_main	 = false,
		.delete_confirmation_requested = true});
	std::vector<shuba::ui::StagedPhotoCardEntry> staged_entries;
	staged_entries.push_back(shuba::ui::StagedPhotoCardEntry{
		.source =
			shuba::ui::PendingPhotoSource{
				.source_index = 0U,
				.display_name = "Pending candidate",
				.status		  = shuba::ui::PendingPhotoStatus::Staged},
		.image		 = juce::Image{juce::Image::RGB, 6, 8, true},
		.placeholder = "Staged preview",
		.state		 = shuba::ui::PreviewImageVisualState::Staged});

	std::uint32_t select_current_calls{};
	std::uint32_t select_staged_calls{};
	std::uint32_t add_staged_calls{};
	std::uint32_t clear_staged_calls{};
	std::uint32_t remove_staged_calls{};
	std::vector<shuba::core::StableIdentifier> set_main_calls;
	std::vector<shuba::core::StableIdentifier> request_delete_calls;
	std::vector<shuba::core::StableIdentifier> confirm_delete_calls;
	std::uint32_t cancel_delete_calls{};
	shuba::localization::Localization localization =
		shuba::localization::make_localization(
			shuba::localization::Language::English, {});
	shuba::ui::ManagedPhotoDeckComponent deck{
		shuba::ui::ManagedPhotoDeckModel{
			.current_entries = std::move(current_entries),
			.staged_entries	 = std::move(staged_entries),
			.staged_selected = false,
			.selected_index	 = 0U},
		shuba::ui::ManagedPhotoDeckHandlers{
			.select_current = [&select_current_calls](
								  std::size_t) { ++select_current_calls; },
			.select_staged =
				[&select_staged_calls](std::size_t) { ++select_staged_calls; },
			.add_staged	  = [&add_staged_calls] { ++add_staged_calls; },
			.clear_staged = [&clear_staged_calls] { ++clear_staged_calls; },
			.remove_staged =
				[&remove_staged_calls](std::size_t) { ++remove_staged_calls; },
			.set_main_current =
				[&set_main_calls](shuba::core::StableIdentifier photo_id) {
		set_main_calls.push_back(std::move(photo_id));
	},
			.request_delete_current =
				[&request_delete_calls](
					shuba::core::StableIdentifier photo_id) {
		request_delete_calls.push_back(std::move(photo_id));
	},
			.confirm_delete_current =
				[&confirm_delete_calls](
					shuba::core::StableIdentifier photo_id) {
		confirm_delete_calls.push_back(std::move(photo_id));
	},
			.cancel_delete_current =
				[&cancel_delete_calls] { ++cancel_delete_calls; }},
		localization};

	deck.setSize(420, 410);
	deck.resized();

	REQUIRE(deck.getWidth() == 420);
	REQUIRE(deck.getHeight() == 410);
	REQUIRE(select_current_calls == 0U);
	REQUIRE(select_staged_calls == 0U);
	REQUIRE(add_staged_calls == 0U);
	REQUIRE(clear_staged_calls == 0U);
	REQUIRE(remove_staged_calls == 0U);
	REQUIRE(set_main_calls.empty());
	REQUIRE(request_delete_calls.empty());
	REQUIRE(confirm_delete_calls.empty());
	REQUIRE(cancel_delete_calls == 0U);
}

TEST_CASE("B22 preview cache hits exact identities and can clear entries",
		  "[b22][image-preview]") {
	shuba::ui::ImagePreviewCache cache{shuba::ui::ImagePreviewCacheSettings{
		.maximum_entries = 4U, .maximum_pixel_bytes = 1024U}};
	const shuba::ui::ImagePreviewRequestIdentity small = identity(
		"photo-a",
		shuba::ui::ImagePreviewSize{.max_width = 32U, .max_height = 32U});
	const shuba::ui::ImagePreviewRequestIdentity large = identity(
		"photo-a",
		shuba::ui::ImagePreviewSize{.max_width = 64U, .max_height = 64U});
	REQUIRE(cache.put(small, make_pixels(2U, 2U)));

	REQUIRE(cache.contains(small));
	REQUIRE_FALSE(cache.contains(large));
	const shuba::platform::ImagePixels* found = cache.find(small);
	REQUIRE(found != nullptr);
	REQUIRE(found->width == 2U);
	REQUIRE(cache.stats().entry_count == 1U);
	REQUIRE(cache.stats().pixel_bytes == 16U);

	cache.remove(small);
	REQUIRE_FALSE(cache.contains(small));
	REQUIRE(cache.empty());
	REQUIRE(cache.stats().pixel_bytes == 0U);

	REQUIRE(cache.put(large, make_pixels(1U, 1U)));
	cache.clear();
	REQUIRE(cache.empty());
	REQUIRE(cache.stats().pixel_bytes == 0U);
}

TEST_CASE("JI.4 preview completion burst coalesces one delayed content rebuild",
		  "[ji4][b22][image-preview][scheduler]") {
	juce::ScopedJuceInitialiser_GUI juce_initialiser;
	PreviewHarness harness;
	const std::filesystem::path first_staged_path =
		harness.paths.staged_content_root / "ji4-first.jpg";
	const std::filesystem::path second_staged_path =
		harness.paths.staged_content_root / "ji4-second.jpg";
	write_text(first_staged_path, "ji4-first-source");
	write_text(second_staged_path, "ji4-second-source");
	harness.source_decoder.set_decoded_pixels(make_pixels(4U, 2U));
	shuba::ui::CatalogSessionState session;
	shuba::ui::AppShellPhotoDisplayState photo_display;
	shuba::ui::ImagePreviewCache cache{shuba::ui::ImagePreviewCacheSettings{
		.maximum_entries = 4U, .maximum_pixel_bytes = 1024U}};
	shuba::localization::Localization localization =
		shuba::localization::make_localization(
			shuba::localization::Language::English, {});
	std::uint32_t rendered_cache_entries{};
	ContentRefreshCoalescer coalescer{85, [&] {
		rendered_cache_entries =
			static_cast<std::uint32_t>(cache.stats().entry_count);
	}};
	shuba::ui::AppShellPreviewScheduler scheduler{
		shuba::ui::AppShellPreviewScheduler::Dependencies{
			.session				 = session,
			.photo_display			 = photo_display,
			.preview_cache			 = cache,
			.internal_photo_codec	 = harness.codec,
			.source_decode_service	 = harness.source_decoder,
			.jpeg_export_service	 = harness.jpeg,
			.document_export_service = harness.document_export,
			.localization			 = localization,
			.refresh_content		 = [&] { coalescer.request_refresh(); }}};

	scheduler.enqueue_staged_preview(
		make_pending_source(first_staged_path, "ji4-first.jpg", 16U),
		shuba::ui::ImagePreviewSize{.max_width = 2U, .max_height = 2U},
		shuba::ui::ImagePreviewRequestPriority::Normal);
	scheduler.enqueue_staged_preview(
		make_pending_source(second_staged_path, "ji4-second.jpg", 17U),
		shuba::ui::ImagePreviewSize{.max_width = 2U, .max_height = 2U},
		shuba::ui::ImagePreviewRequestPriority::Normal);

	pump_messages_until([&] { return coalescer.request_count() == 2U; });
	REQUIRE(cache.stats().entry_count == 2U);
	REQUIRE(coalescer.delivery_count() == 0U);
	pump_messages_until([&] { return coalescer.delivery_count() == 1U; });
	REQUIRE(rendered_cache_entries == 2U);
	REQUIRE(coalescer.delivery_count() == 1U);
}

TEST_CASE(
	"JI.4 scheduler preserves stale completion suppression and teardown safety",
	"[ji4][b22][image-preview][scheduler][lifetime]") {
	juce::ScopedJuceInitialiser_GUI juce_initialiser;
	PreviewHarness harness;
	const std::filesystem::path staged_path =
		harness.paths.staged_content_root / "ji4-stale.jpg";
	write_text(staged_path, "ji4-stale-source");
	BlockingSourceImageDecodeService source_decoder{make_pixels(4U, 2U)};
	shuba::ui::CatalogSessionState session;
	shuba::ui::AppShellPhotoDisplayState photo_display;
	shuba::ui::ImagePreviewCache cache;
	shuba::localization::Localization localization =
		shuba::localization::make_localization(
			shuba::localization::Language::English, {});
	std::uint32_t refresh_requests{};
	{
		shuba::ui::AppShellPreviewScheduler scheduler{
			shuba::ui::AppShellPreviewScheduler::Dependencies{
				.session				 = session,
				.photo_display			 = photo_display,
				.preview_cache			 = cache,
				.internal_photo_codec	 = harness.codec,
				.source_decode_service	 = source_decoder,
				.jpeg_export_service	 = harness.jpeg,
				.document_export_service = harness.document_export,
				.localization			 = localization,
				.refresh_content		 = [&] { ++refresh_requests; }}};
		scheduler.enqueue_staged_preview(
			make_pending_source(staged_path, "ji4-stale.jpg", 16U),
			shuba::ui::ImagePreviewSize{.max_width = 2U, .max_height = 2U},
			shuba::ui::ImagePreviewRequestPriority::Normal);
		source_decoder.wait_until_started();
		scheduler.invalidate_all();
		source_decoder.release();
		source_decoder.wait_until_completed(1U);
		for (std::size_t attempt = 0U; attempt < 10U; ++attempt)
			REQUIRE(
				juce::MessageManager::getInstance()->runDispatchLoopUntil(1));
		REQUIRE(cache.empty());
		REQUIRE(refresh_requests == 0U);
	}

	std::uint32_t teardown_refreshes{};
	std::unique_ptr<ContentRefreshCoalescer> coalescer =
		std::make_unique<ContentRefreshCoalescer>(
			85, [&] { ++teardown_refreshes; });
	coalescer->request_refresh();
	REQUIRE(coalescer->request_count() == 1U);
	coalescer.reset();
	REQUIRE(juce::MessageManager::getInstance()->runDispatchLoopUntil(100));
	REQUIRE(teardown_refreshes == 0U);
}

TEST_CASE(
	"JI.9 scheduler suspension releases only disposable preview state and "
	"suppresses stale completion",
	"[ji9][b22][image-preview][scheduler][lifecycle]") {
	juce::ScopedJuceInitialiser_GUI juce_initialiser;
	PreviewHarness harness;
	const std::filesystem::path staged_path =
		harness.paths.staged_content_root / "ji9-lifecycle-preview.jpg";
	write_text(staged_path, "ji9-lifecycle-source");
	BlockingSourceImageDecodeService source_decoder{make_pixels(4U, 2U)};
	shuba::ui::CatalogSessionState session;
	shuba::ui::AppShellPhotoDisplayState photo_display{
		.result =
			shuba::catalog::PhotoDisplayResult{
				.status = shuba::catalog::PhotoDisplayStatus::Decoded},
		.displayed_photo_id			   = make_id("ji9-display-preserved"),
		.requested_display_photo_id	   = make_id("ji9-request-preserved"),
		.pending_delete_photo_id	   = make_id("ji9-delete-preserved"),
		.viewer_transform_photo_id	   = make_id("ji9-transform-preserved"),
		.display_request_generation	   = 17U,
		.viewer_rotation_quarter_turns = 2};
	const shuba::catalog::PhotoDisplayStatus display_status_before_suspension =
		photo_display.result.status;
	const shuba::core::OperationResultCategory
		display_category_before_suspension = photo_display.result.category;
	const std::optional<shuba::core::StableIdentifier>
		displayed_photo_before_suspension = photo_display.displayed_photo_id;
	const std::optional<shuba::core::StableIdentifier>
		requested_photo_before_suspension =
			photo_display.requested_display_photo_id;
	const std::optional<shuba::core::StableIdentifier>
		pending_delete_before_suspension =
			photo_display.pending_delete_photo_id;
	const std::optional<shuba::core::StableIdentifier>
		viewer_transform_before_suspension =
			photo_display.viewer_transform_photo_id;
	const std::uint64_t display_generation_before_suspension =
		photo_display.display_request_generation;
	const int rotation_before_suspension =
		photo_display.viewer_rotation_quarter_turns;
	shuba::ui::ImagePreviewCache cache;
	const shuba::ui::ImagePreviewRequestIdentity cached_identity = identity(
		"ji9-cached-preview",
		shuba::ui::ImagePreviewSize{.max_width = 2U, .max_height = 2U});
	REQUIRE(cache.put(cached_identity, make_pixels(1U, 1U)));
	shuba::localization::Localization localization =
		shuba::localization::make_localization(
			shuba::localization::Language::English, {});
	std::uint32_t refresh_requests{};
	{
		shuba::ui::AppShellPreviewScheduler scheduler{
			shuba::ui::AppShellPreviewScheduler::Dependencies{
				.session				 = session,
				.photo_display			 = photo_display,
				.preview_cache			 = cache,
				.internal_photo_codec	 = harness.codec,
				.source_decode_service	 = source_decoder,
				.jpeg_export_service	 = harness.jpeg,
				.document_export_service = harness.document_export,
				.localization			 = localization,
				.refresh_content		 = [&] { ++refresh_requests; }}};
		scheduler.enqueue_staged_preview(
			make_pending_source(staged_path, "ji9-lifecycle-preview.jpg", 16U),
			shuba::ui::ImagePreviewSize{.max_width = 2U, .max_height = 2U},
			shuba::ui::ImagePreviewRequestPriority::Normal);
		source_decoder.wait_until_started();

		scheduler.release_disposable_preview_memory();
		scheduler.release_disposable_preview_memory();
		REQUIRE(cache.empty());
		REQUIRE(photo_display.result.status
				== display_status_before_suspension);
		REQUIRE(photo_display.result.category
				== display_category_before_suspension);
		REQUIRE(photo_display.displayed_photo_id
				== displayed_photo_before_suspension);
		REQUIRE(photo_display.requested_display_photo_id
				== requested_photo_before_suspension);
		REQUIRE(photo_display.pending_delete_photo_id
				== pending_delete_before_suspension);
		REQUIRE(photo_display.viewer_transform_photo_id
				== viewer_transform_before_suspension);
		REQUIRE(photo_display.display_request_generation
				== display_generation_before_suspension);
		REQUIRE(photo_display.viewer_rotation_quarter_turns
				== rotation_before_suspension);

		source_decoder.release();
		source_decoder.wait_until_completed(1U);
		for (std::size_t attempt = 0U; attempt < 10U; ++attempt)
			REQUIRE(
				juce::MessageManager::getInstance()->runDispatchLoopUntil(1));
		REQUIRE(cache.empty());
		REQUIRE(refresh_requests == 0U);
		REQUIRE(photo_display.result.status
				== display_status_before_suspension);
		REQUIRE(photo_display.result.category
				== display_category_before_suspension);
		REQUIRE(photo_display.displayed_photo_id
				== displayed_photo_before_suspension);
		REQUIRE(photo_display.requested_display_photo_id
				== requested_photo_before_suspension);
		REQUIRE(photo_display.pending_delete_photo_id
				== pending_delete_before_suspension);
		REQUIRE(photo_display.viewer_transform_photo_id
				== viewer_transform_before_suspension);
		REQUIRE(photo_display.display_request_generation
				== display_generation_before_suspension);
		REQUIRE(photo_display.viewer_rotation_quarter_turns
				== rotation_before_suspension);
	}
}

TEST_CASE(
	"JI.9 scheduler accepts a fresh preview request after suspension removes "
	"an in-flight request",
	"[ji9][b22][image-preview][scheduler][lifecycle]") {
	juce::ScopedJuceInitialiser_GUI juce_initialiser;
	PreviewHarness harness;
	const std::filesystem::path staged_path =
		harness.paths.staged_content_root / "ji9-fresh-preview.jpg";
	write_text(staged_path, "ji9-fresh-source");
	BlockingSourceImageDecodeService source_decoder{make_pixels(4U, 2U)};
	shuba::ui::CatalogSessionState session;
	shuba::ui::AppShellPhotoDisplayState photo_display;
	shuba::ui::ImagePreviewCache cache;
	shuba::localization::Localization localization =
		shuba::localization::make_localization(
			shuba::localization::Language::English, {});
	std::uint32_t refresh_requests{};
	{
		shuba::ui::AppShellPreviewScheduler scheduler{
			shuba::ui::AppShellPreviewScheduler::Dependencies{
				.session				 = session,
				.photo_display			 = photo_display,
				.preview_cache			 = cache,
				.internal_photo_codec	 = harness.codec,
				.source_decode_service	 = source_decoder,
				.jpeg_export_service	 = harness.jpeg,
				.document_export_service = harness.document_export,
				.localization			 = localization,
				.refresh_content		 = [&] { ++refresh_requests; }}};
		const shuba::ui::PendingPhotoSource source =
			make_pending_source(staged_path, "ji9-fresh-preview.jpg", 16U);
		const shuba::ui::ImagePreviewSize target_size{.max_width  = 2U,
													  .max_height = 2U};
		const shuba::ui::ImagePreviewRequestIdentity requested_identity =
			shuba::ui::make_staged_photo_preview_identity(source, target_size);
		scheduler.enqueue_staged_preview(
			source, target_size,
			shuba::ui::ImagePreviewRequestPriority::Normal);
		source_decoder.wait_until_started();
		scheduler.release_disposable_preview_memory();
		source_decoder.release();
		source_decoder.wait_until_completed(1U);
		for (std::size_t attempt = 0U; attempt < 10U; ++attempt)
			REQUIRE(
				juce::MessageManager::getInstance()->runDispatchLoopUntil(1));
		REQUIRE(cache.empty());

		scheduler.enqueue_staged_preview(
			source, target_size, shuba::ui::ImagePreviewRequestPriority::High);
		pump_messages_until([&] { return cache.contains(requested_identity); });
		REQUIRE(refresh_requests == 1U);
	}
}

TEST_CASE("B22 preview cache evicts least recently used entries by count",
		  "[b22][image-preview]") {
	shuba::ui::ImagePreviewCache cache{shuba::ui::ImagePreviewCacheSettings{
		.maximum_entries = 2U, .maximum_pixel_bytes = 1024U}};
	const shuba::ui::ImagePreviewRequestIdentity first =
		identity("photo-first");
	const shuba::ui::ImagePreviewRequestIdentity second =
		identity("photo-second");
	const shuba::ui::ImagePreviewRequestIdentity third =
		identity("photo-third");
	REQUIRE(cache.put(first, make_pixels(1U, 1U, 10U)));
	REQUIRE(cache.put(second, make_pixels(1U, 1U, 20U)));
	REQUIRE(cache.find(first) != nullptr);
	REQUIRE(cache.put(third, make_pixels(1U, 1U, 30U)));

	REQUIRE(cache.contains(first));
	REQUIRE_FALSE(cache.contains(second));
	REQUIRE(cache.contains(third));
	REQUIRE(cache.stats().entry_count == 2U);
}

TEST_CASE("B22 preview cache evicts by pixel byte cap",
		  "[b22][image-preview]") {
	shuba::ui::ImagePreviewCache cache{shuba::ui::ImagePreviewCacheSettings{
		.maximum_entries = 8U, .maximum_pixel_bytes = 20U}};
	const shuba::ui::ImagePreviewRequestIdentity first =
		identity("photo-first");
	const shuba::ui::ImagePreviewRequestIdentity second =
		identity("photo-second");
	REQUIRE(cache.put(first, make_pixels(2U, 2U)));
	REQUIRE(cache.put(second, make_pixels(2U, 2U)));

	REQUIRE_FALSE(cache.contains(first));
	REQUIRE(cache.contains(second));
	REQUIRE(cache.stats().entry_count == 1U);
	REQUIRE(cache.stats().pixel_bytes == 16U);

	REQUIRE_FALSE(cache.put(identity("photo-too-large"), make_pixels(3U, 2U)));
	REQUIRE_FALSE(cache.contains(identity("photo-too-large")));
	REQUIRE(cache.stats().entry_count == 1U);
}

TEST_CASE("B22 preview cache removes internal and staged sources by owner key",
		  "[b22][image-preview]") {
	shuba::ui::ImagePreviewCache cache{shuba::ui::ImagePreviewCacheSettings{
		.maximum_entries = 8U, .maximum_pixel_bytes = 1024U}};
	const shuba::core::StableIdentifier photo_id = make_id("photo-remove");
	const shuba::ui::ImagePreviewRequestIdentity internal_small =
		shuba::ui::make_internal_photo_preview_identity(
			photo_id,
			shuba::ui::ImagePreviewSize{.max_width = 32U, .max_height = 32U});
	const shuba::ui::ImagePreviewRequestIdentity internal_large =
		shuba::ui::make_internal_photo_preview_identity(
			photo_id,
			shuba::ui::ImagePreviewSize{.max_width = 64U, .max_height = 64U});
	const shuba::ui::ImagePreviewRequestIdentity staged =
		shuba::ui::make_staged_photo_preview_identity(
			std::filesystem::path{"tmp/../tmp/staged-photo.jpg"},
			shuba::ui::ImagePreviewSize{.max_width = 32U, .max_height = 32U});
	REQUIRE(cache.put(internal_small, make_pixels(1U, 1U)));
	REQUIRE(cache.put(internal_large, make_pixels(1U, 1U)));
	REQUIRE(cache.put(staged, make_pixels(1U, 1U)));

	cache.remove_internal_photo(photo_id);
	REQUIRE_FALSE(cache.contains(internal_small));
	REQUIRE_FALSE(cache.contains(internal_large));
	REQUIRE(cache.contains(staged));

	cache.remove_staged_photo(std::filesystem::path{"tmp/staged-photo.jpg"});
	REQUIRE(cache.empty());
}

TEST_CASE("B22 staged preview identity includes cheap source fingerprint",
		  "[b22][image-preview]") {
	PreviewHarness harness;
	const std::filesystem::path staged_path =
		harness.paths.staged_content_root / "fingerprinted.jpg";
	write_text(staged_path, "first");
	const shuba::ui::PendingPhotoSource pending =
		make_pending_source(staged_path, "fingerprinted.jpg", 5U);

	const shuba::ui::ImagePreviewRequestIdentity first =
		shuba::ui::make_staged_photo_preview_identity(
			pending,
			shuba::ui::ImagePreviewSize{.max_width = 32U, .max_height = 32U});
	write_text(staged_path, "larger-source");
	const shuba::ui::ImagePreviewRequestIdentity second =
		shuba::ui::make_staged_photo_preview_identity(
			pending,
			shuba::ui::ImagePreviewSize{.max_width = 32U, .max_height = 32U});

	REQUIRE(first.kind == shuba::ui::ImagePreviewRequestKind::StagedPhoto);
	REQUIRE(first.source_key == second.source_key);
	REQUIRE_FALSE(first.source_fingerprint.empty());
	REQUIRE(first != second);
}

TEST_CASE("B22 internal photo preview load scales and caches display pixels",
		  "[b22][image-preview][load]") {
	PreviewHarness harness;
	harness.identifiers.script_operation_identifier("operation-b22-preview");
	const shuba::persistence::ItemEnvelope item = make_item("item-preview");
	const shuba::persistence::PhotoEnvelope photo =
		make_photo("photo-preview", item.record.id);
	const std::filesystem::path media_path =
		harness.paths.active_catalog_root / "media/photos/photo-preview.jxl";
	write_text(media_path, "SHUBA-FAKE-JXL\n");
	const shuba::platform::PlatformOperationContext encode_context{
		.operation_id = *shuba::core::OperationIdentifier::try_create_file_safe(
			"operation-b22-seed"),
		.operation_type = shuba::platform::ProgressOperationType::PhotoImport};
	REQUIRE(
		harness.codec
			.encode_internal_photo(
				shuba::platform::InternalPhotoEncodeRequest{
					.pixels = make_pixels(4U, 2U), .output_path = media_path},
				encode_context, harness.progress, harness.cancellation)
			.succeeded());
	const shuba::catalog::CatalogRepositoryState state =
		state_with_photo(item, photo, {"media/photos/photo-preview.jxl"});
	const std::vector<shuba::persistence::PhotoEnvelope> before_photos =
		state.photos;
	shuba::catalog::PhotoExportUseCase use_case =
		harness.use_case(harness.jpeg);
	shuba::ui::ImagePreviewCache cache{shuba::ui::ImagePreviewCacheSettings{
		.maximum_entries = 4U, .maximum_pixel_bytes = 1024U}};

	shuba::ui::InternalPhotoPreviewLoadResult loaded =
		shuba::ui::load_internal_photo_preview(
			shuba::ui::InternalPhotoPreviewLoadRequest{
				.current_state = state,
				.paths		   = harness.paths,
				.photo_id	   = photo.record.id,
				.target_size   = shuba::ui::ImagePreviewSize{.max_width	 = 2U,
															 .max_height = 2U}},
			cache, use_case, harness.progress, harness.cancellation);

	REQUIRE(loaded.succeeded());
	REQUIRE_FALSE(loaded.cache_hit);
	REQUIRE(loaded.cache_stored);
	REQUIRE(loaded.metrics.has_value());
	REQUIRE(loaded.metrics->decoded_width == 4U);
	REQUIRE(loaded.metrics->decoded_height == 2U);
	REQUIRE(loaded.metrics->preview_width == 2U);
	REQUIRE(loaded.metrics->preview_height == 1U);
	REQUIRE(loaded.metrics->decode_elapsed_milliseconds == 17U);
	REQUIRE(loaded.pixels.has_value());
	REQUIRE(loaded.pixels->width == 2U);
	REQUIRE(loaded.pixels->height == 1U);
	REQUIRE(loaded.pixels->bytes.size() == 8U);
	REQUIRE(cache.stats().entry_count == 1U);
	REQUIRE(cache.stats().pixel_bytes == 8U);
	REQUIRE(state.photos == before_photos);
	REQUIRE(loaded.pixels->source_description.find("4x2 to 2x1")
			!= std::string::npos);

	shuba::ui::InternalPhotoPreviewLoadResult cached =
		shuba::ui::load_internal_photo_preview(
			shuba::ui::InternalPhotoPreviewLoadRequest{
				.current_state = state,
				.paths		   = harness.paths,
				.photo_id	   = photo.record.id,
				.target_size   = shuba::ui::ImagePreviewSize{.max_width	 = 2U,
															 .max_height = 2U}},
			cache, use_case, harness.progress, harness.cancellation);
	REQUIRE(cached.succeeded());
	REQUIRE(cached.cache_hit);
	REQUIRE_FALSE(cached.cache_stored);
	REQUIRE(cached.pixels.has_value());
	REQUIRE(cached.pixels->bytes == loaded.pixels->bytes);
	REQUIRE(cached.pixels->source_description
			== loaded.pixels->source_description);
}

TEST_CASE("B22 internal photo preview load returns missing-media placeholder",
		  "[b22][image-preview][load]") {
	PreviewHarness harness;
	const shuba::persistence::ItemEnvelope item =
		make_item("item-preview-missing-media");
	const shuba::persistence::PhotoEnvelope photo =
		make_photo("photo-preview-missing-media", item.record.id);
	const shuba::catalog::CatalogRepositoryState state =
		state_with_photo(item, photo, {});
	shuba::catalog::PhotoExportUseCase use_case =
		harness.use_case(harness.jpeg);
	shuba::ui::ImagePreviewCache cache{shuba::ui::ImagePreviewCacheSettings{
		.maximum_entries = 4U, .maximum_pixel_bytes = 1024U}};

	shuba::ui::InternalPhotoPreviewLoadResult missing =
		shuba::ui::load_internal_photo_preview(
			shuba::ui::InternalPhotoPreviewLoadRequest{
				.current_state = state,
				.paths		   = harness.paths,
				.photo_id	   = photo.record.id,
				.target_size = shuba::ui::ImagePreviewSize{.max_width  = 64U,
														   .max_height = 64U}},
			cache, use_case, harness.progress, harness.cancellation);

	REQUIRE(missing.failed());
	REQUIRE(missing.status == shuba::ui::ImagePreviewLoadStatus::Broken);
	REQUIRE(missing.category
			== shuba::core::OperationResultCategory::SourceUnavailable);
	REQUIRE(missing.placeholder.has_value());
	REQUIRE(missing.placeholder->diagnostic_code
			== "photo_display_media_missing");
	REQUIRE(missing.media_path.has_value());
	REQUIRE_FALSE(missing.pixels.has_value());
	REQUIRE(cache.empty());
}

TEST_CASE("B22 staged photo preview load scales and caches source pixels",
		  "[b22][image-preview][load]") {
	PreviewHarness harness;
	harness.identifiers.script_operation_identifier(
		"operation-b22-staged-preview");
	const std::filesystem::path staged_path =
		harness.paths.staged_content_root / "pending-preview.jpg";
	write_text(staged_path, "staged-source-bytes");
	const shuba::ui::PendingPhotoSource pending =
		make_pending_source(staged_path, "Pending Preview.JPG", 19U);
	harness.source_decoder.set_decoded_pixels(make_pixels(6U, 4U));
	shuba::ui::ImagePreviewCache cache{shuba::ui::ImagePreviewCacheSettings{
		.maximum_entries = 4U, .maximum_pixel_bytes = 1024U}};

	shuba::ui::StagedPhotoPreviewLoadResult loaded =
		shuba::ui::load_staged_photo_preview(
			shuba::ui::StagedPhotoPreviewLoadRequest{
				.source		 = pending,
				.identifiers = harness.identifiers,
				.target_size = shuba::ui::ImagePreviewSize{.max_width  = 3U,
														   .max_height = 3U}},
			cache, harness.source_decoder, harness.progress,
			harness.cancellation);

	REQUIRE(loaded.succeeded());
	REQUIRE_FALSE(loaded.cache_hit);
	REQUIRE(loaded.cache_stored);
	REQUIRE(loaded.metrics.has_value());
	REQUIRE(loaded.metrics->decoded_width == 6U);
	REQUIRE(loaded.metrics->decoded_height == 4U);
	REQUIRE(loaded.metrics->preview_width == 3U);
	REQUIRE(loaded.metrics->preview_height == 2U);
	REQUIRE(loaded.metrics->decode_elapsed_milliseconds == 17U);
	REQUIRE(loaded.pixels.has_value());
	REQUIRE(loaded.pixels->width == 3U);
	REQUIRE(loaded.pixels->height == 2U);
	REQUIRE(loaded.pixels->bytes.size() == 24U);
	REQUIRE(loaded.staged_path == staged_path);
	REQUIRE(cache.stats().entry_count == 1U);
	REQUIRE(cache.stats().pixel_bytes == 24U);
	REQUIRE(pending.ready_for_import());
	REQUIRE(harness.source_decoder.last_requested_sizing().has_value());
	REQUIRE(harness.source_decoder.last_requested_sizing()->maximum_longest_edge
			== 3U);

	harness.source_decoder.clear_decoded_pixels();
	shuba::ui::StagedPhotoPreviewLoadResult cached =
		shuba::ui::load_staged_photo_preview(
			shuba::ui::StagedPhotoPreviewLoadRequest{
				.source		 = pending,
				.identifiers = harness.identifiers,
				.target_size = shuba::ui::ImagePreviewSize{.max_width  = 3U,
														   .max_height = 3U}},
			cache, harness.source_decoder, harness.progress,
			harness.cancellation);
	REQUIRE(cached.succeeded());
	REQUIRE(cached.cache_hit);
	REQUIRE_FALSE(cached.cache_stored);
	REQUIRE(cached.pixels.has_value());
	REQUIRE(cached.pixels->bytes == loaded.pixels->bytes);
}

TEST_CASE(
	"JI.10 staged previews bound source decode by the existing preview long "
	"edge",
	"[ji10][b22][image-preview][load][decode-sizing]") {
	struct PreviewCase final {
		shuba::ui::ImagePreviewSize target_size;
		std::uint32_t expected_longest_edge;
		std::uint32_t expected_preview_width;
		std::uint32_t expected_preview_height;
	};

	const std::array<PreviewCase, 3U> cases{{
		{.target_size			  = {.max_width = 96U, .max_height = 96U},
		 .expected_longest_edge	  = 96U,
		 .expected_preview_width  = 96U,
		 .expected_preview_height = 72U},
		{.target_size			  = {.max_width = 128U, .max_height = 128U},
		 .expected_longest_edge	  = 128U,
		 .expected_preview_width  = 128U,
		 .expected_preview_height = 96U},
		{.target_size			  = {.max_width = 640U, .max_height = 420U},
		 .expected_longest_edge	  = 640U,
		 .expected_preview_width  = 320U,
		 .expected_preview_height = 240U},
	}};

	PreviewHarness harness;
	harness.source_decoder.set_decoded_pixels(make_pixels(320U, 240U));
	for (std::size_t index = 0U; index < cases.size(); ++index) {
		const PreviewCase& preview_case = cases[index];
		const std::filesystem::path staged_path =
			harness.paths.staged_content_root
			/ ("ji10-preview-" + std::to_string(index) + ".jpg");
		write_text(staged_path, "ji10-staged-source");
		shuba::ui::ImagePreviewCache cache;

		const shuba::ui::StagedPhotoPreviewLoadResult loaded =
			shuba::ui::load_staged_photo_preview(
				shuba::ui::StagedPhotoPreviewLoadRequest{
					.source = make_pending_source(
						staged_path, "ji10-preview.jpg", std::uint64_t{18}),
					.identifiers = harness.identifiers,
					.target_size = preview_case.target_size},
				cache, harness.source_decoder, harness.progress,
				harness.cancellation);

		REQUIRE(loaded.succeeded());
		REQUIRE(harness.source_decoder.last_requested_sizing().has_value());
		REQUIRE(
			harness.source_decoder.last_requested_sizing()->maximum_longest_edge
			== preview_case.expected_longest_edge);
		REQUIRE(loaded.metrics.has_value());
		REQUIRE(loaded.metrics->decoded_width == 320U);
		REQUIRE(loaded.metrics->decoded_height == 240U);
		REQUIRE(loaded.metrics->preview_width
				== preview_case.expected_preview_width);
		REQUIRE(loaded.metrics->preview_height
				== preview_case.expected_preview_height);
		REQUIRE(loaded.pixels.has_value());
		REQUIRE(loaded.pixels->width == preview_case.expected_preview_width);
		REQUIRE(loaded.pixels->height == preview_case.expected_preview_height);
	}
}

TEST_CASE(
	"B22 staged preview decode failures preserve pending import readiness",
	"[b22][image-preview][load]") {
	PreviewHarness harness;
	harness.identifiers.script_operation_identifier(
		"operation-b22-staged-preview-failure");
	const std::filesystem::path staged_path =
		harness.paths.staged_content_root / "pending-broken.jpg";
	write_text(staged_path, "broken-source-bytes");
	const shuba::ui::PendingPhotoSource pending =
		make_pending_source(staged_path, "Pending Broken.JPG", 19U);
	const bool ready_before = pending.ready_for_import();
	shuba::ui::ImagePreviewCache cache{shuba::ui::ImagePreviewCacheSettings{
		.maximum_entries = 4U, .maximum_pixel_bytes = 1024U}};

	shuba::ui::StagedPhotoPreviewLoadResult broken =
		shuba::ui::load_staged_photo_preview(
			shuba::ui::StagedPhotoPreviewLoadRequest{
				.source		 = pending,
				.identifiers = harness.identifiers,
				.target_size = shuba::ui::ImagePreviewSize{.max_width  = 64U,
														   .max_height = 64U}},
			cache, harness.source_decoder, harness.progress,
			harness.cancellation);

	REQUIRE(ready_before);
	REQUIRE(pending.ready_for_import());
	REQUIRE(pending.status == shuba::ui::PendingPhotoStatus::Staged);
	REQUIRE(pending.staged_source.has_value());
	REQUIRE(pending.staged_path == staged_path);
	REQUIRE(pending.diagnostics.empty());
	REQUIRE(broken.failed());
	REQUIRE(broken.status == shuba::ui::ImagePreviewLoadStatus::Broken);
	REQUIRE(broken.category
			== shuba::core::OperationResultCategory::CodecFailure);
	REQUIRE(broken.placeholder.has_value());
	REQUIRE(broken.placeholder->diagnostic_code
			== "synthetic-decode-not-scripted");
	REQUIRE_FALSE(broken.pixels.has_value());
	REQUIRE(cache.empty());
}
