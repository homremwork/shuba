#pragma once

#include "Catalog/CatalogRepository.hpp"
#include "Catalog/Search.hpp"
#include "Core/Identifier.hpp"
#include "Core/Result.hpp"
#include "Persistence/JsonlCatalog.hpp"
#include "Platform/PlatformServices.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace shuba::ui {
inline constexpr std::string_view debug_demo_marker_file_name{
	".debug-demo-catalog"};

enum class CatalogSessionStartupSource : std::uint8_t {
	ExistingCatalog,
	InitializedEmptyCatalog,
	SeededDemoCatalog,
	PathResolutionFailed,
	InitializationFailed,
	LoadFailed,
};

[[nodiscard]] std::string_view to_string(
	CatalogSessionStartupSource source) noexcept;

struct CatalogSessionState final {
	CatalogSessionStartupSource source{
		CatalogSessionStartupSource::PathResolutionFailed};
	std::optional<platform::AppPrivatePaths> paths;
	std::optional<core::StableIdentifier> catalog_id;
	bool existing_canonical_metadata{};
	bool initialized_empty_catalog{};
	bool demo_catalog_seeded{};
	bool demo_catalog_active{};
	persistence::CatalogLoadStatus load_status{
		persistence::CatalogLoadStatus::Fatal};
	std::vector<core::Diagnostic> startup_diagnostics;
	persistence::CatalogJsonlLoadResult load_result;
	catalog::CatalogRepositoryState repository;
	catalog::SearchIndex search_index;

	[[nodiscard]] bool ready_for_browsing() const noexcept;
	[[nodiscard]] bool degraded() const noexcept;
	[[nodiscard]] bool fatal() const noexcept;
};
}	 // namespace shuba::ui
