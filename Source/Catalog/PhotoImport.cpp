#include "Catalog/PhotoImport.hpp"

#include <algorithm>
#include <limits>
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

void append_diagnostic(PhotoImportSummary& summary,
					   PhotoImportPhotoResult& photo_result,
					   core::Diagnostic diagnostic) {
	photo_result.diagnostics.push_back(diagnostic);
	summary.diagnostics.push_back(std::move(diagnostic));
}

void append_summary_diagnostic(PhotoImportSummary& summary,
							   core::Diagnostic diagnostic) {
	summary.diagnostics.push_back(std::move(diagnostic));
}

void append_diagnostics(PhotoImportSummary& summary,
						PhotoImportPhotoResult& photo_result,
						const std::vector<core::Diagnostic>& diagnostics) {
	for (const core::Diagnostic& diagnostic : diagnostics)
		append_diagnostic(summary, photo_result, diagnostic);
}

[[nodiscard]] bool owner_exists(const CatalogRepositoryState& state,
								const domain::PhotoOwner& owner) {
	if (owner.type == domain::PhotoOwnerType::Item)
		return state.item_index_by_id.contains(owner.id.value());

	return state.storage_index_by_id.contains(owner.id.value());
}

[[nodiscard]] std::string display_name_for_source(
	const platform::ContentSourceDescriptor& source) {
	if (!source.display_name.empty())
		return source.display_name;
	if (!source.local_path.empty())
		return source.local_path.filename().string();
	if (!source.opaque_handle.empty())
		return source.opaque_handle;

	return "photo source";
}

[[nodiscard]] std::string infer_source_mime_type(
	std::string_view display_name) {
	const std::string extension =
		platform::file_extension_or_empty(display_name);
	if (extension == ".jpg" || extension == ".jpeg")
		return "image/jpeg";
	if (extension == ".png")
		return "image/png";
	if (extension == ".webp")
		return "image/webp";
	if (extension == ".heic")
		return "image/heic";
	if (extension == ".heif")
		return "image/heif";
	if (extension == ".gif")
		return "image/gif";
	if (extension == ".bmp")
		return "image/bmp";

	return {};
}

[[nodiscard]] std::optional<std::int32_t> image_dimension(
	std::uint32_t value) noexcept {
	if (value == 0U
		|| value > static_cast<std::uint32_t>(
			   std::numeric_limits<std::int32_t>::max())) {
		return std::nullopt;
	}

	return static_cast<std::int32_t>(value);
}

[[nodiscard]] std::filesystem::path photo_media_path(
	const std::filesystem::path& active_catalog_root,
	const core::StableIdentifier& photo_id) {
	return active_catalog_root
		   / std::filesystem::path{
			   expected_photo_media_relative_path(photo_id)};
}

[[nodiscard]] std::vector<domain::PhotoRecord> photo_records(
	const std::vector<persistence::PhotoEnvelope>& photos) {
	std::vector<domain::PhotoRecord> records;
	records.reserve(photos.size());
	for (const persistence::PhotoEnvelope& photo : photos)
		records.push_back(photo.record);
	return records;
}

[[nodiscard]] bool owner_has_no_photos(
	const std::vector<persistence::PhotoEnvelope>& photos,
	const domain::PhotoOwner& owner) {
	const std::vector<domain::PhotoRecord> records = photo_records(photos);
	return domain::ordered_photos_for_owner(records, owner).empty();
}

[[nodiscard]] std::int64_t next_sort_order(
	const std::vector<persistence::PhotoEnvelope>& photos,
	const domain::PhotoOwner& owner) {
	const std::vector<domain::PhotoRecord> records = photo_records(photos);
	return domain::next_photo_sort_order(records, owner);
}

[[nodiscard]] bool same_owner(const domain::PhotoRecord& photo,
							  const domain::PhotoOwner& owner) noexcept {
	return photo.owner_type == owner.type && photo.owner_id == owner.id;
}

