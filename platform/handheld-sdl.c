/*
 * handheld-sdl.c -- SDL GameController adapter for the RG35XX handheld backend.
 *
 * Responsibilities:
 *   - open the first SDL GameController and log its name / SDL_GAMECONTROLLERCONFIG
 *     when BROGUE_HANDHELD_DEBUG_INPUT=1 is set (off by default),
 *   - translate raw controller button/hat input via the pure handheld-input layer,
 *   - derive a UI context from exported Brogue globals + the textInput argument,
 *   - implement virtual-pointer focus navigation for mouse-driven menus/dialogs,
 *   - provide a small rogueEvent queue for synthesized key/mouse events.
 *
 * IMPORTANT: SDL_QUIT, the Power button and /dev/input/event0 are deliberately
 * NOT handled here.  Safe shutdown (MENU+POWER) is owned by keymon.elf; this
 * backend must never consume or interfere with it.
 *
 * New file (Stage 2); no upstream source is modified.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <SDL.h>

#include "platform.h"
#include "tiles.h"
#include "handheld-input.h"
#include "handheld-sdl.h"

#ifdef BROGUE_HANDHELD
/* Declared in GlobalsBase.h, which this translation unit deliberately does not
 * include (that header has no include guard). The guarded core hook assigns the
 * active menu/dialog button state; NULL when no menu is open. */
extern buttonState *hhActiveMenuState;
/* Stage 3 M3.5: authoritative inventory-open signal from the guarded hook. */
extern boolean hhInventoryOpen;
/* Stage 3 M3.6: non-NULL while an item's detail/action box is open. */
extern item *hhInventoryItem;
/* Stage 3 M3.6: authoritative action-targeting signal from the guarded hook. */
extern boolean hhTargetingActive;
/* Stage 3 M3.7: authoritative text-input signal and entry type from the hook. */
extern boolean hhTextInputActive;
extern short   hhTextInputType;
/* Stage 5 UX: set by the guarded core confirm() hook for the single prompt
 * "Save this game and exit?" so A lands on Confirm instead of the safe No. */
extern boolean hhConfirmDefaultYes;
/* Stage 6: current core full-screen text screen (HH_CORE_SCREEN_*). */
extern int hhCoreScreen;
#endif

/* Keep the mirrored SDL constants in handheld-input.h honest. */
#define HH_STATIC_CHECK(cond, tag) typedef char tag[(cond) ? 1 : -1]
HH_STATIC_CHECK(SDL_CONTROLLER_BUTTON_A == HH_SDL_BUTTON_A, hh_chk_btn_a);
HH_STATIC_CHECK(SDL_CONTROLLER_BUTTON_B == HH_SDL_BUTTON_B, hh_chk_btn_b);
HH_STATIC_CHECK(SDL_CONTROLLER_BUTTON_X == HH_SDL_BUTTON_X, hh_chk_btn_x);
HH_STATIC_CHECK(SDL_CONTROLLER_BUTTON_Y == HH_SDL_BUTTON_Y, hh_chk_btn_y);
HH_STATIC_CHECK(SDL_CONTROLLER_BUTTON_BACK == HH_SDL_BUTTON_BACK, hh_chk_btn_back);
HH_STATIC_CHECK(SDL_CONTROLLER_BUTTON_START == HH_SDL_BUTTON_START, hh_chk_btn_start);
HH_STATIC_CHECK(SDL_CONTROLLER_BUTTON_DPAD_UP == HH_SDL_BUTTON_DPAD_UP, hh_chk_btn_dup);
HH_STATIC_CHECK(SDL_CONTROLLER_BUTTON_DPAD_DOWN == HH_SDL_BUTTON_DPAD_DOWN, hh_chk_btn_ddown);
HH_STATIC_CHECK(SDL_CONTROLLER_BUTTON_DPAD_LEFT == HH_SDL_BUTTON_DPAD_LEFT, hh_chk_btn_dleft);
HH_STATIC_CHECK(SDL_CONTROLLER_BUTTON_DPAD_RIGHT == HH_SDL_BUTTON_DPAD_RIGHT, hh_chk_btn_dright);
HH_STATIC_CHECK(SDL_HAT_UP == HH_SDL_HAT_UP, hh_chk_hat_up);
HH_STATIC_CHECK(SDL_HAT_DOWN == HH_SDL_HAT_DOWN, hh_chk_hat_down);
HH_STATIC_CHECK(SDL_HAT_LEFT == HH_SDL_HAT_LEFT, hh_chk_hat_left);
HH_STATIC_CHECK(SDL_HAT_RIGHT == HH_SDL_HAT_RIGHT, hh_chk_hat_right);
HH_STATIC_CHECK(SDL_CONTROLLER_AXIS_TRIGGERLEFT == HH_SDL_AXIS_TRIGGERLEFT, hh_chk_axis_l2);
HH_STATIC_CHECK(SDL_CONTROLLER_AXIS_TRIGGERRIGHT == HH_SDL_AXIS_TRIGGERRIGHT, hh_chk_axis_r2);

#define HH_REPEAT_DELAY_MS  250L
#define HH_REPEAT_RATE_MS   130L
#define HH_QUEUE_LEN        8
#define HH_DEBUG_ENV        "BROGUE_HANDHELD_DEBUG_INPUT"

