/*
 * handheld-ui-state.c -- implementation of the pure handheld UI state model.
 * See handheld-ui-state.h.  No SDL, no Rogue.h, no device dependencies.
 */
#include "handheld-ui-state.h"

#include <string.h>

static int clampInt(int value, int lo, int hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static int isListScreen(HHUIScreen screen) {
    switch (screen) {
        case HHUI_SCREEN_MENU:
        case HHUI_SCREEN_INVENTORY:
        case HHUI_SCREEN_ITEM_ACTIONS:
        case HHUI_SCREEN_TARGETING:
        case HHUI_SCREEN_TEXT_INPUT:
        case HHUI_SCREEN_IN_GAME_MENU:
        case HHUI_SCREEN_INFO:
            return 1;
        default:
            return 0;
    }
}

void hhuiStateInit(HHUIState *s) {
    int i;
    memset(s, 0, sizeof(*s));
    s->stack[0] = HHUI_SCREEN_NONE;
    s->depth = 1;
    s->list.visible = 8;
    for (i = 0; i < HHUI_MAX_ITEMS; i++) {
        s->listLabels[i] = (const char *) 0;
    }
    strcpy(s->title, "BROGUE");
    strcpy(s->hint, "");
}

HHUIScreen hhuiCurrentScreen(const HHUIState *s) {
    if (s->depth <= 0) return HHUI_SCREEN_NONE;
    return s->stack[s->depth - 1];
}

int hhuiPushScreen(HHUIState *s, HHUIScreen screen, int modal) {
    if (s->depth >= HHUI_MAX_STACK) return 0;
    s->stack[s->depth] = screen;
    s->depth++;
    s->modal = modal;
    s->focus = 0;
    s->list.cursor = 0;
    s->list.top = 0;
    s->textCursor = 0;
    return 1;
}

int hhuiPopScreen(HHUIState *s) {
    if (s->depth <= 1) return 0;
    s->depth--;
    s->modal = 0;
    s->focus = 0;
    return 1;
}

void hhuiClearStack(HHUIState *s) {
    s->depth = 1;
    s->stack[0] = HHUI_SCREEN_NONE;
    s->modal = 0;
    s->focus = 0;
}

int hhuiSetFocus(HHUIState *s, int focus) {
    if (focus < 0) return s->focus;
    s->focus = focus;
    return s->focus;
}

int hhuiMoveFocus(HHUIState *s, int delta) {
    s->focus = clampInt(s->focus + delta, 0, 1024);
    return s->focus;
}

int hhuiFocus(const HHUIState *s) {
    return s->focus;
}

void hhuiSetList(HHUIState *s, int count, int visible) {
    s->list.count = count < 0 ? 0 : count;
    s->list.visible = visible < 1 ? 1 : visible;
    s->list.cursor = clampInt(s->list.cursor, 0, s->list.count - 1);
    if (s->list.count == 0) {
        s->list.cursor = 0;
        s->list.top = 0;
    } else {
        s->list.top = clampInt(s->list.top, 0, s->list.count - s->list.visible);
    }
}

void hhuiSetListLabel(HHUIState *s, int index, const char *label) {
    if (index < 0 || index >= HHUI_MAX_ITEMS) return;
    s->listLabels[index] = label;
}

const char *hhuiListLabel(const HHUIState *s, int index) {
    if (index < 0 || index >= HHUI_MAX_ITEMS) return (const char *) 0;
    return s->listLabels[index];
}

int hhuiListMove(HHUIState *s, int delta) {
    if (s->list.count <= 0) return s->list.cursor;
    s->list.cursor = clampInt(s->list.cursor + delta, 0, s->list.count - 1);

    /* Keep the cursor inside the visible window. */
    if (s->list.cursor < s->list.top) {
        s->list.top = s->list.cursor;
    } else if (s->list.cursor >= s->list.top + s->list.visible) {
        s->list.top = s->list.cursor - s->list.visible + 1;
    }
    if (s->list.top < 0) s->list.top = 0;
    return s->list.cursor;
}

int hhuiListScroll(HHUIState *s, int rows) {
    if (s->list.count <= 0) return s->list.top;
    s->list.top = clampInt(s->list.top + rows, 0, s->list.count - s->list.visible);
    if (s->list.top < 0) s->list.top = 0;
    return s->list.top;
}

int hhuiListCursor(const HHUIState *s) {
    return s->list.cursor;
}

int hhuiListMaxTop(const HHUIState *s) {
    int max = s->list.count - s->list.visible;
    return max > 0 ? max : 0;
}

int hhuiScrollThumb(int count, int top, int visible, int trackLen,
                    int *thumbPos, int *thumbLen) {
    int maxTop, len, pos;
    if (!thumbPos || !thumbLen) return 0;
    if (visible < 1 || count <= visible || trackLen < 1) return 0;
    len = trackLen * visible / count;
    if (len < 6) len = 6;
    if (len > trackLen) len = trackLen;
    maxTop = count - visible;
    pos = (trackLen - len) * top / maxTop;
    if (pos < 0) pos = 0;
    if (pos > trackLen - len) pos = trackLen - len;
    *thumbPos = pos;
    *thumbLen = len;
    return 1;
}

int hhuiListTop(const HHUIState *s) {
    return s->list.top;
}

void hhuiSetTitle(HHUIState *s, const char *title) {
    if (!title) {
        s->title[0] = '\0';
        return;
    }
    strncpy(s->title, title, HHUI_MAX_TITLE - 1);
    s->title[HHUI_MAX_TITLE - 1] = '\0';
}

void hhuiSetHint(HHUIState *s, const char *hint) {
    if (!hint) {
        s->hint[0] = '\0';
        return;
    }
    strncpy(s->hint, hint, HHUI_MAX_HINT - 1);
    s->hint[HHUI_MAX_HINT - 1] = '\0';
}

void hhuiSetText(HHUIState *s, const char *text) {
    int i;
    s->text[0] = '\0';
    if (!text) {
        s->textCursor = 0;
        return;
    }
    for (i = 0; text[i] && i < HHUI_MAX_TEXT - 1; i++) {
        s->text[i] = text[i];
    }
    s->text[i] = '\0';
    s->textCursor = i;
}

int hhuiSetTextCursor(HHUIState *s, int pos) {
    int len = (int) strlen(s->text);
    s->textCursor = clampInt(pos, 0, len);
    return s->textCursor;
}

int hhuiTextInsert(HHUIState *s, char c) {
    int len = (int) strlen(s->text);
    int i;
    if (len >= HHUI_MAX_TEXT - 1) return 0;
    if (s->textCursor < 0) s->textCursor = 0;
    if (s->textCursor > len) s->textCursor = len;
    for (i = len; i > s->textCursor; i--) {
        s->text[i] = s->text[i - 1];
    }
    s->text[s->textCursor] = c;
    s->textCursor++;
    s->text[len + 1] = '\0';
    return 1;
}

int hhuiTextDelete(HHUIState *s) {
    int len = (int) strlen(s->text);
    int i;
    if (s->textCursor <= 0) return 0;
    s->textCursor--;
    for (i = s->textCursor; i < len; i++) {
        s->text[i] = s->text[i + 1];
    }
    return 1;
}

int hhuiTextCursor(const HHUIState *s) {
    return s->textCursor;
}

const char *hhuiText(const HHUIState *s) {
    return s->text;
}

HHUIAction hhuiApplyAction(HHUIState *s, HHUIAction action) {
    HHUIScreen screen = hhuiCurrentScreen(s);
    s->lastAction = action;

    switch (action) {
        case HHUI_ACT_UP:
            if (isListScreen(screen) && s->list.count > 0) {
                (void) hhuiListMove(s, -1);
            } else {
                (void) hhuiMoveFocus(s, -1);
            }
            break;
        case HHUI_ACT_DOWN:
            if (isListScreen(screen) && s->list.count > 0) {
                (void) hhuiListMove(s, 1);
            } else {
                (void) hhuiMoveFocus(s, 1);
            }
            break;
        case HHUI_ACT_LEFT:
            (void) hhuiMoveFocus(s, -1);
            break;
        case HHUI_ACT_RIGHT:
            (void) hhuiMoveFocus(s, 1);
            break;
        case HHUI_ACT_SCROLL_UP:
            (void) hhuiListScroll(s, -1);
            break;
        case HHUI_ACT_SCROLL_DOWN:
            (void) hhuiListScroll(s, 1);
            break;
        case HHUI_ACT_DELETE:
            if (screen == HHUI_SCREEN_TEXT_INPUT) {
                (void) hhuiTextDelete(s);
            }
            break;
        default:
            /* Confirm/Cancel/Context/Inspect/SwitchTab/Text/Toggle are
             * resolved by the caller against the gameplay engine. */
            break;
    }
    return action;
}

const char *hhuiActionName(HHUIAction action) {
    switch (action) {
        case HHUI_ACT_NONE:         return "None";
        case HHUI_ACT_UP:           return "Up";
        case HHUI_ACT_DOWN:         return "Down";
        case HHUI_ACT_LEFT:         return "Left";
        case HHUI_ACT_RIGHT:        return "Right";
        case HHUI_ACT_CONFIRM:      return "Confirm";
        case HHUI_ACT_CANCEL:       return "Cancel";
        case HHUI_ACT_CONTEXT:      return "Context";
        case HHUI_ACT_INSPECT:      return "Inspect";
        case HHUI_ACT_SCROLL_UP:    return "ScrollUp";
        case HHUI_ACT_SCROLL_DOWN:  return "ScrollDown";
        case HHUI_ACT_SWITCH_TAB:   return "SwitchTab";
        case HHUI_ACT_TEXT:         return "Text";
        case HHUI_ACT_DELETE:       return "Delete";
        case HHUI_ACT_TOGGLE:       return "Toggle";
        default:                    return "Unknown";
    }
}

const char *hhuiScreenName(HHUIScreen screen) {
    switch (screen) {
        case HHUI_SCREEN_NONE:          return "None";
        case HHUI_SCREEN_GAMEPLAY:      return "Gameplay";
        case HHUI_SCREEN_MENU:          return "MainMenu";
        case HHUI_SCREEN_INVENTORY:     return "Inventory";
        case HHUI_SCREEN_ITEM_ACTIONS:  return "ItemActions";
        case HHUI_SCREEN_TARGETING:     return "Targeting";
        case HHUI_SCREEN_CONFIRM:       return "Confirm";
        case HHUI_SCREEN_TEXT_INPUT:    return "TextInput";
        case HHUI_SCREEN_IN_GAME_MENU:  return "InGameMenu";
        case HHUI_SCREEN_INFO:          return "Info";
        default:                        return "Unknown";
    }
}
