#pragma once

#include <cstdint>
#include <string_view>

namespace shuba::localization {
enum class Language : std::uint8_t {
	English,
	Russian,
};

[[nodiscard]] Language resolve_language(std::string_view language_tag) noexcept;
}	 // namespace shuba::localization