[[nodiscard]] std::optional<core::Diagnostic> duplicate_source_warning(
	std::span<const persistence::PhotoEnvelope> photos,
	const domain::PhotoOwner& owner, std::string_view source_md5,
	std::string_view source_display_name) {
	if (source_md5.empty())
		return std::nullopt;

	for (const persistence::PhotoEnvelope& photo : photos) {
		if (same_owner(photo.record, owner)
			&& photo.record.source_md5 == source_md5) {
			return make_diagnostic(
				core::DiagnosticSeverity::RecoverableWarning,
				"photo_import_duplicate_source_warning",
				"Imported photo source appears to duplicate an existing owner "
				"photo. Import is allowed.",
				std::string{source_display_name} + " matches photo "
					+ photo.record.id.value());
		}
	}

	return std::nullopt;
}

[[nodiscard]] std::optional<core::Diagnostic> validate_photos_jsonl_text(
	std::string_view text) {
	const persistence::PhotoTableLoadResult loaded =
		persistence::load_photo_jsonl(text);
	if (loaded.quarantine_entries.empty())
		return std::nullopt;

	return make_diagnostic(
		core::DiagnosticSeverity::WriteBlockingError,
		"photos_jsonl_validation_failed",
		"Photo metadata JSONL did not validate before replacement.",
		"quarantineEntries="
			+ std::to_string(loaded.quarantine_entries.size()));
}

[[nodiscard]] core::Diagnostic jsonl_write_diagnostic(
	const persistence::JsonlDiagnostic& diagnostic) {
	return make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
						   diagnostic.code, diagnostic.message,
						   diagnostic.details);
}

[[nodiscard]] CatalogMediaSnapshot scan_photo_media(
	const std::filesystem::path& active_catalog_root) {
	CatalogMediaSnapshot snapshot;
	const std::filesystem::path media_directory =
		active_catalog_root
		/ std::filesystem::path{
			std::string{persistence::photo_media_directory_path}};

	std::error_code error;
	if (!std::filesystem::exists(media_directory, error)) {
		if (error)
			snapshot.complete_scan_available = false;
		return snapshot;
	}
	if (error) {
		snapshot.complete_scan_available = false;
		return snapshot;
	}

	std::filesystem::directory_iterator iterator{media_directory, error};
	if (error) {
		snapshot.complete_scan_available = false;
		return snapshot;
	}

	for (const std::filesystem::directory_entry& entry : iterator) {
		error.clear();
		if (!entry.is_regular_file(error) || error)
			continue;

		std::filesystem::path relative_path =
			std::filesystem::path{
				std::string{persistence::photo_media_directory_path}}
			/ entry.path().filename();
		snapshot.readable_photo_media_files.push_back(
			relative_path.generic_string());
	}

	std::ranges::sort(snapshot.readable_photo_media_files);
	return snapshot;
}

[[nodiscard]] CatalogRepositoryState rebuild_state(
	const CatalogRepositoryState& current_state,
	const std::vector<persistence::PhotoEnvelope>& photos,
	const std::filesystem::path& active_catalog_root) {
	return build_catalog_repository(
		CatalogRepositoryInput{.items	 = current_state.items,
							   .storages = current_state.storages,
							   .photos	 = photos,
							   .media = scan_photo_media(active_catalog_root)});
}

void cleanup_staged_source(PhotoImportSummary& summary,
						   PhotoImportPhotoResult& photo_result) {
	if (!photo_result.staged_source_path.has_value())
		return;

	photo_result.staged_source_cleanup_attempted = true;
	std::error_code error;
	std::filesystem::remove(*photo_result.staged_source_path, error);
	if (error) {
		append_diagnostic(
			summary, photo_result,
			make_diagnostic(
				core::DiagnosticSeverity::RecoverableWarning,
				"photo_import_staged_cleanup_failed",
				"Staged source file could not be removed after photo import.",
				photo_result.staged_source_path->string() + ": "
					+ error.message()));
	}
}

