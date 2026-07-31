#include "Localization/Language.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string_view>

namespace {
using shuba::localization::Language;
using shuba::localization::resolve_language;

TEST_CASE("B27 resolves Russian language primary subtags deterministically",
		  "[b27][localization][language]") {
	const std::array<std::string_view, 6> russian_tags{
		"ru", "RU", "rU", "ru-BY", "RU-by", "ru_BY"};

	for (const std::string_view language_tag : russian_tags) {
		CAPTURE(language_tag);
		REQUIRE(resolve_language(language_tag) == Language::Russian);
	}
}

TEST_CASE(
	"B27 falls back to English for missing unsupported and region-only tags",
	"[b27][localization][language]") {
	const std::array<std::string_view, 12> english_tags{
		"",	  "en",	 "EN-US", "be",	 "by",	"BY",
		"uk", "ruu", "r",	  "-ru", "_ru", "  ru"};

	for (const std::string_view language_tag : english_tags) {
		CAPTURE(language_tag);
		REQUIRE(resolve_language(language_tag) == Language::English);
	}
}
}	 // namespace
