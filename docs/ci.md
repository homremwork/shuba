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

The CI lanes deliberately separate responsibilities:

| Job | Contract |
| --- | --- |
| `Lint and release-tool tests` | Validates workflow/action pinning and runs hermetic black-box/mutation tests for release tooling. |
| `Host libjxl cache` | Builds or revalidates the required host libjxl 0.12 prefix from the exact recursive source graph. |
| `GCC host tests and localization` | Configures the `ci-host` preset, builds tests, validates the source/POT/PO localization contract, and runs all registered CTest cases. |
| `GCC AddressSanitizer` | Uses the isolated `ci-asan` preset and leak-detection settings. |
| `Clang-Tidy and Cppcheck` | Configures the generated-resource/compilation database needed for the project static-check targets. |

Host JPEG XL coverage must not silently use an older distribution libjxl package. [`build-libjxl-host.fish`](../tools/ci/build-libjxl-host.fish:1) owns a fingerprinted, reconstructible prefix validated by every consumer.

## Signed Android rehearsal

[`android-release-rehearsal.yml`](../.github/workflows/android-release-rehearsal.yml:1) is manual-only. Before enabling it, create the protected GitHub Environment `android-release-rehearsal`, configure appropriate required reviewers, and provide these environment secrets:

- `SHUBA_ANDROID_KEYSTORE_BASE64`;
- `SHUBA_ANDROID_KEY_ALIAS`;
- `SHUBA_ANDROID_STORE_PASSWORD`; and
- `SHUBA_ANDROID_KEY_PASSWORD`.

The first secret is the base64-encoded PKCS12 release keystore. The workflow decodes it only late in the job to a mode-0600 path under `RUNNER_TEMP`, exports that temporary path as `SHUBA_ANDROID_KEYSTORE_FILE`, and removes it unconditionally. Secrets, private keys, generated signing properties, `dist`, and password-bearing data must never enter caches, artifacts other than the vetted public-safe packet, or logs.

The rehearsal selects JDK 17, installs only the exact missing Android CMake version, validates the release contract, restores/revalidates fingerprinted native dependencies, regenerates and validates the disposable JUCE Android project, runs generated-project mutation tests, builds the signed candidate through [`build-android-release.fish`](../tools/release/build-android-release.fish:12), validates its real artifact packet, then uploads the exact final four-file directory as a private artifact retained for seven days.

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
