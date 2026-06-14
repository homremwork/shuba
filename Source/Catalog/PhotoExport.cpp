#include "Catalog/PhotoExport.hpp"

#include <filesystem>
#include <map>
#include <system_error>
#include <utility>

namespace shuba::catalog {
namespace {
[[nodiscard]] core::Diagnostic make_diagnostic(
	core::DiagnosticSeverity severity, std::string code, std::string message,
	std::string technical_details = {}) {
	return core::Diagnostic{.severity		   = severity,
							.code			   = std::move(code),
							.message		   = std::move(message),
							.technical_details = std::move(technical_details)};
}

void append_diagnostic(std::vector<core::Diagnostic>& diagnostics,
					   core::Diagnostic diagnostic) {
	diagnostics.push_back(std::move(diagnostic));
}

void append_diagnostics(std::vector<core::Diagnostic>& target,
						const std::vector<core::Diagnostic>& source) {
	for (const core::Diagnostic& diagnostic : source)
		target.push_back(diagnostic);
}

[[nodiscard]] std::filesystem::path photo_media_path(
	const std::filesystem::path& active_catalog_root,
	const core::StableIdentifier& photo_id) {
	return active_catalog_root
		   / std::filesystem::path{
			   expected_photo_media_relative_path(photo_id)};
}

[[nodiscard]] std::filesystem::path temp_jpeg_path(
	const platform::AppPrivatePaths& paths,
	const core::OperationIdentifier& operation_id,
	const core::StableIdentifier& photo_id) {
	const std::string file_name = platform::sanitize_platform_file_name(
		"photo-export-" + operation_id.value() + "-" + photo_id.value()
			+ ".jpg",
		"photo-export.jpg");
	return paths.export_tmp_root / file_name;
}

[[nodiscard]] BrokenPhotoPlaceholder make_placeholder(
	std::string message, std::string diagnostic_code = {}) {
	return BrokenPhotoPlaceholder{
		.title			 = "Photo unavailable",
		.message		 = std::move(message),
		.diagnostic_code = std::move(diagnostic_code)};
}

[[nodiscard]] bool media_file_readable(const std::filesystem::path& path) {
	std::error_code error;
	return std::filesystem::is_regular_file(path, error) && !error;
}

[[nodiscard]] std::uint64_t file_size_or_zero(
	const std::filesystem::path& path) {
	std::error_code error;
	const std::uintmax_t size = std::filesystem::file_size(path, error);
	if (error)
		return 0U;

	return static_cast<std::uint64_t>(size);
}

void cleanup_temp_jpeg(PhotoExportResult& result) {
	if (!result.temp_jpeg_path.has_value())
		return;

	result.temp_cleanup_attempted = true;
	std::error_code ignored;
	std::filesystem::remove(*result.temp_jpeg_path, ignored);
}

[[nodiscard]] std::string owner_display_name_for_photo(
	const CatalogRepositoryState& state, const domain::PhotoRecord& photo) {
	if (photo.owner_type == domain::PhotoOwnerType::Item) {
		const persistence::ItemEnvelope* item =
			find_item_envelope(state, photo.owner_id);
		if (item != nullptr
			&& domain::has_non_whitespace(item->record.display_name))
			return item->record.display_name;
	} else {
		const persistence::StorageEnvelope* storage =
			find_storage_envelope(state, photo.owner_id);
		if (storage != nullptr
			&& domain::has_non_whitespace(storage->record.display_name)) {
			return storage->record.display_name;
		}
	}

	return "photo";
}

[[nodiscard]] std::size_t owner_photo_position(
	const CatalogRepositoryState& state, const domain::PhotoRecord& photo) {
	const std::map<std::string, OwnerPhotoProjection>& projections =
		photo.owner_type == domain::PhotoOwnerType::Item
			? state.item_photo_projections
			: state.storage_photo_projections;
	const std::map<std::string, OwnerPhotoProjection>::const_iterator found =
		projections.find(photo.owner_id.value());
	if (found == projections.end())
		return 1U;

	for (std::size_t index = 0; index < found->second.ordered_photo_ids.size();
		 ++index) {
		if (found->second.ordered_photo_ids[index] == photo.id)
			return index + 1U;
	}

	return 1U;
}

void mark_export_failed(PhotoExportResult& result,
						core::OperationResultCategory category) noexcept {
	result.status	= PhotoExportStatus::Failed;
	result.category = category;
}

void mark_export_cancelled(PhotoExportResult& result) noexcept {
	result.status	= PhotoExportStatus::Cancelled;
	result.category = core::OperationResultCategory::UserCancelled;
}

[[nodiscard]] PhotoDisplayResult display_failure(
	core::OperationResultCategory category, core::Diagnostic diagnostic,
	BrokenPhotoPlaceholder placeholder,
	std::optional<std::filesystem::path> media_path = std::nullopt) {
	return PhotoDisplayResult{.status	   = PhotoDisplayStatus::Broken,
							  .category	   = category,
							  .diagnostics = {std::move(diagnostic)},
							  .placeholder = std::move(placeholder),
							  .media_path  = std::move(media_path)};
}
}	 // namespace

std::string_view to_string(PhotoDisplayStatus status) noexcept {
	switch (status) {
		case PhotoDisplayStatus::Decoded:
			return "decoded";
		case PhotoDisplayStatus::Broken:
			return "broken";
		case PhotoDisplayStatus::Cancelled:
			return "cancelled";
	}

	return "unknown photo display status";
}

bool PhotoDisplayResult::succeeded() const noexcept {
	return status == PhotoDisplayStatus::Decoded
		   && category == core::OperationResultCategory::Success
		   && pixels.has_value();
}

bool PhotoDisplayResult::was_user_cancelled() const noexcept {
	return status == PhotoDisplayStatus::Cancelled
		   || category == core::OperationResultCategory::UserCancelled;
}

bool PhotoDisplayResult::failed() const noexcept {
	return !succeeded() && !was_user_cancelled();
}

std::string_view to_string(PhotoExportStatus status) noexcept {
	switch (status) {
		case PhotoExportStatus::Exported:
			return "exported";
		case PhotoExportStatus::Failed:
			return "failed";
		case PhotoExportStatus::Cancelled:
			return "cancelled";
	}

	return "unknown photo export status";
}

bool PhotoExportResult::succeeded() const noexcept {
	return status == PhotoExportStatus::Exported
		   && category == core::OperationResultCategory::Success;
}

bool PhotoExportResult::was_user_cancelled() const noexcept {
	return status == PhotoExportStatus::Cancelled
		   || category == core::OperationResultCategory::UserCancelled;
}

bool PhotoExportResult::failed() const noexcept {
	return !succeeded() && !was_user_cancelled();
}

std::string suggested_jpeg_export_file_name(
	const CatalogRepositoryState& state,
	const core::StableIdentifier& photo_id) {
	const persistence::PhotoEnvelope* photo =
		find_photo_envelope(state, photo_id);
	if (photo == nullptr)
		return "photo.jpg";

	const std::string owner_name =
		owner_display_name_for_photo(state, photo->record);
	const std::size_t position = owner_photo_position(state, photo->record);
	return platform::sanitize_platform_file_name(
		owner_name + "-photo-" + std::to_string(position) + ".jpg",
		"photo.jpg");
}

PhotoExportUseCase::PhotoExportUseCase(
	core::IdentifierSource& identifier_source,
	core::OperationGate& operation_gate,
	platform::InternalPhotoCodec& photo_codec,
	platform::JpegExportService& jpeg_export_service,
	platform::DocumentExportService& document_export_service)
	: identifiers(identifier_source)
	, gate(operation_gate)
	, codec(photo_codec)
	, jpeg_writer(jpeg_export_service)
	, document_exporter(document_export_service) {}

PhotoDisplayResult PhotoExportUseCase::load_photo_for_display(
	const PhotoDisplayRequest& request, platform::ProgressSink& progress_sink,
	platform::CancellationToken& cancellation_token) {
	const persistence::PhotoEnvelope* photo =
		find_photo_envelope(request.current_state, request.photo_id);
	if (photo == nullptr) {
		return display_failure(
			core::OperationResultCategory::ValidationFailure,
			make_diagnostic(
				core::DiagnosticSeverity::ActionValidationError,
				"photo_display_missing_record",
				"Photo record does not exist in the accepted catalog state.",
				request.photo_id.value()),
			make_placeholder("The selected photo record is missing.",
							 "photo_display_missing_record"));
	}

	if (photo->record.media_format != domain::PhotoMediaFormat::JpegXl) {
		return display_failure(
			core::OperationResultCategory::Unsupported,
			make_diagnostic(core::DiagnosticSeverity::ActionValidationError,
							"photo_display_unsupported_media_format",
							"Photo media format is not supported for display."),
			make_placeholder("This photo format cannot be displayed.",
							 "photo_display_unsupported_media_format"));
	}

	const std::filesystem::path media_path =
		photo_media_path(request.paths.active_catalog_root, request.photo_id);
	if (!media_file_readable(media_path)) {
		return display_failure(
			core::OperationResultCategory::SourceUnavailable,
			make_diagnostic(
				core::DiagnosticSeverity::RecoverableWarning,
				"photo_display_media_missing",
				"Internal photo media file is missing or unreadable.",
				media_path.string()),
			make_placeholder("The internal photo media file is missing.",
							 "photo_display_media_missing"),
			media_path);
	}

	platform::PlatformValueResult<platform::ImagePixels> decoded =
		codec.decode_internal_photo(
			platform::InternalPhotoDecodeRequest{.input_path = media_path},
			platform::PlatformOperationContext{
				.operation_id	= identifiers.next_operation_identifier(),
				.operation_type = platform::ProgressOperationType::JpegExport},
			progress_sink, cancellation_token);
	if (decoded.was_user_cancelled()) {
		return PhotoDisplayResult{
			.status		= PhotoDisplayStatus::Cancelled,
			.category	= core::OperationResultCategory::UserCancelled,
			.media_path = media_path};
	}
	if (decoded.failed()) {
		PhotoDisplayResult result{
			.status		 = PhotoDisplayStatus::Broken,
			.category	 = decoded.category,
			.diagnostics = decoded.diagnostics,
			.placeholder = make_placeholder(
				"The internal photo media could not be decoded.",
				decoded.diagnostics.empty() ? "photo_display_decode_failed"
											: decoded.diagnostics.front().code),
			.media_path = media_path};
		return result;
	}

	return PhotoDisplayResult{
		.status		 = PhotoDisplayStatus::Decoded,
		.category	 = core::OperationResultCategory::Success,
		.diagnostics = decoded.diagnostics,
		.pixels		 = std::move(decoded.value),
		.media_path	 = media_path};
}

PhotoExportResult PhotoExportUseCase::export_photo_as_jpeg(
	const PhotoExportRequest& request, platform::ProgressSink& progress_sink,
	platform::CancellationToken& cancellation_token) {
	PhotoExportResult result{.destination	   = request.destination,
							 .metadata_changed = false};
	const core::OperationIdentifier operation_id =
		identifiers.next_operation_identifier();
	platform::PlatformOperationStartResult operation_start =
		platform::try_start_platform_operation(
			gate,
			platform::PlatformOperationStartRequest{
				.operation_kind = core::OperationKind::JpegExport,
				.operation_id	= operation_id,
				.operation_type = platform::ProgressOperationType::JpegExport},
			progress_sink, cancellation_token);
	if (!operation_start.succeeded()) {
		result.category	   = operation_start.category;
		result.diagnostics = std::move(operation_start.diagnostics);
		return result;
	}

	platform::ScopedPlatformOperation& operation = *operation_start.operation;
	operation.publish_progress("jpeg-export-started", std::uint64_t{0},
							   std::uint64_t{4}, "JPEG export started.", true);

	const persistence::PhotoEnvelope* photo =
		find_photo_envelope(request.current_state, request.photo_id);
	if (photo == nullptr) {
		mark_export_failed(result,
						   core::OperationResultCategory::ValidationFailure);
		append_diagnostic(
			result.diagnostics,
			make_diagnostic(
				core::DiagnosticSeverity::ActionValidationError,
				"photo_export_missing_record",
				"Photo record does not exist in the accepted catalog state.",
				request.photo_id.value()));
		return result;
	}

	if (photo->record.media_format != domain::PhotoMediaFormat::JpegXl) {
		mark_export_failed(result, core::OperationResultCategory::Unsupported);
		append_diagnostic(
			result.diagnostics,
			make_diagnostic(
				core::DiagnosticSeverity::ActionValidationError,
				"photo_export_unsupported_media_format",
				"Only internal JPEG XL photos can be exported as JPEG."));
		return result;
	}

	if (!platform::validate_jpeg_export_quality(request.jpeg_quality)) {
		mark_export_failed(result,
						   core::OperationResultCategory::ValidationFailure);
		append_diagnostic(
			result.diagnostics,
			make_diagnostic(
				core::DiagnosticSeverity::ActionValidationError,
				"invalid_jpeg_export_quality",
				"JPEG export quality must be in the range 1..100."));
		return result;
	}

	const std::filesystem::path media_path =
		photo_media_path(request.paths.active_catalog_root, request.photo_id);
	result.media_path = media_path;
	if (!media_file_readable(media_path)) {
		mark_export_failed(result,
						   core::OperationResultCategory::SourceUnavailable);
		append_diagnostic(
			result.diagnostics,
			make_diagnostic(
				core::DiagnosticSeverity::RecoverableWarning,
				"photo_export_media_missing",
				"Internal photo media file is missing or unreadable.",
				media_path.string()));
		return result;
	}

	operation.publish_progress("jpeg-export-decoding", std::uint64_t{1},
							   std::uint64_t{4},
							   "Decoding internal JPEG XL photo.", true);
	platform::PlatformValueResult<platform::ImagePixels> decoded =
		codec.decode_internal_photo(
			platform::InternalPhotoDecodeRequest{.input_path = media_path},
			operation.context(), progress_sink, cancellation_token);
	if (!decoded.succeeded()) {
		if (decoded.was_user_cancelled())
			mark_export_cancelled(result);
		else {
			mark_export_failed(result, decoded.category);
			append_diagnostics(result.diagnostics, decoded.diagnostics);
		}
		return result;
	}

	const std::filesystem::path jpeg_path = temp_jpeg_path(
		request.paths, operation.context().operation_id, request.photo_id);
	result.temp_jpeg_path = jpeg_path;
	operation.publish_progress("jpeg-export-writing-temp", std::uint64_t{2},
							   std::uint64_t{4},
							   "Writing temporary JPEG export.", true);
	platform::PlatformValueResult<platform::MediaWriteResult> written =
		jpeg_writer.write_jpeg(
			platform::JpegExportRequest{.pixels		 = *decoded.value,
										.output_path = jpeg_path,
										.quality	 = request.jpeg_quality},
			operation.context(), progress_sink, cancellation_token);
	if (!written.succeeded()) {
		cleanup_temp_jpeg(result);
		if (written.was_user_cancelled())
			mark_export_cancelled(result);
		else {
			mark_export_failed(result, written.category);
			append_diagnostics(result.diagnostics, written.diagnostics);
		}
		return result;
	}

	result.temp_jpeg_written = true;
	result.bytes_written	 = written.value->bytes_written == 0U
								   ? file_size_or_zero(jpeg_path)
								   : written.value->bytes_written;
	if (result.bytes_written == 0U) {
		cleanup_temp_jpeg(result);
		mark_export_failed(
			result, core::OperationResultCategory::TemporaryStorageFailure);
		append_diagnostic(
			result.diagnostics,
			make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
							"jpeg_export_empty_temp_file",
							"Temporary JPEG export file is empty.",
							jpeg_path.string()));
		return result;
	}

	operation.publish_progress("jpeg-export-copying", std::uint64_t{3},
							   std::uint64_t{4},
							   "Copying JPEG to selected destination.", true);
	core::OperationResult copied = document_exporter.copy_file_to_destination(
		platform::DocumentCopyRequest{.temp_source_path = jpeg_path,
									  .destination		= request.destination},
		operation.context(), progress_sink, cancellation_token);
	cleanup_temp_jpeg(result);
	if (!copied.succeeded()) {
		if (copied.was_user_cancelled())
			mark_export_cancelled(result);
		else {
			mark_export_failed(result, copied.category());
			append_diagnostics(result.diagnostics, copied.diagnostics());
		}
		return result;
	}

	result.status			  = PhotoExportStatus::Exported;
	result.category			  = core::OperationResultCategory::Success;
	result.destination_copied = true;
	operation.publish_progress("jpeg-export-completed", std::uint64_t{4},
							   std::uint64_t{4}, "JPEG export completed.",
							   false);
	return result;
}
}	 // namespace shuba::catalog
