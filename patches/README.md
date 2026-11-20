# patches/

Minimal, self-contained diffs against the pinned upstream tree (Stage 2):

- `0001-handheld-config-mk.patch` — `BROGUE_HANDHELD := 0` option.
- `0002-handheld-makefile.patch` — guarded handheld sources and `-DBROGUE_HANDHELD`.
- `0003-handheld-platform-h.patch` — guarded `extern struct brogueConsole handheldConsole`.
- `0004-handheld-main-c.patch` — handheld selected as the first backend branch.
- `0005-handheld-ui-overlay.patch` — guarded `hhuiRender()` hook before `SDL_RenderPresent`.
- `0006-handheld-menu-hook.patch` — Stage 3 M3.4: guarded `extern buttonState *hhActiveMenuState`
  (declared/defined in `src/brogue/GlobalsBase.h/.c`, assigned in `src/brogue/MainMenu.c`
  `titleMenu()` and `src/brogue/Buttons.c` `buttonInputLoop()`). Explicitly approved by the user;
  every hunk is guarded by `BROGUE_HANDHELD`, so stock SDL and curses behavior is unchanged.
- `0007-handheld-inventory-hook.patch` — Stage 3 M3.5: guarded `extern boolean hhInventoryOpen`
  and `extern item *hhInventoryItem` (declared/defined in `src/brogue/GlobalsBase.h/.c`). Set and
  restored on every return path of `displayInventory()` (`src/brogue/Items.c`) where the core itself
  selects `theItem`, with save/restore for nested calls. Explicitly approved by the user; every hunk
  is guarded by `BROGUE_HANDHELD`. `hhInventoryItem` is non-NULL while the item detail/action box is
  open; `hhInventoryOpen` is true for the whole interactive inventory. The pointer is cleared before
  the core's action switch (an applied item can be freed) and re-established for action-box paging.
- `0008-handheld-targeting-hook.patch` — Stage 3 M3.6: guarded `boolean hhTargetingActive`,
  `enum autoTargetMode hhTargetingMode` and `const item *hhTargetItem` (declared/defined in
  `src/brogue/GlobalsBase.h/.c`). Set in `chooseTarget()` (`src/brogue/Items.c`) after the playback
  early-return and restored on all three post-set return paths. Explicitly approved by the user;
  every hunk is guarded by `BROGUE_HANDHELD`. Required because action targeting runs with
  `cursorMode == false`; the existing `rogue.cursorLoc` global supplies the live cursor cell.
- `0009-handheld-text-input-hook.patch` — Stage 3 M3.7: guarded `boolean hhTextInputActive`,
  `char *hhTextInputBuffer`, `const char *hhTextInputPrompt`, `const char *hhTextInputSuffix`,
  `short *hhTextInputCursor`, `short hhTextInputMaxLength` and `short hhTextInputType` (declared/
  defined in `src/brogue/GlobalsBase.h/.c`). Set in `getInputTextString()` (`src/brogue/IO.c`) after
  local normalization and saved/restored on **both** exits (ESCAPE and RETURN). Explicitly approved
  by the user; every hunk is guarded by `BROGUE_HANDHELD`. Required because the prompt, buffer and
  cursor are stack-locals and the `textInput` argument is not a text-entry signal (seed entry also
  holds the title menu's `hhActiveMenuState`, so text input must outrank the M3.4 menu hook).

- `0010-handheld-gamepad-labels.patch` — Stage 5: guarded, **display-only** gamepad labels. Adds
  `HH_*_TEXT` macros in `Rogue.h`; relabels the five bottom menu buttons and the help screen in
  `IO.c` with gamepad controls (D-pad / A / B / X / Start / Select / L1 / L2 / R2), and changes the
  generic "press space/any key" prompts in `MainMenu.c`, `Movement.c` and `IO.c` to gamepad wording.
  All hunks are under `BROGUE_HANDHELD`; stock SDL and curses builds keep the original strings.
  No gameplay/RNG/combat/item logic is touched. Explicitly approved in the Stage 5 planning review.

- `0011-handheld-camera-zoom.patch` — Stage 5: guarded, **display-only** font-scale camera. Adds a
  second glyph atlas downscaled from the same high-res source PNG at the zoom tile size (crisp, not
  pixel-scaled) and a camera viewport over the **map region only** in `tiles.c`, centered on the
  player. The sidebar, message rows and button bar stay at native scale. Adds `hhZoomLevel()` and
  `hhModalScreenActive` hooks; the level is owned by the platform layer and cycled from the Start
  menu (100/125/150/200%, default 100% = byte-identical stock render). The camera is gated to
  gameplay only — inventory, help, message history, OSK, the in-game menu, title and death screens
  remain 1:1. `COLS/ROWS/DCOLS/DROWS`, RNG and all game state are untouched. Explicitly approved in
  the Stage 5 zoom review.

`upstream/src/brogue/` is modified **only** by the approved, guarded `0006`–`0011`
patches; `upstream/` in git stays pristine and `scripts/apply-patches.sh` applies all patches in
lexical order to a fresh copy of it. No file in `upstream/src/variants/` or `upstream/external/cJSON/`
is modified. Char/DELETE/RETURN/ESCAPE synthesis itself needs no core edit — the hook exists only for
detection and for rendering the live prompt, text and cursor.
