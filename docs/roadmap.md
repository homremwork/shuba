# Roadmap and task framing

## Current position

Shuba has a working local catalog implementation with automated host coverage and prior Android acceptance evidence for the core flow. The tracked source has advanced to the publication identity in [`release/release.properties`](../release/release.properties:1): `1.0.2` and Android code `3`. Local Android 14 base/smoke testing is accepted; existing external testing remains candidate evidence until it is compared against the exact hosted APK bytes.

The current Android native policy is `arm64-v8a`, `-mcpu=cortex-a73`, and safe Release `-O3` with an Armv8-A + NEON + AES + SHA2 + CRC32 feature floor. It intentionally does not require Armv8.2-A and uses no LTO, `-Ofast`, or fast-math. The attempted ThinLTO gate was rejected because JUCE's bare Android `-flto` output becomes full LTO under the pinned NDK, not ThinLTO. No runtime, host-dependent, or generated-file fallback is permitted.

The current source also contains the post-acceptance corrections that:

- make the item-detail storage destination an explicit localized action rather than a duplicate edit route;
- apply all four Android/JUCE safe-area insets to interactive shell layout; and
- clear stale edit feedback without suppressing a genuine unassigned-item acknowledgement.

Those corrections have focused regression coverage, complete host/localization validation, generated-project validation, Android Debug build evidence, and manual UI verification. They have not yet been packaged into a new signed release candidate. Previous acceptance remains historical evidence for its exact source/artifact only.

## Next required block: publication-candidate construction and Android acceptance

This is the immediate project gate. Its owner is the release boundary defined by [`docs/release.md`](release.md) and [`release/release.properties`](../release/release.properties:1), not the UI implementation layer.

### Scope

1. On a Snapdragon 680/685 Kryo 265 Android 14 device, capture CPU feature evidence and cold-launch the exact `cortex-a73`/`-O3`, non-LTO APK.
2. Confirm the corrected item-detail actions, English/Russian text, fullscreen safe-area behavior in gesture and three-button navigation modes, rotation/runtime inset changes, and assigned/unassigned feedback behavior.
3. Repeat catalog/search, JPEG XL import/preview/export, persistence, backup, restore, and same-key upgrade validation.
4. Compare artifact size and representative workload performance against the pre-change baseline; classify any regression before changing tuning again.
5. A local Android 14 smoke launch is compatibility-only evidence. It does not replace the required Snapdragon 680/685 Kryo 265 gate when its CPU topology differs.
6. Build and verify the `1.0.2` / code `3` signed candidate through [`build-android-release.fish`](../tools/release/build-android-release.fish:12). It must validate the historical packet against its retained provenance before atomically replacing it.
7. Build a non-final adjacent-code upgrade probe only after the final candidate passes its baseline flow; prove same-key upgrade and backup restoration as described in [`docs/release.md`](release.md).
8. Complete the publication gates only after candidate-specific evidence is complete, including comparison of existing external evidence with the exact hosted APK bytes.

### Completion evidence

- the final directory contains only a verified APK, checksum, non-secret provenance, and verification evidence;
- the delivered APK’s identity matches [`release/release.properties`](../release/release.properties:1);
- the device evidence identifies the exact APK and confirms no release-blocking crash, ANR, data loss, signature mismatch, unexpected permission, or unsafe overlap;
- the non-final probe remains isolated below [`dist/non-final`](../dist/non-final); and
- a later source change creates a new candidate obligation rather than modifying accepted artifact history.

## Deliberate limitations and deferred scope

These are intentional boundaries, not silent defects. Reopen one only with evidence and a narrow owning task.

| Boundary | Current policy | Reopen when |
| --- | --- | --- |
| Photo preview artifacts | No persisted thumbnail files; lazy decode with bounded in-memory preview cache. | Android measurement shows unacceptable responsiveness or memory behavior and a cache-only adjustment is insufficient. |
| JPEG XL encode progress | The UI may show coarse start/completion progress because the codec does not expose reliable intermediate progress. | A codec/API change or measured UX issue supports a bounded, truthful progress improvement. |
| Photo selection | Use the existing system/JUCE document path with worker-side provider inspection and staging. | A representative Android provider demonstrates unrecoverable picker/URI behavior after the current thread boundary is verified. |
| Storage hierarchy | The domain supports nesting; UI remains intentionally simple and storage-aware. | Real catalog use requires a more capable hierarchy browser/navigation model. |
| Deletion | Archive is the visible lifecycle action; hard deletion remains guarded by multi-file safety behavior. | A use case and deterministic metadata/media/recovery proof define safe owner deletion. |
| Search | In-memory global item/storage search with simple filters; no persisted index/cache. | Target-scale measurement shows a real UX/performance problem. |
| Android scope | Android 14/API 34, arm64-v8a, app-private local data, picker grants only. | A separate compatibility or distribution decision funds ABI/API/permission expansion. |
| Publication | No store publishing or public distribution process is defined. | A release decision defines distribution, corresponding source, notices, and release communications. |

Cloud synchronization, shared/multi-device catalogs, marketplace automation, image recognition, direct camera capture, broad-media access, SQL migration, and advanced accounting remain outside the first-version product boundary.

## Discovery after the candidate gate

After the current candidate is accepted, conduct discovery as observation and triage—not automatic feature work. Explore provider diversity, multiple photo sources, damaged archives/catalogs, low-storage/interruption recovery, preview responsiveness/memory, localization layout breadth, target-scale behavior, and future publication readiness.

For every finding, preserve the candidate identity, device/runtime context where relevant, exact reproduction, expected/actual behavior, severity, and safe evidence. Classify it before implementation:

- release blocker;
- correctness defect;
- compatibility limitation;
- performance issue;
- publication blocker;
- postponed enhancement; or
- false positive/non-reproducible observation.

A release blocker owns a focused correction, a new identified candidate, and affected revalidation. Other findings remain backlog input until explicitly accepted.

## How to define the next small task

Each implementation task must fit one owner and be reviewable without reconstructing deleted planning history. A task brief should contain:

1. **Owner:** one module or subsystem, such as [`Source/Persistence`](../Source/Persistence), [`Source/Catalog`](../Source/Catalog), [`Source/Platform`](../Source/Platform), [`Source/UI/Session`](../Source/UI/Session), [`Source/UI/Screens`](../Source/UI/Screens), or release tooling.
2. **Problem and evidence:** observed behavior, affected platform/data state, and why it matters; no speculative root cause stated as fact.
3. **Invariant:** exact existing rule that must remain true, linked to [`docs/architecture.md`](architecture.md) and the live code contract.
4. **Smallest accepted outcome:** what changes and what is explicitly out of scope.
5. **Validation:** focused test files/commands, wider host gates when shared contracts change, and device/release evidence where the boundary cannot be proven on Linux.
6. **Stop condition:** the point at which an unexpected compatibility, persistence, lifetime, generated-project, signing, or scope issue must return to architecture rather than expand the patch.

Do not use historical stage numbers as requirements. The stable project contracts and present test behavior are the basis for future tasks.
