/*
 * handheld-ui.h -- SDL presentation layer for the Stage 3 handheld UI.
 *
 * The dungeon continues to be drawn by the stock tile renderer (tiles.c).
 * This module draws panels, focus indicators, modal backdrops and the
 * contextual hint strip with SDL primitives, and panel text with SDL2_ttf.
 *
 * New file (Stage 3).  It does not modify any upstream source.
 */
#ifndef BROGUE_HANDHELD_UI_H
#define BROGUE_HANDHELD_UI_H

#include <SDL.h>

#include "handheld-ui-state.h"

/* Initialize fonts and reset UI state.  Safe to call with a missing font:
 * panels still render, text is simply omitted. */
int  hhuiInit(SDL_Renderer *renderer, const char *fontPath);
void hhuiShutdown(void);

/* Draw the overlay.  Called from a guarded hook in tiles.c immediately before
 * SDL_RenderPresent, so it composits over the fully rendered dungeon. */
void hhuiRender(SDL_Renderer *renderer);

/* Global UI state driven by the input/action layer. */
HHUIState *hhuiGetState(void);

#endif /* BROGUE_HANDHELD_UI_H */