static SDL_GameController *hhController = NULL;
static int  hhDebug = -1;
static int  hhL1Held = 0;           /* Stage 6: L1 = run modifier */
static int  hhDiagHeld = 0;         /* Stage 6: R1 = exact-diagonal modifier */
static int  hhMenuHeld = 0;         /* Stage 6: physical Menu (SDL GUIDE) = action layer */
static int  hhDirMask = 0;          /* Stage 6: bitmask of held cardinal D-pad directions */
static int  hhL2Down = 0;           /* digital latch for the L2 trigger axis */
static int  hhR2Down = 0;           /* digital latch for the R2 trigger axis */
#ifndef BROGUE_HANDHELD
/* Stage 2 fallback for builds without hook 0007.  In handheld builds the
 * authoritative hhInventoryOpen is used instead: this flag stayed set when an
 * inventory closed via an item action, misrouting later input to the pointer. */
static int  hhInventoryHeuristic = 0;
#endif
static short hhPointerX = 0;
static short hhPointerY = 0;
/* Last inventory-list focus while browsing (no item detail box open). Used to
 * restore the same row after an item's action box is canceled with B. */
static int hhInvLastFocus = -1;
static HHAction hhHeldDir = HH_ACTION_NONE;   /* resolved action currently repeating */
static Uint32 hhDirSince = 0;
static Uint32 hhDirLastRepeat = 0;

static rogueEvent hhQueue[HH_QUEUE_LEN];
static int hhQueueHead = 0;
static int hhQueueTail = 0;

static int hhDebugEnabled(void) {
    if (hhDebug < 0) {
        hhDebug = getenv(HH_DEBUG_ENV) ? 1 : 0;
    }
    return hhDebug;
}

