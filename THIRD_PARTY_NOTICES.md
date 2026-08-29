# Shuba third-party notices

This notice inventory applies to the Android Release APK produced from the exact
source commit and recursive submodule commits recorded in its provenance. It is
an attribution and source-location record, not a relicensing statement. The
project-owned material is licensed separately in [`LICENSE`](LICENSE). Every
third-party component remains available under the license, notices, and other
terms supplied by its own copyright holders.

The complete corresponding-source asset released with an accepted APK contains
the pinned source files and their original license and attribution texts. The
paths below are relative to that asset's repository root. GitHub's automatic
source archives are not the corresponding-source asset because they do not
include recursive submodule contents.

## Shipped in the Android Release APK

The release build links these components into `libjuce_jni.so` or packages Java
glue compiled from the selected JUCE Android sources. `release/release.properties`
and the APK verification evidence identify the exact contract and artifact; the
source paths below identify the authoritative license text in the pinned source
graph.

| Component | Role in the shipped APK | Version / pinned source | License or notice source |
| --- | --- | --- | --- |
| JUCE | Six framework modules: `juce_core`, `juce_events`, `juce_graphics`, `juce_data_structures`, `juce_cryptography`, and `juce_gui_basics`; includes the generated JUCE Android Java glue. | JUCE 9.0.1, commit `e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8` | [`third_party/JUCE/LICENSE.md`](third_party/JUCE/LICENSE.md) — used under the AGPLv3 route. |
| JUCE Android Java glue | `JuceApp.java` and `Java.java`, compiled from the JUCE Android source sets selected by the generated app project. | Same JUCE commit as above | [`third_party/JUCE/LICENSE.md`](third_party/JUCE/LICENSE.md); source headers also identify the AGPLv3 alternative. |
| zlib (JUCE embedded) | ZIP, GZIP, and PNG support in the selected JUCE modules. | Pinned within JUCE | [`third_party/JUCE/modules/juce_core/zip/zlib/README`](third_party/JUCE/modules/juce_core/zip/zlib/README) — zlib License. |
| pnglib (JUCE embedded) | PNG image support in `juce_graphics`. | Pinned within JUCE | [`third_party/JUCE/modules/juce_graphics/image_formats/pnglib/LICENSE`](third_party/JUCE/modules/juce_graphics/image_formats/pnglib/LICENSE) — libpng License. |
| Independent JPEG Group JPEG | JPEG image support in `juce_graphics`. | Pinned within JUCE | [`third_party/JUCE/modules/juce_graphics/image_formats/jpglib/README`](third_party/JUCE/modules/juce_graphics/image_formats/jpglib/README), “LEGAL ISSUES” — Independent JPEG Group terms. |
| HarfBuzz | Text shaping used by `juce_graphics`. | Pinned within JUCE | [`third_party/JUCE/modules/juce_graphics/fonts/harfbuzz/COPYING`](third_party/JUCE/modules/juce_graphics/fonts/harfbuzz/COPYING) — Old MIT License. |
| SheenBidi | Bidirectional-text processing used by `juce_graphics`. | Pinned within JUCE | [`third_party/JUCE/modules/juce_graphics/unicode/sheenbidi/LICENSE`](third_party/JUCE/modules/juce_graphics/unicode/sheenbidi/LICENSE) — Apache License 2.0. |
| LunaSVG | SVG parsing and rendering compiled by `juce_graphics`. | Pinned within JUCE | [`third_party/JUCE/modules/juce_graphics/drawables/lunasvg/LICENSE`](third_party/JUCE/modules/juce_graphics/drawables/lunasvg/LICENSE) — MIT License. |
| PlutoVG | Vector graphics implementation compiled through LunaSVG. | Pinned within JUCE | [`third_party/JUCE/modules/juce_graphics/drawables/lunasvg/plutovg/LICENSE`](third_party/JUCE/modules/juce_graphics/drawables/lunasvg/plutovg/LICENSE) — MIT License. |
| stb_image | Image loading compiled by PlutoVG. | Pinned within JUCE | [`third_party/JUCE/modules/juce_graphics/drawables/lunasvg/plutovg/source/plutovg-stb-image.h`](third_party/JUCE/modules/juce_graphics/drawables/lunasvg/plutovg/source/plutovg-stb-image.h), license at file end — MIT License or public-domain dedication, at the recipient’s option. |
| stb_image_write | Image writing compiled by PlutoVG. | Pinned within JUCE | [`third_party/JUCE/modules/juce_graphics/drawables/lunasvg/plutovg/source/plutovg-stb-image-write.h`](third_party/JUCE/modules/juce_graphics/drawables/lunasvg/plutovg/source/plutovg-stb-image-write.h), license at file end — MIT License or public-domain dedication, at the recipient’s option. |
| stb_truetype | Font processing compiled by PlutoVG. | Pinned within JUCE | [`third_party/JUCE/modules/juce_graphics/drawables/lunasvg/plutovg/source/plutovg-stb-truetype.h`](third_party/JUCE/modules/juce_graphics/drawables/lunasvg/plutovg/source/plutovg-stb-truetype.h), license at file end — MIT License or public-domain dedication, at the recipient’s option. |
| FreeType-derived PlutoVG portions | The bundled `plutovg-ft-*` raster, stroker, math, and type portions. | Pinned within JUCE | [`third_party/JUCE/modules/juce_graphics/drawables/lunasvg/plutovg/source/FTL.TXT`](third_party/JUCE/modules/juce_graphics/drawables/lunasvg/plutovg/source/FTL.TXT) — FreeType License. Required binary-distribution credit: “Portions of this software are copyright © 1996-2002, 2006 The FreeType Project (www.freetype.org). All rights reserved.” |
| Glaze | Header-only JSON serialization used by project code. | v7.7.0, commit `dc90e3a31ef9b93e53efb8320aae1a36f7abd80f` | [`third_party/glaze/LICENSE`](third_party/glaze/LICENSE) — MIT License. |
| spiritless_po | Header-only PO parser used by project localization code. | commit `c27f703ebc86bb50be975ac4d6f964cd3ae5a500` | [`third_party/spiritless_po/LICENSE`](third_party/spiritless_po/LICENSE) — Boost Software License 1.0. |
| libjxl | JPEG XL decoder/encoder and color-management libraries linked statically. | v0.12.0, commit `a7a9c787341cf703dede03c2009fa460cae5e5df` | [`third_party/libjxl/LICENSE`](third_party/libjxl/LICENSE) — BSD 3-Clause License; [`third_party/libjxl/PATENTS`](third_party/libjxl/PATENTS) — additional patent grant. |
| Brotli | Static codec libraries linked by libjxl. | commit `028fb5a23661f123017c060daa546b55cf4bde29` | [`third_party/libjxl/third_party/brotli/LICENSE`](third_party/libjxl/third_party/brotli/LICENSE) — MIT License. |
| Highway | Static SIMD library linked by libjxl. | commit `457c891775a7397bdb0376bb1031e6e027af1c48` | [`third_party/libjxl/third_party/highway/LICENSE`](third_party/libjxl/third_party/highway/LICENSE) — Apache License 2.0. |
| skcms | Static color-management implementation compiled by libjxl. | commit `96d9171c94b937a1b5f0293de7309ac16311b722` | [`third_party/libjxl/third_party/skcms/LICENSE`](third_party/libjxl/third_party/skcms/LICENSE) — BSD 3-Clause License. |

