# GitHub CI and Android release operation

This document describes the hosted execution of the same contracts used locally. Read [`docs/development.md`](development.md) for local validation and [`docs/release.md`](release.md) for the release safety model. Workflow YAML and scripts are authoritative when this text differs: [`ci.yml`](../.github/workflows/ci.yml:1), [`android-release.yml`](../.github/workflows/android-release.yml:1), [`lint-ci.fish`](../tools/ci/lint-ci.fish:1), and [`run-release-tool-tests.fish`](../tools/ci/run-release-tool-tests.fish:1).

## Required continuous integration

[`ci.yml`](../.github/workflows/ci.yml:1) runs for pull requests targeting `master`, pushes to `master`, matching `v*` tag pushes, and manual dispatch. Repository protection should require these job names:

- `Lint and release-tool tests`;
- `Host libjxl cache`;
- `GCC host tests and localization`;
- `GCC AddressSanitizer`; and
- `Clang-Tidy and Cppcheck`.

All five jobs use GitHub-hosted `ubuntu-24.04`, read-only repository permissions, recursive submodules, shallow checkout, and no persisted checkout credentials. Third-party actions must be pinned to full commit SHAs; [`lint-ci.fish`](../tools/ci/lint-ci.fish:8) enforces this and runs Actionlint/ShellCheck plus the repository CI-format/source-size checks.

The tag-only `Dispatch release candidate` job is not a branch-protection check. It depends on all five required jobs and therefore runs only after the exact tag CI succeeds. Its sole elevated permission is `actions: write`, used to dispatch the `candidate` operation of [`android-release.yml`](../.github/workflows/android-release.yml:1) on the protected default-branch workflow definition. It passes the CI run ID, final-form tag name, peeled commit, and annotated tag-object ID. It has no checkout, signing secret, artifact, or release-write permission.

Host/test UI sources must include the required JUCE module headers directly rather than the ignored Projucer-generated `JuceLibraryCode/JuceHeader.h`. The repository lint rejects a tracked UI or test source dependency on that disposable header, while the Projucer application entry point retains the generated-header contract owned by Android regeneration.

The CI lanes deliberately separate responsibilities:

| Job | Contract |
| --- | --- |
| `Lint and release-tool tests` | Validates workflow/action pinning and runs hermetic black-box/mutation tests for release tooling. |
| `Host libjxl cache` | Builds or revalidates the required host libjxl 0.12 prefix from the exact recursive source graph. |
| `GCC host tests and localization` | Configures the `ci-host` preset, builds tests, validates the source/POT/PO localization contract, and runs all registered CTest cases. |
| `GCC AddressSanitizer` | Uses the isolated `ci-asan` preset with leak detection enabled. The sole documented LeakSanitizer exception is the ASAN-only [`lsan-juce-fontconfig.supp`](../tools/ci/lsan-juce-fontconfig.supp) rule for JUCE 9's Linux FreeType/fontconfig fallback static state; an expected-failure control confirms unrelated project leaks still fail. |
| `Clang-Tidy and Cppcheck` | Configures the generated-resource/compilation database needed for the project static-check targets. Hosted analysis selects the image-provided Clang-Tidy 18 explicitly; the narrow compatibility overrides documented in the workflow do not alter GCC compilation. |

Host JPEG XL coverage must not silently use an older distribution libjxl package. [`build-libjxl-host.fish`](../tools/ci/build-libjxl-host.fish:1) owns a fingerprinted, reconstructible prefix validated by every consumer.

## Android release environments

[`android-release.yml`](../.github/workflows/android-release.yml:1) has three operations with deliberately different authority.

The existing `android-release-rehearsal` environment owns the signing secrets used by both `rehearsal` and automatically dispatched `candidate` builds:

- `SHUBA_ANDROID_KEYSTORE_BASE64`;
- `SHUBA_ANDROID_KEY_ALIAS`;
- `SHUBA_ANDROID_STORE_PASSWORD`; and
- `SHUBA_ANDROID_KEY_PASSWORD`.

