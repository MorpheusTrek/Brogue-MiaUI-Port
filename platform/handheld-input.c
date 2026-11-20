/*
 * handheld-input.c -- pure controller -> virtual key translation.
 *
 * See handheld-input.h.  No SDL, no Rogue.h, no device dependencies: this is
 * the code exercised by tests/host/handheld-input-test.c.
 */
#include <stdio.h>
#include "handheld-input.h"

HHAction hhActionFromSDLButton(int sdlButton) {
    switch (sdlButton) {
        case HH_SDL_BUTTON_A:              return HH_ACTION_A;
        case HH_SDL_BUTTON_B:              return HH_ACTION_B;
        case HH_SDL_BUTTON_X:              return HH_ACTION_X;
        case HH_SDL_BUTTON_Y:              return HH_ACTION_Y;
        case HH_SDL_BUTTON_BACK:           return HH_ACTION_SELECT;   /* "Select"/Back */
        case HH_SDL_BUTTON_START:          return HH_ACTION_START;
        case HH_SDL_BUTTON_DPAD_UP:        return HH_ACTION_DPAD_UP;
        case HH_SDL_BUTTON_DPAD_DOWN:      return HH_ACTION_DPAD_DOWN;
        case HH_SDL_BUTTON_DPAD_LEFT:      return HH_ACTION_DPAD_LEFT;
        case HH_SDL_BUTTON_DPAD_RIGHT:     return HH_ACTION_DPAD_RIGHT;
        /* Stage 6: the physical Menu button is BTN_MODE -> SDL GUIDE (b10 in
         * the RG35XX gamecontrollerdb line). keymon.elf reads the separate
         * event0 node, so consuming GUIDE here does not affect MENU+POWER. */
        case HH_SDL_BUTTON_GUIDE:          return HH_ACTION_MENU;
        case HH_SDL_BUTTON_LEFTSHOULDER:   return HH_ACTION_L1;
        case HH_SDL_BUTTON_RIGHTSHOULDER:  return HH_ACTION_R1;
        default:                           return HH_ACTION_NONE;
    }
}

HHAction hhActionFromSDLHat(int hatValue) {
    if (hatValue & HH_SDL_HAT_UP)    return HH_ACTION_DPAD_UP;
    if (hatValue & HH_SDL_HAT_DOWN)  return HH_ACTION_DPAD_DOWN;
    if (hatValue & HH_SDL_HAT_LEFT)  return HH_ACTION_DPAD_LEFT;
    if (hatValue & HH_SDL_HAT_RIGHT) return HH_ACTION_DPAD_RIGHT;
    return HH_ACTION_NONE;
}

HHAction hhActionFromSDLAxis(int axis, int value) {
    if (value <= HH_SDL_TRIGGER_THRESHOLD) return HH_ACTION_NONE;
    if (axis == HH_SDL_AXIS_TRIGGERLEFT)  return HH_ACTION_L2;
    if (axis == HH_SDL_AXIS_TRIGGERRIGHT) return HH_ACTION_R2;
    return HH_ACTION_NONE;
}

/* --- Authoritative context resolution (M4.2) ----------------------------
 * Single source of truth shared by the SDL adapter and host tests.  Priority
 * matches the documented Stage 4 model: text input > in-game menu > inventory
 * > menu/dialog > title > callback textInput > targeting > exploration.
 */
HHContext hhResolveContext(const HHContextFlags *f) {
    if (!f) return HH_CTX_EXPLORATION;
    if (f->textInputActive)  return HH_CTX_TEXT_INPUT;
    if (f->inGameMenuOpen)   return HH_CTX_IN_GAME_MENU;
    if (f->inventoryOpen)    return HH_CTX_INVENTORY;
    if (f->menuActive)       return f->gameInProgress ? HH_CTX_DIALOG : HH_CTX_MENU;
    if (!f->gameInProgress && !f->playbackMode) return HH_CTX_MENU;
    if (f->textInputArg)     return HH_CTX_DIALOG;
    if (f->targetingActive || f->cursorMode) return HH_CTX_TARGET;
    return HH_CTX_EXPLORATION;
}

/* --- In-game menu state (M4.3) ------------------------------------------ */

