#pragma once

#include "Platform/PlatformServices.hpp"

namespace shuba::platform {
class JuceZipArchiveService final : public ZipArchiveService {
public:
	[[nodiscard]] PlatformValueResult<ZipArchiveInspection> build_zip_archive(
		const ZipArchiveBuildRequest& request,
		const PlatformOperationContext& context, ProgressSink& progress_sink,
		CancellationToken& cancellation_token) override;
	[[nodiscard]] PlatformValueResult<ZipArchiveInspection> inspect_zip_archive(
		const std::filesystem::path& archive_path,
		const PlatformOperationContext& context, ProgressSink& progress_sink,
		CancellationToken& cancellation_token) override;
	[[nodiscard]] PlatformValueResult<ZipArchiveInspection>
	validate_zip_archive(const ZipArchiveValidationRequest& request,
						 const PlatformOperationContext& context,
						 ProgressSink& progress_sink,
						 CancellationToken& cancellation_token) override;
	[[nodiscard]] PlatformValueResult<ZipArchiveInspection> extract_zip_archive(
		const ZipArchiveExtractRequest& request,
		const PlatformOperationContext& context, ProgressSink& progress_sink,
		CancellationToken& cancellation_token) override;
};
}	 // namespace shuba::platform
