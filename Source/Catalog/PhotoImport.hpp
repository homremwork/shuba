#pragma once

#include "Catalog/CatalogRepository.hpp"
#include "Core/Clock.hpp"
#include "Core/Identifier.hpp"
#include "Core/OperationGate.hpp"
#include "Core/Result.hpp"
#include "Persistence/CatalogStorage.hpp"
#include "Platform/PlatformServices.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace shuba::catalog {
enum class PhotoImportPhotoStatus : std::uint8_t {
	Imported,
	Failed,
	Cancelled,
};

[[nodiscard]] std::string_view to_string(
	PhotoImportPhotoStatus status) noexcept;

struct PhotoImportPhotoResult final {
	std::size_t source_index{};
	std::string source_display_name;
	PhotoImportPhotoStatus status{PhotoImportPhotoStatus::Failed};
	core::OperationResultCategory category{
		core::OperationResultCategory::InternalError};
	std::vector<core::Diagnostic> diagnostics;
	std::optional<core::StableIdentifier> photo_id;
	std::optional<std::filesystem::path> staged_source_path;
	std::optional<std::filesystem::path> media_path;
	bool media_written{};
	bool metadata_committed{};
	bool staged_source_cleanup_attempted{};
	bool media_cleanup_attempted{};
	bool orphan_media_left{};
};

struct PhotoImportSummary final {
	core::OperationResultCategory category{
		core::OperationResultCategory::Success};
	std::vector<core::Diagnostic> diagnostics;
	std::vector<PhotoImportPhotoResult> photos;
	CatalogRepositoryState updated_state;
	std::uint64_t success_count{};
	std::uint64_t failure_count{};
	std::uint64_t cancelled_count{};
	bool metadata_changed{};

	[[nodiscard]] bool succeeded() const noexcept;
	[[nodiscard]] bool was_user_cancelled() const noexcept;
	[[nodiscard]] bool failed() const noexcept;
	[[nodiscard]] bool has_partial_failures() const noexcept;
};

struct PhotoImportRequest final {
	CatalogRepositoryState current_state;
	platform::AppPrivatePaths paths;
	domain::PhotoOwner owner;
	std::vector<platform::ContentSourceDescriptor> sources;
	persistence::MetadataTextValidator photo_table_validator;
	bool create_previous_copy{true};
};

class PhotoImportUseCase final {
public:
	PhotoImportUseCase(
		core::IdentifierSource& identifier_source, const core::Clock& clock,
		core::OperationGate& operation_gate,
		platform::ContentStagingService& staging_service,
		platform::SourceByteFingerprintService& fingerprint_service,
		platform::SourceImageDecodeService& decode_service,
		platform::InternalPhotoCodec& photo_codec);

	[[nodiscard]] PhotoImportSummary import_photos(
		const PhotoImportRequest& request,
		platform::ProgressSink& progress_sink,
		platform::CancellationToken& cancellation_token);

private:
	core::IdentifierSource& identifiers;
	const core::Clock& import_clock;
	core::OperationGate& gate;
	platform::ContentStagingService& staging;
	platform::SourceByteFingerprintService& fingerprinting;
	platform::SourceImageDecodeService& decoder;
	platform::InternalPhotoCodec& codec;
};
}	 // namespace shuba::catalog
