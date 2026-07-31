#include "Localization/Facade.hpp"

#include "Localization/Catalog.hpp"
#include "Localization/MessageCatalog.hpp"

#include <memory>
#include <utility>

namespace shuba::localization {
class Localization::Impl final {
public:
	explicit Impl(LocalizationInitialization initialization_value)
		: initialization_state(std::move(initialization_value)) {}

	LocalizationInitialization initialization_state;
	std::shared_ptr<const detail::Catalog> russian_catalog;
};

namespace {
[[nodiscard]] LocalizationInitialization english_initialization(
	Language requested_language, std::vector<LocalizationIssue> issues = {}) {
	return LocalizationInitialization{.requested_language = requested_language,
									  .active_language	  = Language::English,
									  .issues			  = std::move(issues)};
}
}	 // namespace

bool LocalizationInitialization::using_russian_catalog() const noexcept {
	return active_language == Language::Russian;
}

Localization::Localization(std::unique_ptr<Impl> implementation_value)
	: implementation(std::move(implementation_value)) {}

Localization Localization::make_english(Language requested_language,
										std::vector<LocalizationIssue> issues) {
	return Localization{std::make_unique<Impl>(
		english_initialization(requested_language, std::move(issues)))};
}

Localization::Localization(Localization&&) noexcept			   = default;
Localization& Localization::operator=(Localization&&) noexcept = default;
Localization::~Localization()								   = default;

const LocalizationInitialization& Localization::initialization()
	const noexcept {
	return implementation->initialization_state;
}

std::string Localization::text(MessageId message) const {
	const detail::StaticMessage& definition = detail::static_message(message);
	return translate_message(definition.context, definition.english);
}

std::string Localization::recovery_action_label(
	ui::RecoveryAction action) const {
	return text(detail::recovery_action_message(action));
}

std::string Localization::translate_message(std::string_view context,
											std::string_view english) const {
	if (implementation->russian_catalog == nullptr)
		return std::string{english};
	return implementation->russian_catalog->pgettext(context, english);
}

std::string Localization::translate_plural_message(std::string_view context,
												   std::string_view singular,
												   std::string_view plural,
												   std::uint64_t count) const {
	if (implementation->russian_catalog == nullptr)
		return std::string{count == 1U ? singular : plural};
	return implementation->russian_catalog->npgettext(context, singular, plural,
													  count);
}

std::string Localization::translate_photo_workflow_message(
	const PhotoWorkflowMessage& message) const {
	std::string translated =
		message.english_fallback.empty()
			? translate_message(message.context, message.english)
		: implementation->russian_catalog == nullptr
			? message.english_fallback
			: translate_message(message.context, message.english);
	for (const auto& [placeholder, value] : message.replacements) {
		std::size_t position{};
		while ((position = translated.find(placeholder, position))
			   != std::string::npos) {
			translated.replace(position, placeholder.size(), value);
			position += value.size();
		}
	}
	return translated;
}

std::string Localization::photo_workflow_text(
	PhotoWorkflowMessageId message) const {
	return translate_photo_workflow_message(photo_workflow_message(message));
}

std::string Localization::photo_import_completed(
	const PhotoImportCompletion& completion) const {
	return translate_photo_workflow_message(
		photo_import_completed_message(completion));
}

std::string Localization::pending_photo_staging_completed(
	const PendingPhotoStagingCompletion& completion) const {
	return translate_photo_workflow_message(
		pending_photo_staging_completed_message(completion));
}

std::string Localization::jpeg_export_completed(
	std::uint64_t bytes_written) const {
	return translate_photo_workflow_message(
		jpeg_export_completed_message(bytes_written));
}

std::string Localization::pending_save_photo_outcome(
	const PendingSavePhotoOutcome& outcome) const {
	return translate_photo_workflow_message(
		pending_save_photo_outcome_message(outcome));
}

std::string Localization::photo_deletion_outcome(
	const PhotoDeletionOutcome& outcome) const {
	return translate_photo_workflow_message(
		photo_deletion_outcome_message(outcome));
}

Localization make_localization(Language language,
							   std::string_view russian_catalog_bytes) {
	if (language == Language::English)
		return Localization::make_english(language);

	const detail::CatalogLoadResult loaded =
		detail::load_catalog(russian_catalog_bytes);
	if (!loaded.accepted()) {
		std::vector<LocalizationIssue> issues;
		issues.reserve(loaded.issues.size());
		for (const detail::CatalogIssue& issue : loaded.issues)
			issues.push_back(LocalizationIssue{
				.code			   = issue.code,
				.technical_details = issue.technical_details});
		return Localization::make_english(language, std::move(issues));
	}

	std::unique_ptr<Localization::Impl> implementation =
		std::make_unique<Localization::Impl>(
			LocalizationInitialization{.requested_language = language,
									   .active_language = Language::Russian});
	implementation->russian_catalog = loaded.catalog;
	return Localization{std::move(implementation)};
}
}	 // namespace shuba::localization
