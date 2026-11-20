/*
 * handheld-input.h -- SDL-independent controller action translation.
 *
 * This layer converts controller actions (identified either by abstract
 * HHAction values or by raw SDL GameController button/hat numbers) into a
 * small set of virtual keys.  It has NO dependency on SDL or on the Brogue
 * headers, so it can be compiled and tested on any host C compiler.
 *
 * The SDL-facing adapter (handheld-sdl.c) converts the virtual keys into the
 * concrete Brogue key constants from Rogue.h and fills a rogueEvent.  Keeping
 * the two steps separate keeps the mapping logic testable without a gamepad,
 * a cross compiler, or a device.
 *
 * This file is new (Stage 2).  It does not modify any upstream source.
 */
#ifndef BROGUE_HANDHELD_INPUT_H
#define BROGUE_HANDHELD_INPUT_H

/*
 * Numeric SDL constants, mirrored from SDL 2.28.4 headers:
 *   SDL_gamecontroller.h: SDL_GameControllerButton
 *   SDL_joystick.h:       SDL_HAT_*
 * handheld-sdl.c contains compile-time checks against the real headers, so
 * an SDL upgrade that changes these values will break the build loudly.
 */
#define HH_SDL_BUTTON_INVALID         (-1)
#define HH_SDL_BUTTON_A               0
#define HH_SDL_BUTTON_B               1
#define HH_SDL_BUTTON_X               2
#define HH_SDL_BUTTON_Y               3
#define HH_SDL_BUTTON_BACK            4
#define HH_SDL_BUTTON_GUIDE           5
#define HH_SDL_BUTTON_START           6
#define HH_SDL_BUTTON_LEFTSHOULDER    9
#define HH_SDL_BUTTON_RIGHTSHOULDER  10
#define HH_SDL_BUTTON_DPAD_UP        11
#define HH_SDL_BUTTON_DPAD_DOWN      12
#define HH_SDL_BUTTON_DPAD_LEFT      13
#define HH_SDL_BUTTON_DPAD_RIGHT     14

/* SDL_GameControllerAxis values for the two shoulder triggers.  SDL exposes
 * L2/R2 as axes, not buttons; a threshold + digital latch turns them into
 * discrete actions.  Hand-checked against SDL 2.28.4 SDL_gamecontroller.h. */
#define HH_SDL_AXIS_TRIGGERLEFT      4
#define HH_SDL_AXIS_TRIGGERRIGHT     5
#define HH_SDL_TRIGGER_THRESHOLD     16000  /* press point, of SDL_AXIS_MAX 32767 */
#define HH_SDL_TRIGGER_RELEASE       8000   /* release point: hysteresis against jitter */

#define HH_SDL_HAT_CENTERED  0x00
#define HH_SDL_HAT_UP        0x01
#define HH_SDL_HAT_RIGHT     0x02
#define HH_SDL_HAT_DOWN      0x04
#define HH_SDL_HAT_LEFT      0x08

typedef enum {
    HH_ACTION_NONE = 0,
    HH_ACTION_DPAD_UP,
    HH_ACTION_DPAD_DOWN,
    HH_ACTION_DPAD_LEFT,
    HH_ACTION_DPAD_RIGHT,
    /* Stage 6: exact two-direction D-pad chords.  These are deliberately
     * outside the HH_ACTION_DPAD_UP..RIGHT range so hhIsDpad() (pointer and
     * list navigation) keeps seeing cardinals only. */
    HH_ACTION_DPAD_UPLEFT,
    HH_ACTION_DPAD_UPRIGHT,
    HH_ACTION_DPAD_DOWNLEFT,
    HH_ACTION_DPAD_DOWNRIGHT,
    HH_ACTION_A,
    HH_ACTION_B,
    HH_ACTION_X,
    HH_ACTION_Y,
    HH_ACTION_START,
    HH_ACTION_SELECT,
    HH_ACTION_L2,             /* wait  (trigger axis) */
    HH_ACTION_R2,             /* search (trigger axis) */
    HH_ACTION_L1,             /* Stage 6: run modifier (was inert) */
    HH_ACTION_R1,             /* Stage 6: diagonal-chord modifier (was run) */
    HH_ACTION_MENU            /* Stage 6: physical Menu = SDL GUIDE (button 5) */
} HHAction;

