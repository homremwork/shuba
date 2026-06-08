#include "Core/Result.hpp"

#include <stdexcept>
#include <utility>

namespace shuba::core {
std::string_view to_string(OperationResultCategory Category) noexcept {
	switch (Category) {
		case OperationResultCategory::Success:
			return "success";
		case OperationResultCategory::UserCancelled:
			return "user cancelled";
		case OperationResultCategory::Unsupported:
			return "unsupported";
		case OperationResultCategory::PermissionDenied:
			return "permission denied";
		case OperationResultCategory::SourceUnavailable:
			return "source unavailable";
		case OperationResultCategory::DestinationUnavailable:
			return "destination unavailable";
		case OperationResultCategory::TemporaryStorageFailure:
			return "temporary storage failure";
		case OperationResultCategory::CodecFailure:
			return "codec failure";
		case OperationResultCategory::ValidationFailure:
			return "validation failure";
		case OperationResultCategory::ReplacementFailure:
			return "replacement failure";
		case OperationResultCategory::InternalError:
			return "internal error";
	}

	return "unknown operation result category";
}

std::string_view to_string(DiagnosticSeverity Severity) noexcept {
	switch (Severity) {
		case DiagnosticSeverity::FatalCatalogError:
			return "fatal catalog error";
		case DiagnosticSeverity::DegradedLoad:
			return "degraded load";
		case DiagnosticSeverity::RecoverableWarning:
			return "recoverable warning";
		case DiagnosticSeverity::WriteBlockingError:
			return "write-blocking error";
		case DiagnosticSeverity::ActionValidationError:
			return "action validation error";
	}

	return "unknown diagnostic severity";
}

OperationResult OperationResult::success(std::vector<Diagnostic> Diagnostics) {
	return OperationResult(OperationResultCategory::Success,
						   std::move(Diagnostics));
}

OperationResult OperationResult::user_cancelled() {
	return OperationResult(OperationResultCategory::UserCancelled, {});
}

OperationResult OperationResult::failure(OperationResultCategory Category,
										 Diagnostic DiagnosticEntry) {
	if (Category == OperationResultCategory::Success
		|| Category == OperationResultCategory::UserCancelled) {
		throw std::invalid_argument(
			"failure result cannot use success or user-cancelled category");
	}

	return OperationResult(Category, {std::move(DiagnosticEntry)});
}

OperationResultCategory OperationResult::category() const noexcept {
	return result_category;
}

bool OperationResult::succeeded() const noexcept {
	return result_category == OperationResultCategory::Success;
}

bool OperationResult::was_user_cancelled() const noexcept {
	return result_category == OperationResultCategory::UserCancelled;
}

bool OperationResult::failed() const noexcept {
	return !succeeded() && !was_user_cancelled();
}

const std::vector<Diagnostic>& OperationResult::diagnostics() const noexcept {
	return diagnostic_entries;
}

void OperationResult::add_diagnostic(Diagnostic DiagnosticEntry) {
	diagnostic_entries.push_back(std::move(DiagnosticEntry));
}

OperationResult::OperationResult(OperationResultCategory Category,
								 std::vector<Diagnostic> Diagnostics)
	: result_category(Category), diagnostic_entries(std::move(Diagnostics)) {}
}	 // namespace shuba::core
