#include "Localization/CatalogDefinition.hpp"
#include "Localization/EmbeddedCatalog.hpp"
#include "Localization/Facade.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "Catalog/CatalogRepository.hpp"
#include "Catalog/Search.hpp"
#include "Domain/Domain.hpp"
#include "Persistence/JsonlCatalog.hpp"
#include "Platform/PlatformServices.hpp"
#include "UI/Session/CatalogSessionState.hpp"
#include "UI/Session/PhotoSessionTypes.hpp"
#include "UI/View/ScreenText.hpp"

namespace {
using shuba::localization::embedded_russian_catalog;
using shuba::localization::Language;
using shuba::localization::Localization;
using shuba::localization::make_localization;
using shuba::localization::MessageId;

[[nodiscard]] const shuba::localization::LocalizationIssue& require_issue(
	const Localization& localization, std::string_view expected_code) {
	const auto& issues = localization.initialization().issues;
	REQUIRE(issues.size() == 1U);
	REQUIRE(issues.front().code == expected_code);
	return issues.front();
}

void require_english_fallback(std::string_view catalog_bytes,
							  std::string_view expected_issue_code) {
	const Localization localization =
		make_localization(Language::Russian, catalog_bytes);
	REQUIRE_FALSE(localization.initialization().using_russian_catalog());
	REQUIRE(localization.initialization().active_language == Language::English);
	REQUIRE(localization.text(MessageId::Save) == "Save");
	REQUIRE(localization.text(MessageId::ClearFilters) == "Clear filters");
	REQUIRE(localization.text(MessageId::WorkflowPreviewUnavailable)
			== "Photo preview is unavailable.");
	REQUIRE(localization.photo_count(1) == "photo");
	REQUIRE(localization.photo_count(2) == "photos");
	static_cast<void>(require_issue(localization, expected_issue_code));
}

[[nodiscard]] std::string replace_once(std::string source,
									   std::string_view from,
									   std::string_view to) {
	const std::size_t position = source.find(from);
	REQUIRE(position != std::string::npos);
	source.replace(position, from.size(), to);
	return source;
}

[[nodiscard]] shuba::domain::MoneyAmount require_money(
	std::string_view amount, std::string_view currency) {
	const shuba::domain::MoneyParseResult parsed =
		shuba::domain::parse_money_amount(amount, currency);
	REQUIRE(parsed.valid());
	return parsed.value();
}

[[nodiscard]] shuba::core::StableIdentifier require_identifier(
	std::string text) {
	std::optional<shuba::core::StableIdentifier> identifier =
		shuba::core::StableIdentifier::try_create_file_safe(std::move(text));
	REQUIRE(identifier.has_value());
	return std::move(*identifier);
}
}	 // namespace

TEST_CASE("B28 accepts the embedded Russian production catalog atomically",
		  "[b28][localization][facade]") {
	const Localization localization =
		make_localization(Language::Russian, embedded_russian_catalog());

	REQUIRE(localization.initialization().using_russian_catalog());
	REQUIRE(localization.initialization().issues.empty());
	REQUIRE(localization.text(MessageId::Save) == "Сохранить");
	REQUIRE(localization.text(MessageId::Cancel) == "Отмена");
	REQUIRE(localization.text(MessageId::Close) == "Закрыть");
	REQUIRE(localization.text(MessageId::Filters) == "Фильтры");
	REQUIRE(localization.text(MessageId::ClearFilters) == "Очистить фильтры");
	REQUIRE(localization.text(MessageId::WorkflowPreviewUnavailable)
			== "Предпросмотр фотографии недоступен.");
	REQUIRE(localization.text(MessageId::PhotoOperationBusy)
			== "Операция с фотографиями уже выполняется.");
	REQUIRE(localization.text(MessageId::PhotoOperationCancel)
			== "Отменить операцию с фотографиями");
	REQUIRE(localization.technical_information_heading()
			== "Техническая информация");

	const std::array<std::pair<std::uint64_t, std::string_view>, 10> cases{{
		{0, "фотографий"},
		{1, "фотография"},
		{2, "фотографии"},
		{4, "фотографии"},
		{5, "фотографий"},
		{11, "фотографий"},
		{21, "фотография"},
		{22, "фотографии"},
		{25, "фотографий"},
		{1000011, "фотографий"},
	}};
	for (const auto& [count, expected] : cases) {
		CAPTURE(count);
		REQUIRE(localization.photo_count(count) == expected);
	}

	std::string caller_owned = localization.text(MessageId::Save);
	caller_owned.assign("caller-owned");
	REQUIRE(localization.text(MessageId::Save) == "Сохранить");
}

TEST_CASE("Z2 rejects duplicate source catalog contexts",
		  "[z2][localization][catalog]") {
	using shuba::localization::detail::CatalogDefinition;
	using shuba::localization::detail::CatalogDefinitionsResult;
	using shuba::localization::detail::validate_catalog_definitions;

	SECTION("identical source identity") {
		CatalogDefinitionsResult definitions = validate_catalog_definitions(
			{CatalogDefinition{.context = "test.context", .singular = "Source"},
			 CatalogDefinition{.context	 = "test.context",
							   .singular = "Source"}});

		REQUIRE_FALSE(definitions.has_value());
		REQUIRE(definitions.error().technical_details
			== "Source localization definitions contain a duplicate context and source identity.");
	}

	SECTION("conflicting source identity") {
		CatalogDefinitionsResult definitions = validate_catalog_definitions(
			{CatalogDefinition{.context	 = "test.context",
							   .singular = "First source"},
			 CatalogDefinition{.context	 = "test.context",
							   .singular = "Second source"}});

		REQUIRE_FALSE(definitions.has_value());
		REQUIRE(definitions.error().technical_details
			== "Source localization definitions contain a duplicate context with conflicting source identities.");
	}
}

