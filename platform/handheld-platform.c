/*
 * handheld-platform.c -- RG35XX handheld backend vtable.
 *
 * This is a NEW backend.  Rendering is delegated to the upstream SDL console
 * (sdlConsole.plotChar / setGraphicsMode / takeScreenshot) and to tiles.c via
 * the existing SDL build flags; no upstream backend file is edited.  Input
 * polling is replaced by the GameController adapter in handheld-sdl.c.
 *
 * The upstream 100x34 grid, tiles and core are untouched.
 */
#ifdef SDL_PATHS
#include <unistd.h>
#endif

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <SDL.h>
#include <SDL_image.h>

#include "platform.h"
#include "tiles.h"
#include "handheld-sdl.h"

#define HH_PAUSE_BETWEEN_EVENT_POLLING 36L

static rogueEvent hhLastEvent;
static long hhLastDelayTime = 0;

static void hhFatal(const char *what, const char *error) {
    fprintf(stderr, "Fatal %s error in handheld backend: %s\n", what, error);
    exit(EXIT_STATUS_FAILURE_PLATFORM_ERROR);
}

/* Like SDL_Delay, but compensates for time already spent since the last delay.
 * Derived from upstream sdl2-platform.c:_delayUpTo (kept separate so the
 * upstream backend remains pristine). */
static void hhDelayUpTo(short ms) {
    long currentTime = (long) SDL_GetTicks();
    long timeDiff = currentTime - hhLastDelayTime;

    ms = (short) (ms - timeDiff);
    if (ms > 0) {
        SDL_Delay((Uint32) ms);
    }
    hhLastDelayTime = (long) SDL_GetTicks();
}

static void hhGameLoop(void) {
#ifdef SDL_PATHS
    char *path = SDL_GetBasePath();
    if (path) {
        path[strlen(path) - 1] = '\0';
        strcpy(dataDirectory, path);
    } else {
        fprintf(stderr, "Failed to find the path to the application\n");
        exit(EXIT_STATUS_FAILURE_PLATFORM_ERROR);
    }
    free(path);

    path = SDL_GetPrefPath("Brogue", "Brogue CE");
    if (!path || chdir(path) != 0) {
        fprintf(stderr, "Failed to find or change to the save directory\n");
        exit(EXIT_STATUS_FAILURE_PLATFORM_ERROR);
    }
    fprintf(stderr, "Save path: %s\n", path);
    free(path);
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0) {
        hhFatal("SDL", SDL_GetError());
    }
    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) {
        hhFatal("SDL_image", IMG_GetError());
    }

    hhInitController();
    initTiles();

    hhLastEvent.eventType = EVENT_ERROR;

    /* Use the full 640x480 panel instead of the stock centered 512x320 window
     * that tiles.c computes when windowWidth == -1 (tiles.c:740-751).  These
     * globals are exported by tiles.c; no core file is modified.  [V-F] */
    {
        extern int windowWidth;
        extern int windowHeight;
        windowWidth = 640;
        windowHeight = 480;
    }
    resizeWindow(windowWidth, windowHeight);

    {
        int statusCode = rogueMain();
        hhCloseController();
        SDL_Quit();
        exit(statusCode);
    }
}

static boolean hhPauseForMilliseconds(short milliseconds, PauseBehavior behavior) {
    updateScreen();
    hhDelayUpTo(milliseconds);

    if (hhLastEvent.eventType != EVENT_ERROR
        && (hhLastEvent.eventType != MOUSE_ENTERED_CELL || behavior.interuptForMouseMove)) {
        return true;
    }

    return hhPollHandheldEvent(&hhLastEvent, false)
        && hhLastEvent.eventType != EVENT_ERROR
        && (hhLastEvent.eventType != MOUSE_ENTERED_CELL || behavior.interuptForMouseMove);
}

static void hhNextKeyOrMouseEvent(rogueEvent *returnEvent, boolean textInput,
                                  boolean colorsDance) {
    updateScreen();

    if (hhLastEvent.eventType != EVENT_ERROR) {
        *returnEvent = hhLastEvent;
        hhLastEvent.eventType = EVENT_ERROR;
        return;
    }

    while (true) {
        if (colorsDance) {
            shuffleTerrainColors(3, true);
            commitDraws();
        }

        updateScreen();

        if (hhPollHandheldEvent(returnEvent, textInput)) {
            break;
        }
        hhDelayUpTo(HH_PAUSE_BETWEEN_EVENT_POLLING);
    }
}

/* Rendering, screenshots and graphics-mode state stay identical to the stock
 * SDL backend by forwarding to its function pointers. */
static void hhPlotChar(enum displayGlyph inputChar,
                       short x, short y,
                       short foreRed, short foreGreen, short foreBlue,
                       short backRed, short backGreen, short backBlue) {
    sdlConsole.plotChar(inputChar, x, y,
                        foreRed, foreGreen, foreBlue,
                        backRed, backGreen, backBlue);
}

static boolean hhTakeScreenshot(void) {
    return sdlConsole.takeScreenshot();
}

static enum graphicsModes hhSetGraphicsMode(enum graphicsModes mode) {
    return sdlConsole.setGraphicsMode(mode);
}

/* Controller input is intentionally independent of keymap.txt. */
static boolean hhModifierHeld(int modifier) {
    (void) modifier;
    return false;
}

struct brogueConsole handheldConsole = {
    hhGameLoop,
    hhPauseForMilliseconds,
    hhNextKeyOrMouseEvent,
    hhPlotChar,
    NULL,               /* remap: keymap.txt does not affect controller input */
    hhModifierHeld,
    NULL,               /* notifyEvent */
    hhTakeScreenshot,
    hhSetGraphicsMode
};