static const char *const IGM_LABELS[HH_IGM_COUNT] = {
    "Save & Exit", "Help", "Colours", "Stealth range",
    "Graphics", "", "Return to game", "Quit"
};

/* --- Display-camera zoom state (Stage 5/6) -------------------------------- */
static const char *const ZOOM_LABELS[HH_ZOOM_LEVELS] = {
    "100%", "200%", "300%"
};
static int g_zoomLevel = 0;
static char g_zoomMenuLabel[32];

void hhZoomCycle(void) {
    g_zoomLevel = (g_zoomLevel + 1) % HH_ZOOM_LEVELS;
}

void hhZoomSetLevel(int level) {
    if (level < 0) level = 0;
    if (level >= HH_ZOOM_LEVELS) level = HH_ZOOM_LEVELS - 1;
    g_zoomLevel = level;
}

int hhZoomLevel(void) {
    return g_zoomLevel;
}

const char *hhZoomLabel(void) {
    if (g_zoomLevel < 0 || g_zoomLevel >= HH_ZOOM_LEVELS) return ZOOM_LABELS[0];
    return ZOOM_LABELS[g_zoomLevel];
}

/* --- Core text-screen scroll state (Stage 6) ----------------------------
 * Pure model: the 2x viewport shows 50x16 cells of the 100x34 core screen.
 * Clamps mirror the pinned grid; the archive starts at its bottom rows. */
static int g_coreScrollX = HH_SCREEN_MAP_X0;
static int g_coreScrollY = 0;

void hhCoreScrollReset(int screen) {
    (void) screen;
    g_coreScrollX = HH_SCREEN_MAP_X0;   /* fixed: all content columns are visible */
    g_coreScrollY = 0;
}

/* Vertical-only scrolling: the whole content width fits the viewport, so left
 * and right are consumed by the SDL adapter and never move the screen. */
void hhCoreScrollMove(HHAction action) {
    if (action == HH_ACTION_DPAD_UP)   g_coreScrollY -= 1;
    else if (action == HH_ACTION_DPAD_DOWN) g_coreScrollY += 1;
    else return;
    if (g_coreScrollY < 0) g_coreScrollY = 0;
    if (g_coreScrollY > HH_SCREEN_MAX_SCROLL) g_coreScrollY = HH_SCREEN_MAX_SCROLL;
}

int hhCoreScrollX(void) { return g_coreScrollX; }
int hhCoreScrollY(void) { return g_coreScrollY; }

static int g_igmOpen = 0;
static int g_igmFocus = HH_IGM_SAVE_EXIT;

void hhInGameMenuOpen(void) {
    g_igmOpen = 1;
    g_igmFocus = HH_IGM_SAVE_EXIT;
}

void hhInGameMenuClose(void) {
    g_igmOpen = 0;
    g_igmFocus = HH_IGM_SAVE_EXIT;
}

void hhInGameMenuToggle(void) {
    if (g_igmOpen) hhInGameMenuClose();
    else hhInGameMenuOpen();
}

int hhInGameMenuOpenState(void) { return g_igmOpen; }
int hhInGameMenuFocus(void)     { return g_igmFocus; }

int hhInGameMenuIsValue(int index) {
    return (index == HH_IGM_COLORS || index == HH_IGM_STEALTH
            || index == HH_IGM_GRAPHICS || index == HH_IGM_ZOOM);
}

const char *hhInGameMenuLabel(int index) {
    if (index < 0 || index >= HH_IGM_COUNT) return "";
    if (index == HH_IGM_ZOOM) {
        snprintf(g_zoomMenuLabel, sizeof(g_zoomMenuLabel), "Zoom: %s", hhZoomLabel());
        return g_zoomMenuLabel;
    }
    return IGM_LABELS[index];
}

/* Navigation is UP/DOWN only. LEFT/RIGHT are consumed by the SDL adapter to
 * change the value of the focused setting (colours/stealth/graphics/zoom). */
void hhInGameMenuMove(HHAction action) {
    if (!g_igmOpen) return;
    if (action == HH_ACTION_DPAD_UP) {
        g_igmFocus = (g_igmFocus + HH_IGM_COUNT - 1) % HH_IGM_COUNT;
    } else if (action == HH_ACTION_DPAD_DOWN) {
        g_igmFocus = (g_igmFocus + 1) % HH_IGM_COUNT;
    }
}