typedef enum {
    HH_CTX_EXPLORATION = 0,   /* normal play: D-pad moves the player */
    HH_CTX_TARGET,            /* cursor/target selection */
    HH_CTX_INVENTORY,         /* inventory list / item detail */
    HH_CTX_MENU,              /* title screen / flyout / action menu */
    HH_CTX_DIALOG,            /* confirm dialog / text box / help / archive */
    HH_CTX_TEXT_INPUT,        /* M3.7: getInputTextString on-screen keyboard */
    HH_CTX_IN_GAME_MENU       /* M4.3: UI-only in-game menu (no core hook) */
} HHContext;

/*
 * Virtual keys.  These intentionally do NOT use Brogue's numeric values;
 * handheld-sdl.c maps them to the real Rogue.h constants in one place.
 */
typedef enum {
    HH_KEY_NONE = 0,
    HH_KEY_UP,
    HH_KEY_DOWN,
    HH_KEY_LEFT,
    HH_KEY_RIGHT,
    HH_KEY_CONFIRM,
    HH_KEY_CANCEL,
    HH_KEY_INVENTORY,
    HH_KEY_HELP,
    HH_KEY_MAIN_MENU,
    HH_KEY_MESSAGE_ARCHIVE,
    HH_KEY_SELECT_NEXT,
    HH_KEY_REST,
    HH_KEY_BACKSPACE,   /* M3.7: DELETE_KEY in a text field */
    HH_KEY_TEXT_CHAR,   /* M3.7: printable character in HHKeyEvent.character */
    HH_KEY_WAIT,        /* M4.3: PERIOD_KEY (wait one turn) */
    HH_KEY_SEARCH,      /* M4.3: SEARCH_KEY */
    HH_KEY_IGM_ACTIVATE,/* M4.3: in-game menu Confirm (handled by SDL adapter) */
    HH_KEY_IGM_CLOSE,   /* M4.3: in-game menu Cancel (handled by SDL adapter) */
    HH_KEY_ZOOM,        /* Stage 5 UX: Y cycles display-camera zoom (UI-side only) */
    /* Stage 6: exact diagonal movement and Menu-layer / in-game-menu actions. */
    HH_KEY_UPLEFT,
    HH_KEY_UPRIGHT,
    HH_KEY_DOWNLEFT,
    HH_KEY_DOWNRIGHT,
    HH_KEY_AUTOEXPLORE,
    HH_KEY_REST_UNTIL_HEALED,
    HH_KEY_SWAP,
    HH_KEY_RETHROW,
    HH_KEY_DISCOVERIES,
    HH_KEY_FEATS,
    HH_KEY_SEARCH_ALL,
    HH_KEY_TOGGLE_COLORS,
    HH_KEY_TOGGLE_STEALTH,
    HH_KEY_GRAPHICS
} HHKey;

typedef struct {
    HHKey key;          /* HH_KEY_NONE means "no Brogue event" */
    int controlKey;     /* 0 or 1 */
    int shiftKey;       /* 0 or 1 */
    char character;     /* valid only when key == HH_KEY_TEXT_CHAR */
} HHKeyEvent;

/* Raw SDL GameController button index -> abstract action. */
HHAction hhActionFromSDLButton(int sdlButton);

/* Raw SDL hat value -> abstract action (diagonals resolve to one direction). */
HHAction hhActionFromSDLHat(int hatValue);

/* M4.3: raw SDL trigger axis + value -> abstract action (L2/R2 only).
 * Uses HH_SDL_TRIGGER_THRESHOLD; analog noise below it yields NONE. */
HHAction hhActionFromSDLAxis(int axis, int value);

/* --- Authoritative context resolution (Stage 4, M4.2) --------------------
 * Pure function: given the authoritative signals (guarded hook globals and
 * core flags), return the single active UI context.  No SDL, no side effects.
 * The SDL adapter builds this struct every poll; host tests drive it directly.
 */
