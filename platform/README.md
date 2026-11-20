# platform/

Handheld backend and Stage 3 UI sources (all new files; no upstream backend is edited):

- `handheld-platform.c` — `struct brogueConsole handheldConsole`, SDL/IMG init, vtable; delegates
  rendering/screenshots/graphics mode to the unchanged upstream `sdlConsole` and `tiles.c`.
- `handheld-sdl.c/.h` — SDL GameController adapter: controller open, button/hat decoding, context
  inference from hooks `0006`-`0009`, virtual pointer focus navigation, synthesized `rogueEvent`
  queue (including OSK characters), opt-in diagnostics.
- `handheld-input.c/.h` — SDL-independent, host-testable action -> virtual key translation and the
  M3.7 on-screen keyboard model.
- `handheld-ui.c/.h` — SDL/TTF presentation layer: HUD, menus, dialogs, inventory, targeting and OSK
  panels, driven by the guarded hooks and the pure state model.
- `handheld-ui-state.c/.h` — SDL-independent screen stack, list focus and text-buffer model.

`scripts/apply-patches.sh` copies every file here into `build/work/src/platform/` after applying
`patches/*.patch`. Deployment is not performed by any script.