TEST_CASE("B28 localizes complete C3 photo workflow outcomes",
		  "[b28][localization][facade][c3]") {
	const Localization english = make_localization(Language::English, {});
	const Localization russian =
		make_localization(Language::Russian, embedded_russian_catalog());

	const std::array<std::pair<std::uint64_t, std::string_view>, 9> jpeg_cases{{
		{0U, "байт"},
		{1U, "байт"},
		{2U, "байта"},
		{4U, "байта"},
		{5U, "байт"},
		{11U, "байт"},
		{21U, "байт"},
		{22U, "байта"},
		{25U, "байт"},
	}};
	for (const auto& [bytes, russian_unit] : jpeg_cases) {
		CAPTURE(bytes);
		REQUIRE(english.jpeg_export_completed(bytes)
				== "JPEG export completed: " + std::to_string(bytes)
					   + " bytes.");
		REQUIRE(russian.jpeg_export_completed(bytes)
				== "Экспорт JPEG завершён: " + std::to_string(bytes) + " "
					   + std::string{russian_unit} + ".");
	}

	const std::array<shuba::localization::PhotoImportCompletion, 3>
		import_cases{{
			{.imported_count = 1U, .failed_count = 2U},
			{.imported_count = 2U, .failed_count = 5U},
			{.imported_count = 21U, .failed_count = 22U},
		}};
	const std::array<std::string_view, 3> russian_import_expected{{
		"Импорт фотографий завершён: импортирована 1 фотография; не удалось "
		"импортировать 2 фотографии.",
		"Импорт фотографий завершён: импортированы 2 фотографии; не удалось "
		"импортировать 5 фотографий.",
		"Импорт фотографий завершён: импортирована 21 фотография; не удалось "
		"импортировать 22 фотографии.",
	}};
	for (std::size_t index{}; index < import_cases.size(); ++index) {
		CAPTURE(index);
		REQUIRE(russian.photo_import_completed(import_cases[index])
				== russian_import_expected[index]);
	}

	const shuba::localization::PendingSavePhotoOutcome completed_item{
		.owner = shuba::localization::PendingSavePhotoOwner::Item,
		.import_state =
			shuba::localization::PendingSavePhotoImportState::Completed,
		.imported_count = 21U,
		.failed_count	= 22U,
		.cleanup_state =
			shuba::localization::PendingSaveCleanupState::NeedsAttention,
		.main_photo_state =
			shuba::localization::PendingSaveMainPhotoState::Applied,
	};
	REQUIRE(english.pending_save_photo_outcome(completed_item)
		== "Item saved. Pending photo import completed: 21 photos imported; "
			"22 photos failed. Pending source cleanup needs attention. "
			"Selected staged photo is now main.");
	REQUIRE(russian.pending_save_photo_outcome(completed_item)
		== "Предмет сохранён. Импорт ожидающих фотографий завершён: импортирована 21 "
			"фотография; не удалось импортировать 22 фотографии. Очистка исходных "
			"файлов ожидающих фотографий требует внимания. Выбранная подготовленная "
			"фотография стала главной.");

	const shuba::localization::PhotoDeletionOutcome deletion{
		.state = shuba::localization::PhotoDeletionState::Deleted,
		.media_cleanup_needs_attention = true,
	};
	REQUIRE(english.photo_deletion_outcome(deletion)
			== "Photo deleted. Pending source cleanup needs attention.");
	REQUIRE(russian.photo_deletion_outcome(deletion)
		== "Фотография удалена. Очистка исходных файлов ожидающих фотографий требует внимания.");
}

TEST_CASE(
	"B28 uses compiled English without parsing a Russian catalog in English "
	"mode",
	"[b28][localization][facade]") {
	const Localization localization =
		make_localization(Language::English, "not a PO file");

	REQUIRE_FALSE(localization.initialization().using_russian_catalog());
	REQUIRE(localization.initialization().issues.empty());
	REQUIRE(localization.text(MessageId::Save) == "Save");
	REQUIRE(localization.text(MessageId::WorkflowPreviewUnavailable)
			== "Photo preview is unavailable.");
	REQUIRE(localization.technical_information_heading()
			== "Technical information");
	REQUIRE(localization.photo_count(1) == "photo");
	REQUIRE(localization.photo_count(3) == "photos");
}

TEST_CASE("B28 rejects invalid Russian catalogs atomically",
		  "[b28][localization][facade]") {
	const std::string valid_catalog{embedded_russian_catalog()};

	SECTION("empty catalog") {
		require_english_fallback({}, "localization-catalog-empty");
	}

	SECTION("embedded NUL") {
		std::string catalog = valid_catalog;
		catalog.insert(12, 1, '\0');
		require_english_fallback(catalog, "localization-catalog-embedded-nul");
	}

	SECTION("invalid UTF-8") {
		std::string catalog = valid_catalog;
		catalog.insert(12, "\xC3\x28");
		require_english_fallback(catalog, "localization-catalog-invalid-utf8");
	}

	SECTION("malformed PO") {
		require_english_fallback("msgid \"unterminated\n",
								 "localization-catalog-parse-failed");
	}

	SECTION("wrong metadata") {
		const std::string catalog =
			replace_once(valid_catalog, "Language: ru", "Language: en");
		require_english_fallback(catalog,
								 "localization-catalog-metadata-invalid");
	}

	SECTION("duplicate entry") {
		const std::string catalog = valid_catalog
			+ "\nmsgctxt \"common.action.save\"\nmsgid \"Save\"\nmsgstr \"Повтор\"\n";
		require_english_fallback(
			catalog, "localization-catalog-duplicate-or-metadata-invalid");
	}

	SECTION("missing approved entry") {
		const std::string catalog =
			replace_once(valid_catalog,
						 "msgctxt \"common.action.filters\"\nmsgid "
						 "\"Filters\"\nmsgstr \"Фильтры\"\n\n",
						 "");
		require_english_fallback(
			catalog, "localization-catalog-source-definition-mismatch");
	}

	SECTION("unexpected entry") {
		const std::string catalog = valid_catalog
			+ "\nmsgctxt \"unexpected\"\nmsgid \"Unexpected\"\nmsgstr \"Неожиданно\"\n";
		require_english_fallback(
			catalog, "localization-catalog-source-definition-mismatch");
	}

	SECTION("wrong context") {
		const std::string catalog = replace_once(
			valid_catalog, "common.action.filters", "common.action.other");
		require_english_fallback(
			catalog, "localization-catalog-source-definition-mismatch");
	}

	SECTION("missing plural form") {
		const std::string catalog =
			replace_once(valid_catalog, "msgstr[2] \"фотографий\"\n", "");
		require_english_fallback(
			catalog, "localization-catalog-source-definition-mismatch");
	}
}