int hhInGameMenuActivate(void) {
    int chosen;
    if (!g_igmOpen) return -1;
    chosen = g_igmFocus;
    hhInGameMenuClose();
    if (chosen < 0 || chosen >= HH_IGM_COUNT) return -1;
    return chosen;
}

static HHKey directionKey(HHAction action) {
    switch (action) {
        case HH_ACTION_DPAD_UP:       return HH_KEY_UP;
        case HH_ACTION_DPAD_DOWN:     return HH_KEY_DOWN;
        case HH_ACTION_DPAD_LEFT:     return HH_KEY_LEFT;
        case HH_ACTION_DPAD_RIGHT:    return HH_KEY_RIGHT;
        case HH_ACTION_DPAD_UPLEFT:   return HH_KEY_UPLEFT;
        case HH_ACTION_DPAD_UPRIGHT:  return HH_KEY_UPRIGHT;
        case HH_ACTION_DPAD_DOWNLEFT: return HH_KEY_DOWNLEFT;
        case HH_ACTION_DPAD_DOWNRIGHT:return HH_KEY_DOWNRIGHT;
        default:                      return HH_KEY_NONE;
    }
}

static int isDirection(HHAction action) {
    return directionKey(action) != HH_KEY_NONE;
}

/* Stage 6: exact chord resolution. Two ADJACENT cardinals yield the exact
 * diagonal; opposites and duplicates yield NONE (no silent direction guess). */
HHAction hhActionChord(HHAction a, HHAction b) {
    if ((a == HH_ACTION_DPAD_UP && b == HH_ACTION_DPAD_LEFT)
        || (a == HH_ACTION_DPAD_LEFT && b == HH_ACTION_DPAD_UP)) {
        return HH_ACTION_DPAD_UPLEFT;
    }
    if ((a == HH_ACTION_DPAD_UP && b == HH_ACTION_DPAD_RIGHT)
        || (a == HH_ACTION_DPAD_RIGHT && b == HH_ACTION_DPAD_UP)) {
        return HH_ACTION_DPAD_UPRIGHT;
    }
    if ((a == HH_ACTION_DPAD_DOWN && b == HH_ACTION_DPAD_LEFT)
        || (a == HH_ACTION_DPAD_LEFT && b == HH_ACTION_DPAD_DOWN)) {
        return HH_ACTION_DPAD_DOWNLEFT;
    }
    if ((a == HH_ACTION_DPAD_DOWN && b == HH_ACTION_DPAD_RIGHT)
        || (a == HH_ACTION_DPAD_RIGHT && b == HH_ACTION_DPAD_DOWN)) {
        return HH_ACTION_DPAD_DOWNRIGHT;
    }
    return HH_ACTION_NONE;
}

int hhIsDiagonalAction(HHAction action) {
    switch (action) {
        case HH_ACTION_DPAD_UPLEFT:
        case HH_ACTION_DPAD_UPRIGHT:
        case HH_ACTION_DPAD_DOWNLEFT:
        case HH_ACTION_DPAD_DOWNRIGHT:
            return 1;
        default:
            return 0;
    }
}

int hhContextAllowsDiagonal(HHContext context) {
    return context == HH_CTX_EXPLORATION || context == HH_CTX_TARGET;
}

HHKey hhDiagonalKey(HHAction action) {
    return directionKey(action);
}

/* Stage 6: held-Menu action layer. Valid only in exploration; every binding
 * is consumed so the ordinary meaning (A confirm, B cancel, ...) never fires
 * alongside the layer action. Menu+L2 = rest until healed, Menu+R2 = search
 * all (Ctrl+S). Ascend/descend are intentionally NOT bound: walking onto the
 * stairs already triggers travel in the core. */
