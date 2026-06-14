#pragma once

#include "Catalog/CatalogRepository.hpp"
#include "Core/Identifier.hpp"
#include "Core/OperationGate.hpp"
#include "Core/Result.hpp"
#include "Platform/PlatformServices.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace shuba::catalog {
enum class PhotoDisplayStatus : std::uint8_t {
	Decoded,
	Broken,
	Cancelled,
};

[[nodiscard]] std::string_view to_string(PhotoDisplayStatus status) noexcept;

struct BrokenPhotoPlaceholder final {
	std::string title{"Photo unavailable"};
	std::string message;
	std::string diagnostic_code;

	friend bool operator==(const BrokenPhotoPlaceholder&,
						   const BrokenPhotoPlaceholder&) = default;
};

struct PhotoDisplayResult final {
	PhotoDisplayStatus status{PhotoDisplayStatus::Broken};
	core::OperationResultCategory category{
		core::OperationResultCategory::InternalError};
	std::vector<core::Diagnostic> diagnostics;
	std::optional<platform::ImagePixels> pixels;
	std::optional<BrokenPhotoPlaceholder> placeholder;
	std::optional<std::filesystem::path> media_path;

	[[nodiscard]] bool succeeded() const noexcept;
	[[nodiscard]] bool was_user_cancelled() const noexcept;
	[[nodiscard]] bool failed() const noexcept;
};

enum class PhotoExportStatus : std::uint8_t {
	Exported,
	Failed,
	Cancelled,
};

[[nodiscard]] std::string_view to_string(PhotoExportStatus status) noexcept;

struct PhotoExportResult final {
	PhotoExportStatus status{PhotoExportStatus::Failed};
	core::OperationResultCategory category{
		core::OperationResultCategory::InternalError};
	std::vector<core::Diagnostic> diagnostics;
	std::optional<std::filesystem::path> media_path;
	std::optional<std::filesystem::path> temp_jpeg_path;
	std::optional<platform::DocumentDestinationDescriptor> destination;
	std::uint64_t bytes_written{};
	bool temp_jpeg_written{};
	bool destination_copied{};
	bool temp_cleanup_attempted{};
	bool metadata_changed{};

	[[nodiscard]] bool succeeded() const noexcept;
	[[nodiscard]] bool was_user_cancelled() const noexcept;
	[[nodiscard]] bool failed() const noexcept;
};

struct PhotoDisplayRequest final {
	CatalogRepositoryState current_state;
	platform::AppPrivatePaths paths;
	core::StableIdentifier photo_id;
};

struct PhotoExportRequest final {
	CatalogRepositoryState current_state;
	platform::AppPrivatePaths paths;
	core::StableIdentifier photo_id;
	platform::DocumentDestinationDescriptor destination;
	std::uint8_t jpeg_quality{90};
};

[[nodiscard]] std::string suggested_jpeg_export_file_name(
	const CatalogRepositoryState& state,
	const core::StableIdentifier& photo_id);

class PhotoExportUseCase final {
public:
	PhotoExportUseCase(
		core::IdentifierSource& identifier_source,
		core::OperationGate& operation_gate,
		platform::InternalPhotoCodec& photo_codec,
		platform::JpegExportService& jpeg_export_service,
		platform::DocumentExportService& document_export_service);

	[[nodiscard]] PhotoDisplayResult load_photo_for_display(
		const PhotoDisplayRequest& request,
		platform::ProgressSink& progress_sink,
		platform::CancellationToken& cancellation_token);
	[[nodiscard]] PhotoExportResult export_photo_as_jpeg(
		const PhotoExportRequest& request,
		platform::ProgressSink& progress_sink,
		platform::CancellationToken& cancellation_token);

private:
	core::IdentifierSource& identifiers;
	core::OperationGate& gate;
	platform::InternalPhotoCodec& codec;
	platform::JpegExportService& jpeg_writer;
	platform::DocumentExportService& document_exporter;
};
}	 // namespace shuba::catalog
