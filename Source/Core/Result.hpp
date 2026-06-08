#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace shuba::core {
enum class OperationResultCategory {
	Success,
	UserCancelled,
	Unsupported,
	PermissionDenied,
	SourceUnavailable,
	DestinationUnavailable,
	TemporaryStorageFailure,
	CodecFailure,
	ValidationFailure,
	ReplacementFailure,
	InternalError,
};

enum class DiagnosticSeverity {
	FatalCatalogError,
	DegradedLoad,
	RecoverableWarning,
	WriteBlockingError,
	ActionValidationError,
};

[[nodiscard]] std::string_view to_string(
	OperationResultCategory Category) noexcept;
[[nodiscard]] std::string_view to_string(DiagnosticSeverity Severity) noexcept;

struct Diagnostic final {
	DiagnosticSeverity severity{DiagnosticSeverity::RecoverableWarning};
	std::string code;
	std::string message;
	std::string technical_details;

	friend bool operator==(const Diagnostic&, const Diagnostic&) = default;
};

class OperationResult final {
public:
	[[nodiscard]] static OperationResult success(
		std::vector<Diagnostic> Diagnostics = {});
	[[nodiscard]] static OperationResult user_cancelled();
	[[nodiscard]] static OperationResult failure(
		OperationResultCategory Category, Diagnostic DiagnosticEntry);

	[[nodiscard]] OperationResultCategory category() const noexcept;
	[[nodiscard]] bool succeeded() const noexcept;
	[[nodiscard]] bool was_user_cancelled() const noexcept;
	[[nodiscard]] bool failed() const noexcept;
	[[nodiscard]] const std::vector<Diagnostic>& diagnostics() const noexcept;

	void add_diagnostic(Diagnostic DiagnosticEntry);

private:
	explicit OperationResult(OperationResultCategory Category,
							 std::vector<Diagnostic> Diagnostics);

	OperationResultCategory result_category{OperationResultCategory::Success};
	std::vector<Diagnostic> diagnostic_entries;
};
}	 // namespace shuba::core
