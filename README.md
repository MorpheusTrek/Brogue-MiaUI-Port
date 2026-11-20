# Brogue CE for the original RG35XX — v1.0.0

Brogue CE 1.15.1 port for original Anbernic RG35XX, running **MiaUI**. It is adapts the game to be played with gamepad controls, adds new Start menu drawn in Brogue's tile font, zoom levels that make text readable on a small screen, and
inventory and dialogs that scale along with the map.

It installs as a single `BrogueCE.pak` under `/userdata/Ports/`.

## Install and launch

1. Copy the `BrogueCE.pak/` directory from the release zip into `/userdata/Ports/` on the RG35XX.
2. Ensure `brogue` and `launch.sh` are executable (`chmod 755`); the packaged files already are.
3. Launch **BrogueCE** from MiaUI's Ports menu. `launch.sh` keeps all relative files (saves, config,
   high scores, `zoom.cfg`) inside the pak.
4. To update an install, replace only `brogue` and `launch.sh`; keep your saves.

The release zip contains no save data. Installation is a manual copy.

## Controls

This is the same list shown by **Help** in the game:

```
        -- Commands (RG35XX gamepad) --

       D-pad  move / attack / aim
     A / B  confirm / cancel
          X  view inventory
          Y  cycle zoom (100 / 200 / 300%)
       Menu  in-game menu
     Select  message history / next target
         L1  hold + D-pad to run
         R1  hold + D-pad for diagonals
         L2  wait one turn
         R2  search for secrets
          A  inspect

     -- hold Menu button + --
          A  auto-explore
          X  discoveries
          Y  feats
    D-pad L  swap equipment
    D-pad R  re-throw
         L2  rest until healed
         R2  long search

        -- press A to continue --
```

**Start menu** (physical Menu button): Save & Exit, Help, Colours, Stealth range, Graphics, Zoom,
Return to game, Quit. Up/Down navigate; A changes a value or confirms; B goes back.

**Save-load speed:** `launch.sh` temporarily raises the CPU to MinUI's game speed (~1.2 GHz) and
restores the stock 504 MHz setting when the game exits. This cuts loading a long saved game from
about 26 seconds to about 12 seconds on this device.

## Features

- **Core UI first.** Brogue's own UI is the gameplay interface, including the visible-entity
  sidebar. Only the on-screen keyboard and the Start menu are project overlays.
- **Display zoom.** Y cycles 100% → 200% → 300% with crisp, separately built glyph atlases; the
  camera is display-only and the level is saved in `<pak>/zoom.cfg`.
- **Core text screens.** Help, Archive, Feats and Discoveries fill the 640×480 display in Brogue's
  original tile font at every zoom level.
- **Detail popup.** Monster/item/examine details use Brogue's own box at native 1:1 over the zoomed
  map, with an 80% black background.
- **Inventory and dialogs.** At 200%/300% the item list uses a fixed 2× cell with focus-follow,
  while detail boxes and yes/no dialogs scale dynamically to their core window. Only the active
  window is drawn; at 100% everything stays native.
- **Text entry.** Gamepad-first on-screen keyboard for save names and numeric seeds.

## Repository layout

| Path | Purpose |
|---|---|
| `upstream/` | Pristine Brogue CE source at the commit in `UPSTREAM_COMMIT` |
| `UPSTREAM_COMMIT` | Exact pinned upstream commit and metadata |
| `patches/` | Minimal guarded patches, applied in order |
| `platform/` | Handheld SDL/TTF backend, input translation and UI state |
| `scripts/` | SDK setup, build, validation and packaging helpers |
| `assets/` | DejaVu Sans Mono font and its license |
| `LICENSE.txt`, `NOTICE.md` | AGPL-3.0 and third-party attribution |
| `releases/` | Release `BrogueCE-v1.0.0.pak.zip` and its SHA256 |

## Build from source

Prerequisites: Linux or WSL with `bash`, `tar`, `gzip`, `curl`, `sha256sum`, `file`, `readelf`,
`make` and a C toolchain, plus the Koriki/Batocera armhf SDK. No network or device is required once
the SDK and the vendored `upstream/` tree are present.

```bash
cp .env.example .env            # optional; set BROGUE_SDK if reusing an SDK
./scripts/setup-sdk.sh          # reuse BROGUE_SDK, else download to the user cache
./scripts/build.sh --dry-run    # print the exact cross-build command
./scripts/build.sh --handheld   # cross-build the handheld binary -> build/brogue
./scripts/package.sh            # assemble dist/brogue-ce-rg35xx.pak/
```

## Provenance and licensing

- Upstream: Brogue CE, <https://github.com/tmewett/BrogueCE.git>, pinned at commit
  `22c64f58f92509364525800ddbde21b724911c22` (`v1.15.1-60-g22c64f5`).
- Brogue CE is **AGPL-3.0** (`LICENSE.txt`). This port is a modification; notices are in
  `NOTICE.md`, and the corresponding source is this repository.
- Bundled cJSON is MIT; tiles are CC BY-SA 4.0; DejaVu Sans Mono uses the DejaVu Fonts License;
  SDL2_ttf is zlib-licensed and bundled in the pak as a runtime.
- The Koriki/Batocera SDK is **not** redistributed.

Legal interpretation here is informational, not legal advice.