static void hhLog(const char *fmt, ...) {
    va_list args;
    if (!hhDebugEnabled()) {
        return;
    }
    fprintf(stderr, "[handheld] ");
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

#ifdef BROGUE_HANDHELD
/* Stage 5 UX: persist the display-zoom level in <dataDirectory>/zoom.cfg.
 * Loading is lazy (first event poll) because dataDirectory is assigned by the
 * platform bootstrap before the core's event loop begins.  All writes go to
 * the writable pak directory; no firmware/system path is touched. */
static int hhZoomCfgLoaded = 0;

static void hhZoomSaveConfig(void);   /* used by legacy-config migration below */

static void hhZoomLoadOnce(void) {
    char path[512];
    char line[64] = {0};
    FILE *f;
    int level = -1, migrate = 0;
    if (hhZoomCfgLoaded) return;
    hhZoomCfgLoaded = 1;
    snprintf(path, sizeof(path), "%s/zoom.cfg", dataDirectory);
    f = fopen(path, "r");
    if (!f) return;
    if (fgets(line, sizeof(line), f)
        && sscanf(line, "HHZOOM2 %d", &level) == 1) {
        /* current format: level is already 0..2 (100/200/300%) */
    } else if (sscanf(line, "%d", &level) == 1) {
        /* Legacy 3-level file: 0=100%, 1=150% (removed), 2=200%. Map the old
         * middle level to 100% and rewrite the file so it cannot recur. */
        level = (level >= 2) ? 1 : 0;
        migrate = 1;
    } else {
        level = -1;
    }
    fclose(f);
    if (level < 0) return;
    hhZoomSetLevel(level);
    hhLog("zoom.cfg -> level %d (%s)%s\n", hhZoomLevel(), hhZoomLabel(),
          migrate ? " [migrated]" : "");
    if (migrate) hhZoomSaveConfig();
}

static void hhZoomSaveConfig(void) {
    char path[512];
    FILE *f;
    snprintf(path, sizeof(path), "%s/zoom.cfg", dataDirectory);
    f = fopen(path, "w");
    if (!f) {
        hhLog("could not write %s\n", path);
        return;
    }
    fprintf(f, "HHZOOM2 %d\n", hhZoomLevel());
    fclose(f);
    hhLog("saved zoom.cfg -> level %d\n", hhZoomLevel());
}
#endif /* BROGUE_HANDHELD */

static int hhQueuePush(const rogueEvent *event) {
    int next = (hhQueueTail + 1) % HH_QUEUE_LEN;
    if (next == hhQueueHead) {
        return 0; /* queue full: drop rather than overwrite an unconsumed event */
    }
    hhQueue[hhQueueTail] = *event;
    hhQueueTail = next;
    return 1;
}

static int hhQueuePop(rogueEvent *event) {
    if (hhQueueHead == hhQueueTail) {
        return 0;
    }
    *event = hhQueue[hhQueueHead];
    hhQueueHead = (hhQueueHead + 1) % HH_QUEUE_LEN;
    return 1;
}

static void hhQueueKeyFull(signed long key, int controlKey, int shiftKey) {
    rogueEvent event;
    event.eventType = KEYSTROKE;
    event.param1 = key;
    event.param2 = 0;
    event.controlKey = (boolean) (controlKey ? 1 : 0);
    event.shiftKey = (boolean) (shiftKey ? 1 : 0);
    hhQueuePush(&event);
}

static void hhQueueKey(signed long key, int shiftKey) {
    hhQueueKeyFull(key, 0, shiftKey);
}

static void hhQueueMouse(enum eventTypes type, short x, short y) {
    rogueEvent event;
    event.eventType = type;
    event.param1 = x;
    event.param2 = y;
    event.controlKey = 0;
    event.shiftKey = 0;
    hhQueuePush(&event);
}

/* Virtual keys -> concrete Brogue constants from Rogue.h. */
static signed long hhBrogueKey(HHKey key) {
    switch (key) {
        case HH_KEY_UP:              return UP_ARROW;
        case HH_KEY_DOWN:            return DOWN_ARROW;
        case HH_KEY_LEFT:            return LEFT_ARROW;
        case HH_KEY_RIGHT:           return RIGHT_ARROW;
        case HH_KEY_CONFIRM:         return RETURN_KEY;
        case HH_KEY_CANCEL:          return ESCAPE_KEY;
        case HH_KEY_INVENTORY:       return INVENTORY_KEY;
        case HH_KEY_HELP:            return BROGUE_HELP_KEY;
        /* HH_KEY_MAIN_MENU was the Stage 2 save-&-exit fallback.  Stage 4
         * intercepts Start before translation and maps it to the UI-only
         * in-game menu, so this virtual key is intentionally unmapped. */
        case HH_KEY_MESSAGE_ARCHIVE: return MESSAGE_ARCHIVE_KEY;
        case HH_KEY_SELECT_NEXT:     return TAB_KEY;
        case HH_KEY_REST:            return REST_KEY;
        case HH_KEY_BACKSPACE:       return DELETE_KEY;
        case HH_KEY_WAIT:            return PERIOD_KEY;
        case HH_KEY_SEARCH:          return SEARCH_KEY;
        /* Stage 6: exact diagonal movement. */
        case HH_KEY_UPLEFT:          return UPLEFT_KEY;
        case HH_KEY_UPRIGHT:         return UPRIGHT_KEY;
        case HH_KEY_DOWNLEFT:        return DOWNLEFT_KEY;
        case HH_KEY_DOWNRIGHT:       return DOWNRIGHT_KEY;
        /* Stage 6: Menu-layer actions (all pre-existing core commands). */
        case HH_KEY_AUTOEXPLORE:     return EXPLORE_KEY;
        case HH_KEY_REST_UNTIL_HEALED:return AUTO_REST_KEY;
        case HH_KEY_SWAP:            return SWAP_KEY;
        case HH_KEY_RETHROW:         return RETHROW_KEY;
        case HH_KEY_DISCOVERIES:     return DISCOVERIES_KEY;
        case HH_KEY_FEATS:           return FEATS_KEY;
        case HH_KEY_SEARCH_ALL:      return SEARCH_KEY;   /* controlKey = 1 */
        case HH_KEY_TOGGLE_COLORS:   return TRUE_COLORS_KEY;
        case HH_KEY_TOGGLE_STEALTH:  return STEALTH_RANGE_KEY;
        case HH_KEY_GRAPHICS:        return GRAPHICS_KEY;
        /* HH_KEY_TEXT_CHAR never reaches here: hhEmitAction() queues the
         * character directly from HHKeyEvent.character.  The in-game-menu keys
         * are UI-side only and are resolved in hhEmitAction(). */
        case HH_KEY_IGM_ACTIVATE:
        case HH_KEY_IGM_CLOSE:
        case HH_KEY_ZOOM:            /* UI-side only: handled in hhEmitAction() */
        case HH_KEY_NONE:
        default:                     return -1;
    }
}

static int hhIsDpad(HHAction action) {
    return action >= HH_ACTION_DPAD_UP && action <= HH_ACTION_DPAD_RIGHT;
}

/*
 * Context inference.  Only exported globals and the textInput argument are
 * used; no gameplay file is touched.  This is necessarily approximate for
 * inventory (tracked by observing our own INVENTORY/CANCEL emissions).
 */
static HHContext hhCurrentContext(boolean textInput) {
    HHContextFlags flags;
    HHContext context;

    flags.textInputActive  = 0;
    flags.inGameMenuOpen   = hhInGameMenuOpenState();
    flags.inventoryOpen    = 0;
    flags.menuActive       = 0;
    flags.gameInProgress   = (int) rogue.gameInProgress;
    flags.playbackMode     = (int) rogue.playbackMode;
    flags.textInputArg     = (int) textInput;
    flags.targetingActive  = 0;
    flags.cursorMode       = (int) (rogue.cursorMode != 0);

#ifdef BROGUE_HANDHELD
    /* The guarded hooks are the authoritative signals.  The `textInput`
     * callback argument is NOT a text-entry signal (button loops and travel
     * confirmations also pass true), so it is only a late dialog fallback. */
    flags.textInputActive  = (int) hhTextInputActive;
    flags.inventoryOpen    = (int) hhInventoryOpen;
    flags.menuActive       = (hhActiveMenuState != (buttonState *) 0);
    flags.targetingActive  = (int) hhTargetingActive;

    /* OSK session sync is the only side effect in this path.  It must run on
     * every poll (rendering also calls it) so the first frame of a session and
     * a nested prompt with a different type both reset cleanly. */
    hhOskSyncSession(flags.textInputActive,
                     hhTextInputType == TEXT_INPUT_NUMBERS);
#endif

    context = hhResolveContext(&flags);
    hhLog("context=%s\n", hhContextName(context));
    return context;
}

static int hhUsePointer(HHContext context) {
    /* M3.7: text entry must never use the virtual pointer — nextKeyPress()
     * discards mouse events, which is exactly why Confirm used to be dead. */
    if (context == HH_CTX_TEXT_INPUT) return 0;
    /* L1 no longer forces the pointer (Stage 5 UX): L1 is inert. The automatic
     * pointer is still required to navigate the core's mouse-driven menus. */
    return context == HH_CTX_MENU || context == HH_CTX_DIALOG
           || context == HH_CTX_INVENTORY;
}

static void hhMovePointer(HHAction action) {
    switch (action) {
        case HH_ACTION_DPAD_UP:    hhPointerY = (short) (hhPointerY - 1); break;
        case HH_ACTION_DPAD_DOWN:  hhPointerY = (short) (hhPointerY + 1); break;
        case HH_ACTION_DPAD_LEFT:  hhPointerX = (short) (hhPointerX - 1); break;
        case HH_ACTION_DPAD_RIGHT: hhPointerX = (short) (hhPointerX + 1); break;
        default: break;
    }
    if (hhPointerX < 0)            hhPointerX = 0;
    if (hhPointerY < 0)            hhPointerY = 0;
    if (hhPointerX >= COLS)        hhPointerX = (short) (COLS - 1);
    if (hhPointerY >= ROWS)        hhPointerY = (short) (ROWS - 1);
    hhQueueMouse(MOUSE_ENTERED_CELL, hhPointerX, hhPointerY);
    hhLog("virtual pointer -> (%d,%d)\n", (int) hhPointerX, (int) hhPointerY);
}

#ifdef BROGUE_HANDHELD
/* Gamepad navigation over the core's real buttons.  Coordinates and enabled
 * state come from the guarded menu hook; this file never edits core source. */
static int hhMenuTarget(HHAction action, int current) {
    const buttonState *bs = hhActiveMenuState;
    int count, i, idx;
    /* Stage 5 device test: up/down step the list as expected, but left/right
     * were reversed relative to the on-screen order. Keep them distinct. */
    int step = (action == HH_ACTION_DPAD_UP || action == HH_ACTION_DPAD_RIGHT) ? -1 : 1;
    if (!bs || bs->buttonCount <= 0) return -1;
    count = bs->buttonCount;
    if (current < 0 || current >= count) current = step > 0 ? -1 : 0;
    for (i = 1; i <= count; i++) {
        idx = (current + step * i) % count;
        if (idx < 0) idx += count;
        if ((bs->buttons[idx].flags & B_DRAW)
            && (bs->buttons[idx].flags & B_ENABLED)) {
            return idx;
        }
    }
    return -1;
}

static void hhMenuHover(int index) {
    const buttonState *bs = hhActiveMenuState;
    short x, y;
    if (!bs || index < 0 || index >= bs->buttonCount) return;
    x = (short) (bs->buttons[index].x + 1);
    y = bs->buttons[index].y;
    hhPointerX = x;
    hhPointerY = y;
    hhQueueMouse(MOUSE_ENTERED_CELL, x, y);
}

static void hhMenuClick(void) {
    const buttonState *bs = hhActiveMenuState;
    int index;
    short x, y;
    if (!bs || bs->buttonCount <= 0) return;
    index = (bs->buttonFocused >= 0 && bs->buttonFocused < bs->buttonCount)
            ? bs->buttonFocused : 0;
    x = (short) (bs->buttons[index].x + 1);
    y = bs->buttons[index].y;
    hhPointerX = x;
    hhPointerY = y;
    hhQueueMouse(MOUSE_DOWN, x, y);
    hhQueueMouse(MOUSE_UP, x, y);
}

static void hhMenuEnsureFocus(HHContext context) {
    const buttonState *bs = hhActiveMenuState;
    int def = -1, i;
    if (!bs || bs->buttonCount <= 0 || bs->buttonFocused >= 0) return;
    if (context == HH_CTX_DIALOG && rogue.gameInProgress && bs->buttonCount > 1) {
#ifdef BROGUE_HANDHELD
        if (hhConfirmDefaultYes) {
            /* "Save this game and exit?": focus the first real button (Yes) so
             * A confirms and exits in one press. Every other Yes/No confirm
             * keeps the safe "No" default below. */
            for (i = 0; i < bs->buttonCount; i++) {
                if ((bs->buttons[i].flags & B_DRAW)
                    && (bs->buttons[i].flags & B_ENABLED)) {
                    def = i;
                    break;
                }
            }
        } else
#endif
        {
            /* Confirmations default to the last drawn+enabled button (usually
             * the safe "No").  Scanning avoids landing on invisible paging buttons. */
            for (i = bs->buttonCount - 1; i >= 0; i--) {
                if ((bs->buttons[i].flags & B_DRAW)
                    && (bs->buttons[i].flags & B_ENABLED)) {
                    def = i;
                    break;
                }
            }
        }
    } else {
        /* Menus and action boxes default to the first real button. For the
         * inventory list, restore the row the player was on: closing an item's
         * action box with B rebuilds the core's button state with focus -1,
         * which would otherwise jump straight back to the top of the list. */
        int start = 0;
#ifdef BROGUE_HANDHELD
        if (context == HH_CTX_INVENTORY && hhInvLastFocus >= 0
            && hhInvLastFocus < bs->buttonCount) {
            start = hhInvLastFocus;
        }
#endif
        for (i = start; i < bs->buttonCount; i++) {
            if ((bs->buttons[i].flags & B_DRAW)
                && (bs->buttons[i].flags & B_ENABLED)) {
                def = i;
                break;
            }
        }
        if (def < 0) {
            for (i = 0; i < start; i++) {
                if ((bs->buttons[i].flags & B_DRAW)
                    && (bs->buttons[i].flags & B_ENABLED)) {
                    def = i;
                    break;
                }
            }
        }
    }
    if (def >= 0) hhMenuHover(def);
}
#endif /* BROGUE_HANDHELD */

#ifdef BROGUE_HANDHELD
/* Change a Start-menu setting in place: the core key is queued (not returned)
 * so the menu stays open and the next redraw shows the new [] value. */
static void hhIgmChangeValue(int index, int dir) {
    switch (index) {
        case HH_IGM_COLORS:
            hhQueueKey(TRUE_COLORS_KEY, 0);
            break;
        case HH_IGM_STEALTH:
            hhQueueKey(STEALTH_RANGE_KEY, 0);
            break;
        case HH_IGM_GRAPHICS:
            /* The core only cycles forward (3 states); two queued presses
             * step backwards. */
            hhQueueKey(GRAPHICS_KEY, 0);
            if (dir < 0) hhQueueKey(GRAPHICS_KEY, 0);
            break;
        case HH_IGM_ZOOM:
            hhZoomSetLevel((hhZoomLevel()
                            + (dir > 0 ? 1 : HH_ZOOM_LEVELS - 1))
                           % HH_ZOOM_LEVELS);
            hhZoomSaveConfig();
            break;
        default:
            break;
    }
}
#endif /* BROGUE_HANDHELD */

/* Returns 1 when the caller must stop processing the current SDL event batch:
 * the context is frozen for a batch, but a text-input terminator lets the core
 * leave getInputTextString() immediately (M3.7), and toggling the in-game menu
 * changes the context (M4.3).  Translating a later event in the same batch with
 * the stale context could leave a stray event in the queue. */
static int hhEmitAction(HHContext context, HHAction action, int runModifier) {
    HHKeyEvent translated;
    signed long brogueKey;
    int terminator;

    /* M4.3: Start no longer saves and exits directly — it opens the UI-only
     * in-game menu.  Returning 1 stops the batch so the next poll resolves the
     * new context before any further button is translated. */
    if (context == HH_CTX_EXPLORATION && action == HH_ACTION_START) {
        hhInGameMenuToggle();
        return 1;
    }

#ifdef BROGUE_HANDHELD
    /* Stage 6: core text screens use a vertical-only scrollable viewport at
     * 2x. D-pad UP/DOWN scroll; LEFT/RIGHT are consumed (the full content
     * width is already visible and horizontal scrolling felt unnatural).
     * A/B/Start still dismiss the screen through the core. */
    if (hhCoreScreen != HH_CORE_SCREEN_NONE) {
        if (hhCoreScreen == HH_CORE_SCREEN_ARCHIVE) {
            /* Archive content fits the canvas; let the core page through
             * older messages with real UP/DOWN keystrokes. */
            if (action == HH_ACTION_DPAD_LEFT || action == HH_ACTION_DPAD_RIGHT) {
                return 1;
            }
        } else if (action == HH_ACTION_DPAD_UP || action == HH_ACTION_DPAD_DOWN
                   || action == HH_ACTION_DPAD_LEFT || action == HH_ACTION_DPAD_RIGHT) {
            return 1;   /* content fits: no scrolling needed */
        }
    }
#endif

#ifdef BROGUE_HANDHELD
    /* Start menu: UP/DOWN navigate, A changes the focused value (or confirms).
     * D-pad LEFT/RIGHT are deliberately ignored here. */
    if (hhInGameMenuOpenState()) {
        int f = hhInGameMenuFocus();
        if (action == HH_ACTION_A && hhInGameMenuIsValue(f)) {
            hhIgmChangeValue(f, 1);
            return 1;
        }
    }
#endif

    translated = hhTranslateAction(action, context);

    /* Stage 5 UX: Y cycles the display zoom in exploration/target. UI-only:
     * no Brogue event is queued, so the core keeps waiting for real input while
     * the next frame renders at the new scale. */
    if (translated.key == HH_KEY_ZOOM) {
        hhZoomCycle();
#ifdef BROGUE_HANDHELD
        hhZoomSaveConfig();
#endif
        return 1;
    }

    if (translated.key == HH_KEY_NONE) {
        return 0;
    }

    terminator = (context == HH_CTX_TEXT_INPUT)
                 && (translated.key == HH_KEY_CONFIRM
                     || translated.key == HH_KEY_CANCEL);

    /* M4.3: in-game menu confirmations are UI-side.  Closing the overlay first
     * keeps the core in gameplay when the chosen key is consumed next poll. */
    if (translated.key == HH_KEY_IGM_ACTIVATE) {
        int chosen = hhInGameMenuActivate();
        if (chosen < 0) return 1;
        switch (chosen) {
            case HH_IGM_SAVE_EXIT:
                /* Core helper: save with the default file name, no text-entry
                 * or confirmation, and end the session with success. */
                saveGameNoPrompt();
                break;
            /* Save As removed per project owner: Save & Exit is the only save
             * action in the Start menu. */
            /* Load removed — LOAD_SAVED_GAME_KEY has no consumer in
             * normal play (only playback), so the entry was a silent no-op. */
            case HH_IGM_HELP:      hhQueueKey(BROGUE_HELP_KEY, 0);      break;
            /* Discoveries/Feats removed: both have dedicated gamepad combos. */
            case HH_IGM_COLORS:    hhQueueKey(TRUE_COLORS_KEY, 0);      break;
            case HH_IGM_STEALTH:   hhQueueKey(STEALTH_RANGE_KEY, 0);    break;
            case HH_IGM_GRAPHICS:  hhQueueKey(GRAPHICS_KEY, 0);         break;
            case HH_IGM_ZOOM:
                hhZoomCycle();  /* display-only camera */
#ifdef BROGUE_HANDHELD
                hhZoomSaveConfig();
#endif
                break;
            case HH_IGM_QUIT:      hhQueueKey(QUIT_KEY, 0);             break;
            case HH_IGM_RETURN:
            default: break;   /* close only */
        }
        return 1;
    }
    if (translated.key == HH_KEY_IGM_CLOSE) {
        hhInGameMenuClose();
        return 1;
    }

    /* M3.7: a printable character is delivered verbatim as the KEYSTROKE
     * param that getInputTextString() reads via nextKeyPress().  It bypasses
     * the pointer path and the virtual-key table on purpose. */
    if (translated.key == HH_KEY_TEXT_CHAR) {
        hhQueueKey((signed long) (unsigned char) translated.character, 0);
        return 0;
    }

    if (hhUsePointer(context)) {
#ifdef BROGUE_HANDHELD
        if (hhActiveMenuState) {
            /* Real button data is available: navigate the actual button list
             * by synthesizing mouse focus/click events at each button's cell. */
            if (hhIsDpad(action)) {
                hhMenuHover(hhMenuTarget(action, hhActiveMenuState->buttonFocused));
            } else if (action == HH_ACTION_A) {
                hhMenuClick();
            } else if (action == HH_ACTION_B) {
                hhQueueKey(ESCAPE_KEY, 0);
            }
            return 0;
        }
#endif
        /* Fallback (no hook data): free virtual pointer driven by the D-pad. */
        if (hhIsDpad(action)) {
            hhMovePointer(action);
        } else if (action == HH_ACTION_A) {
            /* A click = press + release at the current virtual pointer cell.
             * processButtonInput() already implements focus/activation for
             * these mouse events, so no core change is required. */
            hhQueueMouse(MOUSE_DOWN, hhPointerX, hhPointerY);
            hhQueueMouse(MOUSE_UP, hhPointerX, hhPointerY);
        } else if (action == HH_ACTION_B) {
            hhQueueKey(ESCAPE_KEY, 0);
        }
        return 0;
    }

    brogueKey = hhBrogueKey(translated.key);
    if (brogueKey < 0) {
        return 0;
    }
    hhLog("  -> virtual=%s brogue-key=%ld\n", hhKeyName(translated.key), brogueKey);
    hhQueueKey(brogueKey,
               translated.shiftKey
               || (runModifier && (hhIsDpad(action) || hhIsDiagonalAction(action))));

#ifndef BROGUE_HANDHELD
    if (translated.key == HH_KEY_INVENTORY) {
        hhInventoryHeuristic = 1;
    } else if (translated.key == HH_KEY_CANCEL) {
        hhInventoryHeuristic = 0;
    }
#endif
    return terminator;
}

/* --- Stage 6 direction/multi-modifier helpers ----------------------------- */
#define HH_DIR_UP    0x01
#define HH_DIR_DOWN  0x02
#define HH_DIR_LEFT  0x04
#define HH_DIR_RIGHT 0x08

static int hhDirBit(HHAction action) {
    switch (action) {
        case HH_ACTION_DPAD_UP:    return HH_DIR_UP;
        case HH_ACTION_DPAD_DOWN:  return HH_DIR_DOWN;
        case HH_ACTION_DPAD_LEFT:  return HH_DIR_LEFT;
        case HH_ACTION_DPAD_RIGHT: return HH_DIR_RIGHT;
        default:                   return 0;
    }
}

/* Resolve the currently held D-pad directions into ONE action. With R1 held in
 * a gameplay context, only an exact two-adjacent-direction chord produces a
 * diagonal; a single direction produces no movement (precision, Shiren-style).
 * In modal contexts R1 is ignored and cardinal navigation is unchanged. */
static HHAction hhResolveDirection(HHContext context) {
    int up    = (hhDirMask & HH_DIR_UP)    != 0;
    int down  = (hhDirMask & HH_DIR_DOWN)  != 0;
    int left  = (hhDirMask & HH_DIR_LEFT)  != 0;
    int right = (hhDirMask & HH_DIR_RIGHT) != 0;

    if (hhDiagHeld && hhContextAllowsDiagonal(context)) {
        int count = up + down + left + right;
        HHAction a = HH_ACTION_NONE, b = HH_ACTION_NONE;
        if (count != 2) return HH_ACTION_NONE;
        if (up)    a = HH_ACTION_DPAD_UP;
        else if (down)  a = HH_ACTION_DPAD_DOWN;
        else if (left)  a = HH_ACTION_DPAD_LEFT;
        else            a = HH_ACTION_DPAD_RIGHT;
        if (right && a != HH_ACTION_DPAD_RIGHT)      b = HH_ACTION_DPAD_RIGHT;
        else if (left && a != HH_ACTION_DPAD_LEFT)   b = HH_ACTION_DPAD_LEFT;
        else if (down && a != HH_ACTION_DPAD_DOWN)   b = HH_ACTION_DPAD_DOWN;
        else if (up && a != HH_ACTION_DPAD_UP)       b = HH_ACTION_DPAD_UP;
        else return HH_ACTION_NONE;
        return hhActionChord(a, b);
    }

    /* Existing cardinal priority, preserved for ordinary navigation/repeat. */
    if (up)    return HH_ACTION_DPAD_UP;
    if (down)  return HH_ACTION_DPAD_DOWN;
    if (left)  return HH_ACTION_DPAD_LEFT;
    if (right) return HH_ACTION_DPAD_RIGHT;
    return HH_ACTION_NONE;
}

/* Held-Menu layer: emit the layer command once, consuming the ordinary button
 * meaning. No repeat is registered for layer actions. */
static void hhEmitMenuLayer(HHAction action, HHContext context) {
    HHKeyEvent ev = hhMenuLayerKey(action, context);
    signed long key;
    if (ev.key == HH_KEY_NONE) return;
    key = hhBrogueKey(ev.key);
    if (key < 0) return;
    hhLog("  menu layer -> virtual=%s brogue-key=%ld ctrl=%d\n",
          hhKeyName(ev.key), key, ev.controlKey);
    hhQueueKeyFull(key, ev.controlKey, 0);
}

static void hhOpenController(int deviceIndex) {
    SDL_GameController *controller;
    const char *name;

    if (hhController) {
        return;
    }
    controller = SDL_GameControllerOpen(deviceIndex);
    if (!controller) {
        hhLog("SDL_GameControllerOpen(%d) failed: %s\n", deviceIndex, SDL_GetError());
        return;
    }
    hhController = controller;
    SDL_GameControllerEventState(SDL_ENABLE);
    name = SDL_GameControllerName(controller);
    hhLog("controller opened: %s\n", name ? name : "(unknown)");
}

void hhInitController(void) {
    int i;
    const char *config = getenv("SDL_GAMECONTROLLERCONFIG");
    hhLog("SDL_GAMECONTROLLERCONFIG: %s\n", config ? config : "(unset)");

    for (i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            hhOpenController(i);
            break;
        }
    }
    if (!hhController) {
        hhLog("no controller present at init; waiting for SDL_CONTROLLERDEVICEADDED\n");
    }
    hhPointerX = (short) (COLS / 2);
    hhPointerY = (short) (ROWS / 2);
}

