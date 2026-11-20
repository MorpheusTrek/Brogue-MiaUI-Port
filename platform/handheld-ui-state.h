/*
 * handheld-ui-state.h -- pure, SDL-independent state for the handheld UI.
 *
 * This is the navigation/focus/model layer of the Stage 3 handheld presentation
 * layer. It has NO dependency on SDL, on the Brogue headers, or on any device
 * API, so it can be compiled and exercised by a host C compiler. Rendering and
 * physical input live in separate modules; this file only decides what is
 * focused and what state a screen is in.
 *
 * New file (Stage 3).  It does not modify any upstream source.
 */
#ifndef BROGUE_HANDHELD_UI_STATE_H
#define BROGUE_HANDHELD_UI_STATE_H

#define HHUI_MAX_STACK   8
#define HHUI_MAX_ITEMS   64
#define HHUI_MAX_TITLE   64
#define HHUI_MAX_HINT    160
#define HHUI_MAX_TEXT    64

/* Stage 5: canonical gameplay hint.  Shared by the renderer and the host
 * regression test so the final physical labels cannot silently regress. */
#define HHUI_HINT_GAMEPLAY \
    "X  Inventory   Y  Zoom   L2  Wait   R2  Search   Select  Messages   Start  Menu   Menu+  Actions"

/* Abstract actions (Stage 3 defines actions, not physical buttons). */
typedef enum {
    HHUI_ACT_NONE = 0,
    HHUI_ACT_UP,
    HHUI_ACT_DOWN,
    HHUI_ACT_LEFT,
    HHUI_ACT_RIGHT,
    HHUI_ACT_CONFIRM,
    HHUI_ACT_CANCEL,
    HHUI_ACT_CONTEXT,
    HHUI_ACT_INSPECT,
    HHUI_ACT_SCROLL_UP,
    HHUI_ACT_SCROLL_DOWN,
    HHUI_ACT_SWITCH_TAB,
    HHUI_ACT_TEXT,
    HHUI_ACT_DELETE,
    HHUI_ACT_TOGGLE
} HHUIAction;

/* Logical screens.  Modal screens are marked when pushed. */
typedef enum {
    HHUI_SCREEN_NONE = 0,
    HHUI_SCREEN_GAMEPLAY,
    HHUI_SCREEN_MENU,
    HHUI_SCREEN_INVENTORY,
    HHUI_SCREEN_ITEM_ACTIONS,
    HHUI_SCREEN_TARGETING,
    HHUI_SCREEN_CONFIRM,
    HHUI_SCREEN_TEXT_INPUT,
    HHUI_SCREEN_IN_GAME_MENU,  /* M4.3: Start menu, UI-only */
    HHUI_SCREEN_INFO
} HHUIScreen;

typedef struct {
    int count;      /* total number of items */
    int cursor;     /* selected item index */
    int top;        /* first visible item index */
    int visible;    /* number of simultaneously visible rows */
} HHListState;

typedef struct {
    HHUIScreen stack[HHUI_MAX_STACK];
    int depth;                              /* number of active screens */
    int focus;                              /* focused control index */
    HHListState list;
    const char *listLabels[HHUI_MAX_ITEMS]; /* optional display labels */
    char title[HHUI_MAX_TITLE];
    char hint[HHUI_MAX_HINT];
    char text[HHUI_MAX_TEXT];               /* text-input buffer */
    int textCursor;
    int modal;                              /* top screen traps input */
    HHUIAction lastAction;
} HHUIState;

void        hhuiStateInit(HHUIState *s);
HHUIScreen  hhuiCurrentScreen(const HHUIState *s);
int         hhuiPushScreen(HHUIState *s, HHUIScreen screen, int modal);
int         hhuiPopScreen(HHUIState *s);
void        hhuiClearStack(HHUIState *s);

int         hhuiSetFocus(HHUIState *s, int focus);
int         hhuiMoveFocus(HHUIState *s, int delta);
int         hhuiFocus(const HHUIState *s);

void        hhuiSetList(HHUIState *s, int count, int visible);
void        hhuiSetListLabel(HHUIState *s, int index, const char *label);
const char *hhuiListLabel(const HHUIState *s, int index);
int         hhuiListMove(HHUIState *s, int delta);
int         hhuiListScroll(HHUIState *s, int rows);
int         hhuiListCursor(const HHUIState *s);
int         hhuiListMaxTop(const HHUIState *s);
/* Scrollbar thumb geometry: returns 1 when count > visible and fills pos/len.
 * Pure SDL-free math so both the renderer and host tests can use it. */
int         hhuiScrollThumb(int count, int top, int visible, int trackLen,
                            int *thumbPos, int *thumbLen);
int         hhuiListTop(const HHUIState *s);

void        hhuiSetTitle(HHUIState *s, const char *title);
void        hhuiSetHint(HHUIState *s, const char *hint);
void        hhuiSetText(HHUIState *s, const char *text);
int         hhuiSetTextCursor(HHUIState *s, int pos);
int         hhuiTextInsert(HHUIState *s, char c);
int         hhuiTextDelete(HHUIState *s);
int         hhuiTextCursor(const HHUIState *s);
const char *hhuiText(const HHUIState *s);

/* Applies navigation to focus/list/text and returns the normalized action so
 * the caller can resolve it against the gameplay engine. */
HHUIAction  hhuiApplyAction(HHUIState *s, HHUIAction action);

const char *hhuiActionName(HHUIAction action);
const char *hhuiScreenName(HHUIScreen screen);

#endif /* BROGUE_HANDHELD_UI_STATE_H */
