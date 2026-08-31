# Roadmap and task framing

## Current position

Shuba `1.0.2`, Android code `3`, is the current [public GitHub Release](https://github.com/homremwork/shuba/releases/tag/v1.0.2). Its exact Android artifact, recursive corresponding source, license, notices, checksums, provenance, hosted CI, and device acceptance are complete. The annotated `v1.0.2` tag and its Release assets are fixed evidence for that accepted source and those exact bytes; later `master` changes are post-release work and do not modify or inherit that acceptance.

The current Android native policy is `arm64-v8a`, `-mcpu=cortex-a73`, and safe Release `-O3` with an Armv8-A + NEON + AES + SHA2 + CRC32 feature floor. It intentionally does not require Armv8.2-A and uses no LTO, `-Ofast`, or fast-math. The attempted ThinLTO gate was rejected because JUCE's bare Android `-flto` output becomes full LTO under the pinned NDK, not ThinLTO. No runtime, host-dependent, or generated-file fallback is permitted.

The published release contains the corrections that:

- make the item-detail storage destination an explicit localized action rather than a duplicate edit route;
- apply all four Android/JUCE safe-area insets to interactive shell layout; and
- clear stale edit feedback without suppressing a genuine unassigned-item acknowledgement.

Those corrections have focused regression coverage, complete host/localization validation, generated-project validation, signed artifact evidence, and accepted Android verification. Any new source change still creates a new candidate obligation before distribution.

## Next required block: post-release triage and narrow maintenance

The `1.0.2` publication block is closed. No implementation block is selected automatically by publication completion. The next task must be chosen from observed post-release behavior, routine dependency maintenance, or an explicitly approved product change and must retain one clear owner.

### Scope

1. Triage new reports and maintenance updates without treating them as automatic feature work.
2. Select one owning subsystem and one observable problem or maintenance obligation.
3. Preserve the accepted `v1.0.2` tag, Release assets, and evidence unchanged.
4. Define focused tests and the smallest sufficient wider validation for the selected boundary.
5. If a change is to be distributed, advance release identity first and complete a new candidate, hosted verification, and affected Android acceptance under [`docs/release.md`](release.md).

### Completion evidence

- the task records one owner, evidence-backed scope, preserved invariant, and stop condition;
- focused tests cover the changed behavior or maintenance contract;
- wider validation is proportional to the affected shared, platform, persistence, or release boundary;
- released tags and assets remain unchanged; and
- a distributed change has a new identity and evidence rather than reusing `v1.0.2` acceptance.

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
| Publication | GitHub Release distribution is defined with an exact APK, recursive corresponding source, license, notices, provenance, and checksums; no store channel is defined. | A new version, another distribution channel, or a changed publication policy is explicitly approved. |

Cloud synchronization, shared/multi-device catalogs, marketplace automation, image recognition, direct camera capture, broad-media access, SQL migration, and advanced accounting remain outside the first-version product boundary.

## Discovery after publication

Conduct post-release discovery as observation and triage—not automatic feature work. Explore provider diversity, multiple photo sources, damaged archives/catalogs, low-storage/interruption recovery, preview responsiveness/memory, localization layout breadth, target-scale behavior, and future release readiness.

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