TEST_CASE("B28 serializes concurrent production facade lookups",
		  "[b28][localization][facade]") {
	const Localization localization =
		make_localization(Language::Russian, embedded_russian_catalog());
	REQUIRE(localization.initialization().using_russian_catalog());

	std::atomic<bool> valid{true};
	std::vector<std::thread> workers;
	workers.reserve(8);
	for (std::uint64_t worker_index{}; worker_index < 8U; ++worker_index) {
		workers.emplace_back([&localization, &valid, worker_index] {
			for (std::uint64_t iteration{}; iteration < 4000U; ++iteration) {
				const std::uint64_t count = worker_index * 4000U + iteration;
				const std::string plural  = localization.photo_count(count);
				if ((plural != "фотография" && plural != "фотографии"
					 && plural != "фотографий")
					|| localization.text(MessageId::Save) != "Сохранить") {
					valid.store(false, std::memory_order_relaxed);
				}
			}
		});
	}

	for (std::thread& worker : workers)
		worker.join();

	REQUIRE(valid.load(std::memory_order_relaxed));
}

TEST_CASE(
	"B28 localizes presentation labels without changing canonical enum tokens",
	"[b28][localization][facade][presentation]") {
	const Localization english = make_localization(Language::English, {});
	const Localization russian =
		make_localization(Language::Russian, embedded_russian_catalog());

	REQUIRE(english.item_status_label(shuba::domain::ItemStatus::Draft)
			== "Draft");
	REQUIRE(english.item_status_label(shuba::domain::ItemStatus::Sold)
			== "Sold");
	REQUIRE(russian.item_status_label(shuba::domain::ItemStatus::Draft)
			== "Черновик");
	REQUIRE(russian.item_status_label(shuba::domain::ItemStatus::Sold)
			== "Продано");
	REQUIRE(english.storage_lifecycle_label(
				shuba::domain::StorageLifecycleStatus::Active)
			== "Active");
	REQUIRE(russian.storage_lifecycle_label(
				shuba::domain::StorageLifecycleStatus::Archived)
			== "В архиве");

	REQUIRE(english.photo_presence_label(
				shuba::catalog::PhotoPresenceState::OnlyBrokenPhotos)
			== "Photos need attention");
	REQUIRE(russian.photo_presence_label(
				shuba::catalog::PhotoPresenceState::MixedUsableAndBrokenPhotos)
			== "Некоторые фотографии требуют внимания");
	REQUIRE(english.photo_filter_label(
				shuba::catalog::SearchPhotoPresenceFilter::Any)
			== "Any photo state");
	REQUIRE(russian.photo_filter_label(
				shuba::catalog::SearchPhotoPresenceFilter::BrokenPhotos)
			== "Повреждённые фотографии");
	REQUIRE(english.pending_photo_status_label(
				shuba::ui::PendingPhotoStatus::Staged)
			== "Staged");
	REQUIRE(russian.pending_photo_status_label(
				shuba::ui::PendingPhotoStatus::Consumed)
			== "Импортировано");
	REQUIRE(english.catalog_load_status_label(
				shuba::persistence::CatalogLoadStatus::Degraded)
			== "Loaded with warnings");
	REQUIRE(russian.catalog_load_status_label(
				shuba::persistence::CatalogLoadStatus::Fatal)
			== "Не удалось загрузить");
	REQUIRE(english.startup_source_label(
				shuba::ui::CatalogSessionStartupSource::InitializedEmptyCatalog)
			== "New empty catalog");
	REQUIRE(russian.startup_source_label(
				shuba::ui::CatalogSessionStartupSource::StartupCrashSafeMode)
			== "Безопасный режим запуска");

	REQUIRE(shuba::domain::to_string(shuba::domain::ItemStatus::Draft)
			== "draft");
	REQUIRE(shuba::domain::to_string(
				shuba::domain::StorageLifecycleStatus::Archived)
			== "archived");
	REQUIRE(shuba::catalog::to_string(
				shuba::catalog::SearchPhotoPresenceFilter::BrokenPhotos)
			== "broken photos");
	REQUIRE(shuba::persistence::to_string(
				shuba::persistence::CatalogLoadStatus::Degraded)
			== "degraded");
}

