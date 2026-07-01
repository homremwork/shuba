#include "Catalog/CatalogRepository.hpp"
#include "Platform/LinuxFakes.hpp"
#include "UI/Session/ImagePreviewSession.hpp"
#include "UI/View/Primitives/PhotoManagement.hpp"
#include "UI/View/ScreenText.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
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
			.cancel_delete_current = [&cancel_delete_calls] {
		++cancel_delete_calls;
	}}};

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
