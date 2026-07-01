#include "Platform/JuceHashing.hpp"

#include <juce_cryptography/juce_cryptography.h>

#include <system_error>
#include <utility>

namespace shuba::platform {
namespace {
[[nodiscard]] core::Diagnostic make_diagnostic(
	core::DiagnosticSeverity severity, std::string code, std::string message,
	std::string technical_details = {}) {
	return core::Diagnostic{.severity		   = severity,
							.code			   = std::move(code),
							.message		   = std::move(message),
							.technical_details = std::move(technical_details)};
}

[[nodiscard]] juce::File file_from_path(const std::filesystem::path& path) {
	return juce::File{juce::String{path.string()}};
}
}	 // namespace

PlatformValueResult<SourceByteFingerprint>
JuceMd5SourceByteFingerprintService::fingerprint_source_bytes(
	const SourceByteFingerprintRequest& request,
	const PlatformOperationContext& context, ProgressSink& progress_sink,
	CancellationToken& cancellation_token) {
	if (cancellation_token.cancellation_requested())
		return platform_value_user_cancelled<SourceByteFingerprint>();

	std::error_code error;
	const bool regular_file =
		std::filesystem::is_regular_file(request.source_path, error);
	if (error || !regular_file) {
		return platform_value_failure<SourceByteFingerprint>(
			core::OperationResultCategory::SourceUnavailable,
			make_diagnostic(
				core::DiagnosticSeverity::RecoverableWarning,
				"source-fingerprint-unavailable",
				"Source-byte fingerprint could not read the staged source "
				"file.",
				error ? error.message() : request.source_path.string()));
	}

	std::optional<std::uint64_t> total_units;
	const std::uintmax_t file_size =
		std::filesystem::file_size(request.source_path, error);
	if (!error)
		total_units = static_cast<std::uint64_t>(file_size);

	juce::FileInputStream input{file_from_path(request.source_path)};
	if (!input.openedOk()) {
		return platform_value_failure<SourceByteFingerprint>(
			core::OperationResultCategory::SourceUnavailable,
			make_diagnostic(core::DiagnosticSeverity::RecoverableWarning,
							"source-fingerprint-open-failed",
							"Source-byte fingerprint could not open the staged "
							"source file.",
							request.source_path.string()));
	}

	progress_sink.publish_progress(
		ProgressEvent{.operation_id	  = context.operation_id,
					  .operation_type = context.operation_type,
					  .phase		  = "source-fingerprint-started",
					  .current_units  = std::uint64_t{0},
					  .total_units	  = total_units,
					  .message		  = "Source-byte fingerprint started.",
					  .cancellable	  = false});

	juce::MD5 md5{input};
	if (cancellation_token.cancellation_requested())
		return platform_value_user_cancelled<SourceByteFingerprint>();

	progress_sink.publish_progress(
		ProgressEvent{.operation_id	  = context.operation_id,
					  .operation_type = context.operation_type,
					  .phase		  = "source-fingerprint-completed",
					  .current_units  = total_units,
					  .total_units	  = total_units,
					  .message		  = "Source-byte fingerprint completed.",
					  .cancellable	  = false});
	return platform_value_success(
		SourceByteFingerprint{.source_md5 = md5.toHexString().toStdString()});
}
}	 // namespace shuba::platform