TEST_CASE(
	"C4 localizes recovery and maintenance presentation around stable state",
	"[c4][localization][facade][recovery]") {
	const Localization english = make_localization(Language::English, {});
	const Localization russian =
		make_localization(Language::Russian, embedded_russian_catalog());
	const std::array actions{shuba::ui::RecoveryAction::ExportDiagnosticArchive,
							 shuba::ui::RecoveryAction::ImportBackup,
							 shuba::ui::RecoveryAction::RetryNormalLaunch,
							 shuba::ui::RecoveryAction::Exit};
	const shuba::localization::RecoveryCountsFields counts{
		.accepted_items	   = 12U,
		.accepted_storages = 3U,
		.accepted_photos   = 9U,
		.skipped_items	   = 2U,
		.skipped_storages  = 1U,
		.skipped_photos	   = 4U,
		.broken_references = 5U,
		.orphan_media	   = 6U};
	const shuba::localization::ImportValidationFields english_validation{
		.load_status = english.catalog_load_status_label(
			shuba::persistence::CatalogLoadStatus::Degraded),
		.accepted_items	   = 12U,
		.accepted_storages = 3U,
		.accepted_photos   = 9U,
		.broken_references = 5U,
		.orphan_media	   = 6U};
	const shuba::localization::ImportValidationFields russian_validation{
		.load_status = russian.catalog_load_status_label(
			shuba::persistence::CatalogLoadStatus::Degraded),
		.accepted_items	   = 12U,
		.accepted_storages = 3U,
		.accepted_photos   = 9U,
		.broken_references = 5U,
		.orphan_media	   = 6U};

	REQUIRE(
		english.recovery_action_label(shuba::ui::RecoveryAction::ImportBackup)
		== "Import backup ZIP");
	REQUIRE(
		russian.recovery_action_label(shuba::ui::RecoveryAction::ImportBackup)
		== "Импортировать ZIP резервной копии");
	REQUIRE(english.recovery_actions(actions)
		== "Safe recovery actions: Export diagnostic archive · Import backup ZIP · Retry normal launch · Exit");
	REQUIRE(russian.recovery_actions(actions)
		== "Безопасные действия восстановления: Экспортировать диагностический архив · Импортировать ZIP резервной копии · Повторить обычный запуск · Выйти");
	REQUIRE(english.recovery_counts(counts)
		== "Accepted: items=12 · storages=3 · photos=9 · skipped lines: items=2 storages=1 photos=4 · broken refs=5 · orphan media=6");
	REQUIRE(russian.recovery_counts(counts)
		== "Принято: предметов=12 · хранилищ=3 · фотографий=9 · пропущенные строки: предметов=2 хранилищ=1 фотографий=4 · битые ссылки=5 · потерянные медиафайлы=6");
	REQUIRE(english.import_validation_summary(english_validation)
		== "Staged import: Loaded with warnings · items=12 · storages=3 · photos=9 · broken refs=5 · orphan media=6");
	REQUIRE(russian.import_validation_summary(russian_validation)
		== "Подготовленный импорт: Загружено с предупреждениями · предметов=12 · хранилищ=3 · фотографий=9 · битые ссылки=5 · потерянные медиафайлы=6");
	REQUIRE(russian.text(MessageId::RecoveryFatalGuidance)
		== "Аварийное восстановление никогда не перезаписывает данные автоматически. Импорт резервной копии по-прежнему использует подготовку, проверку и явное подтверждение замены.");
}

TEST_CASE("C5 formats the complete shell status from localized state labels",
		  "[c5][localization][facade][shell-status]") {
	const Localization english = make_localization(Language::English, {});
	const Localization russian =
		make_localization(Language::Russian, embedded_russian_catalog());
	const shuba::localization::ShellStatusFields ordinary{
		.load_status = shuba::persistence::CatalogLoadStatus::Degraded,
		.source		 = shuba::ui::CatalogSessionStartupSource::ExistingCatalog,
		.item_count	 = 7U,
		.storage_count = 5U};
	const shuba::localization::ShellStatusFields demo{
		.load_status = shuba::persistence::CatalogLoadStatus::Normal,
		.source		= shuba::ui::CatalogSessionStartupSource::SeededDemoCatalog,
		.item_count = 7U,
		.storage_count		 = 5U,
		.demo_catalog_active = true};

	REQUIRE(english.shell_status(ordinary)
		== "Load: Loaded with warnings · Existing catalog · items=7 · storages=5");
	REQUIRE(russian.shell_status(ordinary)
		== "Загрузка: Загружено с предупреждениями · Существующий каталог · предметов=7 · хранилищ=5");
	REQUIRE(english.shell_status(demo)
		== "Load: Loaded normally · Demo catalog · items=7 · storages=5 · demo catalog");
	REQUIRE(russian.shell_status(demo)
		== "Загрузка: Загружено без ошибок · Демонстрационный каталог · предметов=7 · хранилищ=5 · демонстрационный каталог");
	REQUIRE(english.text(MessageId::PreviewPlaceholderEmpty)
			== "No photos yet.");
	REQUIRE(russian.text(MessageId::PreviewPlaceholderEmpty)
			== "Фотографий пока нет.");
}

