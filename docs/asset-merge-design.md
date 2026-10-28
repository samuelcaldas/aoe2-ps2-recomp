> [!IMPORTANT]
> **CONSOLIDATED DOCUMENTATION NOTICE**
> This document is historical/fragmented. Its architectural conclusions, tooling commands, and evidence have been unified into the canonical Source of Truth: [`docs/sot/hd-assets-strategy.md`](docs/sot/hd-assets-strategy.md). Please refer to `docs/sot/hd-assets-strategy.md` for current decisions and implementation status.

---

# HD asset merge design for the AoE2 PS2 recompilation

## Context

The intended merge is a **runtime override**, not a replacement of the PS2 data archive or a build-time rewrite of generated game code. The recompiled guest should continue requesting the assets it expects. At a verified graphics seam, a host-side resolver may substitute a compatible HD asset; otherwise it must preserve the original PS2 behavior.

That design is not implemented yet. The codebase has optional tools to import, catalog, stage, and verify loose HD files and a C++ provider that reads loose files by numeric ID. There is no demonstrated game-runtime initialization path calling that provider, no SLP pixel decoder, no verified PS2-to-HD mapping table, and no established gameplay draw hook. Therefore HD assets are **not proven to render in-game**. The goal in `docs/architecture-hd-asset-decoupling.md` is an architectural target, not evidence that this merge already works.

This document distinguishes:

- **Implemented:** behavior directly present in tracked code or tests.
- **Observed:** facts inspected in the locally available, user-owned files.
- **Hypothesis / proposal:** a plausible design that still needs runtime evidence and acceptance checks.
- **Unknown:** not established by the inspected source or files.

## Current implementation

### Build, stage, provider, and runtime truth table

| Area | Implemented / observed behavior | Not implemented or not proven |
|---|---|---|
| HD import/catalog | `tools/import_hd_assets.py` copies requested categories into ignored `game/hd-edition/source/`, builds a local `catalog.json` with source-relative path, numeric ID where filename is decimal, sniffed format, size, and SHA-256. Tracked `hd_assets_manifest.json` contains aggregate counts and category digest only. | The local source/catalog may change independently of the tracked summary; refresh and verify both against the current install before relying on counts or digests. |
| Staging | `tools/docker_build.py` prepends stage + verification for desktop, Android, and Switch only if `game/hd-edition/catalog.json` exists. The stage preserves source bytes and original filenames, placing files at `assets/<category>/<filename>`. Desktop and Android stage beside the PS2 data in `game/SLES-50282/assets/`; Switch stages to `build/hd-stage/switch/assets/`. | Staging does not decode or convert assets, establish ID correlations, embed assets, or install them on a Switch SD card. |
| C++ provider | `IAssetProvider` has `Initialize`, `HasAsset`, `LoadAsset`, and string-table methods. `LooseHdAssetProvider` searches a single supplied root's category folder for `<decimal-id>` with `.slp`, `.bina`, `.wav`, `.mp3`, `.dat`, `.txt`, or no extension, returning raw bytes. Its sniffer labels `2.0N` SLP, RIFF, and BINA only. | It is **not** a compositional overlay provider. It has no PS2 fallback provider, no manifest/mapping lookup, no JASC-PAL or BMP recognition, and no decoding/palette service. It cannot find nonnumeric filenames such as `empires2_x1_p1.dat` with numeric-ID lookup. |
| Runtime root | `AssetPathResolver` accepts an explicit override; defaults are `assets` (or existing XDG path) on desktop, relative `assets` on Android, and `sdmc:/switch/aoe2/assets` on Switch. | No proven game-runtime initialization path ties staged roots to `ResolveRoot()` or calls `LooseHdAssetProvider`. Defaults do not automatically match all stage locations. |
| Android | Android build runs Gradle; `tools/validate_android.py` pushes the contents of `game/SLES-50282/` to the app external-files directory. If staged locally, `assets/` is copied along with the PS2 data. | Validation does not explicitly pass an asset root to the provider; it validates process liveness/progressing logs/changed frames, not HD asset loading or rendering. |
| Switch | Staging outputs a separate `build/hd-stage/switch/assets/`; docs call for manual copy to `sdmc:/switch/aoe2/assets/` next to the NRO. | No automatic SD deployment, RomFS packaging, or runtime asset initialization has been demonstrated. |
| HUD geometry | `HudLayout` and `HudInverseMapper` are geometry primitives. They validate and place source/destination rectangles and map relocated display touches back to guest coordinates. | No actual game HUD regions, source SLP IDs, renderer hook, or input dispatch integration is established. These primitives do not draw assets. |

