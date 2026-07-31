#include "Localization/EmbeddedCatalog.hpp"

#include <BinaryData.h>

#include <cstddef>
#include <limits>

namespace shuba::localization {
std::string_view embedded_russian_catalog() noexcept {
	static_assert(ShubaBinaryData::ru_poSize >= 0);
	static_assert(static_cast<unsigned long long>(ShubaBinaryData::ru_poSize)
				  <= static_cast<unsigned long long>(
					  std::numeric_limits<std::size_t>::max()));
	return {ShubaBinaryData::ru_po,
			static_cast<std::size_t>(ShubaBinaryData::ru_poSize)};
}
}	 // namespace shuba::localization
