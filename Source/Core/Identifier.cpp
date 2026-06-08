#include "Core/Identifier.hpp"

#include <random>
#include <utility>

namespace shuba::core {
bool IdentifierValidation::valid() const noexcept {
	return issue == IdentifierValidationIssue::None;
}

IdentifierValidation::operator bool() const noexcept {
	return valid();
}

std::string_view to_string(IdentifierValidationIssue Issue) noexcept {
	switch (Issue) {
		case IdentifierValidationIssue::None:
			return "none";
		case IdentifierValidationIssue::Empty:
			return "empty";
		case IdentifierValidationIssue::ContainsPathSeparator:
			return "contains path separator";
		case IdentifierValidationIssue::ContainsUnsafeFileCharacter:
			return "contains unsafe file character";
		case IdentifierValidationIssue::InvalidLowercaseUuidShape:
			return "invalid lowercase UUID shape";
	}

	return "unknown identifier validation issue";
}

bool is_lowercase_hex_digit(char Value) noexcept {
	return (Value >= '0' && Value <= '9') || (Value >= 'a' && Value <= 'f');
}

bool is_file_safe_identifier_character(char Value) noexcept {
	return (Value >= '0' && Value <= '9') || (Value >= 'a' && Value <= 'z')
		   || Value == '-' || Value == '_';
}

IdentifierValidation validate_stable_identifier_text(
	std::string_view Text) noexcept {
	if (Text.empty())
		return {IdentifierValidationIssue::Empty};

	for (const auto value : Text)
		if (value == '/' || value == '\\')
			return {IdentifierValidationIssue::ContainsPathSeparator};

	return {};
}

IdentifierValidation validate_file_safe_identifier_text(
	std::string_view Text) noexcept {
	if (const auto stable = validate_stable_identifier_text(Text); !stable)
		return stable;

	for (const auto value : Text)
		if (!is_file_safe_identifier_character(value))
			return {IdentifierValidationIssue::ContainsUnsafeFileCharacter};

	return {};
}

IdentifierValidation validate_lowercase_uuid_text(
	std::string_view Text) noexcept {
	constexpr auto uuid_length = std::size_t{36};

	if (Text.empty())
		return {IdentifierValidationIssue::Empty};

	if (Text.size() != uuid_length)
		return {IdentifierValidationIssue::InvalidLowercaseUuidShape};

	for (auto index = std::size_t{0}; index < Text.size(); ++index) {
		const auto value = Text[index];
		const auto should_be_hyphen =
			index == 8 || index == 13 || index == 18 || index == 23;

		if (should_be_hyphen) {
			if (value != '-')
				return {IdentifierValidationIssue::InvalidLowercaseUuidShape};
		} else if (!is_lowercase_hex_digit(value)) {
			return {IdentifierValidationIssue::InvalidLowercaseUuidShape};
		}
	}

	return {};
}

std::optional<StableIdentifier> StableIdentifier::try_create(std::string Text) {
	if (!validate_stable_identifier_text(Text))
		return std::nullopt;

	return StableIdentifier(std::move(Text));
}

std::optional<StableIdentifier> StableIdentifier::try_create_file_safe(
	std::string Text) {
	if (!validate_file_safe_identifier_text(Text))
		return std::nullopt;

	return StableIdentifier(std::move(Text));
}

const std::string& StableIdentifier::value() const noexcept {
	return text;
}

std::string_view StableIdentifier::view() const noexcept {
	return text;
}

StableIdentifier::StableIdentifier(std::string Value)
	: text(std::move(Value)) {}

std::optional<OperationIdentifier> OperationIdentifier::try_create(
	std::string Text) {
	if (!validate_stable_identifier_text(Text))
		return std::nullopt;

	return OperationIdentifier(std::move(Text));
}

std::optional<OperationIdentifier> OperationIdentifier::try_create_file_safe(
	std::string Text) {
	if (!validate_file_safe_identifier_text(Text))
		return std::nullopt;

	return OperationIdentifier(std::move(Text));
}

const std::string& OperationIdentifier::value() const noexcept {
	return text;
}

std::string_view OperationIdentifier::view() const noexcept {
	return text;
}

OperationIdentifier::OperationIdentifier(std::string Value)
	: text(std::move(Value)) {}

RandomIdentifierSource::RandomIdentifierSource()
	: engine(make_random_engine()) {}

RandomIdentifierSource::RandomIdentifierSource(std::uint64_t Seed)
	: engine(Seed) {}

StableIdentifier RandomIdentifierSource::next_stable_identifier() {
	auto identifier = StableIdentifier::try_create_file_safe(next_uuid_text());
	return *identifier;
}

OperationIdentifier RandomIdentifierSource::next_operation_identifier() {
	auto identifier =
		OperationIdentifier::try_create_file_safe(next_uuid_text());
	return *identifier;
}

std::mt19937_64 RandomIdentifierSource::make_random_engine() {
	std::random_device device;
	std::seed_seq seed_values{device(), device(), device(), device()};
	return std::mt19937_64{seed_values};
}

std::array<std::uint8_t, 16> RandomIdentifierSource::next_uuid_bytes() {
	auto bytes = std::array<std::uint8_t, 16>{};
	auto index = std::size_t{0};

	while (index < bytes.size()) {
		auto word = engine();

		for (auto byte_index = 0; byte_index < 8 && index < bytes.size();
			 ++byte_index) {
			bytes[index] = static_cast<std::uint8_t>(word & 0xffU);
			word >>= 8U;
			++index;
		}
	}

	return bytes;
}

std::string RandomIdentifierSource::next_uuid_text() {
	return format_uuid_v4(next_uuid_bytes());
}

std::string RandomIdentifierSource::format_uuid_v4(
	std::array<std::uint8_t, 16> Bytes) {
	Bytes[6] = static_cast<std::uint8_t>((Bytes[6] & 0x0fU) | 0x40U);
	Bytes[8] = static_cast<std::uint8_t>((Bytes[8] & 0x3fU) | 0x80U);

	auto output = std::string{};
	output.reserve(36);

	for (auto index = std::size_t{0}; index < Bytes.size(); ++index) {
		if (index == 4 || index == 6 || index == 8 || index == 10)
			output.push_back('-');

		output.push_back(to_lower_hex(Bytes[index] >> 4U));
		output.push_back(to_lower_hex(Bytes[index]));
	}

	return output;
}

char RandomIdentifierSource::to_lower_hex(std::uint8_t Value) noexcept {
	constexpr auto digits = std::string_view{"0123456789abcdef"};
	return digits[Value & 0x0fU];
}
}	 // namespace shuba::core