void cleanup_media_file(PhotoImportSummary& summary,
						PhotoImportPhotoResult& photo_result) {
	if (!photo_result.media_path.has_value())
		return;

	photo_result.media_cleanup_attempted = true;
	std::error_code error;
	std::filesystem::remove(*photo_result.media_path, error);
	if (error) {
		photo_result.orphan_media_left = true;
		append_diagnostic(
			summary, photo_result,
			make_diagnostic(
				core::DiagnosticSeverity::RecoverableWarning,
				"photo_import_orphan_media_left",
				"Newly written photo media could not be removed after metadata "
				"failure; recovery diagnostics may report it as orphan media.",
				photo_result.media_path->string() + ": " + error.message()));
	}
}

template<class Value>
void append_platform_failure(PhotoImportSummary& summary,
							 PhotoImportPhotoResult& photo_result,
							 const platform::PlatformValueResult<Value>& result,
							 std::string fallback_code,
							 std::string fallback_message) {
	photo_result.category = result.category;
	append_diagnostics(summary, photo_result, result.diagnostics);
	if (photo_result.diagnostics.empty()
		&& result.category != core::OperationResultCategory::UserCancelled) {
		append_diagnostic(
			summary, photo_result,
			make_diagnostic(core::DiagnosticSeverity::WriteBlockingError,
							std::move(fallback_code),
							std::move(fallback_message)));
	}
}

void mark_cancelled(PhotoImportPhotoResult& photo_result) noexcept {
	photo_result.status	  = PhotoImportPhotoStatus::Cancelled;
	photo_result.category = core::OperationResultCategory::UserCancelled;
}

void mark_failed(PhotoImportPhotoResult& photo_result,
				 core::OperationResultCategory category) noexcept {
	photo_result.status	  = PhotoImportPhotoStatus::Failed;
	photo_result.category = category;
}

void mark_imported(PhotoImportPhotoResult& photo_result) noexcept {
	photo_result.status				= PhotoImportPhotoStatus::Imported;
	photo_result.category			= core::OperationResultCategory::Success;
	photo_result.metadata_committed = true;
}

void finalize_summary(PhotoImportSummary& summary) {
	for (const PhotoImportPhotoResult& photo : summary.photos)
		if (photo.status == PhotoImportPhotoStatus::Imported)
			++summary.success_count;
		else if (photo.status == PhotoImportPhotoStatus::Cancelled)
			++summary.cancelled_count;
		else
			++summary.failure_count;

	if (summary.success_count > 0U) {
		summary.category = core::OperationResultCategory::Success;
		return;
	}

	if (summary.cancelled_count > 0U) {
		summary.category = core::OperationResultCategory::UserCancelled;
		return;
	}

	for (const PhotoImportPhotoResult& photo : summary.photos) {
		if (photo.category != core::OperationResultCategory::Success) {
			summary.category = photo.category;
			return;
		}
	}

	summary.category = core::OperationResultCategory::Success;
}

[[nodiscard]] persistence::PhotoEnvelope build_photo_envelope(
	core::StableIdentifier photo_id, const domain::PhotoOwner& owner,
	std::int64_t sort_order, bool is_main, const platform::ImagePixels& pixels,
	const platform::MediaWriteResult& media_write, std::string source_mime_type,
	std::string source_md5, core::EpochMilliseconds timestamp) {
	std::optional<std::uint64_t> encoded_bytes;
	if (media_write.bytes_written > 0U)
		encoded_bytes = media_write.bytes_written;

	return persistence::PhotoEnvelope{
		.record = domain::PhotoRecord{
			.id			  = std::move(photo_id),
			.owner_type	  = owner.type,
			.owner_id	  = owner.id,
			.media_format = domain::PhotoMediaFormat::JpegXl,
			.sort_order	  = sort_order,
			.is_main	  = is_main,
			.width		  = image_dimension(
				media_write.width == 0U ? pixels.width : media_write.width),
			.height = image_dimension(
				media_write.height == 0U ? pixels.height : media_write.height),
			.encoded_bytes	  = encoded_bytes,
			.source_mime_type = std::move(source_mime_type),
			.source_md5		  = std::move(source_md5),
			.timestamps = domain::RecordTimestamps{.created_at = timestamp,
												   .updated_at = timestamp}}};
}
}	 // namespace