typedef struct {
    int textInputActive;      /* hook 0009 */
    int inGameMenuOpen;       /* M4.3 UI-only state */
    int inventoryOpen;        /* hook 0007 */
    int menuActive;           /* hook 0006: hhActiveMenuState != NULL */
    int gameInProgress;
    int playbackMode;
    int textInputArg;         /* core callback argument; NOT a text-entry signal */
    int targetingActive;      /* hook 0008 */
    int cursorMode;           /* core examine cursor */
} HHContextFlags;

HHContext hhResolveContext(const HHContextFlags *flags);

/* --- In-game menu state (Stage 4, M4.3) ----------------------------------
 * Pure, SDL-independent UI state for the Start menu.  The SDL adapter toggles
 * it and moves focus; rendering reads the same model.  No core hook is used.
 */
typedef enum {
    HH_IGM_SAVE_EXIT = 0,  /* quick save with the default name, no prompts */
    HH_IGM_HELP,
    HH_IGM_COLORS,         /* [] value: LEFT/RIGHT toggles true-colour mode */
    HH_IGM_STEALTH,        /* [] value: LEFT/RIGHT toggles stealth range */
    HH_IGM_GRAPHICS,       /* [] value: LEFT/RIGHT cycles text/tiles/hybrid */
    HH_IGM_ZOOM,           /* [] value: LEFT/RIGHT cycles 100%/200% */
    HH_IGM_RETURN,
    HH_IGM_QUIT,
    HH_IGM_COUNT
} HHInGameMenuEntry;

/* Stage 6: core text screens that need a scrollable 2x viewport instead of
 * snapping the whole frame back to 1:1. Mirrored by the guarded hhCoreScreen
 * global in GlobalsBase.h/.c (set by patch 0011). */
typedef enum {
    HH_CORE_SCREEN_NONE = 0,
    HH_CORE_SCREEN_HELP,
    HH_CORE_SCREEN_ARCHIVE,
    HH_CORE_SCREEN_DISCOVERIES,
    HH_CORE_SCREEN_FEATS
} HHCoreScreen;

/* Geometry mirrors Rogue.h (COLS=100, DCOLS=79, mapToWindowX(0)=21) and is
 * pinned with the grid. SCREEN view is 50x16 so it reuses the same 13x30
 * glyph atlas as the 200% map viewport. */
/* SCREEN mode fits all 79 content columns (core x 21..99) at a uniform ~1.27x
 * scale: 80 cells x 8px wide, 24 rows x 20px high at 640x480. Text is scaled
 * evenly (no stretching) and only vertical scrolling is needed. */
#define HH_SCREEN_VIEW_COLS 80
#define HH_SCREEN_VIEW_ROWS 24
#define HH_SCREEN_MAP_X0    21   /* legacy scroll-model value; rendering pins 0 */
#define HH_SCREEN_MAX_SCROLL (34 - HH_SCREEN_VIEW_ROWS)

/* Device-specific virtual canvas for the four core text screens. Core cells
 * x=0..79, y=0..23 map to the full 640x480 output at a uniform 8x20 px/cell.
 * There is no sidebar region, so those glyphs are never drawn in SCREEN mode. */
#define HH_SCREEN_CANVAS_X0    0
#define HH_SCREEN_CANVAS_COLS  80
#define HH_SCREEN_CANVAS_ROWS  24
#define HH_SCREEN_TEXT_X0      1
#define HH_SCREEN_TEXT_W       78
#define HH_SCREEN_OPAQUE       100  /* INTERFACE_OPACITY (95) leaks 5% of the map */

/* Brogue-style Start menu panel geometry, in core cells and atlas pixels.
 * Shared by IO.c (buffer layout) and tiles.c (glyph/background renderer). */
#define HH_MENU_PANEL_X   24
#define HH_MENU_PANEL_Y   6
#define HH_MENU_PANEL_W   48
#define HH_MENU_PANEL_H   15
#define HH_MENU_TILE_W    9
#define HH_MENU_TILE_H    18

/* Inventory zoom viewport, active only when the display zoom is 200%.
 * 49x16 cells at 13x28 px = 637x448 px: ~2x the native 6.4x14.1 cell with a
 * near-identical aspect ratio, so glyphs enlarge without being squished. */
#define HH_INV_VIEW_COLS  49
#define HH_INV_VIEW_ROWS  16
#define HH_INV_TILE_W     13
#define HH_INV_TILE_H     28

