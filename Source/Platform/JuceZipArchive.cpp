#include "Platform/JuceZipArchive.hpp"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <system_error>
#include <utility>

namespace shuba::platform {
namespace {
constexpr std::uint64_t classic_zip_limit  = 0xffff'ffffULL;
constexpr std::size_t zip_read_buffer_size = 32768U;

[[nodiscard]] core::Diagnostic make_diagnostic(
	core::DiagnosticSeverity severity, std::string code, std::string message,
	std::string technical_details = {}) {
	return core::Diagnostic{.severity		   = severity,
							.code			   = std::move(code),
							.message		   = std::move(message),
							.technical_details = std::move(technical_details)};
}

template<class Value>
[[nodiscard]] PlatformValueResult<Value> zip_failure(
	core::OperationResultCategory category, core::DiagnosticSeverity severity,
	std::string code, std::string message, std::string technical_details = {}) {
	return platform_value_failure<Value>(
		category, make_diagnostic(severity, std::move(code), std::move(message),
								  std::move(technical_details)));
}

[[nodiscard]] juce::File file_from_path(const std::filesystem::path& path) {
	return juce::File{juce::String{path.string()}};
}

[[nodiscard]] std::uint64_t file_size_or_zero(
	const std::filesystem::path& path) {
	std::error_code error;
	const std::uintmax_t size = std::filesystem::file_size(path, error);
	if (error)
		return 0U;

	return static_cast<std::uint64_t>(size);
}

[[nodiscard]] bool entry_is_directory(std::string_view archive_path) noexcept {
	return archive_path.ends_with('/');
}

[[nodiscard]] PlatformValueResult<std::uint64_t> read_entry_size(
	juce::ZipFile& zip, int entry_index, std::string_view archive_path,
	CancellationToken& cancellation_token) {
	std::unique_ptr<juce::InputStream> stream{
		zip.createStreamForEntry(entry_index)};
	if (stream == nullptr) {
		return zip_failure<std::uint64_t>(
			core::OperationResultCategory::ValidationFailure,
			core::DiagnosticSeverity::WriteBlockingError,
			"zip-entry-unreadable",
			"ZIP entry could not be opened for validation.",
			std::string{archive_path});
	}

	std::array<char, zip_read_buffer_size> buffer{};
	std::uint64_t bytes_read_total{};
	while (!stream->isExhausted()) {
		if (cancellation_token.cancellation_requested())
			return platform_value_user_cancelled<std::uint64_t>();

		const int bytes_read =
			stream->read(buffer.data(), static_cast<int>(buffer.size()));
		if (bytes_read < 0) {
			return zip_failure<std::uint64_t>(
				core::OperationResultCategory::ValidationFailure,
				core::DiagnosticSeverity::WriteBlockingError,
				"zip-entry-read-failed",
				"ZIP entry could not be read completely.",
				std::string{archive_path});
		}
		if (bytes_read == 0)
			break;

		bytes_read_total += static_cast<std::uint64_t>(bytes_read);
	}

	return platform_value_success(bytes_read_total);
}
}	 // namespace

PlatformValueResult<ZipArchiveInspection>
JuceZipArchiveService::build_zip_archive(
	const ZipArchiveBuildRequest& request,
	const PlatformOperationContext& context, ProgressSink& progress_sink,
	CancellationToken& cancellation_token) {
	if (cancellation_token.cancellation_requested())
		return platform_value_user_cancelled<ZipArchiveInspection>();
	if (request.output_path.empty()) {
		return zip_failure<ZipArchiveInspection>(
			core::OperationResultCategory::ValidationFailure,
			core::DiagnosticSeverity::ActionValidationError,
			"zip-output-path-empty", "ZIP output path is required.");
	}
	if (request.entries.empty()) {
		return zip_failure<ZipArchiveInspection>(
			core::OperationResultCategory::ValidationFailure,
			core::DiagnosticSeverity::ActionValidationError,
			"zip-entry-list-empty",
			"ZIP archive must contain at least one file entry.");
	}
	if (request.compression_level < 0 || request.compression_level > 9) {
		return zip_failure<ZipArchiveInspection>(
			core::OperationResultCategory::ValidationFailure,
			core::DiagnosticSeverity::ActionValidationError,
			"zip-compression-level-invalid",
			"ZIP compression level must be in the range 0..9.");
	}

	progress_sink.publish_progress(ProgressEvent{
		.operation_id	= context.operation_id,
		.operation_type = context.operation_type,
		.phase			= "zip-build-started",
		.message_id		= ProgressMessageId::ZipBuildStarted,
		.current_units	= std::uint64_t{0},
		.total_units	= static_cast<std::uint64_t>(request.entries.size()),
		.message		= "ZIP archive build started.",
		.cancellable	= true});

	std::set<std::string> archive_paths;
	juce::ZipFile::Builder builder;
	for (const ZipArchiveEntrySource& entry : request.entries) {
		if (!zip_archive_path_is_safe(entry.archive_path)) {
			return zip_failure<ZipArchiveInspection>(
				core::OperationResultCategory::ValidationFailure,
				core::DiagnosticSeverity::ActionValidationError,
				"zip-entry-path-unsafe",
				"ZIP entry path is not safe and relative.", entry.archive_path);
		}
		const std::pair<std::set<std::string>::iterator, bool> inserted =
			archive_paths.insert(entry.archive_path);
		if (!inserted.second) {
			return zip_failure<ZipArchiveInspection>(
				core::OperationResultCategory::ValidationFailure,
				core::DiagnosticSeverity::ActionValidationError,
				"zip-entry-path-duplicate",
				"ZIP archive cannot contain duplicate entry paths.",
				entry.archive_path);
		}

		std::error_code error;
		if (!std::filesystem::is_regular_file(entry.source_path, error)
			|| error) {
			return zip_failure<ZipArchiveInspection>(
				core::OperationResultCategory::SourceUnavailable,
				core::DiagnosticSeverity::WriteBlockingError,
				"zip-source-file-unavailable",
				"Source file for ZIP entry is not readable as a regular file.",
				entry.source_path.string());
		}

		builder.addFile(file_from_path(entry.source_path),
						request.compression_level,
						juce::String{entry.archive_path});
	}

	std::error_code error;
	std::filesystem::create_directories(request.output_path.parent_path(),
										error);
	if (error) {
		return zip_failure<ZipArchiveInspection>(
			core::OperationResultCategory::TemporaryStorageFailure,
			core::DiagnosticSeverity::WriteBlockingError,
			"zip-output-directory-unavailable",
			"ZIP output directory could not be created.", error.message());
	}

	juce::File output_file = file_from_path(request.output_path);
	std::unique_ptr<juce::FileOutputStream> output =
		output_file.createOutputStream();
	if (output == nullptr || output->failedToOpen()) {
		return zip_failure<ZipArchiveInspection>(
			core::OperationResultCategory::TemporaryStorageFailure,
			core::DiagnosticSeverity::WriteBlockingError,
			"zip-output-unavailable", "ZIP output file could not be opened.",
			request.output_path.string());
	}
	output->setPosition(0);
	output->truncate();

	progress_sink.publish_progress(ProgressEvent{
		.operation_id	= context.operation_id,
		.operation_type = context.operation_type,
		.phase			= "zip-build-writing",
		.message_id		= ProgressMessageId::ZipBuildWriting,
		.current_units	= std::uint64_t{0},
		.total_units	= static_cast<std::uint64_t>(request.entries.size()),
		.message		= "Writing ZIP archive.",
		.cancellable	= true});
	if (!builder.writeToStream(*output, nullptr)) {
		output.reset();
		std::filesystem::remove(request.output_path, error);
		return zip_failure<ZipArchiveInspection>(
			core::OperationResultCategory::TemporaryStorageFailure,
			core::DiagnosticSeverity::WriteBlockingError, "zip-write-failed",
			"JUCE ZIP builder failed to write the archive.",
			request.output_path.string());
	}
	output->flush();
	if (output->getStatus().failed()) {
		const std::string details =
			output->getStatus().getErrorMessage().toStdString();
		output.reset();
		std::filesystem::remove(request.output_path, error);
		return zip_failure<ZipArchiveInspection>(
			core::OperationResultCategory::TemporaryStorageFailure,
			core::DiagnosticSeverity::WriteBlockingError, "zip-flush-failed",
			"ZIP output file could not be flushed.", details);
	}
	output.reset();

	progress_sink.publish_progress(
		ProgressEvent{.operation_id	  = context.operation_id,
					  .operation_type = context.operation_type,
					  .phase		  = "zip-build-validating",
					  .message_id	  = ProgressMessageId::ZipBuildValidating,
					  .message		  = "Validating ZIP archive.",
					  .cancellable	  = true});
	return inspect_zip_archive(request.output_path, context, progress_sink,
							   cancellation_token);
}

PlatformValueResult<ZipArchiveInspection>
JuceZipArchiveService::inspect_zip_archive(
	const std::filesystem::path& archive_path,
	const PlatformOperationContext& context, ProgressSink& progress_sink,
	CancellationToken& cancellation_token) {
	if (cancellation_token.cancellation_requested())
		return platform_value_user_cancelled<ZipArchiveInspection>();

	std::error_code error;
	if (!std::filesystem::is_regular_file(archive_path, error) || error) {
		return zip_failure<ZipArchiveInspection>(
			core::OperationResultCategory::SourceUnavailable,
			core::DiagnosticSeverity::WriteBlockingError,
			"zip-archive-unavailable",
			"ZIP archive file is not available for reading.",
			archive_path.string());
	}

	juce::ZipFile zip{file_from_path(archive_path)};
	const int entry_count = zip.getNumEntries();
	if (entry_count <= 0) {
		return zip_failure<ZipArchiveInspection>(
			core::OperationResultCategory::ValidationFailure,
			core::DiagnosticSeverity::WriteBlockingError,
			"zip-empty-or-unreadable", "ZIP archive has no readable entries.",
			archive_path.string());
	}

	progress_sink.publish_progress(
		ProgressEvent{.operation_id	  = context.operation_id,
					  .operation_type = context.operation_type,
					  .phase		  = "zip-inspecting",
					  .message_id	  = ProgressMessageId::ZipInspecting,
					  .current_units  = std::uint64_t{0},
					  .total_units	  = static_cast<std::uint64_t>(entry_count),
					  .message		  = "Inspecting ZIP archive entries.",
					  .cancellable	  = true});

	ZipArchiveInspection inspection;
	inspection.archive_byte_count = file_size_or_zero(archive_path);
	std::set<std::string> seen_paths;
	for (int index = 0; index < entry_count; ++index) {
		if (cancellation_token.cancellation_requested())
			return platform_value_user_cancelled<ZipArchiveInspection>();

		const juce::ZipFile::ZipEntry* entry = zip.getEntry(index);
		if (entry == nullptr) {
			return zip_failure<ZipArchiveInspection>(
				core::OperationResultCategory::ValidationFailure,
				core::DiagnosticSeverity::WriteBlockingError,
				"zip-entry-metadata-unreadable",
				"ZIP entry metadata could not be read.");
		}

		const std::string archive_entry_path = entry->filename.toStdString();
		if (!zip_archive_path_is_safe(archive_entry_path)) {
			return zip_failure<ZipArchiveInspection>(
				core::OperationResultCategory::ValidationFailure,
				core::DiagnosticSeverity::ActionValidationError,
				"zip-entry-path-unsafe",
				"ZIP entry path is unsafe and cannot be extracted.",
				archive_entry_path);
		}
		const std::pair<std::set<std::string>::iterator, bool> inserted =
			seen_paths.insert(archive_entry_path);
		if (!inserted.second) {
			return zip_failure<ZipArchiveInspection>(
				core::OperationResultCategory::ValidationFailure,
				core::DiagnosticSeverity::ActionValidationError,
				"zip-entry-path-duplicate",
				"ZIP archive contains duplicate entry paths.",
				archive_entry_path);
		}
		if (entry->isSymbolicLink) {
			return zip_failure<ZipArchiveInspection>(
				core::OperationResultCategory::ValidationFailure,
				core::DiagnosticSeverity::ActionValidationError,
				"zip-symbolic-link-entry",
				"ZIP archive symbolic link entries are not supported.",
				archive_entry_path);
		}

		const bool directory = entry_is_directory(archive_entry_path);
		std::uint64_t entry_bytes{};
		if (!directory) {
			PlatformValueResult<std::uint64_t> read_size = read_entry_size(
				zip, index, archive_entry_path, cancellation_token);
			if (!read_size.succeeded())
				return zip_failure<ZipArchiveInspection>(
					read_size.category,
					read_size.diagnostics.empty()
						? core::DiagnosticSeverity::WriteBlockingError
						: read_size.diagnostics.front().severity,
					read_size.diagnostics.empty()
						? "zip-entry-read-failed"
						: read_size.diagnostics.front().code,
					read_size.diagnostics.empty()
						? "ZIP entry could not be read."
						: read_size.diagnostics.front().message,
					read_size.diagnostics.empty()
						? std::string{archive_entry_path}
						: read_size.diagnostics.front().technical_details);
			entry_bytes = *read_size.value;
		}

		inspection.largest_entry_byte_count =
			std::max(inspection.largest_entry_byte_count, entry_bytes);
		inspection.entries.push_back(
			ZipArchiveEntryInfo{.archive_path		= archive_entry_path,
								.uncompressed_bytes = entry_bytes,
								.directory			= directory});
		progress_sink.publish_progress(ProgressEvent{
			.operation_id	= context.operation_id,
			.operation_type = context.operation_type,
			.phase			= "zip-inspecting",
			.message_id		= ProgressMessageId::ZipInspecting,
			.current_units	= static_cast<std::uint64_t>(index + 1),
			.total_units	= static_cast<std::uint64_t>(entry_count),
			.message		= "Inspecting ZIP archive entries.",
			.cancellable	= true});
	}

	inspection.classic_zip64_risk_observed =
		inspection.archive_byte_count >= classic_zip_limit
		|| inspection.largest_entry_byte_count >= classic_zip_limit;
	return platform_value_success(std::move(inspection));
}

PlatformValueResult<ZipArchiveInspection>
JuceZipArchiveService::validate_zip_archive(
	const ZipArchiveValidationRequest& request,
	const PlatformOperationContext& context, ProgressSink& progress_sink,
	CancellationToken& cancellation_token) {
	PlatformValueResult<ZipArchiveInspection> inspected = inspect_zip_archive(
		request.archive_path, context, progress_sink, cancellation_token);
	if (!inspected.succeeded())
		return inspected;

	std::set<std::string> entries;
	for (const ZipArchiveEntryInfo& entry : inspected.value->entries)
		entries.insert(entry.archive_path);

	for (const std::string& required_entry : request.required_entries) {
		if (request.reject_unsafe_paths
			&& !zip_archive_path_is_safe(required_entry)) {
			return zip_failure<ZipArchiveInspection>(
				core::OperationResultCategory::ValidationFailure,
				core::DiagnosticSeverity::ActionValidationError,
				"zip-required-entry-path-unsafe",
				"Required ZIP entry path is unsafe.", required_entry);
		}
		if (!entries.contains(required_entry)) {
			return zip_failure<ZipArchiveInspection>(
				core::OperationResultCategory::ValidationFailure,
				core::DiagnosticSeverity::WriteBlockingError,
				"zip-required-entry-missing",
				"ZIP archive does not contain a required catalog entry.",
				required_entry);
		}
	}

	return inspected;
}

PlatformValueResult<ZipArchiveInspection>
JuceZipArchiveService::extract_zip_archive(
	const ZipArchiveExtractRequest& request,
	const PlatformOperationContext& context, ProgressSink& progress_sink,
	CancellationToken& cancellation_token) {
	PlatformValueResult<ZipArchiveInspection> inspected = inspect_zip_archive(
		request.archive_path, context, progress_sink, cancellation_token);
	if (!inspected.succeeded())
		return inspected;

	std::error_code error;
	std::filesystem::create_directories(request.target_directory, error);
	if (error) {
		return zip_failure<ZipArchiveInspection>(
			core::OperationResultCategory::TemporaryStorageFailure,
			core::DiagnosticSeverity::WriteBlockingError,
			"zip-extract-directory-unavailable",
			"ZIP extraction target directory could not be created.",
			error.message());
	}

	juce::ZipFile zip{file_from_path(request.archive_path)};
	const int entry_count = zip.getNumEntries();
	const juce::File target_directory =
		file_from_path(request.target_directory);
	const juce::ZipFile::OverwriteFiles overwrite =
		request.overwrite_files ? juce::ZipFile::OverwriteFiles::yes
								: juce::ZipFile::OverwriteFiles::no;
	for (int index = 0; index < entry_count; ++index) {
		if (cancellation_token.cancellation_requested())
			return platform_value_user_cancelled<ZipArchiveInspection>();

		progress_sink.publish_progress(ProgressEvent{
			.operation_id	= context.operation_id,
			.operation_type = context.operation_type,
			.phase			= "zip-extracting",
			.message_id		= ProgressMessageId::ZipExtracting,
			.current_units	= static_cast<std::uint64_t>(index),
			.total_units	= static_cast<std::uint64_t>(entry_count),
			.message		= "Extracting ZIP archive.",
			.cancellable	= true});

		const juce::Result result =
			zip.uncompressEntry(index, target_directory, overwrite,
								juce::ZipFile::FollowSymlinks::no);
		if (result.failed()) {
			return zip_failure<ZipArchiveInspection>(
				core::OperationResultCategory::ValidationFailure,
				core::DiagnosticSeverity::WriteBlockingError,
				"zip-extract-entry-failed",
				"ZIP entry could not be extracted safely.",
				result.getErrorMessage().toStdString());
		}
	}

	progress_sink.publish_progress(
		ProgressEvent{.operation_id	  = context.operation_id,
					  .operation_type = context.operation_type,
					  .phase		  = "zip-extract-completed",
					  .message_id	  = ProgressMessageId::ZipExtractCompleted,
					  .current_units  = static_cast<std::uint64_t>(entry_count),
					  .total_units	  = static_cast<std::uint64_t>(entry_count),
					  .message		  = "ZIP archive extraction completed.",
					  .cancellable	  = false});
	return inspected;
}
}	 // namespace shuba::platform