std::string_view to_string(PhotoImportPhotoStatus status) noexcept {
	switch (status) {
		case PhotoImportPhotoStatus::Imported:
			return "imported";
		case PhotoImportPhotoStatus::Failed:
			return "failed";
		case PhotoImportPhotoStatus::Cancelled:
			return "cancelled";
	}

	return "unknown photo import status";
}

bool PhotoImportSummary::succeeded() const noexcept {
	return category == core::OperationResultCategory::Success;
}

bool PhotoImportSummary::was_user_cancelled() const noexcept {
	return category == core::OperationResultCategory::UserCancelled;
}

bool PhotoImportSummary::failed() const noexcept {
	return !succeeded() && !was_user_cancelled();
}

bool PhotoImportSummary::has_partial_failures() const noexcept {
	return success_count > 0U && (failure_count > 0U || cancelled_count > 0U);
}

PhotoImportUseCase::PhotoImportUseCase(
	core::IdentifierSource& identifier_source, const core::Clock& clock,
	core::OperationGate& operation_gate,
	platform::ContentStagingService& staging_service,
	platform::SourceByteFingerprintService& fingerprint_service,
	platform::SourceImageDecodeService& decode_service,
	platform::InternalPhotoCodec& photo_codec)
	: identifiers(identifier_source)
	, import_clock(clock)
	, gate(operation_gate)
	, staging(staging_service)
	, fingerprinting(fingerprint_service)
	, decoder(decode_service)
	, codec(photo_codec) {}