TEST_CASE("B28 formats complete count and progress messages in both languages",
		  "[b28][localization][facade][formatters]") {
	const Localization english = make_localization(Language::English, {});
	const Localization russian =
		make_localization(Language::Russian, embedded_russian_catalog());

	REQUIRE(english.result_count(1U) == "1 result");
	REQUIRE(english.result_count(2U) == "2 results");
	REQUIRE(english.item_count(1U) == "1 item");
	REQUIRE(english.item_count(5U) == "5 items");
	REQUIRE(english.staged_photo_count(1U) == "1 photo staged");
	REQUIRE(english.staged_photo_count(2U) == "2 photos staged");

	const std::array<std::pair<std::uint64_t, std::string_view>, 5>
		result_cases{{
			{0U, "0 результатов"},
			{1U, "1 результат"},
			{2U, "2 результата"},
			{5U, "5 результатов"},
			{21U, "21 результат"},
		}};
	for (const auto& [count, expected] : result_cases) {
		CAPTURE(count);
		REQUIRE(russian.result_count(count) == expected);
	}

	REQUIRE(russian.item_count(1U) == "1 предмет");
	REQUIRE(russian.item_count(2U) == "2 предмета");
	REQUIRE(russian.item_count(5U) == "5 предметов");
	REQUIRE(russian.staged_photo_count(1U) == "подготовлена 1 фотография");
	REQUIRE(russian.staged_photo_count(2U) == "подготовлены 2 фотографии");
	REQUIRE(russian.staged_photo_count(5U) == "подготовлено 5 фотографий");

	REQUIRE(english.draft_result_count(1U) == "Draft results: 1 result");
	REQUIRE(english.draft_result_count(2U) == "Draft results: 2 results");
	REQUIRE(english.photo_deck_tab(false, 1U) == "Current: 1 photo");
	REQUIRE(english.photo_deck_tab(false, 2U) == "Current: 2 photos");
	REQUIRE(english.photo_deck_tab(true, 1U) == "Staged: 1 photo");
	REQUIRE(english.photo_deck_tab(true, 5U) == "Staged: 5 photos");
	REQUIRE(english.photo_deck_selection_summary(false, 2U, 5U, 7U)
			== "Current photo 2 of 5; total photos: 7");
	REQUIRE(english.photo_deck_selection_summary(true, 3U, 4U, 9U)
			== "Staged photo 3 of 4; total photos: 9");
	REQUIRE(english.photo_position(false, 2U, 7U) == "Stored photo 2 of 7");
	REQUIRE(english.photo_position(true, 3U, 9U) == "Staged photo 3 of 9");
	REQUIRE(english.preview_viewer_zoom_hint(2.5f)
			== "Zoom 2.50x; drag to pan; double-tap to fit.");

	REQUIRE(russian.draft_result_count(1U) == "1 результат чернового поиска");
	REQUIRE(russian.draft_result_count(5U) == "5 результатов чернового поиска");
	REQUIRE(russian.photo_deck_tab(false, 1U) == "Текущие: 1 фотография");
	REQUIRE(russian.photo_deck_tab(false, 5U) == "Текущие: 5 фотографий");
	REQUIRE(russian.photo_deck_tab(true, 2U) == "Подготовлено: 2 фотографии");
	REQUIRE(russian.photo_deck_tab(true, 5U) == "Подготовлено: 5 фотографий");
	REQUIRE(russian.photo_deck_selection_summary(false, 2U, 5U, 7U)
			== "Current: фотография 2 из 5; всего фотографий: 7");
	REQUIRE(russian.photo_deck_selection_summary(true, 3U, 4U, 9U)
			== "Staged: фотография 3 из 4; всего фотографий: 9");
	REQUIRE(russian.photo_position(false, 2U, 7U)
			== "Сохранённая фотография 2 из 7");
	REQUIRE(russian.photo_position(true, 3U, 9U)
			== "Подготовленная фотография 3 из 9");
	REQUIRE(russian.preview_viewer_zoom_hint(2.5f)
		== "Масштаб 2.50x; перетаскивайте для панорамирования; дважды коснитесь для подгонки.");

	REQUIRE(english.catalog_result_count(1U) == "Results: 1 result");
	REQUIRE(english.catalog_result_count(2U) == "Results: 2 results");
	REQUIRE(russian.catalog_result_count(1U) == "Результаты: 1 результат");
	REQUIRE(russian.catalog_result_count(5U) == "Результаты: 5 результатов");
	REQUIRE(english.catalog_filter_summary(
				shuba::localization::CatalogFilterSummaryKind::Applied,
				"No active filters")
			== "Active filters: No active filters");
	REQUIRE(english.catalog_filter_summary(
				shuba::localization::CatalogFilterSummaryKind::Draft,
				"No active filters")
			== "Draft filters: No active filters");
	REQUIRE(russian.catalog_filter_summary(
				shuba::localization::CatalogFilterSummaryKind::Applied,
				"Категории: Обувь")
			== "Активные фильтры: Категории: Обувь");
	REQUIRE(russian.catalog_filter_summary(
				shuba::localization::CatalogFilterSummaryKind::Draft,
				"Категории: Обувь")
			== "Черновые фильтры: Категории: Обувь");
	REQUIRE(english.item_storage_field("Shelf A") == "Storage: Shelf A");
	REQUIRE(russian.item_storage_field("Полка А") == "Хранилище: Полка А");
	REQUIRE(english.parent_storage_field("Home") == "Parent storage: Home");
	REQUIRE(russian.parent_storage_field("Дом")
			== "Родительское хранилище: Дом");
	REQUIRE(english.missing_storage_label("storage-42")
			== "Storage not found: storage-42");
	REQUIRE(russian.missing_storage_label("storage-42")
			== "Хранилище не найдено: storage-42");
	REQUIRE(english.field_value("Notes", "linen") == "Notes: linen");
	REQUIRE(russian.field_value("Заметки", "лён") == "Заметки: лён");
	REQUIRE(english.tags_summary("brand=Acme") == "Tags: brand=Acme");
	REQUIRE(russian.tags_summary("brand=Acme") == "Теги: brand=Acme");
	REQUIRE(english.listing_summary("Kufar", "https://example.test/item",
		"50.00 BYN", "linen")
		== "Listing: marketplace Kufar; URL https://example.test/item; price "
		   "50.00 BYN; note linen");
	REQUIRE(russian.listing_summary("Kufar", "https://example.test/item",
		"50.00 BYN", "лён")
		== "Объявление: площадка Kufar; URL https://example.test/item; цена "
		   "50.00 BYN; заметка лён");
	REQUIRE(english.finance_summary("gift", "10.00 BYN", "50.00 BYN",
		"5.00 BYN", "35.00 BYN")
		== "Finance: source gift; acquisition cost 10.00 BYN; sale price "
		   "50.00 BYN; expenses 5.00 BYN; profit 35.00 BYN");
	REQUIRE(russian.finance_summary("подарок", "10.00 BYN", "50.00 BYN",
		"5.00 BYN", "35.00 BYN")
		== "Финансы: источник подарок; стоимость приобретения 10.00 BYN; цена "
		   "продажи 50.00 BYN; расходы 5.00 BYN; прибыль 35.00 BYN");
	REQUIRE(english.storage_choice("Wardrobe", "shelf", "Home / Wardrobe")
			== "Storage: Wardrobe; type: shelf; location: Home / Wardrobe");
	REQUIRE(russian.storage_choice("Шкаф", "полка", "Дом / Шкаф")
			== "Хранилище: Шкаф; тип: полка; расположение: Дом / Шкаф");
	REQUIRE(english.catalog_warning_label(
				shuba::localization::CatalogWarning::BrokenStorage)
			== "broken storage");
	REQUIRE(russian.catalog_warning_label(
				shuba::localization::CatalogWarning::ArchivedStorage)
			== "архивное хранилище");
	REQUIRE(english.item_header(shuba::localization::ItemHeaderFields{
		.name = "Dress",
		.photo_state = "Photos available",
		.category = "clothing",
		.status = "Listed",
		.storage_path = "Home / Wardrobe",
		.warnings = "archived storage"})
		== "Item: Dress; photo state: Photos available; category: clothing; "
		   "status: Listed; storage: Home / Wardrobe; warnings: archived storage");
	REQUIRE(russian.item_header(shuba::localization::ItemHeaderFields{
		.name = "Платье",
		.photo_state = "Фотографии доступны",
		.category = "одежда",
		.status = "Выставлено",
		.storage_path = "Дом / Шкаф",
		.warnings = "архивное хранилище"})
		== "Предмет: Платье; состояние фотографий: Фотографии доступны; "
		   "категория: одежда; статус: Выставлено; хранилище: Дом / Шкаф; "
		   "предупреждения: архивное хранилище");
	REQUIRE(russian.storage_header(shuba::localization::StorageHeaderFields{
		.name = "Шкаф",
		.type = "wardrobe",
		.path = "Дом / Шкаф",
		.location = "Спальня",
		.notes = "Сезонная одежда",
		.warnings = "родительское хранилище с ошибкой"})
		== "Хранилище: Шкаф; тип: wardrobe; путь: Дом / Шкаф; расположение: "
		   "Спальня; заметки: Сезонная одежда; предупреждения: родительское "
		   "хранилище с ошибкой");
	REQUIRE(russian.item_result_card(shuba::localization::ItemResultFields{
		.title = "Платье",
		.photo_state = "Фотографии доступны",
		.category = "одежда",
		.status = "Выставлено",
		.location = "Дом / Шкаф",
		.details = "лён",
		.warnings = "архивное хранилище"})
		== "Предмет: Платье; состояние фотографий: Фотографии доступны; "
		   "категория: одежда; статус: Выставлено; место: Дом / Шкаф; "
		   "сведения: лён; предупреждения: архивное хранилище");
	REQUIRE(russian.item_result_card(shuba::localization::ItemResultFields{
				.title = "Платье", .photo_state = "Фотографии доступны"})
			== "Предмет: Платье; состояние фотографий: Фотографии доступны");
	REQUIRE(english.storage_result_card(
		shuba::localization::StorageResultFields{
			.title = "Wardrobe",
			.type = "wardrobe",
			.lifecycle = "Active",
			.location = "Home / Wardrobe",
			.direct_children = 2U,
			.direct_items = 3U,
			.nested_items = 5U,
			.details = "seasonal",
			.warnings = "broken parent"})
		== "Storage: Wardrobe; type: wardrobe; lifecycle: Active; location: "
		   "Home / Wardrobe; child storages: 2; items: 3 direct, 5 including "
		   "nested contents; details: seasonal; warnings: broken parent");
	REQUIRE(russian.storage_result_card(
		shuba::localization::StorageResultFields{
			.title = "Шкаф",
			.type = "wardrobe",
			.lifecycle = "Активно",
			.location = "Дом / Шкаф",
			.direct_children = 2U,
			.direct_items = 3U,
			.nested_items = 5U,
			.details = "сезонное",
			.warnings = "родительское хранилище с ошибкой"})
		== "Хранилище: Шкаф; тип: wardrobe; состояние: Активно; место: Дом / "
		   "Шкаф; дочерние хранилища: 2; предметы: 3 напрямую, 5 с учётом "
		   "вложенного содержимого; сведения: сезонное; предупреждения: "
		   "родительское хранилище с ошибкой");
	REQUIRE(english.storage_result_card(
		shuba::localization::StorageResultFields{
			.title = "Wardrobe",
			.direct_children = 2U,
			.direct_items = 3U,
			.nested_items = 5U})
		== "Storage: Wardrobe; child storages: 2; items: 3 direct, 5 including "
		   "nested contents");

	const shuba::localization::ProgressSummary progress{
		.phase			   = "Import",
		.message		   = "Copying",
		.current_units	   = 2U,
		.total_units	   = 5U,
		.has_current_units = true,
		.has_total_units   = true,
		.cancellable	   = true};
	REQUIRE(english.progress_summary(progress)
			== "Progress: Import · Copying · 2/5 · cancellable");
	REQUIRE(russian.progress_summary(progress)
			== "Ход выполнения: Import · Copying · 2/5 · можно отменить");

	const shuba::localization::ProgressSummary non_cancellable_progress{
		.phase			   = "Экспорт",
		.message		   = "Запись",
		.current_units	   = 7U,
		.has_current_units = true,
		.cancellable	   = false};
	REQUIRE(english.progress_summary(non_cancellable_progress)
			== "Progress: Экспорт · Запись · 7 · not cancellable");
	REQUIRE(russian.progress_summary(non_cancellable_progress)
			== "Ход выполнения: Экспорт · Запись · 7 · нельзя отменить");
}

