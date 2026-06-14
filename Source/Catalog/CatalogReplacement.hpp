#pragma once

#include "Catalog/BackupArchive.hpp"
#include "Core/Clock.hpp"
#include "Core/Identifier.hpp"
#include "Core/OperationGate.hpp"
#include "Core/Result.hpp"
#include "Platform/PlatformServices.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace shuba::catalog {
inline constexpr std::size_t default_full_catalog_rollback_retention = 1U;

enum class CatalogReplacementStatus : std::uint8_t {
	Replaced,
	Rejected,
	RolledBack,
	FatalRecoveryRequired,
	Cancelled,
};

[[nodiscard]] std::string_view to_string(
	CatalogReplacementStatus status) noexcept;

enum class CatalogReplacementFaultMode : std::uint8_t {
	None,
	FailAfterActiveCatalogParked,
	ForceImportedLoadFatal,
	FailRollbackRestore,
	ForceImportedLoadFatalAndFailRollbackRestore,
};

struct CatalogReplacementRequest final {
	std::filesystem::path app_private_root;
	std::filesystem::path staged_catalog_root;
	bool replacement_confirmed{};
	bool degraded_import_confirmed{};
	std::size_t rollback_retention{default_full_catalog_rollback_retention};
	CatalogReplacementFaultMode fault_mode{CatalogReplacementFaultMode::None};

	friend bool operator==(const CatalogReplacementRequest&,
						   const CatalogReplacementRequest&) = default;
};

struct CatalogReplacementResult final {
	CatalogReplacementStatus status{CatalogReplacementStatus::Rejected};
	core::OperationResultCategory category{
		core::OperationResultCategory::ValidationFailure};
	std::vector<core::Diagnostic> diagnostics;
	StagedCatalogValidationResult staged_validation;
	StagedCatalogValidationResult active_validation;
	std::optional<std::filesystem::path> rollback_copy_directory;
	std::optional<std::filesystem::path> parked_catalog_directory;
	bool rollback_copy_created{};
	bool active_catalog_parked{};
	bool staged_catalog_moved{};
	bool rollback_attempted{};
	bool rollback_succeeded{};
	bool old_active_cleanup_attempted{};
	bool rollback_retention_cleanup_attempted{};
	bool critical_section_entered{};

	[[nodiscard]] bool succeeded() const noexcept;
	[[nodiscard]] bool failed() const noexcept;
	[[nodiscard]] bool fatal_recovery_required() const noexcept;

	void add_diagnostic(core::Diagnostic diagnostic);
};

class CatalogReplacementUseCase final {
public:
	CatalogReplacementUseCase(core::IdentifierSource& identifier_source,
							  const core::Clock& clock,
							  core::OperationGate& operation_gate);

	[[nodiscard]] CatalogReplacementResult replace_with_staged_import(
		const CatalogReplacementRequest& request,
		platform::ProgressSink& progress_sink,
		platform::CancellationToken& cancellation_token);

private:
	core::IdentifierSource& identifiers;
	const core::Clock& replacement_clock;
	core::OperationGate& gate;
};
}	 // namespace shuba::catalog