The first secret is the base64-encoded PKCS12 release keystore. Keep this environment reviewer-free and without deployment-branch restrictions: a required reviewer would prevent automatic candidate construction after successful tag CI. The workflow validates the successful tag CI dispatch before checkout, tool installation, or signing-secret use. It decodes the keystore only late in the signed-build job to a mode-0600 path under `RUNNER_TEMP`, exports that temporary path as `SHUBA_ANDROID_KEYSTORE_FILE`, and removes it unconditionally.

Create a separate `android-release-promotion` environment with no secrets. Configure at least one required reviewer and restrict its deployment branch policy to `master`. The no-checkout write job queries the environment and fails closed unless a required-reviewer rule with at least one reviewer exists; an implicitly auto-created, reviewer-free environment cannot promote a release. The workflow also requires that it was manually dispatched from `master` and that `confirm_tag` exactly repeats `tag`. Environment approval is the human assertion that retained representative-device evidence accepts the exact prerelease assets; it is not a substitute for the workflow's byte and contract verification.

Secrets, private keys, generated signing properties, `dist`, and password-bearing data must never enter caches, public releases, release assets, logs, or artifacts other than the short-lived private rehearsal packet. The candidate workflow artifact contains only the already verified public-safe bundle and is retained for one day solely to cross the read-only build/write-only publication boundary.

## Manual private rehearsal

Dispatch `Android release` from `master` with `operation=rehearsal` and leave all tag/promotion fields empty. The signed-build job selects JDK 17, resolves the release-contract-selected Android command-line-tools revision, installs only the missing exact Android CMake version, validates the toolchain/native policy, restores and revalidates fingerprinted dependencies, regenerates the disposable Android project, runs generated-project mutation tests, builds and verifies the signed packet, then uploads only the four-file packet as a private workflow artifact retained for seven days.

Rehearsal does not create or move a tag, create a GitHub Release, publish to a store/channel, or claim device acceptance. It remains useful before advancing release identity or provisioning the final tag.

## Automatic public release candidate

Candidate publication begins only by pushing the unsigned annotated final-form tag `v<app.version_name>`. The tag annotation is the reviewed Markdown release body; it must be non-empty and contain no signing block. The tag name, application version/code, artifact name, generated identity, and source commit must already be atomically advanced and committed.

The hosted sequence is fail-closed:

1. The tag push runs all five required CI jobs against that tag.
2. Only their successful tag-only dependent job dispatches `operation=candidate` on the `master` workflow definition.
3. The signed-build job queries the immutable upstream run record and requires the expected repository, workflow path/name, `push` event, completed/success conclusion, tag name, and peeled commit. It separately requires that the remote ref is the supplied annotated tag object, that the object points directly to the supplied commit, and that the commit is in `master` history.
4. The checked-out tag must match the exact contract version and the supplied tag object/commit. A lightweight, signed, moved, nested, off-branch, malformed, contract-mismatched, or already-released tag is rejected before publication.
5. The workflow builds and independently verifies the signed four-file packet, extracts the exact tag annotation as release notes, builds deterministic recursive corresponding source, and constructs/verifies the complete twelve-file public bundle.
6. A read-only workflow artifact crosses into a read-only verification job. That job checks out the same tag and revalidates the Android/publication contract and complete bundle.
7. Only after verification succeeds does a no-checkout publication job receive `contents: write`. It downloads the same immutable same-run artifact, refuses to replace an existing release or asset, creates one non-draft GitHub prerelease on the existing tag, and compares GitHub's uploaded asset names, sizes, and SHA-256 digests with the verified bundle.

The prerelease is intentionally public so its exact APK can be tested on required devices. It is not the latest stable release and does not claim acceptance. The GitHub tag and all twelve assets are candidate evidence. If testing rejects them, retain the prerelease unchanged and advance both version name and Android version code for the correction. Never move/recreate the tag, delete/replace assets, or republish changed bytes under the rejected identity.

