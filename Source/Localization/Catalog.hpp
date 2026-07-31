#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace shuba::localization::detail {
struct CatalogIssue final {
	std::string code;
	std::string technical_details;
};

struct CatalogLoadResult final {
	std::shared_ptr<const class Catalog> catalog;
	std::vector<CatalogIssue> issues;

	[[nodiscard]] bool accepted() const noexcept;
};

class Catalog final {
public:
	Catalog(const Catalog&)			   = delete;
	Catalog& operator=(const Catalog&) = delete;
	Catalog(Catalog&&) noexcept;
	Catalog& operator=(Catalog&&) noexcept;
	~Catalog();

	[[nodiscard]] std::string pgettext(std::string_view context,
									   std::string_view message_id) const;
	[[nodiscard]] std::string npgettext(std::string_view context,
										std::string_view singular_message_id,
										std::string_view plural_message_id,
										std::uint64_t count) const;

private:
	struct State;

	explicit Catalog(std::shared_ptr<State> state);

	std::shared_ptr<State> state;

	friend CatalogLoadResult load_catalog(std::string_view catalog_bytes);
};

[[nodiscard]] CatalogLoadResult load_catalog(std::string_view catalog_bytes);
}	 // namespace shuba::localization::detail