Relevant implementation: `assets/include/aoe2/assets/asset_provider.hpp`, `assets/src/loose_hd_asset_provider.cpp`, `assets/src/asset_path_resolver.cpp`, `tools/hd_assets/stage_plan.py`, `tools/stage_hd_assets.py`, `tools/docker_build.py`, `tools/validate_android.py`, `renderer/src/hud_layout.cpp`, and `input/src/hud_inverse_mapper.cpp`. See also `docs/hd-assets.md` and `docs/plans/hd-widescreen-handoff.md`.

### Format decoding status: HD Edition

The available HD v5.8 files are under the user-owned install at `game/hd-edition/install/Age.of.Empires.II.HD.v5.8/Age Of Empires II HD/`; imported examples are under `game/hd-edition/source/resources/_common/drs/interface/`. The HD edition's relevant art is loose under `resources/_common/drs/`, not a `.drs` archive in this profile. It is a distinct source from the PS2 disc's `DATA.CAT` / `ALL_DATA.BIN`.

The local interface directory has **354 files**. Its filenames/extensions alone are misleading: among 134 `.bina`-extension files, content sniffing identifies 32 JASC-PAL palettes, 8 BMP images, and 94 other/unknown BINA or UI/table payloads. The directory also contains 170 SLP 2.0N files and 50 RIFF WAV files. These are observed content-signature/catalog counts, not counts by extension. The tracked `hd_assets_manifest.json` currently records the same interface totals and format buckets (170 SLP, 50 RIFF, 32 JASC-PAL, 8 BMP, 94 unknown); it is a digest summary rather than a live scan, so re-verify freshness if local files or sniffing rules change.

**SLP facts and decoder status:** `tools/hd_assets/slp_header.py` reads a 32-byte `<4si24s` file header, checks the `2.0N` signature and positive frame count, then reads one 32-byte `<4I4i` first-frame record and reports dimensions. For example, inspected file `50100.slp` declares one frame and a first frame of 800×600. This is only header/first-frame metadata: the local tooling does **not** parse all frames, validate offsets/tables, decode drawing commands, or produce pixels. The command stream, palette-index pixels, player colors, transparency, and shadows therefore are not currently available to the runtime.

**Palette facts and decoder status:** the strict Python `read_jasc_palette()` parser checks ASCII `JASC-PAL`, version `0100`, exactly 256 RGB triples, and byte-range channels, then returns opaque RGBA entries. It has been exercised against the 32 local HD palette files. This is host-side validation, not a C++ runtime palette loader and not proof of how any PS2 asset's colors are represented or selected. BMP bytes are preserved and cataloged but no runtime BMP decoder path is present.

### Format decoding status: PS2

The owned PS2 data root is `game/SLES-50282/`. The extracted disc includes `ALL_DATA.BIN`, `DATA.CAT`, and regional directories such as `EN/`, `FR/`, `GR/`, `IT/`, and `SP/`, with regional overlay binaries. `tools/import_owned_disc.py` extracts the owned ISO tree after checking the expected executable hash and records an import marker; it does not parse `DATA.CAT`, index `ALL_DATA.BIN`, or decode overlay payloads. The project describes the pair at a high level as archive and catalog, but this inspection did not verify their binary structure.

The owner-authorized matching local decompilation at `../aoe` was read-only and used only for behavior research. No reliable evidence was found there that establishes the PS2 asset-container layout, sprite encoding, graphic IDs, palette format/selection, or a correlation to HD IDs. No names, addresses, or code from that decompilation are reproduced here. A bounded search that found no such evidence is not proof that no relevant logic exists.

**Unknowns:** whether PS2 graphics are SLP-compatible or use a different encoding; how archive and regional overlay records are indexed; which guest IDs correspond to graphic resources; how palettes are selected; and where gameplay sprite requests can be intercepted without changing generated files. Existing project prose that assumes a PS2 palette/sprite pipeline should be treated as a hypothesis until confirmed from owned-file bytes and runtime behavior. The high-level rendering strategy favors a logical sprite/tile/UI replacement over GS emulation, but the actual gameplay draw seam remains unresolved. Do not assume `kt_gpu` or any particular subsystem is the sprite-rendering seam.