HHKeyEvent hhMenuLayerKey(HHAction action, HHContext context) {
    HHKeyEvent out;
    out.key = HH_KEY_NONE;
    out.controlKey = 0;
    out.shiftKey = 0;
    out.character = 0;
    if (context != HH_CTX_EXPLORATION) return out;
    switch (action) {
        case HH_ACTION_A:      out.key = HH_KEY_AUTOEXPLORE;       break;
        case HH_ACTION_X:      out.key = HH_KEY_DISCOVERIES;       break;
        case HH_ACTION_Y:      out.key = HH_KEY_FEATS;             break;
        case HH_ACTION_DPAD_LEFT:  out.key = HH_KEY_SWAP;          break;
        case HH_ACTION_DPAD_RIGHT: out.key = HH_KEY_RETHROW;       break;
        case HH_ACTION_L2:     out.key = HH_KEY_REST_UNTIL_HEALED; break;
        case HH_ACTION_R2:
            out.key = HH_KEY_SEARCH_ALL;
            out.controlKey = 1;
            break;
        default: break;
    }
    return out;
}

/* --- On-screen keyboard model (M3.7) ------------------------------------ */

/* Uppercase letters and symbols; hhOskChar() lowercases letters unless shift
 * is active.  '_' in the last row is a placeholder for the action row. */
static const char *const OSK_ROWS[HH_OSK_ROWS] = {
    "1234567890",
    "QWERTYUIOP",
    "ASDFGHJKL ",
    "ZXCVBNM-. ",
    "__________"
};

static int g_oskFocus = 0;
static int g_oskShift = 0;
static int g_oskNumeric = 0;

static int oskRowEnabled(int row) {
    if (row == 0 || row == 4) return 1;              /* digits and actions */
    return !g_oskNumeric;                            /* QWERTY rows hidden in numeric mode */
}

static int oskCellEnabled(int row, int col) {
    if (row < 0 || row >= HH_OSK_ROWS || col < 0 || col >= HH_OSK_COLS) return 0;
    if (!oskRowEnabled(row)) return 0;
    if (row >= 1 && row <= 3 && OSK_ROWS[row][col] == ' ') return 0; /* stagger gap */
    if (row == HH_OSK_ROWS - 1 && g_oskNumeric) {
        /* Seed entry is digit-only: Space would be discarded by the core's
         * TEXT_INPUT_NUMBERS bounds and Shift has no letter layer to toggle. */
        if (col <= 1 || (col >= 4 && col <= 5)) return 0;
    }
    return 1;
}

void hhOskReset(int numeric) {
    g_oskNumeric = numeric ? 1 : 0;
    g_oskShift = 0;
    g_oskFocus = 0;
}

void hhOskSyncSession(int active, int numeric) {
    static int wasActive = 0;
    static int lastNumeric = 0;
    if (active && (!wasActive || numeric != lastNumeric)) {
        hhOskReset(numeric);
    }
    wasActive = active;
    lastNumeric = numeric;
}

int hhOskNumeric(void) { return g_oskNumeric; }
int hhOskShift(void)   { return g_oskShift; }
int hhOskFocus(void)   { return g_oskFocus; }

int hhOskEnabled(int index) {
    if (index < 0 || index >= HH_OSK_COUNT) return 0;
    return oskCellEnabled(index / HH_OSK_COLS, index % HH_OSK_COLS);
}

HHOSKKeyKind hhOskKind(int index) {
    int row, col;
    if (!hhOskEnabled(index)) return HH_OSK_NONE;
    row = index / HH_OSK_COLS;
    col = index % HH_OSK_COLS;
    if (row <= 3) return HH_OSK_CHAR;
    if (col <= 1) return HH_OSK_SPACE;
    if (col <= 3) return HH_OSK_BACKSPACE;
    if (col <= 5) return HH_OSK_SHIFT;
    if (col <= 7) return HH_OSK_CONFIRM;
    return HH_OSK_CANCEL;
}

char hhOskBaseChar(int index) {
    if (hhOskKind(index) != HH_OSK_CHAR) return 0;
    return OSK_ROWS[index / HH_OSK_COLS][index % HH_OSK_COLS];
}

char hhOskChar(int index) {
    char c = hhOskBaseChar(index);
    if (c && !g_oskShift && c >= 'A' && c <= 'Z') {
        c = (char) (c - 'A' + 'a');
    }
    return c;
}

void hhOskToggleShift(void) {
    if (!g_oskNumeric) {           /* numeric mode has no letter layer */
        g_oskShift = !g_oskShift;
    }
}

