# Shuba

[![Download latest APK](https://img.shields.io/badge/Download-latest%20APK-3DDC84?logo=android&logoColor=white)](https://github.com/homremwork/shuba/releases/latest/download/Shuba-arm64-v8a.apk)

[English version below](#english)

## Русский

Shuba — локальное Android-приложение для учёта личных вещей, которые хранятся, ищутся или продаются на вторичном рынке. Оно хранит предметы, места хранения, фотографии, заметки, метки, статус продажи, сведения об объявлении и базовые данные о покупке или продаже на одном устройстве. Это целевой каталог, а не складская платформа, облачный сервис, клиент маркетплейса или бухгалтерская система.

### Граница продукта

- Канонические данные каталога и внутренние фотографии находятся в приватном хранилище приложения на одном Android-устройстве.
- Предметы могут быть назначены вложенным хранилищам; оба вида записей поддерживают метки, заметки, фотографии и архивацию.
- Фотографии выбираются системным средством, внутри хранятся как JPEG XL, а пользователю экспортируются как JPEG.
- Поддерживаются ручные незашифрованные ZIP-резервные копии и импорт: каталог переносится во временную область, проверяется, явно подтверждается и защищается откатом.
- В первую версию не входят облачная синхронизация, автоматизация маркетплейсов, классификация изображений, SQL, расширенный учёт финансов, широкие разрешения на медиатеку, прямая съёмка камерой и полноценный настольный продукт.

Если текст документации расходится с исполняемым контрактом или исходным кодом, приоритет у контракта и кода. Семантика продукта находится в [`Source/Domain`](Source/Domain), сценарии каталога — в [`Source/Catalog`](Source/Catalog), хранение данных — в [`Source/Persistence`](Source/Persistence), платформенные границы — в [`Source/Platform`](Source/Platform), а JUCE-интерфейс — в [`Source/UI`](Source/UI).

### Карта документации

| Документ | Когда читать |
| --- | --- |
| [`docs/architecture.md`](docs/architecture.md) | Нужны границы системы, неизменяемые правила, модель данных/восстановления и правила владения. |
| [`docs/development.md`](docs/development.md) | Нужны правила локальной разработки, владения генерируемыми файлами, проверки хоста и сопровождение локализации. |
| [`docs/release.md`](docs/release.md) | Нужны граница подписи, процедура получения проверенного артефакта, доказательства приёмки и стоп-условия выпуска. |
| [`docs/ci.md`](docs/ci.md) | Нужны обязательные GitHub-проверки, границы кэшей и защищённые сценарии подписанной репетиции, кандидата и продвижения выпуска. |
| [`docs/roadmap.md`](docs/roadmap.md) | Нужны текущий контекст следующей задачи, сознательные ограничения и правила определения малого последующего блока. |

### Карта источников истины

| Источник | Чем владеет |
| --- | --- |
| [`Shuba.jucer`](Shuba.jucer:3) | Метаданные JUCE-проекта и авторитетный набор Android-исходников/ресурсов. |
| [`CMakeLists.txt`](CMakeLists.txt:1) и [`CMakePresets.json`](CMakePresets.json:1) | Linux host targets, тесты, контракт локализации, статический анализ и CI-пресеты. |
| [`release/release.properties`](release/release.properties:1) | Несекретные идентификаторы приложения, требования к Android/инструментам, имя артефакта, отпечаток сертификата и запретный список разрешений. |
| [`Source`](Source) | Поведение программы и публичные внутренние контракты. |
| [`tests`](tests) | Регрессионные доказательства поведения; номера в именах тестов сохранены как стабильные имена файлов. |
| [`Localization`](Localization) | Шаблон для переводчиков и production-каталог русского языка. |
| [`tools`](tools) и [`.github/workflows`](.github/workflows) | Исполняемые процедуры выпуска и размещённого CI. |

### Неизменяемые правила сопровождения

1. Логику продукта и предметной области нужно писать на C++23. Интеграция Android/JUCE должна оставаться небольшой, стабильной и изолированной интерфейсами.
2. Генерируемые [`Builds/Android`](Builds/Android) и [`JuceLibraryCode`](JuceLibraryCode) считаются одноразовым выводом. Изменяйте [`Shuba.jucer`](Shuba.jucer:3), исходный код или отслеживаемые инструменты выпуска, затем регенерируйте; не вносите ручные изменения в Android-файлы как в источник истины.
3. Нельзя добавлять зависимость сборки, тестов, запуска, упаковки или выпуска от временных плановых материалов. Они не являются источником истины проекта.
4. Надёжное восстановление и безопасность выпуска важнее оптимизаций производительности. Изменение, ослабляющее staging, атомарную замену, сериализацию операций, работу с секретами или проверку, требует явного архитектурного решения.
5. Новая задача должна быть узкой: один владеющий подсистемой блок, сохраняемый инвариант, целевые тесты и конкретное доказательство приёмки. Текущая граница работ описана в [`docs/roadmap.md`](docs/roadmap.md).

Перед изменением постоянных данных, медиа, восстановления, локализации или платформенного кода прочитайте [`docs/architecture.md`](docs/architecture.md).

---

## English

Shuba is a local-first Android catalog for people who store, find, and sell personal resale items. It keeps items, storage locations, photos, notes, tags, sale state, listing details, and basic acquisition or sale figures on one device. It is a focused catalog, not an inventory platform, cloud service, marketplace client, or accounting system.

### Product boundary

- Canonical catalog data and internal photo media are app-private and local to one Android device.
- Items can be assigned to nested storages; both records support tags, notes, photos, and archive state.
- Photos are selected through the system path, stored internally as JPEG XL, and exported for users as JPEG.
- Manual, unencrypted ZIP backup/import is supported; catalog import is staged, validated, confirmed, and rollback-protected.
- First-version scope excludes cloud sync, marketplace automation, image classification, SQL, advanced accounting, broad media-library permission, direct camera capture, and a general desktop product.

The executable contract and the code take precedence over prose when they differ. Product semantics live in [`Source/Domain`](Source/Domain), catalog use cases in [`Source/Catalog`](Source/Catalog), persistence in [`Source/Persistence`](Source/Persistence), platform boundaries in [`Source/Platform`](Source/Platform), and the JUCE UI in [`Source/UI`](Source/UI).

### Documentation map

| Read this | When you need |
| --- | --- |
| [`docs/architecture.md`](docs/architecture.md) | System boundaries, durable invariants, data/recovery model, and ownership rules. |
| [`docs/development.md`](docs/development.md) | Local development rules, generated-file ownership, host validation, and localization maintenance. |
| [`docs/release.md`](docs/release.md) | Signing boundary, verified-artifact procedure, acceptance evidence, and release stop conditions. |
| [`docs/ci.md`](docs/ci.md) | Required GitHub checks, cache boundaries, and the protected signed rehearsal, candidate, and promotion workflows. |
| [`docs/roadmap.md`](docs/roadmap.md) | Current next-task context, deliberate limitations, and how to define a small follow-up block. |

### Repository authority map

| Authority | Owns |
| --- | --- |
| [`Shuba.jucer`](Shuba.jucer:3) | JUCE project metadata and the authoritative Android source/resource inventory. |
| [`CMakeLists.txt`](CMakeLists.txt:1) and [`CMakePresets.json`](CMakePresets.json:1) | Linux host targets, tests, localization contract, static analysis, and CI presets. |
| [`release/release.properties`](release/release.properties:1) | Non-secret application identity, Android/toolchain requirements, artifact name, signer fingerprint, and permission denylist. |
| [`Source`](Source) | Runtime behavior and public internal contracts. |
| [`tests`](tests) | Regression evidence for owned behavior; the test names follow the original implementation block numbers only as stable filenames. |
| [`Localization`](Localization) | Tracked translator template and Russian production catalog. |
| [`tools`](tools) and [`.github/workflows`](.github/workflows) | Executable release and hosted-CI procedures. |

### Non-negotiable maintenance rules

1. Keep product and business logic in C++23. Android/JUCE integration must remain small, stable, and isolated behind interfaces.
2. Treat generated [`Builds/Android`](Builds/Android) and [`JuceLibraryCode`](JuceLibraryCode) output as disposable. Change [`Shuba.jucer`](Shuba.jucer:3), source, or tracked release tooling, then regenerate; never patch generated Android build files as the source of truth.
3. Do not add a build, test, runtime, packaging, or release dependency on temporary planning material. The directory is historical working material and is not a project authority.
4. Preserve recovery and release safety over performance shortcuts. A change that weakens staging, atomic replacement, operation serialization, secret handling, or verification requires an explicit architecture decision.
5. Keep a new task narrow: name one owning subsystem, the invariant to preserve, targeted tests, and concrete acceptance evidence. See [`docs/roadmap.md`](docs/roadmap.md) for the current work boundary.

See [`docs/architecture.md`](docs/architecture.md) for the system model before changing persistent data, media, recovery, localization, or platform code.