### Exploratory overlay inventory status

Any candidates produced solely by matching file sizes are exploratory, not verified asset or overlay identities. No such matches are used as evidence in this design. An overlay map must first be verified from container/index structure and owned bytes.

## Proposed asset resolution flow

**Proposal, not current behavior:** add a resolver at the verified logical asset-request seam, before resource decoding/upload. Keep PS2 as the compatibility baseline and make every HD substitution conditional on evidence.

```text
Guest logical request (category + PS2 ID / request context)
                         |
                         v
      Verified PS2-ID -> HD asset mapping entry?
              / no                    \\ yes
     PS2 asset path               Load loose HD bytes
                                         |
                         digest/integrity check and format dispatch
                            / valid and supported       \\ unsupported,
                     decode + palette + upload            corrupt/no match
                     HD result with PS2-compatible       use PS2 fallback
                     dimensions/semantics                       |
                                                              render
```

1. Preserve the original guest request identity/context and resolve the PS2 baseline asset path using the existing guest/runtime behavior. Do not invent a numeric ID based only on similar-looking filenames.
2. Consult a generated/local **verified correlation table**. A match must tie an observed PS2 logical resource to a particular HD numeric asset ID and category, with source provenance and expected digest. Until verified, there is no override.
3. Ask the HD provider for raw bytes. Verify expected content digest where a catalog entry supplies one. Dispatch by detected content signature, not extension. Implement SLP 2.0N command decoding and palette handling before considering SLP override rendering; implement BMP/WAV paths separately if needed.
4. On no verified match, unsupported format, or decoder-rejected/corrupt image payload, choose the PS2 fallback and emit a diagnostic suitable for development. If integrity validation fails (digest mismatch, unexpected truncation/read failure), mark the HD candidate invalid, surface/log a clear asset error, and use the PS2 fallback when available; never silently treat altered bytes as a valid override. If no usable PS2 fallback exists, fail visibly rather than draw uninitialized data.
5. Cache decoded results under a key that includes source identity/digest, category, asset ID, and palette/context; keep fallback and override lifetimes independent. This cache is a proposal and should follow runtime/platform memory constraints.

The existing `LooseHdAssetProvider` is a useful raw-file source for step 3 only. It searches one root by numeric filename; it is **not** a compositional provider that checks HD first and falls through to PS2. A separate overlay/resolution module should compose providers and own mapping, integrity, decode/fallback policy. `empires2_x1_p1.dat` is a named game-data file, not a decimal numeric asset filename; it requires a dedicated data-file loader/parser and a separately justified compatibility policy, not the sprite-ID lookup method. Avoid assuming that swapping the HD DAT file preserves PS2 game logic or data semantics.

## Provenance and repository rules

- HD and PS2 files remain user-owned local inputs under ignored `game/`; generated PS2 code, disc images, archives, imported assets, and asset catalogs must not be added to tracked source. Follow `docs/hd-assets.md`, `.gitignore`, and the clean-room constraints in `CLAUDE.md`.
- Tracked metadata may contain only permitted aggregate counts, hashes, and tool/version information. Do not track asset names/IDs/paths, binary bytes, decoded pixels, game function names/addresses, disassembly, or code snippets.
- Derive correlations from authorized user-owned files and observable behavior. Public format documentation can inform independent parsers; do not copy GPL-licensed implementation code. The openage converter configuration and media docs are cited below as factual format/layout references only.
- Preserve source files byte-for-byte through import/staging. Verify digests at import, staging, and runtime candidate load; fail visibly on integrity errors.

## Remaining questions and acceptance checks