const char *hhOskActionLabel(HHOSKKeyKind kind) {
    switch (kind) {
        case HH_OSK_SPACE:     return "Space";
        case HH_OSK_BACKSPACE: return "Bksp";
        case HH_OSK_SHIFT:     return "Shift";
        case HH_OSK_CONFIRM:   return "Confirm";
        case HH_OSK_CANCEL:    return "Cancel";
        default:               return "";
    }
}

void hhOskMove(HHAction action) {
    int row = g_oskFocus / HH_OSK_COLS;
    int col = g_oskFocus % HH_OSK_COLS;
    int i, r, c, best, bestDist, d;

    if (action == HH_ACTION_DPAD_LEFT || action == HH_ACTION_DPAD_RIGHT) {
        int step = (action == HH_ACTION_DPAD_LEFT) ? -1 : 1;
        for (i = 1; i <= HH_OSK_COLS; i++) {
            c = (col + step * i + HH_OSK_COLS * i) % HH_OSK_COLS;
            if (oskCellEnabled(row, c)) {
                g_oskFocus = row * HH_OSK_COLS + c;
                return;
            }
        }
        return;
    }
    if (action == HH_ACTION_DPAD_UP || action == HH_ACTION_DPAD_DOWN) {
        int step = (action == HH_ACTION_DPAD_UP) ? -1 : 1;
        for (i = 1; i <= HH_OSK_ROWS; i++) {
            r = (row + step * i + HH_OSK_ROWS * i) % HH_OSK_ROWS;
            if (!oskRowEnabled(r)) continue;
            best = -1;
            bestDist = HH_OSK_COLS + 1;
            for (c = 0; c < HH_OSK_COLS; c++) {
                if (!oskCellEnabled(r, c)) continue;
                d = c - col;
                if (d < 0) d = -d;
                if (d < bestDist) {
                    bestDist = d;
                    best = c;
                }
            }
            if (best >= 0) {
                g_oskFocus = r * HH_OSK_COLS + best;
                return;
            }
        }
    }
}