TEST_CASE("Z3 localizes typed progress events and preserves raw fallback",
		  "[z3][localization][progress]") {
	const Localization english = make_localization(Language::English, {});
	const Localization russian =
		make_localization(Language::Russian, embedded_russian_catalog());
	const std::optional<shuba::core::OperationIdentifier> operation =
		shuba::core::OperationIdentifier::try_create_file_safe(
			"operation-z3-progress");
	REQUIRE(operation.has_value());

	const shuba::platform::ProgressEvent known{
		.operation_id	= *operation,
		.operation_type = shuba::platform::ProgressOperationType::PhotoImport,
		.phase			= "photo-import-started",
		.message_id	   = shuba::platform::ProgressMessageId::PhotoImportStarted,
		.current_units = 0U,
		.total_units   = 3U,
		.message	   = "Photo import started.",
		.cancellable   = true};
	REQUIRE(
		english.progress_summary(known)
		== "Progress: Photo import · Photo import started. · 0/3 · "
		   "cancellable");
	REQUIRE(russian.progress_summary(known)
		== "Ход выполнения: Импорт фотографий · Импорт фотографий начат. · "
			"0/3 · можно отменить");
	const std::array<shuba::platform::ProgressEvent, 1> known_events{known};
	REQUIRE(shuba::ui::progress_summary(known_events, russian)
		== "Ход выполнения: Импорт фотографий · Импорт фотографий начат. · "
			"0/3 · можно отменить");

	const shuba::platform::ProgressEvent unknown{
		.operation_id	= *operation,
		.operation_type = shuba::platform::ProgressOperationType::BackupExport,
		.phase			= "opaque-phase",
		.current_units	= 7U,
		.message		= "Opaque technical message.",
		.cancellable	= false};
	REQUIRE(english.progress_summary(unknown)
		== "Progress: opaque-phase · Opaque technical message. · 7 · not cancellable");
	REQUIRE(russian.progress_summary(unknown)
		== "Ход выполнения: opaque-phase · Opaque technical message. · 7 · "
			"нельзя отменить");
	const std::array<shuba::platform::ProgressEvent, 0> no_events{};
	REQUIRE(shuba::ui::progress_summary(no_events, russian)
			== "Ход выполнения: события пока не поступали.");
	REQUIRE(english.progress_no_events()
			== "Progress: no events reported yet.");
	REQUIRE(russian.progress_no_events()
			== "Ход выполнения: события пока не поступали.");
}