/* Pure scroll state for core text screens. Host-testable, no SDL/Brogue deps. */
void hhCoreScrollReset(int screen);
void hhCoreScrollMove(HHAction action);
int  hhCoreScrollX(void);
int  hhCoreScrollY(void);

/* Stage 6 helpers (pure, host-testable). */
HHAction hhActionChord(HHAction a, HHAction b);      /* adjacent cardinals -> diagonal */
int      hhIsDiagonalAction(HHAction action);
int      hhContextAllowsDiagonal(HHContext context); /* exploration/target only */
HHKey    hhDiagonalKey(HHAction action);
HHKeyEvent hhMenuLayerKey(HHAction action, HHContext context);

void        hhInGameMenuToggle(void);
void        hhInGameMenuOpen(void);
void        hhInGameMenuClose(void);
int         hhInGameMenuOpenState(void);
void        hhInGameMenuMove(HHAction action);
int         hhInGameMenuFocus(void);
int         hhInGameMenuIsValue(int index);  /* rows whose value LEFT/RIGHT changes */
/* Returns the chosen entry on Confirm (and closes the menu), or -1 if the
 * menu was not open / focus is invalid. */
int         hhInGameMenuActivate(void);
const char *hhInGameMenuLabel(int index);

/* --- Display-camera zoom (Stage 5) ---------------------------------------
 * Pure state owned by this layer; tiles.c reads hhZoomLevel() through the
 * guarded hook declared in GlobalsBase.h. Level 0 is the stock view. */
/* Stage 6: only integer 100% and 200% are supported; 150% was removed because
 * non-integer glyph scaling looked poor on device. */
#define HH_ZOOM_LEVELS  3
void        hhZoomCycle(void);
void        hhZoomSetLevel(int level);  /* clamp + set; used when loading zoom.cfg */
int         hhZoomLevel(void);
const char *hhZoomLabel(void);   /* "100%", "150%", "200%" */

/* Translate an action for a given UI context into a virtual key event. */
HHKeyEvent hhTranslateAction(HHAction action, HHContext context);

/* Convenience: raw SDL button/hat + context -> virtual key event. */
HHKeyEvent hhKeyEventFromSDLButton(int sdlButton, HHContext context);
HHKeyEvent hhKeyEventFromSDLHat(int hatValue, HHContext context);

/* Stable names for diagnostics (never NULL). */
const char *hhActionName(HHAction action);
const char *hhContextName(HHContext context);
const char *hhKeyName(HHKey key);

/* --- On-screen keyboard model (Stage 3, M3.7) ----------------------------
 * Pure, SDL-independent navigation state for the text-entry overlay.  The grid
 * is 10 columns x 5 rows: a digits row, three QWERTY rows, and an action row
 * (Space / Backspace / Shift / Confirm / Cancel, two grid cells each).  Numeric
 * mode keeps only the digits and action rows enabled.  Rendering reads the same
 * model so focus and highlighting cannot disagree with input translation. */
#define HH_OSK_COLS   10
#define HH_OSK_ROWS   5
#define HH_OSK_COUNT  (HH_OSK_COLS * HH_OSK_ROWS)

typedef enum {
    HH_OSK_NONE = 0,
    HH_OSK_CHAR,
    HH_OSK_SPACE,
    HH_OSK_BACKSPACE,
    HH_OSK_SHIFT,
    HH_OSK_CONFIRM,
    HH_OSK_CANCEL
} HHOSKKeyKind;

void            hhOskReset(int numeric);
/* Call from both the render and input paths: resets focus/layer on the rising
 * edge of a text session, or when a nested prompt changes the entry type. */
void            hhOskSyncSession(int active, int numeric);
int             hhOskNumeric(void);
int             hhOskShift(void);
int             hhOskFocus(void);
int             hhOskEnabled(int index);
HHOSKKeyKind    hhOskKind(int index);
char            hhOskBaseChar(int index);   /* uppercase/symbol, 0 if not a char key */
char            hhOskChar(int index);        /* char to type, respecting shift */
void            hhOskToggleShift(void);
void            hhOskMove(HHAction action);  /* updates focus; emits no Brogue event */
const char *    hhOskActionLabel(HHOSKKeyKind kind);

#endif /* BROGUE_HANDHELD_INPUT_H */
