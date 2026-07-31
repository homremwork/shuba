#include "Localization/Language.hpp"

#include <cstddef>

namespace shuba::localization {
namespace {
[[nodiscard]] constexpr bool is_ascii_separator(char character) noexcept {
	return character == '-' || character == '_';
}

[[nodiscard]] constexpr char ascii_lowercase(char character) noexcept {
	if (character >= 'A' && character <= 'Z')
		return static_cast<char>(character - 'A' + 'a');
	return character;
}
}	 // namespace

Language resolve_language(std::string_view language_tag) noexcept {
	std::size_t primary_subtag_length{};
	while (primary_subtag_length < language_tag.size()
		   && !is_ascii_separator(language_tag[primary_subtag_length])) {
		++primary_subtag_length;
	}

	if (primary_subtag_length != 2)
		return Language::English;

	const char first  = ascii_lowercase(language_tag[0]);
	const char second = ascii_lowercase(language_tag[1]);
	return first == 'r' && second == 'u' ? Language::Russian
										 : Language::English;
}
}	 // namespace shuba::localization
