# Development guide

## Working agreement

Read [`docs/architecture.md`](architecture.md) before changing a persistent-data, media, recovery, platform, UI-session, or localization contract. Keep each change small and evidence-driven:

1. identify the one owning subsystem and the invariant being changed or preserved;
2. read its public header and nearest focused tests;
3. add or adjust focused regression coverage before relying on broad manual testing;
4. run the smallest relevant validation path, then the wider required gates; and
5. record concrete acceptance evidence when a change needs Android/device verification.

C++23 is required. Product and business logic stay in C++; Java/Kotlin is limited to generated JUCE and unavoidable Android glue. Prefer a mature dependency behind a narrow interface over a custom parser, codec, or platform bridge, but do not add a library that broadens scope or makes the Android build fragile.

## Source and generated-file ownership

[`Shuba.jucer`](../Shuba.jucer:3) is the Android JUCE project authority. [`CMakeLists.txt`](../CMakeLists.txt:1) is the host build/test authority. When a C++ source, header, resource, or project setting changes, update each authoritative input that owns it:

| Change | Update | Never treat as authority |
| --- | --- | --- |
| Runtime C++ module | [`Source`](../Source), its focused tests, host CMake source list, and [`Shuba.jucer`](../Shuba.jucer:3) source inventory | Generated Android CMake files |
| Test module | [`tests`](../tests) and [`tests/CMakeLists.txt`](../tests/CMakeLists.txt:1) | Generated Android output |
| Android/resource inventory or project settings | [`Shuba.jucer`](../Shuba.jucer:3) | [`Builds/Android`](../Builds/Android) and [`JuceLibraryCode`](../JuceLibraryCode) |
| Application/release identity | [`release/release.properties`](../release/release.properties:1), then authoritative project inputs if identity must match generated output | A copied constant in a script or generated Gradle file |
| Translatable message | Source-owned definitions, [`Localization/shuba-ui.pot`](../Localization/shuba-ui.pot), and [`Localization/ru.po`](../Localization/ru.po) | Generated `BinaryData` |

[`Builds/Android`](../Builds/Android), [`JuceLibraryCode`](../JuceLibraryCode), local build directories, Gradle caches, release artifacts, and diagnostic output are ignored outputs. Regenerate them with [`generate-android.fish`](../tools/release/generate-android.fish:1); do not hand-edit them to repair a source or inventory mismatch. [`check-generated-android.fish`](../tools/release/check-generated-android.fish:1) should reject a mismatch rather than normalize it.

Use the project formatting configuration in [`.clang-format`](../.clang-format) when formatting C++ is required. Keep changes local and use [`git diff --check`](../.git:1) to catch whitespace errors.

## Host builds and tests

The checked-in CI presets are the reproducible default for a complete host gate. They require Linux, Ninja, GNU C++, Catch2, gettext, pkg-config, and a validated libjxl 0.12 prefix. CI creates that prefix with [`build-libjxl-host.fish`](../tools/ci/build-libjxl-host.fish:1).

For an ordinary local build, configure a separate ignored directory using the same relevant options as the change. A baseline example is:

```sh
cmake -S . -B build/local-host -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DSHUBA_BUILD_HEADLESS_TESTS=ON
cmake --build build/local-host --target shuba_headless_tests
ctest --test-dir build/local-host --output-on-failure
```

The real JPEG XL codec tests are enabled by default through [`SHUBA_ENABLE_REAL_JPEG_XL_CODEC_TESTS`](../CMakeLists.txt:14). They require `libjxl >= 0.12.0`; if the system does not provide a suitable host package, use the same fingerprinted host builder as CI instead of lowering the requirement.

For the closest CI-equivalent host lane, first build or validate the prefix and then use the presets:

```sh
tools/ci/build-libjxl-host.fish
cmake --preset ci-host
cmake --build --preset ci-host
ctest --preset ci-host
```

[`CMakePresets.json`](../CMakePresets.json:8) defines three distinct lanes:

- `ci-host`: normal GNU Debug tests plus the localization contract;
- `ci-asan`: a separate GNU Debug AddressSanitizer build; and
- `ci-static`: generated resources plus standalone Cppcheck and Clang-Tidy.

Run focused test names while developing, but do not close changes to shared contracts without the relevant full lane. Test filenames preserve historical block prefixes only as stable names; their present meaning is determined by the test content.

## Static analysis and sanitizers

The `shuba_static_checks` target runs the project Cppcheck and Clang-Tidy targets against owned [`Source`](../Source) and [`tests`](../tests) translation units. The analyzer profile deliberately has narrow documented suppressions for third-party/tool limitations; do not add broad suppressions to silence new project findings.

The ASAN lane retains leak detection. Its sole LeakSanitizer exception is [`lsan-juce-fontconfig.supp`](../tools/ci/lsan-juce-fontconfig.supp), which matches only JUCE 9's Linux `FreeTypeTypeface::fromPattern` fallback path. It is active only for ASAN CTest runs and its expected-failure probe verifies that unrelated project leaks still fail. Reassess and remove it for any JUCE/font-backend change, upstream cleanup, or new allocation path; never replace it with `detect_leaks=0` or a broad library-level rule.

Use the CI static preset when the required tools are installed:

