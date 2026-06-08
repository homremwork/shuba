#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <string_view>

namespace shuba::core {
enum class IdentifierValidationIssue {
	None,
	Empty,
	ContainsPathSeparator,
	ContainsUnsafeFileCharacter,
	InvalidLowercaseUuidShape,
};

struct IdentifierValidation final {
	IdentifierValidationIssue issue{IdentifierValidationIssue::None};

	[[nodiscard]] bool valid() const noexcept;
	[[nodiscard]] explicit operator bool() const noexcept;
};

[[nodiscard]] std::string_view to_string(
	IdentifierValidationIssue Issue) noexcept;
[[nodiscard]] bool is_lowercase_hex_digit(char Value) noexcept;
[[nodiscard]] bool is_file_safe_identifier_character(char Value) noexcept;
[[nodiscard]] IdentifierValidation validate_stable_identifier_text(
	std::string_view Text) noexcept;
[[nodiscard]] IdentifierValidation validate_file_safe_identifier_text(
	std::string_view Text) noexcept;
[[nodiscard]] IdentifierValidation validate_lowercase_uuid_text(
	std::string_view Text) noexcept;

class StableIdentifier final {
public:
	[[nodiscard]] static std::optional<StableIdentifier> try_create(
		std::string Text);
	[[nodiscard]] static std::optional<StableIdentifier> try_create_file_safe(
		std::string Text);

	[[nodiscard]] const std::string& value() const noexcept;
	[[nodiscard]] std::string_view view() const noexcept;

	friend bool operator==(const StableIdentifier&,
						   const StableIdentifier&) = default;

private:
	explicit StableIdentifier(std::string Value);

	std::string text;
};

class OperationIdentifier final {
public:
	[[nodiscard]] static std::optional<OperationIdentifier> try_create(
		std::string Text);
	[[nodiscard]] static std::optional<OperationIdentifier>
	try_create_file_safe(std::string Text);

	[[nodiscard]] const std::string& value() const noexcept;
	[[nodiscard]] std::string_view view() const noexcept;

	friend bool operator==(const OperationIdentifier&,
						   const OperationIdentifier&) = default;

private:
	explicit OperationIdentifier(std::string Value);

	std::string text;
};

class IdentifierSource {
public:
	IdentifierSource()										 = default;
	IdentifierSource(const IdentifierSource&)				 = default;
	IdentifierSource& operator=(const IdentifierSource&)	 = default;
	IdentifierSource(IdentifierSource&&) noexcept			 = default;
	IdentifierSource& operator=(IdentifierSource&&) noexcept = default;
	virtual ~IdentifierSource()								 = default;

	[[nodiscard]] virtual StableIdentifier next_stable_identifier()		  = 0;
	[[nodiscard]] virtual OperationIdentifier next_operation_identifier() = 0;
};

class RandomIdentifierSource final : public IdentifierSource {
public:
	RandomIdentifierSource();
	explicit RandomIdentifierSource(std::uint64_t Seed);

	[[nodiscard]] StableIdentifier next_stable_identifier() override;
	[[nodiscard]] OperationIdentifier next_operation_identifier() override;

private:
	[[nodiscard]] static std::mt19937_64 make_random_engine();
	[[nodiscard]] std::array<std::uint8_t, 16> next_uuid_bytes();
	[[nodiscard]] std::string next_uuid_text();
	[[nodiscard]] static std::string format_uuid_v4(
		std::array<std::uint8_t, 16> Bytes);
	[[nodiscard]] static char to_lower_hex(std::uint8_t Value) noexcept;

	std::mt19937_64 engine;
};
}	 // namespace shuba::core