PhotoImportSummary PhotoImportUseCase::import_photos(
	const PhotoImportRequest& request, platform::ProgressSink& progress_sink,
	platform::CancellationToken& cancellation_token) {
	PhotoImportSummary summary{.updated_state = request.current_state};
	const core::OperationIdentifier operation_id =
		identifiers.next_operation_identifier();
	platform::PlatformOperationStartResult operation_start =
		platform::try_start_platform_operation(
			gate,
			platform::PlatformOperationStartRequest{
				.operation_kind = core::OperationKind::PhotoImport,
				.operation_id	= operation_id,
				.operation_type = platform::ProgressOperationType::PhotoImport},
			progress_sink, cancellation_token);
	if (!operation_start.succeeded()) {
		summary.category	= operation_start.category;
		summary.diagnostics = std::move(operation_start.diagnostics);
		return summary;
	}

	platform::ScopedPlatformOperation& operation = *operation_start.operation;
	if (!owner_exists(request.current_state, request.owner)) {
		summary.category = core::OperationResultCategory::ValidationFailure;
		append_summary_diagnostic(
			summary,
			make_diagnostic(core::DiagnosticSeverity::ActionValidationError,
							"photo_import_missing_owner",
							"Photo import owner does not exist in the accepted "
							"catalog state.",
							request.owner.id.value()));
		return summary;
	}

	const std::uint64_t total_sources =
		static_cast<std::uint64_t>(request.sources.size());
	operation.publish_progress(
		"photo-import-started", platform::ProgressMessageId::PhotoImportStarted,
		std::uint64_t{0}, total_sources, "Photo import started.", true);

	std::vector<persistence::PhotoEnvelope> committed_photos =
		request.current_state.photos;
	const persistence::MetadataTextValidator validator =
		request.photo_table_validator
			? request.photo_table_validator
			: persistence::MetadataTextValidator{validate_photos_jsonl_text};

	for (std::size_t index = 0; index < request.sources.size(); ++index) {
		const platform::ContentSourceDescriptor& source =
			request.sources[index];
		PhotoImportPhotoResult photo_result{
			.source_index		 = index,
			.source_display_name = display_name_for_source(source)};

		if (operation.cancellation_requested()) {
			mark_cancelled(photo_result);
			summary.photos.push_back(std::move(photo_result));
			break;
		}

		operation.publish_progress(
			"photo-import-photo-started",
			platform::ProgressMessageId::PhotoImportSourceStarted,
			static_cast<std::uint64_t>(index + 1U), total_sources,
			"Photo import source started.", true);

		core::StableIdentifier photo_id = identifiers.next_stable_identifier();
		photo_result.photo_id			= photo_id;
		const std::string staged_name = platform::make_staged_content_file_name(
			"photo-source", operation.context().operation_id, index + 1U,
			photo_result.source_display_name);
		platform::PlatformValueResult<platform::StagedContent> staged =
			staging.stage_content(
				platform::ContentStagingRequest{
					.source			  = source,
					.target_directory = request.paths.staged_content_root,
					.target_file_name = staged_name,
					.allow_no_copy_optimization = false},
				operation.context(), progress_sink, cancellation_token);
		if (!staged.succeeded()) {
			if (staged.was_user_cancelled()) {
				mark_cancelled(photo_result);
			} else {
				mark_failed(photo_result, staged.category);
				append_platform_failure(summary, photo_result, staged,
										"photo_import_staging_failed",
										"Photo source could not be staged.");
			}
			summary.photos.push_back(std::move(photo_result));
			if (staged.was_user_cancelled())
				break;
			continue;
		}

		if (!staged.value->display_name.empty())
			photo_result.source_display_name = staged.value->display_name;
		photo_result.staged_source_path = staged.value->staged_path;
		std::string source_md5;
		platform::PlatformValueResult<platform::SourceByteFingerprint>
			fingerprint = fingerprinting.fingerprint_source_bytes(
				platform::SourceByteFingerprintRequest{
					.source_path = staged.value->staged_path},
				operation.context(), progress_sink, cancellation_token);
		if (fingerprint.was_user_cancelled()) {
			cleanup_staged_source(summary, photo_result);
			mark_cancelled(photo_result);
			summary.photos.push_back(std::move(photo_result));
			break;
		}
		if (fingerprint.succeeded()) {
			source_md5 = std::move(fingerprint.value->source_md5);
			if (std::optional<core::Diagnostic> warning =
					duplicate_source_warning(
						committed_photos, request.owner, source_md5,
						photo_result.source_display_name)) {
				append_diagnostic(summary, photo_result, std::move(*warning));
			}
		} else {
			append_diagnostics(summary, photo_result, fingerprint.diagnostics);
			if (fingerprint.diagnostics.empty()) {
				append_diagnostic(
					summary, photo_result,
					make_diagnostic(
						core::DiagnosticSeverity::RecoverableWarning,
						"photo_import_source_fingerprint_failed",
						"Source-byte duplicate fingerprint could not be "
						"computed; import will continue without a duplicate "
						"warning fingerprint."));
			}
		}
		platform::PlatformValueResult<platform::ImagePixels> decoded =
			decoder.decode_source_image(
				platform::SourceImageDecodeRequest{
					.content = *staged.value,
					.sizing	 = platform::
						default_durable_photo_source_image_decode_sizing()},
				operation.context(), progress_sink, cancellation_token);
		if (!decoded.succeeded()) {
			cleanup_staged_source(summary, photo_result);
			if (decoded.was_user_cancelled()) {
				mark_cancelled(photo_result);
			} else {
				mark_failed(photo_result, decoded.category);
				append_platform_failure(summary, photo_result, decoded,
										"photo_import_decode_failed",
										"Photo source could not be decoded.");
			}
			summary.photos.push_back(std::move(photo_result));
			if (decoded.was_user_cancelled())
				break;
			continue;
		}

		const platform::ImagePixelsValidation pixel_validation =
			platform::validate_image_pixels(*decoded.value);
		if (!pixel_validation.valid()) {
			cleanup_staged_source(summary, photo_result);
			mark_failed(photo_result,
						core::OperationResultCategory::CodecFailure);
			append_diagnostic(
				summary, photo_result,
				make_diagnostic(
					core::DiagnosticSeverity::WriteBlockingError,
					"photo_import_invalid_pixels",
					"Decoded source image pixels are not valid for internal "
					"photo encoding.",
					std::string{platform::to_string(pixel_validation.issue)}));
			summary.photos.push_back(std::move(photo_result));
			continue;
		}

		const std::filesystem::path media_path =
			photo_media_path(request.paths.active_catalog_root, photo_id);
		photo_result.media_path = media_path;
		platform::PlatformValueResult<platform::MediaWriteResult> encoded =
			codec.encode_internal_photo(
				platform::InternalPhotoEncodeRequest{
					.pixels		 = *decoded.value,
					.output_path = media_path,
					.settings =
						platform::default_internal_photo_encode_settings()},
				operation.context(), progress_sink, cancellation_token);
		if (!encoded.succeeded()) {
			cleanup_staged_source(summary, photo_result);
			if (encoded.was_user_cancelled()) {
				mark_cancelled(photo_result);
			} else {
				mark_failed(photo_result, encoded.category);
				append_platform_failure(
					summary, photo_result, encoded,
					"photo_import_encode_failed",
					"Photo could not be written as internal media.");
			}
			summary.photos.push_back(std::move(photo_result));
			if (encoded.was_user_cancelled())
				break;
			continue;
		}

		photo_result.media_written = true;
		if (operation.cancellation_requested()) {
			cleanup_media_file(summary, photo_result);
			cleanup_staged_source(summary, photo_result);
			mark_cancelled(photo_result);
			summary.photos.push_back(std::move(photo_result));
			break;
		}

		std::vector<persistence::PhotoEnvelope> candidate_photos =
			committed_photos;
		candidate_photos.push_back(build_photo_envelope(
			photo_id, request.owner,
			next_sort_order(committed_photos, request.owner),
			owner_has_no_photos(committed_photos, request.owner),
			*decoded.value, *encoded.value,
			infer_source_mime_type(photo_result.source_display_name),
			std::move(source_md5), import_clock.now()));
		persistence::JsonTextWriteResult photos_jsonl =
			persistence::write_photo_jsonl(candidate_photos);
		if (!photos_jsonl.succeeded()) {
			cleanup_media_file(summary, photo_result);
			cleanup_staged_source(summary, photo_result);
			mark_failed(photo_result,
						core::OperationResultCategory::InternalError);
			for (const persistence::JsonlDiagnostic& diagnostic :
				 photos_jsonl.diagnostics) {
				append_diagnostic(summary, photo_result,
								  jsonl_write_diagnostic(diagnostic));
			}
			summary.photos.push_back(std::move(photo_result));
			continue;
		}

		operation.publish_progress(
			"photo-import-committing",
			platform::ProgressMessageId::PhotoImportCommitting,
			static_cast<std::uint64_t>(index + 1U), total_sources,
			"Photo metadata commit started.", false);
		persistence::CatalogStorageResult committed =
			persistence::commit_metadata_file(
				persistence::CatalogMetadataCommitRequest{
					.active_catalog_root  = request.paths.active_catalog_root,
					.relative_target_path = std::filesystem::path{std::string{
						persistence::photos_data_file_path}},
					.serialized_content	  = std::move(photos_jsonl.text),
					.committed_at		  = import_clock.now(),
					.operation_id		  = operation.context().operation_id,
					.validator			  = validator,
					.create_previous_copy = request.create_previous_copy});
		append_diagnostics(summary, photo_result, committed.diagnostics);
		if (committed.failed()) {
			cleanup_media_file(summary, photo_result);
			cleanup_staged_source(summary, photo_result);
			mark_failed(photo_result, committed.category);
			summary.photos.push_back(std::move(photo_result));
			continue;
		}

		committed_photos		 = std::move(candidate_photos);
		summary.metadata_changed = true;
		cleanup_staged_source(summary, photo_result);
		mark_imported(photo_result);
		summary.photos.push_back(std::move(photo_result));
	}

	operation.publish_progress(
		"photo-import-completed",
		platform::ProgressMessageId::PhotoImportCompleted, total_sources,
		total_sources, "Photo import completed.", false);
	summary.updated_state =
		rebuild_state(request.current_state, committed_photos,
					  request.paths.active_catalog_root);
	finalize_summary(summary);
	return summary;
}
}	 // namespace shuba::catalog
