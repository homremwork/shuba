#include "Localization/CatalogDefinition.hpp"

#include "Localization/MessageCatalog.hpp"
#include "Localization/PhotoWorkflowLocalization.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace shuba::localization::detail {
namespace {
class CatalogDefinitionBuilder final {
public:
	void add_static(std::string_view context, std::string_view singular) {
		definitions.push_back(
			CatalogDefinition{.context	= std::string{context},
							  .singular = std::string{singular}});
	}

	void add_plural(std::string_view context, std::string_view singular,
					std::string_view plural) {
		definitions.push_back(
			CatalogDefinition{.context	= std::string{context},
							  .singular = std::string{singular},
							  .plural	= std::string{plural}});
	}

	[[nodiscard]] CatalogDefinitionsResult finish() && {
		return validate_catalog_definitions(std::move(definitions));
	}

private:
	std::vector<CatalogDefinition> definitions;
};

void add_static_messages(CatalogDefinitionBuilder& builder,
						 std::span<const StaticMessage> messages) {
	for (const StaticMessage& message : messages)
		builder.add_static(message.context, message.english);
}

void add_presentation_messages(CatalogDefinitionBuilder& builder,
							   std::span<const PresentationMessage> messages) {
	for (const PresentationMessage& message : messages)
		builder.add_static(message.context, message.english);
}

void add_formatter_messages(CatalogDefinitionBuilder& builder,
							std::span<const FormatterMessage> messages) {
	for (const FormatterMessage& message : messages)
		builder.add_plural(message.context, message.singular, message.plural);
}
}	 // namespace

bool CatalogDefinition::is_plural() const noexcept {
	return !plural.empty();
}

CatalogDefinitionsResult validate_catalog_definitions(
	// The definition vector is an intentional ownership sink returned on
	// success. cppcheck-suppress passedByValue
	std::vector<CatalogDefinition> definitions) {
	for (std::size_t index{}; index < definitions.size(); ++index) {
		const CatalogDefinition& definition = definitions[index];
		if (definition.context.empty() || definition.singular.empty()
			|| (definition.is_plural()
				&& definition.singular == definition.plural)) {
			return std::unexpected(CatalogDefinitionError{
				.technical_details = "A source localization definition has an "
									 "invalid context or message identity."});
		}
		for (std::size_t previous{}; previous < index; ++previous) {
			const CatalogDefinition& prior_definition = definitions[previous];
			if (prior_definition.context != definition.context)
				continue;
			if (prior_definition.singular == definition.singular
				&& prior_definition.plural == definition.plural) {
				return std::unexpected(CatalogDefinitionError{
					.technical_details =
						"Source localization definitions contain a duplicate "
						"context and source identity."});
			}
			return std::unexpected(CatalogDefinitionError{
				.technical_details =
					"Source localization definitions contain a duplicate "
					"context with conflicting source identities."});
		}
	}
	return definitions;
}

CatalogDefinitionsResult catalog_definitions() {
	CatalogDefinitionBuilder builder;
	add_static_messages(builder, static_messages());
	add_presentation_messages(builder, presentation_messages());
	add_presentation_messages(builder, catalog_warning_messages());
	add_formatter_messages(builder, plural_messages());
	builder.add_plural("common.photo_count", "photo", "photos");
	for (const PhotoWorkflowMessage& message :
		 photo_workflow_catalog_messages())
		builder.add_static(message.context, message.english);
	add_presentation_messages(builder, template_messages());
	for (const ProgressMessageDefinition& definition :
		 progress_message_definitions()) {
		builder.add_static(
			"progress." + std::string{definition.phase_code} + ".phase",
			definition.english_phase);
		builder.add_static(
			"progress." + std::string{definition.phase_code} + ".message",
			definition.english_message);
	}
	return std::move(builder).finish();
}
}	 // namespace shuba::localization::detail
