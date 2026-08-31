# GitHub CI and rehearsal operation

This document describes the hosted execution of the same contracts used locally. Read [`docs/development.md`](development.md) for local validation and [`docs/release.md`](release.md) for the release safety model. Workflow YAML and scripts are authoritative when this text differs: [`ci.yml`](../.github/workflows/ci.yml:1), [`android-release-rehearsal.yml`](../.github/workflows/android-release-rehearsal.yml:1), [`lint-ci.fish`](../tools/ci/lint-ci.fish:1), and [`run-release-tool-tests.fish`](../tools/ci/run-release-tool-tests.fish:1).

## Required continuous integration

[`ci.yml`](../.github/workflows/ci.yml:1) runs for pull requests targeting `master`, pushes to `master`, and manual dispatch. Repository protection should require these job names:

- `Lint and release-tool tests`;
- `Host libjxl cache`;
- `GCC host tests and localization`;
- `GCC AddressSanitizer`; and
- `Clang-Tidy and Cppcheck`.

All jobs use GitHub-hosted `ubuntu-24.04`, read-only repository permissions, recursive submodules, shallow checkout, and no persisted checkout credentials. Third-party actions must be pinned to full commit SHAs; [`lint-ci.fish`](../tools/ci/lint-ci.fish:8) enforces this and runs Actionlint/ShellCheck plus the repository CI-format/source-size checks.

Host/test UI sources must include the required JUCE module headers directly rather than the ignored Projucer-generated `JuceLibraryCode/JuceHeader.h`. The repository lint rejects a tracked UI or test source dependency on that disposable header, while the Projucer application entry point retains the generated-header contract owned by Android regeneration.

The CI lanes deliberately separate responsibilities:

| Job | Contract |
| --- | --- |
| `Lint and release-tool tests` | Validates workflow/action pinning and runs hermetic black-box/mutation tests for release tooling. |
| `Host libjxl cache` | Builds or revalidates the required host libjxl 0.12 prefix from the exact recursive source graph. |
| `GCC host tests and localization` | Configures the `ci-host` preset, builds tests, validates the source/POT/PO localization contract, and runs all registered CTest cases. |
| `GCC AddressSanitizer` | Uses the isolated `ci-asan` preset with leak detection enabled. The sole documented LeakSanitizer exception is the ASAN-only [`lsan-juce-fontconfig.supp`](../tools/ci/lsan-juce-fontconfig.supp) rule for JUCE 9's Linux FreeType/fontconfig fallback static state; an expected-failure control confirms unrelated project leaks still fail. |
| `Clang-Tidy and Cppcheck` | Configures the generated-resource/compilation database needed for the project static-check targets. Hosted analysis selects the image-provided Clang-Tidy 18 explicitly so check behavior does not depend on the runner's unversioned analyzer symlink. The narrow `__cpp_concepts=202002L` compatibility override allows that Clang frontend to parse GCC 13 libstdc++'s C++23 `std::expected` implementation, while a matching warning override permits that intentional builtin-feature-macro adjustment; neither changes GCC compilation. Clang Static Analyzer allocator inlining is disabled to avoid its Clang 18 false positive for a returned libstdc++ `shared_ptr` control block while retaining `clang-analyzer-cplusplus.NewDeleteLeaks` for project allocations. Catch2's assertion-decomposer expansion is excluded from `bugprone-chained-comparison`; real project expressions remain covered by the other configured checks and normal compilation/tests. |

Host JPEG XL coverage must not silently use an older distribution libjxl package. [`build-libjxl-host.fish`](../tools/ci/build-libjxl-host.fish:1) owns a fingerprinted, reconstructible prefix validated by every consumer.

## Signed Android rehearsal

[`android-release-rehearsal.yml`](../.github/workflows/android-release-rehearsal.yml:1) is manual-only. The reviewer-free GitHub Environment `android-release-rehearsal` scopes these environment secrets under the current repository contract; do not configure required reviewers or deployment-branch restrictions:

- `SHUBA_ANDROID_KEYSTORE_BASE64`;
- `SHUBA_ANDROID_KEY_ALIAS`;
- `SHUBA_ANDROID_STORE_PASSWORD`; and
- `SHUBA_ANDROID_KEY_PASSWORD`.

The first secret is the base64-encoded PKCS12 release keystore. The workflow decodes it only late in the job to a mode-0600 path under `RUNNER_TEMP`, exports that temporary path as `SHUBA_ANDROID_KEYSTORE_FILE`, and removes it unconditionally. Secrets, private keys, generated signing properties, `dist`, and password-bearing data must never enter caches, artifacts other than the vetted public-safe packet, or logs.

The rehearsal selects JDK 17, resolves the release-contract-selected Android command-line-tools revision, and uses that exact `sdkmanager` to install only the missing Android CMake version. It then validates the release contract, including the pinned NDK compile/archive/shared-link probe for `cortex-a73`, Armv8-A + NEON + AES + SHA2 + CRC32, and Release `-O3`; restores/revalidates fingerprinted native dependencies; regenerates and validates the disposable JUCE Android project; runs generated-project mutation tests; builds the signed candidate through [`build-android-release.fish`](../tools/release/build-android-release.fish:12); validates its real artifact packet; then uploads the exact final four-file directory as a private artifact retained for seven days.

The hosted native policy is `arm64-v8a` only, with no Armv8.2-A baseline, no `-Ofast` or fast-math, and no LTO flag. Do not enable bare `-flto` as a ThinLTO substitute: with the pinned NDK Clang 21 it selects full LTO, while the JUCE Android exporter cannot emit deterministic `-flto=thin` authority.

The rehearsal does **not** create or move tags, create a GitHub Release, publish to a channel/store, update a deployment, or decide that a candidate is accepted. It is a controlled build/rehearsal step; Android/device acceptance and publication remain governed by [`docs/release.md`](release.md).

## Cache boundaries

- Host libjxl, Android libjxl, and Projucer caches contain only ignored, reconstructible trees and are keyed by validated source/tool/configuration fingerprints.
- Restored dependency caches are revalidated by the owning builder before use.
- The Android SDK is not cached. The workflow validates the current release contract and installs only missing SDK CMake.
- The Gradle cache contains dependency/wrapper state only. It is restored and saved before signing material is decoded or mapped.
- Never cache keystores, signing files, passwords, final artifacts, generated signing configuration, or `dist`.

## Operating the release-tool test entry point

[`run-release-tool-tests.fish`](../tools/ci/run-release-tool-tests.fish:8) is the release-tool test dispatcher:

- `--hermetic` runs fish syntax/format checks plus hermetic tests that need no generated Android project or signing key;
- `--generated` adds the generated Android mutation corpus; and
- `--rehearsal` additionally validates the real signed APK and its public-safe packet.

The dispatcher has a fixed test inventory. Add, remove, or rename a release-tool test only together with the explicit inventory change and the relevant CI expectation; do not let test discovery drift silently.

## Hosted-evidence boundary

Do not claim a hosted result solely because local scripts passed. Conversely, a successful hosted rehearsal does not claim device acceptance or publication. Record each layer honestly: local validation, hosted CI, signed artifact verification, device acceptance, and publication are distinct evidence classes.