void hhCloseController(void) {
    if (hhController) {
        SDL_GameControllerClose(hhController);
        hhController = NULL;
    }
}

boolean hhRunModifierHeld(void) {
    return hhL1Held ? true : false;
}

boolean hhPollHandheldEvent(rogueEvent *returnEvent, boolean textInput) {
    /* M4.3 review: the UI-only in-game menu must not outlive the game it belongs
     * to.  Otherwise the title screen becomes unreachable while the overlay is
     * open. */
    if (hhInGameMenuOpenState()
        && (!rogue.gameInProgress || rogue.gameHasEnded)) {
        hhInGameMenuClose();
    }

#ifdef BROGUE_HANDHELD
    /* Lazy one-time load of the persisted zoom level (first event poll). */
    hhZoomLoadOnce();
#endif

    HHContext context = hhCurrentContext(textInput);
    SDL_Event event;
    Uint32 now;
    int stopBatch = 0;   /* M3.7: a text-input terminator ended the session */

#ifdef BROGUE_HANDHELD
    /* Remember the inventory row while the player is actually browsing the
     * list (no item detail box). This is what gets restored when B closes an
     * item's actions and the core hands us a brand-new, unfocused list. */
    if (context == HH_CTX_INVENTORY && !hhInventoryItem
        && hhActiveMenuState && hhActiveMenuState->buttonFocused >= 0) {
        hhInvLastFocus = hhActiveMenuState->buttonFocused;
    }
    /* Give a freshly opened menu/dialog a visible, safe default focus. */
    if (context == HH_CTX_MENU || context == HH_CTX_DIALOG
        || context == HH_CTX_INVENTORY) {
        hhMenuEnsureFocus(context);
    }
#endif

    if (hhQueuePop(returnEvent)) {
        return true;
    }

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_CONTROLLERDEVICEADDED:
                hhOpenController(event.cdevice.which);
                break;

            case SDL_CONTROLLERDEVICEREMOVED:
                if (hhController
                    && SDL_GameControllerFromInstanceID(event.cdevice.which) == hhController) {
                    hhLog("controller removed\n");
                    hhCloseController();
                    hhHeldDir = HH_ACTION_NONE;
                    hhDirMask = 0;
                    hhL1Held = 0;
                    hhDiagHeld = 0;
                    hhMenuHeld = 0;
                    hhL2Down = 0;
                    hhR2Down = 0;
                    hhInGameMenuClose();  /* avoid an overlay no controller can close */
                }
                break;

            case SDL_CONTROLLERBUTTONDOWN: {
                int button = (int) event.cbutton.button;
                HHAction action;
                const char *buttonName =
                    SDL_GameControllerGetStringForButton((SDL_GameControllerButton) button);
                hhLog("button down: index=%d name=%s\n", button,
                      buttonName ? buttonName : "?");

                action = hhActionFromSDLButton(button);
                hhLog("  action=%s context=%s\n", hhActionName(action), hhContextName(context));
                if (action == HH_ACTION_NONE) break;

                /* Stage 6: physical Menu is a transient modifier layer. */
                if (action == HH_ACTION_MENU) {
                    hhMenuHeld = 1;
                    hhDirMask = 0;
                    hhHeldDir = HH_ACTION_NONE;
                    break;
                }
                if (action == HH_ACTION_L1) { hhL1Held = 1; break; }
                if (action == HH_ACTION_R1) {
                    HHAction r;
                    hhDiagHeld = 1;
                    r = hhResolveDirection(context);
                    if (r != hhHeldDir) {
                        hhHeldDir = r;
                        hhDirSince = SDL_GetTicks();
                        hhDirLastRepeat = hhDirSince;
                        if (r != HH_ACTION_NONE
                            && hhEmitAction(context, r, hhL1Held && context == HH_CTX_EXPLORATION)) {
                            stopBatch = 1;
                        }
                    }
                    break;
                }

                /* While Menu is held the face/D-pad/trigger buttons belong to
                 * the layer only; their normal meanings never also fire. */
                if (hhMenuHeld) {
                    hhEmitMenuLayer(action, context);
                    break;
                }

                if (hhDirBit(action)) {
                    HHAction r;
                    hhDirMask |= hhDirBit(action);
                    r = hhResolveDirection(context);
                    if (r != hhHeldDir) {
                        hhHeldDir = r;
                        hhDirSince = SDL_GetTicks();
                        hhDirLastRepeat = hhDirSince;
                        if (r != HH_ACTION_NONE
                            && hhEmitAction(context, r, hhL1Held && context == HH_CTX_EXPLORATION)) {
                            stopBatch = 1;
                        }
                    }
                    break;
                }

                if (hhEmitAction(context, action, hhL1Held && context == HH_CTX_EXPLORATION)) {
                    stopBatch = 1;
                }
                break;
            }

            case SDL_CONTROLLERBUTTONUP: {
                int button = (int) event.cbutton.button;
                HHAction action = hhActionFromSDLButton(button);
                if (action == HH_ACTION_MENU) { hhMenuHeld = 0; break; }
                if (action == HH_ACTION_L1)   { hhL1Held = 0;   break; }
                if (action == HH_ACTION_R1) {
                    HHAction r;
                    hhDiagHeld = 0;
                    r = hhResolveDirection(context);
                    if (r != hhHeldDir) {
                        hhHeldDir = r;
                        hhDirSince = SDL_GetTicks();
                        hhDirLastRepeat = hhDirSince;
                    }
                    break;
                }
                if (hhDirBit(action)) {
                    HHAction r;
                    hhDirMask &= ~hhDirBit(action);
                    r = hhResolveDirection(context);
                    if (r != hhHeldDir) {
                        hhHeldDir = r;
                        hhDirSince = SDL_GetTicks();
                        hhDirLastRepeat = hhDirSince;
                    }
                }
                break;
            }

            /* M4.3: L2/R2 arrive as trigger axes.  A press threshold plus a
             * lower release threshold gives hysteresis, so jitter near the
             * press point cannot oscillate the latch and emit repeat actions. */
            case SDL_CONTROLLERAXISMOTION:
                if (hhController
                    && event.caxis.which == SDL_JoystickInstanceID(
                           SDL_GameControllerGetJoystick(hhController))) {
                    int axis = (int) event.caxis.axis;
                    int value = (int) event.caxis.value;
                    if (axis == HH_SDL_AXIS_TRIGGERLEFT) {
                        if (value <= HH_SDL_TRIGGER_RELEASE) {
                            hhL2Down = 0;
                        } else if (value > HH_SDL_TRIGGER_THRESHOLD && !hhL2Down) {
                            hhL2Down = 1;
                            if (hhMenuHeld) {
                                hhEmitMenuLayer(HH_ACTION_L2, context);
                            } else if (hhEmitAction(context, HH_ACTION_L2, 0)) {
                                stopBatch = 1;
                            }
                        }
                    } else if (axis == HH_SDL_AXIS_TRIGGERRIGHT) {
                        if (value <= HH_SDL_TRIGGER_RELEASE) {
                            hhR2Down = 0;
                        } else if (value > HH_SDL_TRIGGER_THRESHOLD && !hhR2Down) {
                            hhR2Down = 1;
                            if (hhMenuHeld) {
                                hhEmitMenuLayer(HH_ACTION_R2, context);
                            } else if (hhEmitAction(context, HH_ACTION_R2, 0)) {
                                stopBatch = 1;
                            }
                        }
                    }
                }
                break;

            case SDL_JOYHATMOTION:
                if (hhController
                    && event.jhat.which == SDL_JoystickInstanceID(
                           SDL_GameControllerGetJoystick(hhController))) {
                    HHAction cardinal = hhActionFromSDLHat(event.jhat.value);
                    HHAction resolved;
                    hhLog("hat %d value=0x%02x -> %s\n", (int) event.jhat.hat,
                          (unsigned) event.jhat.value, hhActionName(cardinal));
                    /* Rebuild the held-direction bitmask from the hat. A hat
                     * diagonally held still resolves to one cardinal here; the
                     * supported precise diagonal is R1 + two cardinals. */
                    hhDirMask = 0;
                    if (event.jhat.value & HH_SDL_HAT_UP)    hhDirMask |= HH_DIR_UP;
                    if (event.jhat.value & HH_SDL_HAT_DOWN)  hhDirMask |= HH_DIR_DOWN;
                    if (event.jhat.value & HH_SDL_HAT_LEFT)  hhDirMask |= HH_DIR_LEFT;
                    if (event.jhat.value & HH_SDL_HAT_RIGHT) hhDirMask |= HH_DIR_RIGHT;
                    resolved = hhMenuHeld ? HH_ACTION_NONE : hhResolveDirection(context);
                    if (resolved != hhHeldDir) {
                        hhHeldDir = resolved;
                        if (resolved != HH_ACTION_NONE) {
                            hhDirSince = SDL_GetTicks();
                            hhDirLastRepeat = hhDirSince;
                            if (hhEmitAction(context, resolved,
                                             hhL1Held && context == HH_CTX_EXPLORATION)) {
                                stopBatch = 1;
                            }
                        }
                    }
                }
                break;

            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    resizeWindow(event.window.data1, event.window.data2);
                }
                break;

            /* SDL_QUIT and SDL keyboard/mouse events are intentionally ignored:
             * the handheld backend is controller-driven, and system shutdown
             * (MENU+POWER) belongs to keymon.elf, not to this process. */
            default:
                break;
        }
        /* The context was frozen when this batch began; once a terminator has
         * been queued the core can leave text input at any moment, so no later
         * event may be translated with that stale context. */
        if (stopBatch) break;
    }

    now = SDL_GetTicks();
    if (!stopBatch && hhHeldDir != HH_ACTION_NONE
        && now - hhDirSince >= (Uint32) HH_REPEAT_DELAY_MS
        && now - hhDirLastRepeat >= (Uint32) HH_REPEAT_RATE_MS) {
        hhDirLastRepeat = now;
        hhEmitAction(context, hhHeldDir, hhL1Held && context == HH_CTX_EXPLORATION);
    }

    if (hhQueuePop(returnEvent)) {
        return true;
    }
    returnEvent->eventType = EVENT_ERROR;
    returnEvent->param1 = 0;
    returnEvent->param2 = 0;
    returnEvent->controlKey = 0;
    returnEvent->shiftKey = 0;
    return false;
}