TEST_CASE("B28 formats typed active-filter summaries deterministically",
		  "[b28][localization][facade][formatters][filters]") {
	const Localization english = make_localization(Language::English, {});
	const Localization russian =
		make_localization(Language::Russian, embedded_russian_catalog());

	REQUIRE(english.catalog_filter_clauses({}) == "No active filters");
	REQUIRE(russian.catalog_filter_clauses({}) == "Активных фильтров нет");

	const shuba::localization::CatalogFilterSummaryFields complete{
		.categories				= {"Обувь", "Accessories"},
		.statuses				= {"Выставлено", "Продано"},
		.storage				= "Шкаф",
		.storage_unassigned		= true,
		.include_nested_storage = true,
		.photo_presence			= "Есть фотографии",
		.listed_shortcut		= true,
		.sold_shortcut			= true,
		.include_archived		= true};
	REQUIRE(english.catalog_filter_clauses(complete)
		== "Categories: Обувь, Accessories · Statuses: Выставлено, Продано · "
		   "Storage: Шкаф · Including nested contents · Photos: Есть фотографии · "
		   "Listed shortcut · Sold shortcut · Including archived records");
	REQUIRE(russian.catalog_filter_clauses(complete)
		== "Категории: Обувь, Accessories · Статусы: Выставлено, Продано · "
		   "Хранилище: Шкаф · С учётом вложенного содержимого · Фотографии: "
		   "Есть фотографии · Быстрый фильтр: выставлено · Быстрый фильтр: "
		   "продано · С архивными записями");

	const shuba::localization::CatalogFilterSummaryFields defensive{
		.categories				= {"", "A", "", "Б"},
		.statuses				= {"", "Draft"},
		.storage_unassigned		= true,
		.include_nested_storage = true,
		.photo_presence			= std::string{}};
	REQUIRE(english.catalog_filter_clauses(defensive)
			== "Categories: A, Б · Statuses: Draft · Storage: unassigned");
	REQUIRE(russian.catalog_filter_clauses(defensive)
			== "Категории: A, Б · Статусы: Draft · Хранилище: не назначено");

	const std::string russian_clauses =
		russian.catalog_filter_clauses(complete);
	REQUIRE(russian.catalog_filter_summary(
				shuba::localization::CatalogFilterSummaryKind::Applied,
				russian_clauses)
			== "Активные фильтры: " + russian_clauses);
	REQUIRE(russian.catalog_filter_summary(
				shuba::localization::CatalogFilterSummaryKind::Draft,
				russian_clauses)
			== "Черновые фильтры: " + russian_clauses);
}

