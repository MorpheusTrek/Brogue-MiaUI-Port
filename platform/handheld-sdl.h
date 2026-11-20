/*
 * handheld-sdl.h -- SDL-facing bridge used by handheld-platform.c.
 * New file (Stage 2); not an upstream header.
 */
#ifndef BROGUE_HANDHELD_SDL_H
#define BROGUE_HANDHELD_SDL_H

#include "Rogue.h"  /* guarded; platform.h has no include guard */

void hhInitController(void);
void hhCloseController(void);
boolean hhRunModifierHeld(void);
boolean hhPollHandheldEvent(rogueEvent *returnEvent, boolean textInput);

#endif /* BROGUE_HANDHELD_SDL_H */
