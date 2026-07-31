#pragma once

#include "Catalog/Search.hpp"
#include "Domain/Domain.hpp"
#include "Localization/MessageId.hpp"
#include "Localization/Types.hpp"
#include "Persistence/JsonlCatalog.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace shuba::localization::detail {
struct StaticMessage final {
	MessageId id;
	std::string_view context;
	std::string_view english;
};

struct PresentationMessage final {
	std::string_view context;
	std::string_view english;
};

struct FormatterMessage final {
	std::string_view context;
	std::string_view singular;
	std::string_view plural;
};

enum class PresentationMessageIndex : std::uint8_t {
	ItemStatusDraft,
	ItemStatusPlanned,
	ItemStatusListed,
	ItemStatusSold,
	ItemStatusArchived,
	StorageLifecycleActive,
	StorageLifecycleArchived,
	PhotoPresenceUsable,
	PhotoPresenceNone,
	PhotoPresenceBroken,
	PhotoPresenceMixed,
	PhotoFilterAny,
	PhotoFilterHas,
	PhotoFilterNone,
	PhotoFilterBroken,
	PendingPhotoSelected,
	PendingPhotoStaged,
	PendingPhotoFailed,
	PendingPhotoCancelled,
	PendingPhotoRemoved,
	PendingPhotoConsumed,
	CatalogLoadNormal,
	CatalogLoadDegraded,
	CatalogLoadFatal,
	StartupSourceExisting,
	StartupSourceEmpty,
	StartupSourceDemo,
	StartupSourcePathFailed,
	StartupSourceInitializationFailed,
	StartupSourceLoadFailed,
	StartupSourceException,
	StartupSourceSafeMode,
};

enum class TemplateMessageIndex : std::uint8_t {
	TechnicalInformationHeading,
	AppliedCatalogFilterSummary,
	DraftCatalogFilterSummary,
	CatalogFilterSummaryNone,
	CatalogFilterSummaryCategories,
	CatalogFilterSummaryStatuses,
	CatalogFilterSummaryStorageUnassigned,
	CatalogFilterSummaryStorage,
	CatalogFilterSummaryNested,
	CatalogFilterSummaryPhotos,
	CatalogFilterSummaryListedShortcut,
	CatalogFilterSummarySoldShortcut,
	CatalogFilterSummaryIncludeArchived,
	ItemStorageField,
	ParentStorageField,
	MissingStorageLabel,
	ScreenListingSummary,
	ScreenFinanceSummary,
	StorageChoice,
	ShellStatus,
	ShellStatusDemo,
	ItemHeader,
	StorageHeader,
	ItemResultCard,
	StorageResultCard,
	PhotoDeckSelectionSummary,
	StoredPhotoCaption,
	StagedPhotoCaption,
	PreviewViewerZoomHint,
	ProgressTemplate,
	ProgressNoEvents,
	ProgressUnits,
	ProgressCurrentUnits,
	ProgressCancellable,
	ProgressNotCancellable,
};

enum class PluralMessageIndex : std::uint8_t {
	ResultCount,
	ItemCount,
	StagedPhotoCount,
	CatalogResultCount,
	DraftResultCount,
	PhotoDeckCurrent,
	PhotoDeckStaged,
};

[[nodiscard]] std::span<const StaticMessage> static_messages() noexcept;
[[nodiscard]] std::span<const PresentationMessage>
presentation_messages() noexcept;
[[nodiscard]] std::span<const PresentationMessage>
catalog_warning_messages() noexcept;
[[nodiscard]] std::span<const PresentationMessage> template_messages() noexcept;
[[nodiscard]] std::span<const FormatterMessage> plural_messages() noexcept;
[[nodiscard]] std::span<const ProgressMessageDefinition>
progress_message_definitions() noexcept;

[[nodiscard]] const StaticMessage& static_message(MessageId id) noexcept;
[[nodiscard]] const StaticMessage& static_template_message(
	MessageId id) noexcept;
[[nodiscard]] const PresentationMessage& presentation_message(
	PresentationMessageIndex index) noexcept;
[[nodiscard]] const PresentationMessage& template_message(
	TemplateMessageIndex index) noexcept;
[[nodiscard]] const FormatterMessage& plural_message(
	PluralMessageIndex index) noexcept;
[[nodiscard]] MessageId recovery_action_message(
	ui::RecoveryAction action) noexcept;

[[nodiscard]] PresentationMessageIndex presentation_message_index(
	domain::ItemStatus status) noexcept;
[[nodiscard]] PresentationMessageIndex presentation_message_index(
	domain::StorageLifecycleStatus status) noexcept;
[[nodiscard]] PresentationMessageIndex presentation_message_index(
	catalog::PhotoPresenceState state) noexcept;
[[nodiscard]] PresentationMessageIndex presentation_message_index(
	catalog::SearchPhotoPresenceFilter filter) noexcept;
[[nodiscard]] PresentationMessageIndex presentation_message_index(
	ui::PendingPhotoStatus status) noexcept;
[[nodiscard]] PresentationMessageIndex presentation_message_index(
	persistence::CatalogLoadStatus status) noexcept;
[[nodiscard]] PresentationMessageIndex presentation_message_index(
	ui::CatalogSessionStartupSource source) noexcept;
}	 // namespace shuba::localization::detail