TEST_CASE("B28 adapts catalog filter state without changing search semantics",
		  "[b28][localization][facade][formatters][filters]") {
	const Localization english = make_localization(Language::English, {});
	const Localization russian =
		make_localization(Language::Russian, embedded_russian_catalog());
	shuba::catalog::CatalogRepositoryState repository;
	const shuba::persistence::StorageEnvelope storage{
		.record = shuba::domain::StorageRecord{
			.id = require_identifier("wardrobe"), .display_name = "Шкаф"}};
	repository = shuba::catalog::build_catalog_repository(
		shuba::catalog::CatalogRepositoryInput{.storages = {storage}});

	shuba::catalog::CatalogSearchFilters filters{
		.categories				 = {"Обувь", "одежда"},
		.statuses				 = {shuba::domain::ItemStatus::Listed,
									shuba::domain::ItemStatus::Sold},
		.include_archived		 = true,
		.storage_id				 = storage.record.id,
		.storage_unassigned_only = true,
		.include_nested_storage	 = true,
		.photo_presence =
			shuba::catalog::SearchPhotoPresenceFilter::BrokenPhotos,
		.listed_only = true,
		.sold_only	 = true};
	REQUIRE(shuba::ui::has_catalog_filters(filters));
	REQUIRE(shuba::ui::active_filter_summary(filters, repository, russian)
		== "Категории: Обувь, одежда · Статусы: Выставлено, Продано · "
		   "Хранилище: Шкаф · С учётом вложенного содержимого · Фотографии: "
		   "Повреждённые фотографии · Быстрый фильтр: выставлено · Быстрый "
		   "фильтр: продано · С архивными записями");

	filters.storage_id			   = require_identifier("missing-storage");
	filters.include_nested_storage = false;
	REQUIRE(shuba::ui::active_filter_summary(filters, repository, english)
		== "Categories: Обувь, одежда · Statuses: Listed, Sold · Storage: "
		   "missing-storage · Photos: Broken photos · Listed shortcut · Sold "
		   "shortcut · Including archived records");

	shuba::catalog::CatalogSearchFilters defaults;
	REQUIRE_FALSE(shuba::ui::has_catalog_filters(defaults));
	REQUIRE(shuba::ui::active_filter_summary(defaults, repository, russian)
			== "Активных фильтров нет");
}

TEST_CASE("B28 localizes listing and finance summaries while preserving data",
		  "[b28][localization][facade][formatters]") {
	const Localization english = make_localization(Language::English, {});
	const Localization russian =
		make_localization(Language::Russian, embedded_russian_catalog());
	const shuba::domain::ListingData listing{
		.marketplace = "Kufar",
		.url		 = "https://example.test/item",
		.price		 = require_money("50.00", "BYN"),
		.note		 = "лён"};
	const shuba::domain::AcquisitionData acquisition{
		.source = "подарок", .cost = require_money("10.00", "BYN")};
	const shuba::domain::FinanceData finance{
		.real_sale_price = require_money("50.00", "BYN"),
		.expenses_total	 = require_money("5.00", "BYN")};

	REQUIRE(shuba::ui::listing_summary(listing, english)
		== "Listing: marketplace Kufar; URL https://example.test/item; price "
		   "50 BYN; note лён");
	REQUIRE(shuba::ui::listing_summary(listing, russian)
		== "Объявление: площадка Kufar; URL https://example.test/item; цена "
		   "50 BYN; заметка лён");
	REQUIRE(shuba::ui::finance_summary(acquisition, finance, english)
		== "Finance: source подарок; acquisition cost 10 BYN; sale price "
		   "50 BYN; expenses 5 BYN; profit 35 BYN");
	REQUIRE(shuba::ui::finance_summary(acquisition, finance, russian)
		== "Финансы: источник подарок; стоимость приобретения 10 BYN; цена "
		   "продажи 50 BYN; расходы 5 BYN; прибыль 35 BYN");

	const shuba::domain::ListingData partial_listing{.marketplace = "Kufar"};
	REQUIRE(shuba::ui::listing_summary(partial_listing, russian)
			== "Объявление: площадка Kufar");
	REQUIRE(shuba::ui::listing_summary({}, russian)
			== "Объявление: значений пока нет");
	REQUIRE(shuba::ui::finance_summary({}, {}, russian)
			== "Финансы: значений пока нет");

	const shuba::persistence::StorageEnvelope storage{
		.record = shuba::domain::StorageRecord{
			.id			  = require_identifier("storage-choice"),
			.display_name = "Шкаф",
			.storage_type = "wardrobe",
			.location	  = "Спальня"}};
	shuba::catalog::CatalogRepositoryState repository;
	repository.storage_projections.emplace(
		storage.record.id.value(),
		shuba::catalog::StorageProjection{.id		  = storage.record.id,
										  .path_label = "Дом / Шкаф"});
	REQUIRE(shuba::ui::storage_choice_label(repository, storage, english)
			== "Storage: Шкаф; type: wardrobe; location: Дом / Шкаф");
	REQUIRE(shuba::ui::storage_choice_label(repository, storage, russian)
			== "Хранилище: Шкаф; тип: wardrobe; расположение: Дом / Шкаф");

	const shuba::persistence::StorageEnvelope location_only_storage{
		.record = shuba::domain::StorageRecord{
			.id			  = require_identifier("storage-location-only"),
			.display_name = "Коробка",
			.location	  = "Верхняя полка"}};
	REQUIRE(shuba::ui::storage_choice_label(
				shuba::catalog::CatalogRepositoryState{}, location_only_storage,
				russian)
			== "Хранилище: Коробка; расположение: Верхняя полка");

	const shuba::persistence::ItemEnvelope item{
		.record = shuba::domain::ItemRecord{
			.id			  = require_identifier("item-header"),
			.display_name = "Платье",
			.category	  = "одежда",
			.status		  = shuba::domain::ItemStatus::Listed}};
	const shuba::catalog::ItemProjection item_projection{
		.id					= item.record.id,
		.storage_path_label = "Дом / Шкаф",
		.storage_archived	= true,
		.photo_presence = shuba::catalog::PhotoPresenceState::HasUsablePhotos};
	REQUIRE(shuba::ui::item_detail_header(item, item_projection, russian)
		== "Предмет: Платье; состояние фотографий: Фотографии доступны; "
		   "категория: одежда; статус: Выставлено; хранилище: Дом / Шкаф; "
		   "предупреждения: архивное хранилище");

	shuba::catalog::StorageProjection broken_parent_projection{
		.id						= storage.record.id,
		.parent_reference_state = shuba::domain::ReferenceState::Broken,
		.path_label				= "Дом / Шкаф"};
	REQUIRE(shuba::ui::storage_detail_header(
		storage, broken_parent_projection, russian)
		== "Хранилище: Шкаф; тип: wardrobe; путь: Дом / Шкаф; расположение: "
		   "Спальня; предупреждения: родительское хранилище с "
		   "ошибкой");
}
