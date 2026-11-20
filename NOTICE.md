# NOTICE — attribution and third-party components

This project bundles an unmodified copy of Brogue CE under `upstream/`.

## Brogue CE

- License: **GNU Affero General Public License v3.0** — see `LICENSE.txt` (verbatim upstream copy).
- Upstream: https://github.com/tmewett/BrogueCE.git
- Pinned commit: `22c64f58f92509364525800ddbde21b724911c22` (`v1.15.1-60-g22c64f5`).
- Upstream README describes CE as "a convenient base for forks and ports to new platforms" `[V-F]`.

## Bundled third-party components (inside `upstream/`)

| Component | Path | License | Attribution / obligation |
|---|---|---|---|
| cJSON 1.7.19 | `upstream/external/cJSON/` | MIT (`upstream/external/cJSON/LICENSE`) | Copyright (c) 2009-2017 Dave Gamble and cJSON contributors; license text must accompany copies |
| Tiles artwork | `upstream/bin/assets/tiles.png`, `tiles.bin`, `upstream/bin/assets/LICENSE.txt` | CC BY-SA 4.0 | Copyright (C) Brogue: Community Edition maintainers; originally by Antony Boucher based on Oryx Design Lab "Brogue Tiles"; keep notice; modifications trigger ShareAlike |
| `icon.png` | `upstream/bin/assets/icon.png` | Not separately stated `[U]` | Ship alongside the tiles notice; treat conservatively as covered by the tiles attribution |

## Assets and libraries added in Stage 3 (handheld UI)

| Component | Path | License | Attribution / obligation |
|---|---|---|---|
| DejaVu Sans Mono | `assets/DejaVuSansMono.ttf` | DejaVu Fonts License (Bitstream Vera + Arev, permissive) | Full text in `assets/LICENSE-DejaVu.txt`; copied into the pak's `assets/` directory. Used for SDL2_ttf panel text only; the dungeon keeps the stock tile atlas. |
| SDL2_ttf runtime | SDK `libSDL2_ttf-2.0.so.0` | zlib license (SDL) | Bundled in the generated pak's `lib/` by `scripts/package.sh`; not committed to this repository. On-device availability of the system copy is `[U]`; bundling avoids installing anything system-wide. |

`scripts/package.sh` copies `LICENSE.txt`, `cJSON_LICENSE.txt`, the DejaVu font and its license,
and the assets directory (which contains its own `LICENSE.txt`) into the generated `.pak`.

## Toolchain

The Koriki/Batocera armhf SDK is **not** redistributed by this repository. `scripts/setup-sdk.sh`
downloads it from the upstream release or reuses an SDK supplied via `BROGUE_SDK`. Its license is
governed by its own upstream distribution; consult that distribution before redistribution.

## Modifications

`upstream/` remains pristine in git; all edits are minimal `#ifdef BROGUE_HANDHELD`-guarded patches
under `patches/`, applied only into a disposable build tree by `scripts/apply-patches.sh`:

- **Stage 2:** `0001-handheld-config-mk.patch`, `0002-handheld-makefile.patch`,
  `0003-handheld-platform-h.patch`, `0004-handheld-main-c.patch` — guarded backend selection and
  Makefile/platform plumbing.
- **M3.2:** `0005-handheld-ui-overlay.patch` — guarded `hhuiRender()` hook in `src/platform/tiles.c`.
- **M3.4:** `0006-handheld-menu-hook.patch` — `src/brogue/GlobalsBase.h/.c`, `src/brogue/MainMenu.c`,
  `src/brogue/Buttons.c`: exports `hhActiveMenuState`.
- **M3.5:** `0007-handheld-inventory-hook.patch` — `src/brogue/GlobalsBase.h/.c`, `src/brogue/Items.c`:
  exports `hhInventoryOpen` and `hhInventoryItem`.
- **M3.6:** `0008-handheld-targeting-hook.patch` — `src/brogue/GlobalsBase.h/.c`, `src/brogue/Items.c`:
  exports `hhTargetingActive`, `hhTargetingMode` and `hhTargetItem`.
- **M3.7:** `0009-handheld-text-input-hook.patch` — `src/brogue/GlobalsBase.h/.c`, `src/brogue/IO.c`:
  exports the live text-entry state (`hhTextInputActive`, buffer, prompt, suffix, cursor, length, type).
- **Stage 5:** `0010-handheld-gamepad-labels.patch` — `src/brogue/IO.c`, `src/brogue/MainMenu.c`,
  `src/brogue/Rogue.h`: gamepad-only labels, button-bar help, prompts and "Press A to continue"
  (display-only; the continue waits also accept `RETURN_KEY`).
- **Stage 5:** `0011-handheld-camera-zoom.patch` — `src/brogue/GlobalsBase.h/.c`, `src/brogue/IO.c`,
  `src/brogue/Items.c`, `src/brogue/MainMenu.c`, `src/brogue/Rogue.h`, `src/platform/tiles.c`:
  display-only font-scale map camera, detail-box rectangle hooks, and zoom/save-flag state.

The six `src/brogue/` hooks (`0006`–`0011`) were explicitly approved by the project owner; every
hunk is guarded so stock SDL and curses builds compile the upstream code unchanged. No file under
`upstream/src/variants/` or `upstream/external/cJSON/` is modified. In accordance with AGPL-3.0 §5,
notice is given that these modifications were made between 17 and 18 November 2026 by this project's
handheld port contributors; the unmodified upstream source remains available at the pinned commit
above.

## Evidence tags

`[V-F]` verified from firmware/source file · `[V-D]` verified on device · `[U-S]` upstream source
documentation · `[I]` inferred · `[U]` unknown.
