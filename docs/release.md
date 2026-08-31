# Android release procedure

## Release authority and current status

[`release/release.properties`](../release/release.properties:1) is the sole non-secret machine-readable release contract. Every release helper parses it rather than duplicating identity values. It defines the application ID, version name/code, Android API/ABI/toolchain requirements, final artifact basename, expected native library, pinned public signing-certificate fingerprint, and forbidden permissions.

The current public GitHub Release is [`v1.0.2`](https://github.com/homremwork/shuba/releases/tag/v1.0.2), Android code `3`. Its annotated tag is fixed to the accepted source; its checksum manifest, APK provenance, and verification assets own exact artifact evidence. Later `master` changes are not part of that release and do not inherit its acceptance. Any later distribution requires an atomically advanced release identity, a newly built and independently verified candidate, and acceptance against the resulting contract.

Do not copy historical APK hashes, rejected candidates, prior tags, password locations, or device-specific process details into release instructions. The generated provenance and verification sidecars own exact artifact evidence for a particular candidate.

## Non-secret release contract

The current release contract enforces:

- Android 14/API 34 minimum, target, and compile SDK;
- `arm64-v8a` as the only supported ABI;
- `cortex-a73` code generation for the application and Android static dependencies, with an Armv8-A + NEON + AES + SHA2 + CRC32 feature floor;
- safe Release `-O3` and Debug `-O0`; no Armv8.2-A minimum, `-Ofast`, fast-math, or LTO;
- JDK 17, NDK, build-tools, CMake, Gradle, Android Gradle Plugin, and command-line-tool versions as declared in [`release/release.properties`](../release/release.properties:8);
- generated JUCE Android output from [`Shuba.jucer`](../Shuba.jucer:3), not hand-maintained Gradle/CMake files;
- static Android libjxl built from the pinned recursive submodule graph;
- a private PKCS12 signing key whose public SHA-256 certificate fingerprint matches the contract; and
- no network, broad-media, camera, microphone, storage, Bluetooth, billing, notification, boot, or other denied permissions.

`cortex-a73` is the deterministic Clang CPU model, not a claim that every accepted device has that marketing name. Snapdragon 680/685 Kryo 265-class hardware is within the intended compatibility class, but only exact-device evidence can close device acceptance. Armv8.2-A is deliberately not selected because it would be unsafe to claim compatibility with Cortex-A53/A73-era implementations. The rejected ThinLTO experiment must not be revived as a bare `-flto` shortcut: the pinned NDK resolves that spelling to full LTO, not ThinLTO.

Update a release identity atomically. If the version, package ID, signing certificate, SDK/ABI policy, CPU/optimization policy, or artifact name changes, update the contract and every authoritative project input, regenerate, and let the identity/generated-project checks prove alignment. Never update only an artifact filename or a generated Gradle constant.

## Signing boundary

The release key and passwords remain outside the repository. The build coordinator requires exactly these externally supplied variables:

- `SHUBA_ANDROID_KEYSTORE_FILE` — readable, regular PKCS12 file outside the workspace;
- `SHUBA_ANDROID_KEY_ALIAS`;
- `SHUBA_ANDROID_STORE_PASSWORD`; and
- `SHUBA_ANDROID_KEY_PASSWORD`.

[`prepare-android-release-signing.fish`](../tools/release/prepare-android-release-signing.fish:1) validates the inputs and pinned certificate before Gradle runs. It maps secrets into child-only Gradle properties. Passwords must never appear in command-line arguments, shell tracing, tracked/generated files, cache keys, provenance, verification output, or screenshots/logs.

[`android-release-signing.init.gradle`](../tools/release/android-release-signing.init.gradle:1) modifies only the generated app Release signing configuration. Debug keeps its generated debug signer. If AGP lifecycle behavior makes this separation impossible without patching generated files, stop and return to architecture; do not weaken the signer boundary.

## Build a verified candidate packet

For a private local candidate or hosted rehearsal, run only from the workspace root in an operator terminal that has the required non-secret tools, JDK 17, Android SDK/NDK specified by the contract, recursive submodules, and the four signing variables. Do not run password-bearing commands through a terminal/logging integration that captures environment values.

```sh
tools/release/build-android-release.fish
```

[`build-android-release.fish`](../tools/release/build-android-release.fish:12) is the only final-candidate entry point. It performs these ordered safeguards:

1. validates the release contract, structured tools, Android toolchain, SDK layout, JDK, submodules, and ignored owned-output locations;
2. validates the signing boundary without exposing secrets;
3. records immutable source/submodule/build-input state;
4. probes the exact Android CPU/optimization policy, then builds or validates fingerprinted `cortex-a73`/`-O3` Android libjxl and pinned Projucer outputs without LTO;
5. regenerates Android/JUCE output and runs identity/generated-project assertions;
6. builds only the explicit signed `:app:assembleRelease_Release` variant;
7. finds exactly one output matching contract identity, then verifies the generated APK;
8. copies the bytes into a private staging directory, re-verifies them, emits non-secret provenance and checksum/verification sidecars; and
9. atomically replaces [`dist/release`](../dist/release) only after all gates pass.

A failed or interrupted candidate must not replace an existing verified packet. When a prior accepted packet occupies [`dist/release`](../dist/release), the coordinator first validates it against its own retained provenance, checksum, verification evidence, signer, structural APK evidence, and lower version code. It never judges historical bytes against the successor contract. A malformed or unverifiable prior packet is a stop condition; do not manually populate, delete, move, or rename [`dist/release`](../dist/release) to bypass it.

## Artifact packet and verification

A final candidate directory contains exactly four public-safe files:

1. the contract-named APK;
2. a SHA-256 checksum sidecar;
3. non-secret provenance; and
4. retained pre/post publication verification evidence.

[`verify-android-apk.fish`](../tools/release/verify-android-apk.fish:1) validates the final artifact. Its underlying checks include certificate identity/signature, alignment, package/version/SDK/label, non-debuggable manifest, forbidden permissions, permitted component shape, ABI/native-library inventory, stripped AArch64 output, launcher resources, and final-directory isolation from probes. Verification runs before and after atomic publication; the staged and published bytes must match.

Provenance binds the candidate to its contract, source/submodule/build-input state, toolchain and dependency fingerprints, and verifier output without retaining secrets. Treat the packet as the evidence authority for its own exact hash and contents.

## Upgrade probe

An upgrade probe proves installation compatibility; it is never a deliverable. Use it only after a signed current-base candidate has passed the required baseline device flow:

```sh
tools/release/build-android-upgrade-probe.fish
```

[`build-android-upgrade-probe.fish`](../tools/release/build-android-upgrade-probe.fish:55) reuses the same package, ABI, source identity, and signing key while creating only the adjacent version-code probe. It does not regenerate project authority and publishes only below [`dist/non-final`](../dist/non-final). [`verify-android-upgrade-probe.fish`](../tools/release/verify-android-upgrade-probe.fish:1) enforces its separate name/location and signer/version relationship. Never move a probe into [`dist/release`](../dist/release), distribute it as a release, or let it overwrite final evidence.

## Candidate acceptance

Before a public prerelease can be promoted to the stable/latest release, retain evidence for its exact APK that covers:

1. host source tests, localization validation, static/relevant sanitizer checks, and release-tool tests;
2. clean regeneration plus generated-project validation from tracked inputs;
3. independent artifact verification and final four-file inventory;
4. fresh Android 14 installation of the exact APK;
5. core user flow: create storage, create item, import photo, view it, export JPEG/open it externally, export a normal backup, force-stop/cold-launch, and confirm persistence;
6. same-key upgrade using a separately built non-final adjacent-code probe, with catalog/photo preservation confirmed;
7. fresh final installation followed by staged, explicitly confirmed restore of the retained backup; and
8. safe evidence for device/API, package/signature identity, steps, expected/actual behavior, screenshots/logs, and any failure classification.

The current non-LTO tuning additionally requires proof that the final native library is AArch64, the generated project and dependency builder use `cortex-a73` and `-O3`, no prohibited fast-math or LTO flag is present, and the pinned NDK capability probe exposes the required Armv8-A feature floor. A successful local Android 14 cold-start smoke test is useful compatibility evidence only; it is not representative acceptance when its CPU topology differs from the required Snapdragon 680/685 Kryo 265 target class.

For the representative-device gate, retain the exact APK identity and verify on a Snapdragon 680/685 Kryo 265 Android 14 device: supported feature evidence, cold launch, catalog/search, JPEG XL import/preview/export, backup/restore, and same-key upgrade. Compare the complete workload, artifact size, and measured performance against the pre-change baseline. A regression, SIGILL, crash, ANR, corruption, unsupported feature result, or unacceptable performance regression blocks acceptance.

A crash, ANR, signature/install failure, lost/corrupted data, unexpected permission/component/network behavior, inability to complete the core path, failed upgrade/restore, private-material exposure, or mismatch between delivered bytes and accepted evidence is a release blocker.

A change after acceptance creates a new candidate requirement. Do not retarget an earlier tag/packet or represent historical device evidence as acceptance of changed source. Rebuild, reverify, and rerun the affected acceptance matrix under a newly advanced version name/code and a new tag.

## Automated candidate publication and manual promotion

Project-owned material is licensed as `AGPL-3.0-or-later` under [`LICENSE`](../LICENSE:1). The corresponding attribution boundary is recorded in [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md:1). JUCE 9 remains dual-licensed under AGPLv3 or the JUCE 9 commercial licence; this project selects the AGPL route, as recorded in [`third_party/JUCE/LICENSE.md`](../third_party/JUCE/LICENSE.md:1). A verified APK alone remains insufficient authorization for external distribution.

Advance release identity atomically on `master` before creating a candidate: update [`release/release.properties`](../release/release.properties:1), authoritative project identity, artifact basename, and Android version code; regenerate and validate; commit the complete release source; and prepare reviewed Markdown release notes. Create one **unsigned annotated final-form tag** whose name is exactly `v<app.version_name>` and whose complete annotation message is those release notes. Do not use an RC-suffixed tag, lightweight tag, signed tag, empty annotation, or a tag outside `master` history.

For example, after replacing the version with the newly reviewed contract identity:

```sh
git tag --annotate --no-sign v1.0.3 --file path/to/reviewed-release-notes.md
git push origin v1.0.3
```

The tag push is the candidate-publication request. [`ci.yml`](../.github/workflows/ci.yml:1) runs every required host/release-tool/static/sanitizer lane against that exact tag. Only after all required jobs succeed does its tag-only final job dispatch [`android-release.yml`](../.github/workflows/android-release.yml:1). The release workflow validates the upstream CI run, remote annotated tag object, peeled commit, `master` ancestry, contract version, and absence of an existing release before using signing secrets.

The signed-build job creates and verifies the exact four-file packet, extracts the tag annotation through [`shuba_exact_source_write_tag_notes()`](../tools/release/lib/exact-source.fish:210), builds deterministic recursive corresponding source, and constructs the complete public bundle. A separate read-only job downloads and fully re-verifies that public-safe bundle against the exact tag. Only its successful dependent publication job receives release-write permission; that no-checkout job creates a non-draft GitHub **prerelease** on the existing tag, uploads exactly the verified twelve assets, and proves their GitHub-reported names, sizes, and SHA-256 digests. GitHub's automatic source archives are not the corresponding-source asset because they omit recursive submodule contents.

The tracked builders remain the local/offline equivalent and the executable authority beneath hosted publication:

```sh
tools/release/build-exact-source.fish --tag v1.0.3 --output dist/public-source
tools/release/build-publication-bundle.fish \
  --tag v1.0.3 \
  --packet-dir path/to/accepted-four-file-packet \
  --source-dir dist/public-source \
  --release-notes path/to/reviewed-release-notes.md \
  --output dist/public-bundle
tools/release/verify-publication-bundle.fish --tag v1.0.3 --bundle-dir dist/public-bundle
```

[`build-exact-source.fish`](../tools/release/build-exact-source.fish:1) accepts only the annotated `v<contract-version>` tag and clean initialized recursive checkouts. It exports Git objects at the tag and recorded gitlink commits, rather than copying the worktree. It emits version-named source, checksum, mode/hash inventory, and recursive commit-map assets. The archive has one version-named root, sorted entries, numeric `0:0` ownership, normalized public modes, and the annotated tagger time as its deterministic timestamp. It excludes Git metadata, generated Android/JUCE output, build/release output, unsafe links and paths, local configuration, and signing/private-key material.

[`build-publication-bundle.fish`](../tools/release/build-publication-bundle.fish:1) copies—without rebuilding or changing—the four-file APK packet, exact-source assets, standalone [`LICENSE`](../LICENSE:1) and [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md:1), and reviewed release notes. It writes the version-named `SHA256SUMS`, which hashes every other asset and never itself. Its verifier rejects unexpected inventory, unsafe modes, APK/checksum/provenance/tag mismatches, source-graph drift, private material, and checksum errors. The public GitHub prerelease must upload exactly the verified bundle bytes.

Perform representative-device testing only against the APK downloaded from that prerelease. If the candidate is rejected, retain its tag, release record, and assets unchanged as historical evidence. A correction consumes a new semantic version, a greater Android version code, a new final-form tag, fresh CI/build assets, and affected device acceptance. Never move/recreate a rejected tag or delete, replace, rename, or force-upload its assets.

After acceptance, manually dispatch `Android release` from `master` with `operation=promotion`, the accepted `tag`, and identical `confirm_tag`. A read-only job downloads all twelve assets from the existing prerelease, checks the tag annotation still equals the release body, reruns complete bundle/APK/source/checksum verification, and snapshots the release ID, tag object/commit, and every asset identity into immutable same-run evidence. Its no-checkout dependent job then waits for protected `android-release-promotion` approval, rechecks the live identities against that evidence, and patches only the existing GitHub Release title, prerelease flag, and latest designation. It does not build, reupload, copy, delete, or rename artifacts and proves the same release ID, tag, body, tag object/commit, and asset IDs/digests remain afterward.

Environment provisioning, workflow permissions, failure handling, private rehearsal, automatic candidate operation, and exact promotion inputs are documented in [`docs/ci.md`](ci.md).

Android API 35 adoption remains a separate platform-compatibility decision. The current JUCE 9 migration intentionally retains API 34 as the minimum, target, and compile SDK declared by [`release/release.properties`](../release/release.properties:8).

## Stop conditions

Return to architecture rather than adding ad hoc work if any of these conditions occur:

- generated JUCE output cannot express a needed build/signing change without a manual patch;
- the signing key cannot be safely backed up or validated against the pinned public certificate;
- a release/probe cannot be isolated from final artifact output;
- dependency fingerprints or generated-project validation are not reproducible from tracked inputs;
- artifact inspection finds an unexpected ABI, permission, component, native dependency, or debug residue;
- Android acceptance exposes a crash, ANR, data loss, unbounded UI work, or unsupported provider behavior; or
- release tooling would require a custom parser, secret exposure, weakened rollback/publication behavior, or an uncontrolled compatibility workaround.

For hosted workflow operation and secret provisioning, use [`docs/ci.md`](ci.md). For regular local development gates, use [`docs/development.md`](development.md).
