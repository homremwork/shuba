#pragma once

#include <expected>
#include <string>
#include <vector>

namespace shuba::localization::detail {
struct CatalogDefinition final {
	std::string context;
	std::string singular;
	std::string plural;

	[[nodiscard]] bool is_plural() const noexcept;
};

struct CatalogDefinitionError final {
	std::string technical_details;
};

using CatalogDefinitionsResult =
	std::expected<std::vector<CatalogDefinition>, CatalogDefinitionError>;

[[nodiscard]] CatalogDefinitionsResult validate_catalog_definitions(
	std::vector<CatalogDefinition> definitions);
[[nodiscard]] CatalogDefinitionsResult catalog_definitions();
}	 // namespace shuba::localization::detail