## Manual same-release promotion

After retaining all required device evidence from [`docs/release.md`](release.md), dispatch `Android release` from `master` with:

- `operation=promotion`;
- `tag=v<accepted-version>`; and
- `confirm_tag` set to the identical tag.

Leave the CI-only fields empty. A read-only verification job first requires an existing non-draft prerelease with the exact candidate title and twelve uploaded assets. It snapshots the release ID, annotated tag object/commit, and every asset ID/name/size/digest/state/timestamp; downloads all release assets; extracts the current tag annotation; checks that it still equals the release body; reruns complete publication-bundle, APK, signer, source, checksum, inventory, and contract verification; and uploads only the resulting non-secret identity evidence as an immutable one-day same-run artifact.

Only then is the no-checkout write job eligible for `android-release-promotion` approval. After approval it downloads that evidence, re-queries and compares the live release, asset, and tag identities, then patches the existing release record: the title loses `release candidate`, `prerelease` becomes false, and GitHub marks that same release as latest. It does not rebuild, reupload, delete, rename, or copy an asset and does not create another tag or Release. Post-patch assertions require the same release ID, body, tag, tag object/commit, and byte-identical asset metadata, then require `/releases/latest` to resolve to that release.

## Permission and failure boundaries

- Global workflow authority is `contents: read`.
- Tag CI dispatch receives only `actions: write` in its no-checkout final job.
- Signed build receives `actions: read` for upstream-run validation and `contents: read`; it cannot create a Release.
- Candidate verification receives `actions: read` and `contents: read`; the dependent no-checkout publication job alone receives `contents: write` for the GitHub prerelease.
- Promotion verification receives only `contents: read`; the dependent no-checkout promotion job receives `actions: read`, `contents: write`, no signing secrets, and a protected manual environment.
- No job that checks out or executes tag-controlled source has release-write permission.
- Per-tag concurrency never cancels an in-progress signing/publication/promotion run.
- An interrupted upload may leave a draft Release created internally by GitHub CLI. Treat any existing release record as a stop condition; inspect and resolve it deliberately rather than forcing replacement.
- Repository tag protection or immutable-release settings are recommended additional controls. The tracked workflow remains fail-closed without them by checking tag object/commit and asset identities before and after mutation.

## Cache boundaries

- Host libjxl, Android libjxl, and Projucer caches contain only ignored, reconstructible trees and are keyed by validated source/tool/configuration fingerprints.
- Restored dependency caches are revalidated by the owning builder before use.
- The Android SDK is not cached. The workflow validates the current release contract and installs only missing SDK CMake.
- The Gradle cache contains dependency/wrapper state only. It is restored and saved before signing material is decoded or mapped.
- Never cache keystores, signing files, passwords, final artifacts, generated signing configuration, or `dist`.

## Operating the release-tool test entry point

[`run-release-tool-tests.fish`](../tools/ci/run-release-tool-tests.fish:8) is the release-tool test dispatcher:

- `--hermetic` runs Fish syntax/format checks plus hermetic tests that need no generated Android project or signing key;
- `--generated` adds the generated Android mutation corpus; and
- `--rehearsal` additionally validates the real signed APK and its public-safe packet.

The dispatcher has a fixed test inventory. Add, remove, or rename a release-tool test only together with the explicit inventory change and relevant CI expectation; do not let test discovery drift silently. The publication-asset suite owns annotated-tag note extraction plus source/bundle mutation coverage.

## Hosted-evidence boundary

Do not claim a hosted result solely because local scripts passed. Conversely, successful tag CI and candidate publication do not claim device acceptance. Record each layer honestly: local validation, hosted required CI, signed artifact verification, public prerelease identity, representative-device acceptance, and same-release promotion are distinct evidence classes.