```sh
cmake --preset ci-static
cmake --build --preset ci-static
cmake --build build/ci/ci-static --target shuba_static_checks
```

AddressSanitizer changes compile/link flags and therefore needs its own build tree:

```sh
cmake -S . -B build/local-asan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DSHUBA_BUILD_HEADLESS_TESTS=ON \
  -DSHUBA_ENABLE_ASAN=ON
cmake --build build/local-asan --target shuba_headless_tests
ctest --test-dir build/local-asan --output-on-failure
```

Run ASAN for ownership/lifetime, filesystem replacement, buffer/codec, or asynchronous-worker changes. The CI equivalent is `cmake --preset ci-asan`, followed by its build and CTest preset.

## Localization workflow

The application uses an English compiled fallback and a tracked Russian catalog. The only translatable identities are defined in the live C++ catalog tables and enumeration, including typed photo and progress messages. Do not create CSV inventories, copied message counts, generated text lists, or a second placeholder contract.

For a new or changed message:

1. add/change the source-owned definition in [`MessageCatalog.cpp`](../Source/Localization/MessageCatalog.cpp:1) or [`PhotoWorkflowLocalization.cpp`](../Source/Localization/PhotoWorkflowLocalization.cpp:1);
2. use or extend a typed method on [`Localization`](../Source/Localization/Facade.hpp:22), rather than concatenating translated fragments at a screen call site;
3. update the tracked POT and Russian PO as one coherent change; and
4. run the public CMake validation target.

The sole public validation entry point is `shuba_validate_localization`:

```sh
cmake --build build/local-host --target shuba_validate_localization
```

It derives the catalog from source, verifies the POT/PO pair, runs gettext checks, validates placeholders and plural forms, and confirms the production parser accepts the Russian catalog. It intentionally validates tracked inputs before generated BinaryData is considered. Keep raw diagnostic codes/messages, filenames, MIME types, JSON fields, user data, and protocol values outside translation catalogs.

## Android integration checks

Use the release tooling rather than a hand-written Gradle invocation when checking generated Android ownership:

```sh
tools/release/build-projucer.fish
tools/release/generate-android.fish
tools/release/check-release-contract.fish
tools/release/check-release-identity.fish
tools/release/check-generated-android.fish
```

The tooling enforces the current contract: JDK 17, API 34, pinned NDK/build-tools/CMake/Gradle values, C++23 without GNU extensions, `arm64-v8a`, the complete source/resource inventory, and the permission denylist. Android command-line tools use the ordered bounded allowlist in [`release/release.properties`](../release/release.properties:19): the resolver selects `20.0` before `12.0`, accepting `cmdline-tools/latest` only when its installed metadata names an allowed revision. The selected tool path, revision, and hash are recorded in release evidence. It owns dependency builds and generated output checks. Do not substitute a manual generated-file edit for a failed assertion.

The Android native policy is deliberately narrow and must remain uniform across JUCE/application code and Android libjxl, Brotli, and Highway archives:

- only `arm64-v8a` is produced;
- `-mcpu=cortex-a73` supplies the CPU model and the Armv8-A + NEON + AES + SHA2 + CRC32 feature floor;
- this is not an Armv8.2-A baseline, because that would exclude the stated Cortex-A73/A53-compatible device class;
- Debug is generated with `-O0`, Release with safe `-O3`; and
- `-Ofast`, `-ffast-math`, `-funsafe-math-optimizations`, and all LTO flags are prohibited from the generated Android project and release dependency build.

The exact pinned NDK policy probe compiles, archives, links, and inspects an AArch64 native sample. Run it through [`check-release-contract.fish`](../tools/release/check-release-contract.fish:1); validate/rebuild the dependency tree with [`build-libjxl-android.fish`](../tools/release/build-libjxl-android.fish:1); then regenerate and validate the disposable project. A useful local sequence is:

```sh
tools/release/check-release-contract.fish
tools/release/build-libjxl-android.fish --check
tools/release/generate-android.fish
tools/release/check-generated-android.fish
fish --no-config tools/ci/run-release-tool-tests.fish --hermetic
fish --no-config tools/ci/run-release-tool-tests.fish --generated
```

ThinLTO is not an active fallback or optional local switch. JUCE 9.0.1's Android exporter emits bare `-flto`; the pinned NDK Clang 21 lowers that spelling to full LTO rather than ThinLTO. Do not label it ThinLTO, add generated-file patches, or re-enable it without a new authority decision and complete pinned-toolchain evidence.

A host test pass cannot prove Android picker behavior, provider behavior, media responsiveness, safe-area layout, install/upgrade behavior, or runtime resource rendering. Device validation is required for a change that touches those boundaries; record device/API, tested APK identity, steps, expected/actual outcome, and safe screenshots/logs.

## Documentation and planning boundary

Documentation explains durable decisions and procedures. Code, executable configuration, and the current release contract are authoritative for exact behavior. Temporary planning material must not be read by builds, tests, generators, runtime code, packaging, validation, or release tooling. When retiring a plan, transfer only information that remains useful and hard to infer from code into [`README.md`](../README.md), [`docs/architecture.md`](architecture.md), [`docs/release.md`](release.md), [`docs/roadmap.md`](roadmap.md), or the owning code contract.