HHKeyEvent hhTranslateAction(HHAction action, HHContext context) {
    HHKeyEvent out;
    out.key = HH_KEY_NONE;
    out.controlKey = 0;
    out.shiftKey = 0;
    out.character = 0;

    if (action == HH_ACTION_NONE) {
        return out;
    }

    if (isDirection(action)) {
        /* Stage 6: diagonals are gameplay-only. In menus, dialogs, inventory
         * and the OSK the D-pad must stay strictly cardinal. */
        if (hhIsDiagonalAction(action) && !hhContextAllowsDiagonal(context)) {
            return out;
        }
        /* M3.7: on the OSK the D-pad moves the focused key, not the player or
         * the cursor.  It emits no Brogue event of its own. */
        if (context == HH_CTX_TEXT_INPUT || context == HH_CTX_IN_GAME_MENU) {
            if (context == HH_CTX_TEXT_INPUT) hhOskMove(action);
            else hhInGameMenuMove(action);
            return out;
        }
        /* Arrow keys work in every Brogue context that reads movement:
         * exploration, targeting, inventory paging, dialog scrolling and
         * flyout scrolling.  h/j/k/l are deliberately avoided because they
         * are item letters inside the inventory.  Stage 5 fix: all four D-pad
         * directions must translate here — the pointer-navigation path in the
         * SDL adapter needs a non-NONE key to reach hhMenuTarget/hhMovePointer.
         * Suppressing left/right made them dead in the inventory item menu. */
        out.key = directionKey(action);
        return out;
    }

    switch (context) {
        case HH_CTX_EXPLORATION:
            switch (action) {
                case HH_ACTION_A:      out.key = HH_KEY_CONFIRM;         break;
                case HH_ACTION_B:      out.key = HH_KEY_CANCEL;          break;
                case HH_ACTION_X:      out.key = HH_KEY_INVENTORY;       break;
                /* Stage 5 UX: Y cycles the display zoom; Help stays at Start -> Help. */
                case HH_ACTION_Y:      out.key = HH_KEY_ZOOM;            break;
                /* Start is intercepted by the SDL adapter (M4.3) to toggle the
                 * in-game menu; it is deliberately not translated here. */
                case HH_ACTION_SELECT: out.key = HH_KEY_MESSAGE_ARCHIVE; break;
                case HH_ACTION_L2:     out.key = HH_KEY_WAIT;            break;
                case HH_ACTION_R2:     out.key = HH_KEY_SEARCH;          break;
                default: break;
            }
            break;

        case HH_CTX_IN_GAME_MENU:
            /* M4.3: D-pad navigation was handled above (focus only, no event).
             * Activate/Close are resolved by the SDL adapter against the pure
             * in-game menu model. */
            switch (action) {
                case HH_ACTION_A:      out.key = HH_KEY_IGM_ACTIVATE; break;
                case HH_ACTION_B:      out.key = HH_KEY_IGM_CLOSE;    break;
                case HH_ACTION_START:  out.key = HH_KEY_IGM_CLOSE;    break;
                default: break;
            }
            break;

        case HH_CTX_TARGET:
            /* Targeting: A confirms, B cancels, SELECT cycles auto-targets via
             * the core's TAB handling in moveCursor(). X/Start stay suppressed;
             * Y cycles the display zoom (UI-side, explore and target only). */
            switch (action) {
                case HH_ACTION_A:      out.key = HH_KEY_CONFIRM;      break;
                case HH_ACTION_B:      out.key = HH_KEY_CANCEL;       break;
                case HH_ACTION_SELECT: out.key = HH_KEY_SELECT_NEXT;  break;
                case HH_ACTION_Y:      out.key = HH_KEY_ZOOM;         break;
                default: break;
            }
            break;

        case HH_CTX_TEXT_INPUT:
            /* M3.7: A activates the focused OSK key; B cancels, X backspaces
             * and Y toggles the letter-case layer.  Directions were handled
             * above.  Start/Select stay suppressed. */
            switch (action) {
                case HH_ACTION_A: {
                    HHOSKKeyKind kind = hhOskKind(hhOskFocus());
                    if (kind == HH_OSK_CHAR || kind == HH_OSK_SPACE) {
                        out.key = HH_KEY_TEXT_CHAR;
                        out.character = (kind == HH_OSK_SPACE)
                                        ? ' ' : hhOskChar(hhOskFocus());
                    } else if (kind == HH_OSK_BACKSPACE) {
                        out.key = HH_KEY_BACKSPACE;
                    } else if (kind == HH_OSK_SHIFT) {
                        hhOskToggleShift();
                    } else if (kind == HH_OSK_CONFIRM) {
                        out.key = HH_KEY_CONFIRM;
                    } else if (kind == HH_OSK_CANCEL) {
                        out.key = HH_KEY_CANCEL;
                    }
                    break;
                }
                case HH_ACTION_B: out.key = HH_KEY_CANCEL;     break;
                case HH_ACTION_X: out.key = HH_KEY_BACKSPACE;  break;
                case HH_ACTION_Y: hhOskToggleShift();          break;
                default: break;
            }
            break;

        case HH_CTX_INVENTORY:
        case HH_CTX_MENU:
        case HH_CTX_DIALOG:
            /* Modal contexts: only confirm/cancel are safe.  X/Y/Start/Select
             * would be interpreted as hotkeys or, in text boxes, as literal
             * characters, so they are suppressed here. */
            switch (action) {
                case HH_ACTION_A: out.key = HH_KEY_CONFIRM; break;
                case HH_ACTION_B: out.key = HH_KEY_CANCEL;  break;
                default: break;
            }
            break;

        default:
            break;
    }
    return out;
}

HHKeyEvent hhKeyEventFromSDLButton(int sdlButton, HHContext context) {
    return hhTranslateAction(hhActionFromSDLButton(sdlButton), context);
}

HHKeyEvent hhKeyEventFromSDLHat(int hatValue, HHContext context) {
    return hhTranslateAction(hhActionFromSDLHat(hatValue), context);
}