### Excluded source-graph dependencies

The recursive libjxl checkout also records testdata, GoogleTest, lcms, libjpeg-turbo,
libpng, sjpeg, and zlib commits. The Release configuration disables the relevant
optional libjxl features or uses its explicitly selected dependencies, so these
items are **not** asserted to be incorporated into the APK. They remain in the
complete corresponding-source archive because it reproduces the pinned recursive
source graph, with their original notices preserved there.

## Build and test prerequisites, not APK runtime dependencies

These tools or host libraries are used to generate, assemble, test, inspect, or
validate a candidate. They are not packaged as Android runtime dependencies in
the Release APK; the generated app’s Gradle `dependencies` block is empty and
APK verification remains the final evidence.

| Component | Use | License / notice source |
| --- | --- | --- |
| Gradle Wrapper 8.13 | Generated Android build launcher and distribution selector. | Generated wrapper’s `gradle/wrapper/LICENSE-for-gradlewrapper.txt` — Apache License 2.0. It is disposable generated output, not a tracked source authority. |
| Android Gradle Plugin 8.13.2 | Android build plugin resolved by Gradle. | Declared in the generated project; Android build tooling is not packaged in the APK. Consult the version’s upstream distribution notice when redistributing build tooling itself. |
| Android SDK, NDK, JDK, CMake, command-line tools | Android compilation, packaging, signing, and inspection toolchain. | External toolchain prerequisites; they are neither project source nor APK runtime payload. |
| Catch2 | Linux headless test framework. | Host/test prerequisite resolved by the CI environment; not packaged in the APK. |
| Host system libraries and analysis tools | Host JUCE support, libjxl test support, localization, sanitizers, and static analysis. | Development/test prerequisites supplied by the host or CI image; not packaged in the APK. |

## Project-owned material

Unless an individual file states otherwise, the project-owned application code,
documentation, release tooling, tests, localization material, Android resources,
and Shuba application icons are copyright © 2026 homremwork and licensed under
`AGPL-3.0-or-later` in [`LICENSE`](LICENSE). The retained
`docs/archive/icon-concepts-archive.tar.zst` contains project-owned artwork
concepts and is covered by that same project license. This statement does not
claim ownership of, or change the terms for, third-party material identified
above or preserved in the corresponding-source asset.

## Release-maintainer obligations

Before publishing an APK, the release maintainer must verify this inventory
against the exact generated Release project and the accepted APK’s verification
evidence. A new packaged dependency, source-graph change, or build-policy change
requires an inventory update and a fresh review before public distribution. The
public GitHub Release must publish this notice, [`LICENSE`](LICENSE), the
checked corresponding-source asset, and the release asset checksum manifest.
