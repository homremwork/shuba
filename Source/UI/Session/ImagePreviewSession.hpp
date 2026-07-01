#pragma once

#include "Catalog/PhotoExport.hpp"
#include "Core/Identifier.hpp"
#include "Platform/PlatformServices.hpp"
#include "UI/Session/PhotoSessionTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace shuba::ui {
enum class ImagePreviewRequestKind : std::uint8_t {
	InternalPhoto,
	StagedPhoto,
};

enum class ImagePreviewRequestPriority : std::uint8_t {
	Low,
	Normal,
	High,
};

struct ImagePreviewSize final {
	std::uint32_t max_width{};
	std::uint32_t max_height{};

	friend bool operator==(const ImagePreviewSize&,
						   const ImagePreviewSize&) = default;
};

struct ImagePreviewRequestIdentity final {
	ImagePreviewRequestKind kind{ImagePreviewRequestKind::InternalPhoto};
	std::string source_key;
	std::string source_fingerprint;
	ImagePreviewSize target_size;

	friend bool operator==(const ImagePreviewRequestIdentity&,
						   const ImagePreviewRequestIdentity&) = default;
};

struct ImagePreviewCacheSettings final {
	std::size_t maximum_entries{512U};
	std::uint64_t maximum_pixel_bytes{128U * 1024U * 1024U};
};

struct ImagePreviewCacheStats final {
	std::size_t entry_count{};
	std::uint64_t pixel_bytes{};
};

struct ImagePreviewCacheEntry final {
	ImagePreviewRequestIdentity identity;
	platform::ImagePixels pixels;
	std::uint64_t approximate_pixel_bytes{};
};

enum class ImagePreviewLoadStatus : std::uint8_t {
	Loaded,
	Broken,
	Cancelled,
};

[[nodiscard]] std::string_view to_string(
	ImagePreviewLoadStatus status) noexcept;

struct ImagePreviewLoadMetrics final {
	std::uint32_t decoded_width{};
	std::uint32_t decoded_height{};
	std::uint32_t preview_width{};
	std::uint32_t preview_height{};
	std::uint64_t decode_elapsed_milliseconds{};

	friend bool operator==(const ImagePreviewLoadMetrics&,
						   const ImagePreviewLoadMetrics&) = default;
};

struct InternalPhotoPreviewLoadRequest final {
	catalog::CatalogRepositoryState current_state;
	platform::AppPrivatePaths paths;
	core::StableIdentifier photo_id;
	ImagePreviewSize target_size;
};

struct InternalPhotoPreviewLoadResult final {
	ImagePreviewLoadStatus status{ImagePreviewLoadStatus::Broken};
	core::OperationResultCategory category{
		core::OperationResultCategory::InternalError};
	ImagePreviewRequestIdentity identity;
	std::vector<core::Diagnostic> diagnostics;
	std::optional<platform::ImagePixels> pixels;
	std::optional<catalog::BrokenPhotoPlaceholder> placeholder;
	std::optional<std::filesystem::path> media_path;
	std::optional<ImagePreviewLoadMetrics> metrics;
	bool cache_hit{};
	bool cache_stored{};

	[[nodiscard]] bool succeeded() const noexcept;
	[[nodiscard]] bool was_user_cancelled() const noexcept;
	[[nodiscard]] bool failed() const noexcept;
};

struct StagedPhotoPreviewLoadRequest final {
	PendingPhotoSource source;
	core::IdentifierSource& identifiers;
	ImagePreviewSize target_size;
};

struct StagedPhotoPreviewLoadResult final {
	ImagePreviewLoadStatus status{ImagePreviewLoadStatus::Broken};
	core::OperationResultCategory category{
		core::OperationResultCategory::InternalError};
	ImagePreviewRequestIdentity identity;
	std::vector<core::Diagnostic> diagnostics;
	std::optional<platform::ImagePixels> pixels;
	std::optional<catalog::BrokenPhotoPlaceholder> placeholder;
	std::optional<std::filesystem::path> staged_path;
	std::optional<ImagePreviewLoadMetrics> metrics;
	bool cache_hit{};
	bool cache_stored{};

	[[nodiscard]] bool succeeded() const noexcept;
	[[nodiscard]] bool was_user_cancelled() const noexcept;
	[[nodiscard]] bool failed() const noexcept;
};

[[nodiscard]] ImagePreviewRequestIdentity make_internal_photo_preview_identity(
	const core::StableIdentifier& photo_id, ImagePreviewSize target_size);
[[nodiscard]] ImagePreviewRequestIdentity make_staged_photo_preview_identity(
	const std::filesystem::path& staged_path, ImagePreviewSize target_size);
[[nodiscard]] ImagePreviewRequestIdentity make_staged_photo_preview_identity(
	const PendingPhotoSource& source, ImagePreviewSize target_size);
[[nodiscard]] bool valid_image_preview_size(ImagePreviewSize size) noexcept;
[[nodiscard]] std::optional<platform::ImagePixels>
scale_image_pixels_for_preview(const platform::ImagePixels& pixels,
							   ImagePreviewSize target_size);

class ImagePreviewCache final {
public:
	explicit ImagePreviewCache(ImagePreviewCacheSettings settings_value = {});

	[[nodiscard]] const ImagePreviewCacheSettings& settings() const noexcept;
	[[nodiscard]] ImagePreviewCacheStats stats() const noexcept;
	[[nodiscard]] bool empty() const noexcept;
	[[nodiscard]] bool contains(
		const ImagePreviewRequestIdentity& identity) const;
	[[nodiscard]] const platform::ImagePixels* find(
		const ImagePreviewRequestIdentity& identity);

	bool put(ImagePreviewRequestIdentity identity,
			 platform::ImagePixels pixels);
	void remove(const ImagePreviewRequestIdentity& identity);
	void remove_internal_photo(const core::StableIdentifier& photo_id);
	void remove_staged_photo(const std::filesystem::path& staged_path);
	void clear() noexcept;

private:
	[[nodiscard]] std::vector<ImagePreviewCacheEntry>::iterator find_entry(
		const ImagePreviewRequestIdentity& identity);
	[[nodiscard]] std::vector<ImagePreviewCacheEntry>::const_iterator
	find_entry(const ImagePreviewRequestIdentity& identity) const;
	void enforce_limits();

	ImagePreviewCacheSettings cache_settings;
	std::vector<ImagePreviewCacheEntry> entries;
	std::uint64_t approximate_pixel_bytes{};
};

[[nodiscard]] InternalPhotoPreviewLoadResult load_internal_photo_preview(
	const InternalPhotoPreviewLoadRequest& request, ImagePreviewCache& cache,
	catalog::PhotoExportUseCase& photo_export_use_case,
	platform::ProgressSink& progress_sink,
	platform::CancellationToken& cancellation_token);
[[nodiscard]] StagedPhotoPreviewLoadResult load_staged_photo_preview(
	const StagedPhotoPreviewLoadRequest& request, ImagePreviewCache& cache,
	platform::SourceImageDecodeService& decode_service,
	platform::ProgressSink& progress_sink,
	platform::CancellationToken& cancellation_token);
}	 // namespace shuba::ui