1. **PS2 archive/index:** document `DATA.CAT` and `ALL_DATA.BIN` structure from owned bytes and validate parser output against bounds, offsets, duplicate IDs, and regional overlays. Establish what overlay files add or replace; extraction alone does not answer this.
2. **PS2 graphics seam:** trace a real gameplay sprite request from guest ID through resource lookup to final draw/presentation, using allowed local sources and runtime evidence. Establish whether it can be replaced without GS emulation and without editing generated C++.
3. **Correlation quality:** produce a local-only mapping with explicit evidence for each PS2-to-HD pair; test mismatches and unknown IDs. A filename resemblance or shared franchise identifier is not sufficient evidence.
4. **HD decode:** implement and test SLP 2.0N parsing beyond headers (bounds-checked frame metadata, row offsets, command decoding, transparency/player/shadow semantics), JASC palette association, and color conversion. Use known local files for golden checks; do not commit decoded game data.
5. **Resolution compatibility:** determine expected PS2 sprite geometry/hotspots and HD counterpart semantics. Validate palettes, player colors, animation frame counts, scaling, and UI/input hitboxes. HUD geometry helpers need actual region specs and must be wired in both render and input paths before relocating panels.
6. **Provider composition:** add a single-root HD provider plus baseline PS2 source behind a resolution policy with tests for verified override, no match, unsupported format, corrupt payload, digest mismatch, read failure, and missing fallback. Distinguish fallback-worthy decode errors from visible integrity errors.
7. **Roots and delivery:** explicitly initialize the provider with desktop `<ELF directory>/assets`, Android `<external files dir>/assets`, and Switch `sdmc:/switch/aoe2/assets`; test each against the actual staged directory. Choose and test a deployment/package step for Switch if manual copy is retained.
8. **Named data and non-graphics:** decide independently whether HD DAT, strings, audio, terrain, or menus can replace PS2 equivalents. Each needs its own format loader, compatibility evidence, and fallback semantics; do not infer support from the graphic provider.
9. **No false-positive acceptance:** only claim that HD assets render after a runtime test proves a known, digest-verified asset is decoded, selected for a mapped guest request, submitted to the renderer, and visibly differs from the PS2 fallback on target(s).

## Source references

### Local implementation and project evidence

- `docs/architecture-hd-asset-decoupling.md` — intended decoupled target and phased status; explicitly says runtime initialization/draw path and PS2-to-HD correlation remain future work.
- `docs/hd-assets.md` — import/catalog/staging design, current interface observations, paths, resolver defaults, and unimplemented runtime wiring.
- `docs/architecture.md`, §§5 and 7 — high-level rendering mandate; widescreen and HD asset material is design/future work with open questions.
- `docs/plans/hd-widescreen-handoff.md` — current staged roots, provider boundary, format findings, and unresolved runtime seam.
- `tools/hd_assets/slp_header.py`, `tools/hd_assets/palette.py`, `tools/hd_assets/format_sniffer.py`, `tools/hd_assets/asset_catalog.py` — actual limits and behavior of local HD inspection tooling.
- `tools/hd_assets/stage_plan.py`, `tools/stage_hd_assets.py`, `tools/docker_build.py`, `tools/validate_android.py` — staging, conditional build step, and Android delivery/validation.
- `assets/include/aoe2/assets/asset_provider.hpp`, `assets/src/loose_hd_asset_provider.cpp`, `assets/src/asset_path_resolver.cpp` — provider interface, numeric single-root lookup, sniffing, and resolver defaults.
- `renderer/src/hud_layout.cpp`, `input/src/hud_inverse_mapper.cpp` — geometry helpers only; not asset decode/render integration.
- `tools/import_owned_disc.py`, `docs/game-installations.md` — owned disc extraction and local archive/overlay layout.

### Public primary references

- openage, [SLP file format](https://github.com/SFTtech/openage/blob/master/doc/media/slp-files.md) — describes the 32-byte SLP 2.0N header, per-frame records, outline/command-offset tables, row drawing commands, palette-index pixels, player colors, and shadow behavior. This document informed format terminology; no source implementation was copied.
- openage, [HD Edition configuration](https://github.com/SFTtech/openage/blob/master/cfg/converter/games/game_editions.toml) — `[HDEDITION.mediapaths]` identifies the loose HD `resources/_common` locations, including interface, graphics, gamedata, terrain, sounds, and `empires2_x1_p1.dat`.
- openage, [DRS file format](https://github.com/SFTtech/openage/blob/master/doc/media/drs-files.md) — describes DRS archive headers, per-type tables, and numeric-ID/offset/size entries. This is context for the classic PC archive format, **not evidence that PS2 `DATA.CAT` / `ALL_DATA.BIN` use DRS**.
- openage converter docs, [game-data conversion overview](https://simonsan.github.io/openage-webdocs/sphinx/doc/nyan/conversion.html) — identifies `empires2_x1_p1.dat` as a game-data input parsed by a distinct conversion path; it does not establish compatibility with the PS2 build.