const char *hhActionName(HHAction action) {
    switch (action) {
        case HH_ACTION_NONE:       return "NONE";
        case HH_ACTION_DPAD_UP:    return "DPAD_UP";
        case HH_ACTION_DPAD_DOWN:  return "DPAD_DOWN";
        case HH_ACTION_DPAD_LEFT:  return "DPAD_LEFT";
        case HH_ACTION_DPAD_RIGHT: return "DPAD_RIGHT";
        case HH_ACTION_DPAD_UPLEFT:   return "DPAD_UPLEFT";
        case HH_ACTION_DPAD_UPRIGHT:  return "DPAD_UPRIGHT";
        case HH_ACTION_DPAD_DOWNLEFT: return "DPAD_DOWNLEFT";
        case HH_ACTION_DPAD_DOWNRIGHT:return "DPAD_DOWNRIGHT";
        case HH_ACTION_A:          return "A";
        case HH_ACTION_B:          return "B";
        case HH_ACTION_X:          return "X";
        case HH_ACTION_Y:          return "Y";
        case HH_ACTION_START:      return "START";
        case HH_ACTION_SELECT:     return "SELECT";
        case HH_ACTION_L2:         return "L2";
        case HH_ACTION_R2:         return "R2";
        case HH_ACTION_L1:         return "L1";
        case HH_ACTION_R1:         return "R1";
        case HH_ACTION_MENU:       return "MENU";
        default:                   return "UNKNOWN";
    }
}

const char *hhContextName(HHContext context) {
    switch (context) {
        case HH_CTX_EXPLORATION: return "exploration";
        case HH_CTX_TARGET:      return "target";
        case HH_CTX_INVENTORY:   return "inventory";
        case HH_CTX_MENU:        return "menu";
        case HH_CTX_DIALOG:      return "dialog";
        case HH_CTX_TEXT_INPUT:  return "text_input";
        case HH_CTX_IN_GAME_MENU:return "in_game_menu";
        default:                 return "unknown";
    }
}

const char *hhKeyName(HHKey key) {
    switch (key) {
        case HH_KEY_NONE:            return "NONE";
        case HH_KEY_UP:              return "UP";
        case HH_KEY_DOWN:            return "DOWN";
        case HH_KEY_LEFT:            return "LEFT";
        case HH_KEY_RIGHT:           return "RIGHT";
        case HH_KEY_CONFIRM:         return "CONFIRM";
        case HH_KEY_CANCEL:          return "CANCEL";
        case HH_KEY_INVENTORY:       return "INVENTORY";
        case HH_KEY_HELP:            return "HELP";
        case HH_KEY_MAIN_MENU:       return "MAIN_MENU";
        case HH_KEY_MESSAGE_ARCHIVE: return "MESSAGE_ARCHIVE";
        case HH_KEY_SELECT_NEXT:     return "SELECT_NEXT";
        case HH_KEY_REST:            return "REST";
        case HH_KEY_BACKSPACE:       return "BACKSPACE";
        case HH_KEY_TEXT_CHAR:       return "TEXT_CHAR";
        case HH_KEY_WAIT:            return "WAIT";
        case HH_KEY_SEARCH:          return "SEARCH";
        case HH_KEY_IGM_ACTIVATE:    return "IGM_ACTIVATE";
        case HH_KEY_IGM_CLOSE:       return "IGM_CLOSE";
        case HH_KEY_ZOOM:            return "ZOOM";
        case HH_KEY_UPLEFT:          return "UPLEFT";
        case HH_KEY_UPRIGHT:         return "UPRIGHT";
        case HH_KEY_DOWNLEFT:        return "DOWNLEFT";
        case HH_KEY_DOWNRIGHT:       return "DOWNRIGHT";
        case HH_KEY_AUTOEXPLORE:     return "AUTOEXPLORE";
        case HH_KEY_REST_UNTIL_HEALED:return "REST_UNTIL_HEALED";
        case HH_KEY_SWAP:            return "SWAP";
        case HH_KEY_RETHROW:         return "RETHROW";
        case HH_KEY_DISCOVERIES:     return "DISCOVERIES";
        case HH_KEY_FEATS:           return "FEATS";
        case HH_KEY_SEARCH_ALL:      return "SEARCH_ALL";
        case HH_KEY_TOGGLE_COLORS:   return "TOGGLE_COLORS";
        case HH_KEY_TOGGLE_STEALTH:  return "TOGGLE_STEALTH";
        case HH_KEY_GRAPHICS:        return "GRAPHICS";
        default:                     return "UNKNOWN";
    }
}
