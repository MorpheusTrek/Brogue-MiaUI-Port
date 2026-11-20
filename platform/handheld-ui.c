/*
 * handheld-ui.c -- SDL2 + SDL2_ttf rendering for the handheld presentation layer.
 *
 * The dungeon continues to be drawn by the stock tile renderer (tiles.c).  In
 * gameplay this module overlays a message band, a status panel and a hint strip
 * using SDL primitives and SDL2_ttf panel text (per docs/mockups/01-gameplay.svg).
 * All displayed values are read directly from the unmodified Brogue core
 * globals; no src/brogue/ file is edited.  Text textures are cached so glyphs
 * are not re-rasterized every frame on the ATM7039S.  A missing font is
 * non-fatal: geometry still draws.
 *
 * New file (Stage 3).  It does not modify any upstream source.
 */
#include "handheld-ui.h"

#include <stdio.h>
#include <string.h>
#include <SDL_ttf.h>

#include "platform.h"      /* dataDirectory; platform.h also includes Rogue.h */
#include "GlobalsBase.h"   /* rogue, player, displayedMessage, text-input hook */
#include "Globals.h"       /* statusEffectCatalog */
#include "handheld-input.h" /* shared OSK model (M3.7) */

#define HHUI_SCREEN_W       640
#define HHUI_SCREEN_H       480
#define HHUI_MARGIN         8
#define HHUI_HINT_Y         444
#define HHUI_HINT_H         28
#define HHUI_CACHE_SLOTS    80
#define HHUI_TEXT_MAX       96
#define HHUI_FONT_SIZE      15
#define HHUI_SMALL_SIZE     11
#define HHUI_TITLE_SIZE     30

/* Visual tokens (see the Stage 3 UI design document and SVG mockups). */
static const SDL_Color C_PANEL    = { 0x1a, 0x1a, 0x1f, 0xff };
static const SDL_Color C_EDGE     = { 0x5a, 0x50, 0x40, 0xff };
static const SDL_Color C_ACCENT   = { 0xd9, 0xb4, 0x5b, 0xff };
static const SDL_Color C_TEXT     = { 0xe8, 0xe6, 0xdf, 0xff };
static const SDL_Color C_DIM      = { 0x9a, 0x96, 0x8b, 0xff };
static const SDL_Color C_DARKTEXT = { 0x0b, 0x0b, 0x0d, 0xff };
static const SDL_Color C_HPBG     = { 0x2a, 0x2a, 0x30, 0xff };
static const SDL_Color C_HPFILL   = { 0x7f, 0xc9, 0x7f, 0xff };

typedef struct {
    char text[HHUI_TEXT_MAX];
    SDL_Color color;
    SDL_Texture *texture;
    int width;
    int height;
    int fontKind;   /* 0 = body, 1 = small, 2 = title */
    int used;
} HHTexCacheEntry;

static TTF_Font *g_font = (TTF_Font *) 0;
static TTF_Font *g_fontSmall = (TTF_Font *) 0;
static TTF_Font *g_fontTitle = (TTF_Font *) 0;
static int g_ttfReady = 0;
static int g_initialized = 0;
/* Stage 6: 2 when the display camera is zoomed; our own panels scale with it
 * so menus stay readable instead of snapping back to 1:1. */
static int g_uiScale = 1;
static int g_lastInProgress = 0;   /* previous raw rogue.gameInProgress */
static unsigned long g_lastTurn = 0; /* previous rogue.playerTurnNumber */
static int g_hudEverActive = 0;    /* a live game has been entered this session */
static HHUIState g_state;
static HHTexCacheEntry g_cache[HHUI_CACHE_SLOTS];
static int g_cacheNext = 0;
/* Stable storage for menu labels copied from the core hook (escape-stripped). */
static char g_menuLabels[HHUI_MAX_ITEMS][HHUI_TEXT_MAX];
/* Stage 3 M3.5: inventory list + item-action models.  The item list is rebuilt
 * from core globals; the action labels mirror the core's own action hotkeys. */
#define HHUI_MAX_ACTION_KEYS 12
static item *g_invItems[HHUI_MAX_ITEMS];
static char  g_invLabels[HHUI_MAX_ITEMS][HHUI_TEXT_MAX];
static char  g_actionLabels[HHUI_MAX_ACTION_KEYS][HHUI_TEXT_MAX];

static void fillRect(SDL_Renderer *r, int x, int y, int w, int h, SDL_Color c) {
    SDL_Rect rect;
    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(r, &rect);
}

static void drawPanel(SDL_Renderer *r, int x, int y, int w, int h) {
    fillRect(r, x, y, w, h, C_PANEL);
    SDL_SetRenderDrawColor(r, C_EDGE.r, C_EDGE.g, C_EDGE.b, 0xff);
    {
        SDL_Rect edge;
        edge.x = x;
        edge.y = y;
        edge.w = w;
        edge.h = h;
        SDL_RenderDrawRect(r, &edge);
    }
}

static void drawSeparator(SDL_Renderer *r, int x, int y, int w) {
    SDL_Rect line;
    line.x = x;
    line.y = y;
    line.w = w;
    line.h = 1;
    SDL_SetRenderDrawColor(r, C_EDGE.r, C_EDGE.g, C_EDGE.b, 0xff);
    SDL_RenderFillRect(r, &line);
}

static void drawBackdrop(SDL_Renderer *r) {
    SDL_Rect full;
    full.x = 0;
    full.y = 0;
    full.w = HHUI_SCREEN_W;
    full.h = HHUI_SCREEN_H;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 140);
    SDL_RenderFillRect(r, &full);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

static void cacheClear(void) {
    int i;
    for (i = 0; i < HHUI_CACHE_SLOTS; i++) {
        if (g_cache[i].texture) {
            SDL_DestroyTexture(g_cache[i].texture);
            g_cache[i].texture = (SDL_Texture *) 0;
        }
        g_cache[i].used = 0;
        g_cache[i].text[0] = '\0';
    }
    g_cacheNext = 0;
}

static TTF_Font *fontFor(int kind) {
    if (kind == 2) return g_fontTitle;
    return kind == 1 ? g_fontSmall : g_font;
}

/* Draw one string at pixel coordinates (top-left origin).  fontKind selects
 * body (0), compact (1) or title (2); strings are cached per font and color. */
static void drawText(SDL_Renderer *r, const char *text, int x, int y,
                     SDL_Color color, int fontKind) {
    HHTexCacheEntry *slot = (HHTexCacheEntry *) 0;
    TTF_Font *font = fontFor(fontKind);
    int i;

    if (!font || !text || text[0] == '\0') return;

    for (i = 0; i < HHUI_CACHE_SLOTS; i++) {
        if (g_cache[i].used && g_cache[i].fontKind == fontKind
            && g_cache[i].color.r == color.r
            && g_cache[i].color.g == color.g && g_cache[i].color.b == color.b
            && strcmp(g_cache[i].text, text) == 0) {
            slot = &g_cache[i];
            break;
        }
    }

    if (!slot) {
        SDL_Surface *surface;
        slot = &g_cache[g_cacheNext];
        g_cacheNext = (g_cacheNext + 1) % HHUI_CACHE_SLOTS;
        if (slot->texture) {
            SDL_DestroyTexture(slot->texture);
            slot->texture = (SDL_Texture *) 0;
        }
        surface = TTF_RenderUTF8_Blended(font, text, color);
        if (!surface) return;
        slot->texture = SDL_CreateTextureFromSurface(r, surface);
        slot->width = surface->w;
        slot->height = surface->h;
        SDL_FreeSurface(surface);
        if (!slot->texture) return;
        slot->used = 1;
        slot->fontKind = fontKind;
        strncpy(slot->text, text, HHUI_TEXT_MAX - 1);
        slot->text[HHUI_TEXT_MAX - 1] = '\0';
        slot->color = color;
    }

    {
        SDL_Rect dest;
        dest.x = x;
        dest.y = y;
        dest.w = slot->width;
        dest.h = slot->height;
        SDL_RenderCopy(r, slot->texture, (const SDL_Rect *) 0, &dest);
    }
}

static void drawHintStrip(SDL_Renderer *r, const HHUIState *s) {
    /* Stage 5: cover the full bottom edge.  The core's sidebar command row is
     * drawn at y=472..480; the original panel stopped at y=472 and left it
     * visible underneath our hint text. */
    drawPanel(r, 0, HHUI_HINT_Y, HHUI_SCREEN_W, HHUI_SCREEN_H - HHUI_HINT_Y);
    if (s->hint[0] != '\0') {
        drawText(r, s->hint, 8, HHUI_HINT_Y + 8, C_DIM, 1);
    }
}

/* --- Gameplay overlay (Milestone 3.3) ---------------------------------- */

/* Copy a core message line, removing Brogue's four-byte color escape
 * sequences (which are control characters, not printable glyphs) and
 * trailing padding spaces.  Mirrors the core's own read loop at IO.c:3624. */
static void trimMessage(const char *src, char *dst, size_t dstSize) {
    int m = 0;
    int j = 0;
    color ignored;
    if (!src || dstSize == 0) {
        if (dstSize) dst[0] = '\0';
        return;
    }
    while (src[m] && j < (int) dstSize - 1) {
        while (src[m] == COLOR_ESCAPE) {
            m = decodeMessageColor(src, (short) m, &ignored);
        }
        if (!src[m]) break;
        dst[j++] = src[m++];
    }
    dst[j] = '\0';
    while (j > 0 && dst[j - 1] == ' ') dst[--j] = '\0';
}

static void drawMessageBand(SDL_Renderer *r) {
    int row;
    /* Stage 5: cover the full top edge.  The core renders its own text rows in
     * the top 8 px; a margin here left them visible above our band. */
    drawPanel(r, 0, 0, HHUI_SCREEN_W, 48);
    /* displayedMessage[0] is the BOTTOM (newest) row in the core
     * (IO.c:3634 draws index i at row MESSAGE_LINES-i-1).  Invert so the
     * newest message is the bright, bottom-most line of our band. */
    for (row = 0; row < MESSAGE_LINES; row++) {
        int idx = MESSAGE_LINES - 1 - row;
        char line[HHUI_TEXT_MAX];
        trimMessage(displayedMessage[idx], line, sizeof(line));
        if (line[0] != '\0') {
            SDL_Color color = (row == MESSAGE_LINES - 1) ? C_TEXT : C_DIM;
            drawText(r, line, 8, 2 + row * 11, color, 1);
        }
    }
}

static void drawHpBar(SDL_Renderer *r, int x, int y, int w) {
    int hp = (int) player.currentHP;
    int maxhp = (int) player.info.maxHP;
    int fill = 0;
    SDL_Rect edge;
    if (maxhp > 0) {
        fill = (w * hp) / maxhp;
        if (fill < 0) fill = 0;
        if (fill > w) fill = w;
    }
    fillRect(r, x, y, w, 12, C_HPBG);
    if (fill > 0) fillRect(r, x, y, fill, 12, C_HPFILL);
    edge.x = x;
    edge.y = y;
    edge.w = w;
    edge.h = 12;
    SDL_SetRenderDrawColor(r, C_EDGE.r, C_EDGE.g, C_EDGE.b, 0xff);
    SDL_RenderDrawRect(r, &edge);
}

/* Encode a Unicode codepoint as UTF-8.  dst must hold at least 5 bytes. */
static int utf8Encode(unsigned int cp, char *dst) {
    if (cp < 0x80) {
        dst[0] = (char) cp;
        dst[1] = '\0';
        return 1;
    }
    if (cp < 0x800) {
        dst[0] = (char) (0xC0 | (cp >> 6));
        dst[1] = (char) (0x80 | (cp & 0x3F));
        dst[2] = '\0';
        return 2;
    }
    if (cp < 0x10000) {
        dst[0] = (char) (0xE0 | (cp >> 12));
        dst[1] = (char) (0x80 | ((cp >> 6) & 0x3F));
        dst[2] = (char) (0x80 | (cp & 0x3F));
        dst[3] = '\0';
        return 3;
    }
    dst[0] = (char) (0xF0 | (cp >> 18));
    dst[1] = (char) (0x80 | ((cp >> 12) & 0x3F));
    dst[2] = (char) (0x80 | ((cp >> 6) & 0x3F));
    dst[3] = (char) (0x80 | (cp & 0x3F));
    dst[4] = '\0';
    return 4;
}

static const char *modeName(void) {
    switch (rogue.mode) {
        case GAME_MODE_WIZARD: return "WIZARD";
        case GAME_MODE_EASY:   return "EASY";
        case GAME_MODE_NORMAL:
        default:               return "NORMAL";
    }
}

static int drawEquippedRow(SDL_Renderer *r, int x, int y, const item *it) {
    char raw[COLS * 3];
    char name[HHUI_TEXT_MAX];
    char glyphText[5];
    int k;
    int len;
    if (!it) return y;
    raw[0] = '\0';
    itemName(it, raw, false, false, NULL);
    /* Brogue stores extended glyph codes (>=128) in displayChar; the core's
     * own glyphToUnicode() maps them (e.g. G_ARMOR -> '[').  Keep the glyph in
     * its own texture so multi-byte UTF-8 is never split by truncation. */
    utf8Encode(glyphToUnicode(it->displayChar), glyphText);
    drawText(r, glyphText, x + 8, y, C_ACCENT, 1);
    len = (int) strlen(raw);
    if (len > 11) len = 11;
    for (k = 0; k < len; k++) name[k] = raw[k];
    name[len] = '\0';
    drawText(r, name, x + 20, y, C_TEXT, 1);
    return y + 16;
}

static void drawStatusText(SDL_Renderer *r, int x, int y) {
    char line[HHUI_TEXT_MAX];
    int i;
    int count = 0;
    line[0] = '\0';
    for (i = 0; i < NUMBER_OF_STATUS_EFFECTS && count < 2; i++) {
        if (statusEffectCatalog[i].name[0] && player.status[i] > 0) {
            if (count == 0) {
                strncpy(line, statusEffectCatalog[i].name, sizeof(line) - 1);
                line[sizeof(line) - 1] = '\0';
            } else {
                strncat(line, "/", sizeof(line) - strlen(line) - 1);
                strncat(line, statusEffectCatalog[i].name,
                        sizeof(line) - strlen(line) - 1);
            }
            count++;
        }
    }
    if (count == 0) strcpy(line, "Okay");
    if ((int) strlen(line) > 14) line[14] = '\0';
    drawText(r, line, x + 8, y, C_TEXT, 1);
}

static void drawStatusPanel(SDL_Renderer *r) {
    /* Stage 5: anchor flush to the left edge and to the hint strip.  The old
     * 8px inset left core sidebar glyphs visible and looked like a floating
     * overlay rather than part of the game UI. */
    const int x = 0;
    const int y = 48;
    const int w = 136;
    const int h = HHUI_HINT_Y - y;
    int cy = y + 8;
    char line[HHUI_TEXT_MAX];

    drawPanel(r, x, y, w, h);

    drawText(r, modeName(), x + 8, cy, C_ACCENT, 0);
    cy += 20;
    drawSeparator(r, x + 6, cy, w - 12);
    cy += 6;

    snprintf(line, sizeof(line), "Depth %d", (int) rogue.depthLevel);
    drawText(r, line, x + 8, cy, C_DIM, 0);
    cy += 18;

    drawText(r, "HP", x + 8, cy + 1, C_DIM, 1);
    drawHpBar(r, x + 32, cy, 80);
    cy += 16;

    snprintf(line, sizeof(line), "%d / %d",
             (int) player.currentHP, (int) player.info.maxHP);
    drawText(r, line, x + 8, cy, C_TEXT, 0);
    cy += 18;

    snprintf(line, sizeof(line), "Str %d", (int) rogue.strength);
    drawText(r, line, x + 8, cy, C_DIM, 0);
    cy += 18;

    snprintf(line, sizeof(line), "Def %d  Acc %d",
             (int) player.info.defense, (int) player.info.accuracy);
    drawText(r, line, x + 8, cy, C_DIM, 1);
    cy += 18;

    snprintf(line, sizeof(line), "Gold %lu", rogue.gold);
    drawText(r, line, x + 8, cy, C_TEXT, 0);
    cy += 18;

    snprintf(line, sizeof(line), "Turn %lu", rogue.playerTurnNumber);
    drawText(r, line, x + 8, cy, C_DIM, 0);
    cy += 18;

    drawSeparator(r, x + 6, cy, w - 12);
    cy += 6;

    drawText(r, "Equipped", x + 8, cy, C_DIM, 1);
    cy += 16;
    cy = drawEquippedRow(r, x, cy, rogue.weapon);
    cy = drawEquippedRow(r, x, cy, rogue.armor);
    cy = drawEquippedRow(r, x, cy, rogue.ringLeft);
    cy = drawEquippedRow(r, x, cy, rogue.ringRight);

    cy += 6;
    drawText(r, "Status", x + 8, cy, C_DIM, 1);
    cy += 16;
    drawStatusText(r, x, cy);
}

/* --- Modal/list screens (foundation, reused by later milestones) -------- */

/* Stage 6: compact scrollbar. Track is drawn inside the panel on the right;
 * the thumb position/length come from the pure model so it cannot disagree
 * with the list cursor. */
static void drawScrollbar(SDL_Renderer *r, int trackX, int trackY,
                          int trackW, int trackH,
                          int count, int top, int visible) {
    int pos = 0, len = 0;
    fillRect(r, trackX, trackY, trackW, trackH, C_HPBG);
    if (!hhuiScrollThumb(count, top, visible, trackH, &pos, &len)) return;
    fillRect(r, trackX, trackY + pos, trackW, len, C_ACCENT);
}

static void drawListPanel(SDL_Renderer *r, const HHUIState *s) {
    int big = (g_uiScale >= 2);
    int x = big ? 60 : 150;
    int y = big ? 50 : 120;
    int w = big ? 520 : 340;
    int h = big ? 380 : 240;
    int rowH = big ? 44 : 24;
    int maxRows = 7;
    int fontKind = big ? 2 : 0;   /* title font is exactly 2x the body font */
    int textX = x + (big ? 26 : 16);
    int rowsY = y + (big ? 62 : 44);
    int trackW = big ? 10 : 6;
    int trackX = x + w - (big ? 26 : 14);
    int trackH = maxRows * rowH;
    int i;

    drawPanel(r, x, y, w, h);
    drawText(r, s->title, x + (big ? 20 : 12), y + (big ? 16 : 10), C_ACCENT, fontKind);
    drawSeparator(r, x + 10, y + (big ? 52 : 34), w - 20);

    if (s->list.count == 0) {
        drawText(r, "(empty)", textX, rowsY, C_DIM, fontKind);
        return;
    }

    for (i = 0; i < maxRows && i < s->list.count; i++) {
        int index = s->list.top + i;
        const char *label = hhuiListLabel(s, index);
        char buf[HHUI_TEXT_MAX];
        int rowY = rowsY + i * rowH;
        if (index >= s->list.count) break;
        if (!label) {
            snprintf(buf, sizeof(buf), "Item %d", index + 1);
            label = buf;
        }
        if (index == s->list.cursor) {
            fillRect(r, x + 8, rowY - 2, w - (big ? 46 : 26), rowH - 2, C_ACCENT);
            drawText(r, label, textX, rowY + (big ? 1 : 2), C_DARKTEXT, fontKind);
        } else {
            drawText(r, label, textX, rowY + (big ? 1 : 2), C_TEXT, fontKind);
        }
    }
    drawScrollbar(r, trackX, rowsY, trackW, trackH,
                  s->list.count, hhuiListTop(s), 7);
}

/* --- Text input / on-screen keyboard (Milestone 3.7) -------------------- */

static void drawTextCentered(SDL_Renderer *r, const char *text, int cx, int y,
                             SDL_Color color, int fontKind);

/* Brogue color escapes are COLOR_ESCAPE (25) followed by three color bytes.
 * Strip them so TTF never tries to render the raw control characters. */
static void stripColorEscapes(const char *src, char *dst, size_t dstSize) {
    size_t di = 0, i = 0, len;
    if (!src || !dst || dstSize == 0) return;
    len = strlen(src);
    while (i < len && di + 1 < dstSize) {
        if ((unsigned char) src[i] == COLOR_ESCAPE) {
            /* Four-byte sequence; never read past the NUL on a truncated one. */
            i = (i + 3 < len) ? i + 4 : len;
            continue;
        }
        dst[di++] = src[i++];
    }
    dst[di] = '\0';
}

/* The hook buffer is space-padded and only NUL-terminated on exit, so copy at
 * most [0, cursor) — strlen() would read padding and possibly past the field. */
static void copyInputField(char *dst, size_t dstSize) {
    short cur = 0, max = 0;
    int n, i;
    if (hhTextInputCursor) cur = *hhTextInputCursor;
    max = hhTextInputMaxLength;
    if (cur < 0) cur = 0;
    if (max < 0) max = 0;
    if (cur > max) cur = max;
    n = (cur < (int) dstSize - 1) ? cur : (int) dstSize - 1;
    for (i = 0; i < n; i++) {
        char c = hhTextInputBuffer ? hhTextInputBuffer[i] : ' ';
        dst[i] = (c == '\0') ? ' ' : c;
    }
    dst[n] = '\0';
}

static void drawOskKey(SDL_Renderer *r, int x, int y, int w, int h,
                       const char *label, int focused) {
    SDL_Rect rect;
    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;
    if (focused) {
        SDL_SetRenderDrawColor(r, C_ACCENT.r, C_ACCENT.g, C_ACCENT.b,
                               C_ACCENT.a);
        SDL_RenderFillRect(r, &rect);
    } else {
        drawPanel(r, x, y, w, h);
    }
    drawTextCentered(r, label, x + w / 2, y + (h - 15) / 2 - 1,
                     focused ? C_DARKTEXT : C_TEXT, 0);
}

static void drawTextInput(SDL_Renderer *r, const HHUIState *s) {
    const int kx = 30, ky = 124, kw = 54, kh = 40, kgap = 4;
    const int ay = 318, ah = 42, agap = 4;
    const int aw = (10 * (kw + kgap) - agap) / 5;
    char prompt[HHUI_TEXT_MAX], field[HHUI_TEXT_MAX], lbl[2];
    int focus = hhOskFocus();
    int frow = focus / HH_OSK_COLS, fcol = focus % HH_OSK_COLS;
    int row, col;

    (void) s;

    /* Prompt + editable field. */
    drawPanel(r, 16, 12, 608, 86);
    stripColorEscapes(hhTextInputPrompt ? hhTextInputPrompt : "",
                      prompt, sizeof(prompt));
    if (!prompt[0]) strcpy(prompt, "Enter text");
    if ((int) strlen(prompt) > 70) prompt[70] = '\0';
    drawText(r, prompt, 28, 30, C_ACCENT, 0);

    drawPanel(r, 28, 48, 584, 38);
    copyInputField(field, sizeof(field));
    drawText(r, field, 40, 58, C_TEXT, 0);
    {
        short cur = hhTextInputCursor ? *hhTextInputCursor : 0;
        int max = (int) hhTextInputMaxLength;
        int cx = 40;
        int i;
        SDL_Rect caret;
        if (cur < 0) cur = 0;
        if (max >= 0 && cur > max) cur = max;
        for (i = 0; i < cur && i < (int) strlen(field); i++) {
            cx += 9; /* monospace advance at 15px */
        }
        caret.x = cx;
        caret.y = 56;
        caret.w = 2;
        caret.h = 24;
        SDL_SetRenderDrawColor(r, C_ACCENT.r, C_ACCENT.g, C_ACCENT.b,
                               C_ACCENT.a);
        SDL_RenderFillRect(r, &caret);
    }

    /* On-screen keyboard panel. */
    drawPanel(r, 16, 108, 608, 326);
    if (hhOskNumeric()) {
        drawText(r, "NUMERIC MODE", 30, 116, C_DIM, 1);
    }

    for (row = 0; row < HH_OSK_ROWS - 1; row++) {
        int y = ky + row * (kh + 6);
        if (!hhOskEnabled(row * HH_OSK_COLS)) continue; /* numeric mode */
        for (col = 0; col < HH_OSK_COLS; col++) {
            int idx = row * HH_OSK_COLS + col;
            char c;
            if (!hhOskEnabled(idx)) continue;           /* stagger gap */
            c = hhOskBaseChar(idx);
            if (!c) continue;
            lbl[0] = c;
            lbl[1] = '\0';
            drawOskKey(r, kx + col * (kw + kgap), y, kw, kh, lbl,
                       (frow == row && fcol == col));
        }
    }

    /* Action row: five wide keys, each covering two grid columns. */
    {
        int group;
        for (group = 0; group < 5; group++) {
            HHOSKKeyKind kind;
            char label[HHUI_TEXT_MAX];
            int gx = kx + group * (aw + agap);
            if (!hhOskEnabled((HH_OSK_ROWS - 1) * HH_OSK_COLS + group * 2)) {
                continue; /* Space/Shift are hidden in numeric mode */
            }
            switch (group) {
                case 0: kind = HH_OSK_SPACE;     break;
                case 1: kind = HH_OSK_BACKSPACE; break;
                case 2: kind = HH_OSK_SHIFT;     break;
                case 3: kind = HH_OSK_CONFIRM;   break;
                default: kind = HH_OSK_CANCEL;   break;
            }
            snprintf(label, sizeof(label), "%s", hhOskActionLabel(kind));
            if (group == 2 && hhOskShift()) {
                strncat(label, "*", sizeof(label) - strlen(label) - 1);
            }
            drawOskKey(r, gx, ay, aw, ah, label,
                       (frow == HH_OSK_ROWS - 1 && fcol / 2 == group));
        }
    }
    drawText(r, "D-Pad select   A type/confirm   B cancel   X backspace   Y shift",
             30, 412, C_DIM, 1);
}

static void syncTextInputModel(HHUIState *s) {
    hhuiSetTitle(s, "TEXT INPUT");
    if (hhOskNumeric()) {
        strcpy(s->hint, "D-Pad  Select digit/action   A  Type/Confirm   B  Cancel   X  Backspace");
    } else {
        strcpy(s->hint, "D-Pad  Select key   A  Type/Confirm   B  Cancel   X  Backspace   Y  Shift");
    }
}

/* --- Main menu (Milestone 3.4) ------------------------------------------ */

static void drawTextCentered(SDL_Renderer *r, const char *text, int cx, int y,
                             SDL_Color color, int fontKind) {
    TTF_Font *font = fontFor(fontKind);
    int w = 0;
    int h = 0;
    if (!font || !text) return;
    if (TTF_SizeUTF8(font, text, &w, &h) == 0) {
        drawText(r, text, cx - w / 2, y, color, fontKind);
    }
}

/* Copy the active core menu's real button labels and focus index into the UI
 * state.  buttonState is exported by the guarded milestone-3.4 hook. */
static void syncMenuModel(HHUIState *s, const buttonState *bs) {
    int i;
    int count;
    char *p;
    if (!bs) return;
    count = bs->buttonCount;
    if (count < 0) count = 0;
    if (count > HHUI_MAX_ITEMS) count = HHUI_MAX_ITEMS;
    hhuiSetList(s, count, 7);
    for (i = 0; i < count; i++) {
        trimMessage(bs->buttons[i].text, g_menuLabels[i], HHUI_TEXT_MAX);
        p = g_menuLabels[i];
        while (*p == ' ') p++;                 /* drop button padding */
        if (p != g_menuLabels[i]) {
            memmove(g_menuLabels[i], p, strlen(p) + 1);
        }
        hhuiSetListLabel(s, i, g_menuLabels[i]);
    }
    {
        int focus = bs->buttonFocused;
        if (focus < 0) focus = 0;
        if (focus >= count) focus = count - 1;
        (void) hhuiListMove(s, focus - hhuiListCursor(s));
    }
}

static void drawMainMenu(SDL_Renderer *r, const HHUIState *s) {
    const int px = 170;
    const int py = 150;
    const int pw = 300;
    const int ph = 252;
    SDL_Color band1 = { 0x2a, 0x16, 0x08, 0xff };
    SDL_Color band2 = { 0x1a, 0x0e, 0x06, 0xff };
    SDL_Color buttonFill = { 0x23, 0x22, 0x28, 0xff };
    int i;
    int shown;

    /* Subtle flame-like bands echoing 02-main-menu.svg (the core still draws
     * its own animated title flames underneath). */
    fillRect(r, 0, 120, HHUI_SCREEN_W, 120, band1);
    fillRect(r, 0, 240, HHUI_SCREEN_W, 120, band2);

    drawTextCentered(r, "BROGUE", HHUI_SCREEN_W / 2, 56, C_ACCENT, 2);
    drawTextCentered(r, "a roguelike for the hands", HHUI_SCREEN_W / 2, 108,
                     C_DIM, 1);

    drawPanel(r, px, py, pw, ph);
    shown = s->list.count;
    if (shown > 4) shown = 4;
    for (i = 0; i < shown; i++) {
        const char *label = hhuiListLabel(s, i);
        int rowY = py + 16 + i * 48;
        if (!label) continue;
        if (i == s->list.cursor) {
            fillRect(r, px + 16, rowY, pw - 32, 40, C_ACCENT);
            drawText(r, label, px + 30, rowY + 12, C_DARKTEXT, 0);
        } else {
            fillRect(r, px + 16, rowY, pw - 32, 40, buttonFill);
            SDL_SetRenderDrawColor(r, C_EDGE.r, C_EDGE.g, C_EDGE.b, 0xff);
            {
                SDL_Rect edge;
                edge.x = px + 16;
                edge.y = rowY;
                edge.w = pw - 32;
                edge.h = 40;
                SDL_RenderDrawRect(r, &edge);
            }
            drawText(r, label, px + 30, rowY + 12, C_TEXT, 0);
        }
    }

    drawText(r, "Play: seeded, load, variant, mode", px + 16, py + 232, C_DIM, 1);
    drawText(r, "View: recording, scores, stats", px + 16, py + 248, C_DIM, 1);
}

/* --- Inventory / item actions (Milestone 3.5) ---------------------------- */

/* Map a core action hotkey to its display label.  Invisible paging keys (and
 * anything unrecognized) return NULL so they are never offered as actions. */
static const char *actionLabelFor(signed long key) {
    if (key == APPLY_KEY)   return "Apply";
    if (key == EQUIP_KEY)   return "Equip";
    if (key == UNEQUIP_KEY) return "Remove";
    if (key == DROP_KEY)    return "Drop";
    if (key == THROW_KEY)   return "Throw";
    if (key == CALL_KEY)    return "Call item kind";
    /* relabel() reads a single character with nextKeyPress(true), not through
     * getInputTextString(), so the M3.7 OSK never activates for it.  Keep the
     * core button/order intact but mark it as keyboard-only. */
    if (key == RELABEL_KEY) return "Relabel (keyboard)";
    return (const char *) 0;
}

/* Reproduce, for presentation only, the exact valid-action order built by the
 * core in printCarriedItemDetails() (IO.c:5063-5097).  The real action is still
 * dispatched by the core button the controller clicks. */
static int validActionKeys(const item *it, signed long *keys, int max) {
    int n = 0;
    if (!it) return 0;
    if ((it->category & (FOOD | SCROLL | POTION | WAND | STAFF | CHARM))
        && n < max) {
        keys[n++] = APPLY_KEY;
    }
    if ((it->category & (ARMOR | WEAPON | RING)) && n < max) {
        keys[n++] = (it->flags & ITEM_EQUIPPED) ? UNEQUIP_KEY : EQUIP_KEY;
    }
    if (n < max) keys[n++] = DROP_KEY;
    if (n < max) keys[n++] = THROW_KEY;
    if (itemCanBeCalled(it) && n < max) keys[n++] = CALL_KEY;
    if (KEYBOARD_LABELS && n < max) keys[n++] = RELABEL_KEY;
    return n;
}

static void makeItemLabel(const item *it, char *dst, size_t dstSize) {
    char raw[COLS * 3], glyph[5];
    const char *marker;
    if (!it || dstSize == 0) {
        if (dstSize) dst[0] = '\0';
        return;
    }
    raw[0] = '\0';
    itemName(it, raw, false, false, NULL);
    trimMessage(raw, raw, sizeof(raw));        /* strip any color escapes */
    utf8Encode(glyphToUnicode(it->displayChar), glyph);
    marker = (it->flags & ITEM_EQUIPPED) ? " (equipped)" : "";
    snprintf(dst, dstSize, "%c %s %s%s",
             it->inventoryLetter ? it->inventoryLetter : ' ', glyph, raw, marker);
}

/* Rebuild the inventory list from core globals in the core's own display order
 * (equipped weapon/armor/rings first, then unequipped pack items). */
static void syncInventoryModel(HHUIState *s) {
    int n = 0, i, focus = -1;
    item *it;
    if (!packItems) {
        hhuiSetList(s, 0, 8);
        return;
    }
    if (rogue.weapon)    g_invItems[n++] = rogue.weapon;
    if (rogue.armor)     g_invItems[n++] = rogue.armor;
    if (rogue.ringLeft)  g_invItems[n++] = rogue.ringLeft;
    if (rogue.ringRight) g_invItems[n++] = rogue.ringRight;
    for (it = packItems->nextItem; it && n < HHUI_MAX_ITEMS; it = it->nextItem) {
        if (!(it->flags & ITEM_EQUIPPED)) {
            g_invItems[n++] = it;
        }
    }
    for (i = 0; i < n; i++) {
        makeItemLabel(g_invItems[i], g_invLabels[i], sizeof(g_invLabels[i]));
        hhuiSetListLabel(s, i, g_invLabels[i]);
    }
    hhuiSetList(s, n, 8);

    /* While the detail/action box is open, the exported item is authoritative;
     * while browsing the list, the core's focused button is. */
    if (hhInventoryItem) {
        for (i = 0; i < n; i++) {
            if (g_invItems[i] == hhInventoryItem) {
                focus = i;
                break;
            }
        }
    }
    if (focus < 0 && hhActiveMenuState && hhActiveMenuState->buttonFocused >= 0
        && hhActiveMenuState->buttonFocused < n) {
        focus = hhActiveMenuState->buttonFocused;
    }
    if (focus < 0) focus = 0;
    (void) hhuiListMove(s, focus - hhuiListCursor(s));
}

static void syncActionModel(HHUIState *s) {
    signed long keys[HHUI_MAX_ACTION_KEYS];
    int n = 0, i, focus = 0, visible = 0;
    /* The core omits action buttons when printCarriedItemDetails() is called
     * with includeButtons=false (playback/exotic paths).  Only offer actions
     * that really have a drawn+enabled core button to click. */
    if (hhActiveMenuState) {
        for (i = 0; i < hhActiveMenuState->buttonCount; i++) {
            if ((hhActiveMenuState->buttons[i].flags & B_DRAW)
                && (hhActiveMenuState->buttons[i].flags & B_ENABLED)) {
                visible++;
            }
        }
    }
    if (!hhInventoryItem || visible == 0) {
        hhuiSetList(s, 0, 6);
        return;
    }
    n = validActionKeys(hhInventoryItem, keys, HHUI_MAX_ACTION_KEYS);
    for (i = 0; i < n; i++) {
        const char *label = actionLabelFor(keys[i]);
        snprintf(g_actionLabels[i], HHUI_TEXT_MAX, "%s", label ? label : "Action");
        hhuiSetListLabel(s, i, g_actionLabels[i]);
    }
    hhuiSetList(s, n, 6);
    /* The core's action buttons are in this same order, with two invisible
     * paging buttons appended; clamp any focus that lands on those. */
    if (hhActiveMenuState && hhActiveMenuState->buttonFocused > 0) {
        focus = hhActiveMenuState->buttonFocused;
        if (focus >= n) focus = n - 1;
    }
    if (focus < 0) focus = 0;
    (void) hhuiListMove(s, focus - hhuiListCursor(s));
}

/* Strip color escapes, fold newlines to spaces and collapse trailing spaces. */
static int cleanText(const char *src, char *dst, size_t n) {
    int i = 0, j = 0;
    color ignored;
    if (!src || n == 0) {
        if (n) dst[0] = '\0';
        return 0;
    }
    while (src[i] && j < (int) n - 1) {
        if (src[i] == COLOR_ESCAPE) {
            i = decodeMessageColor(src, (short) i, &ignored);
            continue;
        }
        if (src[i] == '\n' || src[i] == '\r') {
            dst[j++] = ' ';
            i++;
            continue;
        }
        dst[j++] = src[i++];
    }
    dst[j] = '\0';
    while (j > 0 && dst[j - 1] == ' ') dst[--j] = '\0';
    return j;
}

static void drawWrapped(SDL_Renderer *r, const char *text, int x, int y,
                        int maxChars, int lineH, int maxLines, SDL_Color c) {
    char buf[COLS * 100];
    int n, start = 0, line = 0;
    cleanText(text, buf, sizeof(buf));
    n = (int) strlen(buf);
    while (start < n && line < maxLines) {
        int end = start + maxChars;
        char linebuf[160];
        int len, k, lastSpace = -1;
        if (end >= n) {
            end = n;
        } else {
            for (k = start; k < end; k++) {
                if (buf[k] == ' ') lastSpace = k;
            }
            if (lastSpace > start) end = lastSpace;
        }
        len = end - start;
        if (len > (int) sizeof(linebuf) - 1) len = (int) sizeof(linebuf) - 1;
        memcpy(linebuf, buf + start, len);
        linebuf[len] = '\0';
        drawText(r, linebuf, x, y + line * lineH, c, 1);
        line++;
        start = end;
        while (buf[start] == ' ') start++;
    }
}

static void drawInventory(SDL_Renderer *r, const HHUIState *s) {
    const int x = HHUI_MARGIN, y = HHUI_MARGIN, w = 296, h = 428;
    const int rx = 312, rw = 320;
    int i, rowY;
    item *focusItem = (item *) 0;
    char buf[64];
    signed long keys[HHUI_MAX_ACTION_KEYS];
    int nkeys;

    drawPanel(r, x, y, w, h);
    drawText(r, "INVENTORY", x + 10, y + 8, C_ACCENT, 0);
    drawSeparator(r, x + 6, y + 30, w - 12);

    if (s->list.count == 0) {
        drawText(r, "(pack empty)", x + 12, y + 48, C_DIM, 0);
    } else {
        for (i = s->list.top; i < s->list.count && i < s->list.top + 8; i++) {
            const char *label = hhuiListLabel(s, i);
            rowY = y + 40 + (i - s->list.top) * 24;
            if (i == s->list.cursor) {
                fillRect(r, x + 6, rowY - 2, w - 12, 22, C_ACCENT);
                drawText(r, label ? label : "", x + 12, rowY + 1, C_DARKTEXT, 1);
            } else {
                drawText(r, label ? label : "", x + 12, rowY + 1, C_TEXT, 1);
            }
        }
        snprintf(buf, sizeof(buf), "Scroll: %d of %d",
                 s->list.top / 8 + 1, (s->list.count + 7) / 8);
        drawText(r, buf, x + 12, y + h - 26, C_DIM, 1);
    }

    if (s->list.cursor >= 0 && s->list.cursor < s->list.count) {
        focusItem = g_invItems[s->list.cursor];
    }

    drawPanel(r, rx, y, rw, h);
    drawText(r, "SELECTED ITEM", rx + 10, y + 8, C_ACCENT, 0);
    drawSeparator(r, rx + 6, y + 30, rw - 12);
    if (focusItem) {
        char name[COLS * 3], shown[HHUI_TEXT_MAX], glyph[5], detail[COLS * 100];
        itemName(focusItem, name, true, true, NULL);
        trimMessage(name, name, sizeof(name));
        strncpy(shown, name, sizeof(shown) - 1);
        shown[sizeof(shown) - 1] = '\0';
        utf8Encode(glyphToUnicode(focusItem->displayChar), glyph);
        drawText(r, glyph, rx + 10, y + 44, C_ACCENT, 0);
        drawText(r, shown, rx + 30, y + 44, C_TEXT, 0);
        itemDetails(detail, focusItem);
        drawWrapped(r, detail, rx + 10, y + 72, 38, 16, 6, C_DIM);
        nkeys = validActionKeys(focusItem, keys, HHUI_MAX_ACTION_KEYS);
        drawText(r, "ACTIONS", rx + 10, y + 208, C_ACCENT, 0);
        for (i = 0; i < nkeys && i < 5; i++) {
            drawText(r, actionLabelFor(keys[i]), rx + 16, y + 232 + i * 20,
                     C_DIM, 1);
        }
    } else {
        drawText(r, "(no item selected)", rx + 10, y + 48, C_DIM, 0);
    }
}

static void drawItemActions(SDL_Renderer *r, const HHUIState *s) {
    const int px = 170, py = 80, pw = 300, ph = 332;
    char name[COLS * 3], shown[HHUI_TEXT_MAX], glyph[5];
    int i, rowY;

    drawPanel(r, px, py, pw, ph);
    if (hhInventoryItem) {
        itemName(hhInventoryItem, name, true, true, NULL);
        trimMessage(name, name, sizeof(name));
        strncpy(shown, name, sizeof(shown) - 1);
        shown[sizeof(shown) - 1] = '\0';
        utf8Encode(glyphToUnicode(hhInventoryItem->displayChar), glyph);
    } else {
        shown[0] = '\0';
        glyph[0] = '\0';
    }
    drawText(r, glyph, px + 16, py + 16, C_ACCENT, 0);
    drawText(r, shown, px + 40, py + 16, C_ACCENT, 0);
    drawSeparator(r, px + 12, py + 44, pw - 24);
    drawText(r, "Choose an action:", px + 16, py + 58, C_DIM, 1);

    if (s->list.count == 0) {
        drawText(r, "(no valid actions)", px + 16, py + 96, C_DIM, 0);
    } else {
        for (i = s->list.top; i < s->list.count && i < s->list.top + 6; i++) {
            const char *label = hhuiListLabel(s, i);
            rowY = py + 84 + (i - s->list.top) * 32;
            if (i == s->list.cursor) {
                fillRect(r, px + 14, rowY - 2, pw - 28, 30, C_ACCENT);
                drawText(r, label ? label : "", px + 24, rowY + 6, C_DARKTEXT, 0);
            } else {
                fillRect(r, px + 14, rowY - 2, pw - 28, 30, C_PANEL);
                drawText(r, label ? label : "", px + 24, rowY + 6, C_TEXT, 0);
            }
        }
    }
    drawText(r, "Only valid actions are shown.", px + 16, py + ph - 30, C_DIM, 1);
}

/* --- Targeting (Milestone 3.6) ------------------------------------------ */

/* The action verb for the header.  AUTOTARGET_MODE_NONE (wizard helper) and
 * AUTOTARGET_MODE_EXPLORE (not used by chooseTarget) fall back to "targeting". */
static const char *targetVerb(enum autoTargetMode mode) {
    if (mode == AUTOTARGET_MODE_USE_STAFF_OR_WAND) return "using";
    if (mode == AUTOTARGET_MODE_THROW)              return "throwing";
    return "targeting";
}

static const char *monsterStateLabel(enum creatureStates state) {
    switch (state) {
        case MONSTER_SLEEPING:       return "asleep";
        case MONSTER_TRACKING_SCENT: return "tracking";
        case MONSTER_WANDERING:      return "unaware";
        case MONSTER_FLEEING:        return "fleeing";
        case MONSTER_ALLY:           return "ally";
        default:                     return "unknown";
    }
}

static int cursorPosValid(void) {
    return rogue.cursorLoc.x >= 0 && rogue.cursorLoc.x < DCOLS
        && rogue.cursorLoc.y >= 0 && rogue.cursorLoc.y < DROWS;
}

/* The core already draws the dungeon, the cursor and the trajectory.  This
 * overlay adds a header, a structured target panel and the abstract-action hint
 * strip; it never dims the map.  The panel sits in the LEFT sidebar strip (the
 * same x-range as drawStatusPanel()), because the core map is offset right by
 * STAT_BAR_WIDTH+1 (`mapToWindowX()` at Rogue.h:1265) and starts near x=134 px;
 * a right-side panel as drawn in mockup 05 would hide the core cursor/trajectory.
 * That placement deviation from the mockup is recorded in section 8E.  The
 * header/hint strip replace the core's top/bottom text rows exactly as the
 * gameplay HUD already does.  All text builders here are RNG-neutral (§9.5). */
static void drawTargeting(SDL_Renderer *r, const HHUIState *s) {
    const int hx = HHUI_MARGIN, hy = HHUI_MARGIN, hw = 624, hh = 30;
    const int px = HHUI_MARGIN, py = 48, pw = 120, ph = 392;
    char header[HHUI_TEXT_MAX], buf[HHUI_TEXT_MAX], label[COLS * 3];
    char location[DCOLS + 1], glyph[5];
    const item *it = hhTargetItem;
    creature *mon = (creature *) 0;
    int cy = py + 8;

    (void) s;

    drawPanel(r, hx, hy, hw, hh);
    if (it) {
        label[0] = '\0';
        itemName(it, label, false, false, NULL);
        trimMessage(label, label, sizeof(label));
        snprintf(header, sizeof(header), "TARGETING - %s a %.40s",
                 targetVerb(hhTargetingMode), label);
    } else {
        snprintf(header, sizeof(header), "TARGETING - examine cursor");
    }
    drawText(r, header, hx + 8, hy + 7, C_ACCENT, 0);

    drawPanel(r, px, py, pw, ph);
    drawText(r, "TARGET", px + 8, cy, C_ACCENT, 0);
    cy += 20;
    drawSeparator(r, px + 6, cy, pw - 12);
    cy += 8;

    if (cursorPosValid()) {
        location[0] = '\0';
        location[DCOLS] = '\0';
        describeLocation(location, rogue.cursorLoc.x, rogue.cursorLoc.y);
        if (location[0]) {
            trimMessage(location, location, sizeof(location));
            snprintf(buf, sizeof(buf), "%.16s", location);
            drawText(r, buf, px + 8, cy, C_TEXT, 1);
            cy += 18;
        }
        mon = monsterAtLoc(rogue.cursorLoc);
    }

    if (mon && mon != &player) {
        label[0] = '\0';
        monsterName(label, mon, false);
        glyph[0] = '\0';
        utf8Encode(glyphToUnicode(mon->info.displayChar), glyph);
        drawText(r, glyph, px + 8, cy, C_ACCENT, 1);
        snprintf(buf, sizeof(buf), "%.12s", label);
        drawText(r, buf, px + 26, cy, C_TEXT, 1);
        cy += 18;
        snprintf(buf, sizeof(buf), "HP: %d / %d", (int) mon->currentHP,
                 (int) mon->info.maxHP);
        drawText(r, buf, px + 8, cy, C_DIM, 1);
        cy += 18;
        snprintf(buf, sizeof(buf), "Status: %s",
                 monsterStateLabel(mon->creatureState));
        drawText(r, buf, px + 8, cy, C_DIM, 1);
        cy += 18;
    }

    if (cursorPosValid()) {
        short dist = distanceBetween(player.loc, rogue.cursorLoc);
        snprintf(buf, sizeof(buf), "Distance: %d", (int) dist);
        drawText(r, buf, px + 8, cy, C_DIM, 1);
        cy += 18;
    }

    cy += 4;
    drawSeparator(r, px + 6, cy, pw - 12);
    cy += 10;
    drawText(r, "THROWN ITEM", px + 8, cy, C_DIM, 1);
    cy += 18;
    if (it) {
        label[0] = '\0';
        itemName(it, label, false, false, NULL);
        trimMessage(label, label, sizeof(label));
        glyph[0] = '\0';
        utf8Encode(glyphToUnicode(it->displayChar), glyph);
        drawText(r, glyph, px + 8, cy, C_ACCENT, 1);
        snprintf(buf, sizeof(buf), "%.12s", label);
        drawText(r, buf, px + 26, cy, C_TEXT, 1);
    } else {
        drawText(r, "(none)", px + 8, cy, C_DIM, 1);
    }
    drawText(r, "Core highlights path.", px + 8, py + ph - 24, C_DIM, 1);
}

static void syncTargetingModel(HHUIState *s) {
    hhuiSetTitle(s, "TARGETING");
    strcpy(s->hint,
           "D-Pad  Move cursor   Select  Next target   A  Confirm   B  Cancel");
}

/* --- Lifecycle and render entrypoint ----------------------------------- */

int hhuiInit(SDL_Renderer *renderer, const char *fontPath) {
    (void) renderer;
    if (!g_ttfReady) {
        if (TTF_Init() == 0) g_ttfReady = 1;
    }
    if (g_ttfReady && !g_font && fontPath) {
        g_font = TTF_OpenFont(fontPath, HHUI_FONT_SIZE);
        g_fontSmall = TTF_OpenFont(fontPath, HHUI_SMALL_SIZE);
        g_fontTitle = TTF_OpenFont(fontPath, HHUI_TITLE_SIZE);
        if (!g_font || !g_fontSmall || !g_fontTitle) {
            fprintf(stderr, "handheld UI: could not load font '%s': %s\n",
                    fontPath, TTF_GetError());
        }
    }
    hhuiStateInit(&g_state);
    strcpy(g_state.hint, "Navigate  Confirm  Cancel  Context  Inspect  Scroll");
    return 0;
}

void hhuiShutdown(void) {
    cacheClear();
    if (g_font) {
        TTF_CloseFont(g_font);
        g_font = (TTF_Font *) 0;
    }
    if (g_fontSmall) {
        TTF_CloseFont(g_fontSmall);
        g_fontSmall = (TTF_Font *) 0;
    }
    if (g_fontTitle) {
        TTF_CloseFont(g_fontTitle);
        g_fontTitle = (TTF_Font *) 0;
    }
    if (g_ttfReady) {
        TTF_Quit();
        g_ttfReady = 0;
    }
    g_initialized = 0;
}

HHUIState *hhuiGetState(void) {
    return &g_state;
}

void hhuiRender(SDL_Renderer *renderer) {
    HHUIScreen screen;
    int inProgress;
    int gameplay;
    int menuActive;
    int inventoryOpen;
    int detailOpen;
    int targeting;
    int textInput;
    int inGameMenu;
    int fresh;

    g_uiScale = (hhZoomLevel() > 0) ? 2 : 1;

    if (!g_initialized) {
        char fontPath[512];
        snprintf(fontPath, sizeof(fontPath), "%s/assets/DejaVuSansMono.ttf",
                 dataDirectory);
        (void) hhuiInit(renderer, fontPath);
        g_initialized = 1;
    }

    inProgress = (int) rogue.gameInProgress;
    gameplay = inProgress && !rogue.gameHasEnded;
    menuActive = (hhActiveMenuState != (buttonState *) 0);
    /* The M3.5 hook is authoritative: hhInventoryOpen stays set across the
     * gaps between the core's successive inventory button loops, and the item
     * pointer is non-NULL only while the detail/action box is open. */
    inventoryOpen = gameplay && (int) hhInventoryOpen;
    detailOpen = inventoryOpen && (hhInventoryItem != (item *) 0);
    /* M3.6: chooseTarget() clears cursorMode, so the guarded hook is
     * authoritative for action targeting; cursorMode covers examine mode. */
    targeting = gameplay && ((int) hhTargetingActive
                             || (int) (rogue.cursorMode != 0));
    /* M3.7: the guarded hook is authoritative, and outranks every other
     * screen (seed entry runs under the title menu's buttonState).  Sync the
     * OSK here too: rendering happens before input polling each frame, so the
     * first frame of a session must not use the previous session's layout. */
    textInput = (int) hhTextInputActive;
    hhOskSyncSession(textInput, hhTextInputType == TEXT_INPUT_NUMBERS);
    /* M4.3: the Start menu is UI-only state, below text input but above all
     * core-driven screens. */
    inGameMenu = hhInGameMenuOpenState();

    /* A live game is detected on its rising edge (or a turn-counter reset),
     * never on a plain level check: several core paths end a session with only
     * gameHasEnded set, and the menu loop then clears it (MainMenu.c:1124). */
    fresh = (inProgress && !g_lastInProgress)
         || (gameplay && rogue.playerTurnNumber < g_lastTurn);
    if (fresh) g_hudEverActive = 1;
    if (!gameplay) g_hudEverActive = 0;
    g_lastInProgress = inProgress;
    g_lastTurn = rogue.playerTurnNumber;

    screen = hhuiCurrentScreen(&g_state);

    if (textInput) {
        /* M3.7: clear the stack and show only the OSK.  On exit the normal
         * chain below rebuilds the menu/inventory/gameplay screen as needed. */
        while (hhuiCurrentScreen(&g_state) != HHUI_SCREEN_NONE) {
            (void) hhuiPopScreen(&g_state);
        }
        (void) hhuiPushScreen(&g_state, HHUI_SCREEN_TEXT_INPUT, 1);
        syncTextInputModel(&g_state);
    } else if (inGameMenu) {
        /* M4.3: Start menu.  Clear to NONE so closing it always falls back to
         * the underlying gameplay screen on the next frame. */
        while (hhuiCurrentScreen(&g_state) != HHUI_SCREEN_NONE) {
            (void) hhuiPopScreen(&g_state);
        }
        (void) hhuiPushScreen(&g_state, HHUI_SCREEN_IN_GAME_MENU, 1);
        /* Stage 6: more entries than the 7-row panel; enable real scrolling. */
        hhuiSetList(&g_state, HH_IGM_COUNT, 7);
        {
            int i;
            for (i = 0; i < HH_IGM_COUNT; i++) {
                hhuiSetListLabel(&g_state, i, hhInGameMenuLabel(i));
            }
        }
        (void) hhuiListMove(&g_state, hhInGameMenuFocus() - hhuiListCursor(&g_state));
        hhuiSetTitle(&g_state, "MENU");
        strcpy(g_state.hint,
               "D-Pad  Scroll/Select   A  Confirm   B/Start  Back");
    } else if (inventoryOpen) {
        /* Inventory takes priority over the generic menu hook: while it is open
         * the exported buttonState describes either the item list or the action
         * box, both of which have dedicated overlays. */
        HHUIScreen want = detailOpen ? HHUI_SCREEN_ITEM_ACTIONS
                                     : HHUI_SCREEN_INVENTORY;
        if (screen != want) {
            if (want == HHUI_SCREEN_ITEM_ACTIONS
                && screen == HHUI_SCREEN_INVENTORY) {
                (void) hhuiPushScreen(&g_state, want, 1);
            } else {
                while (hhuiCurrentScreen(&g_state) == HHUI_SCREEN_ITEM_ACTIONS
                       || hhuiCurrentScreen(&g_state) == HHUI_SCREEN_INVENTORY) {
                    (void) hhuiPopScreen(&g_state);
                }
                screen = hhuiCurrentScreen(&g_state);
                if (screen != want) {
                    if (screen != HHUI_SCREEN_NONE
                        && screen != HHUI_SCREEN_GAMEPLAY) {
                        (void) hhuiPopScreen(&g_state);
                    }
                    (void) hhuiPushScreen(&g_state, want, 1);
                }
            }
        }
        if (want == HHUI_SCREEN_INVENTORY) {
            syncInventoryModel(&g_state);
            hhuiSetTitle(&g_state, "INVENTORY");
            strcpy(g_state.hint,
                   "D-Pad  Select item   A  Item actions   B  Close");
        } else {
            syncActionModel(&g_state);
            hhuiSetTitle(&g_state, "Item actions");
            strcpy(g_state.hint,
                   "D-Pad  Choose action   A  Confirm   B  Cancel");
        }
    } else {
        /* Leaving text input, the Start menu or inventory: unwind before
         * normal menu handling. */
        while (hhuiCurrentScreen(&g_state) == HHUI_SCREEN_TEXT_INPUT
               || hhuiCurrentScreen(&g_state) == HHUI_SCREEN_IN_GAME_MENU
               || hhuiCurrentScreen(&g_state) == HHUI_SCREEN_ITEM_ACTIONS
               || hhuiCurrentScreen(&g_state) == HHUI_SCREEN_INVENTORY) {
            (void) hhuiPopScreen(&g_state);
        }
        screen = hhuiCurrentScreen(&g_state);
        if (menuActive) {
            /* The exported buttonState is authoritative: non-NULL means a menu
             * or dialog is open.  During play it is a confirmation; otherwise
             * it is the title menu / a pre-game dialog (main menu panel). */
            HHUIScreen want = gameplay ? HHUI_SCREEN_CONFIRM : HHUI_SCREEN_MENU;
            if (screen != want) {
                if (screen == HHUI_SCREEN_GAMEPLAY) {
                    (void) hhuiPopScreen(&g_state);
                    screen = hhuiCurrentScreen(&g_state);
                }
                if (screen != want) {
                    if (screen != HHUI_SCREEN_NONE) (void) hhuiPopScreen(&g_state);
                    (void) hhuiPushScreen(&g_state, want, 1);
                }
            }
            syncMenuModel(&g_state, hhActiveMenuState);
            if (want == HHUI_SCREEN_MENU) {
                hhuiSetTitle(&g_state, "BROGUE");
            } else {
                hhuiSetTitle(&g_state, "Confirm");
            }
            strcpy(g_state.hint, "D-Pad  Select   A  Confirm   B  Cancel");
        } else if (targeting) {
            /* M3.6: the core keeps rendering the map, cursor and trajectory;
             * only the header, target panel and hint strip are added. */
            if (screen != HHUI_SCREEN_TARGETING) {
                (void) hhuiPushScreen(&g_state, HHUI_SCREEN_TARGETING, 0);
            }
            syncTargetingModel(&g_state);
        } else {
            if (screen == HHUI_SCREEN_TARGETING) {
                (void) hhuiPopScreen(&g_state);
                screen = hhuiCurrentScreen(&g_state);
            }
            if (screen == HHUI_SCREEN_MENU || screen == HHUI_SCREEN_CONFIRM) {
                (void) hhuiPopScreen(&g_state);
                screen = hhuiCurrentScreen(&g_state);
            }
            if (screen == HHUI_SCREEN_NONE) {
                if (gameplay && g_hudEverActive) {
                    (void) hhuiPushScreen(&g_state, HHUI_SCREEN_GAMEPLAY, 0);
                    strcpy(g_state.hint, HHUI_HINT_GAMEPLAY);
                }
            } else if (screen == HHUI_SCREEN_GAMEPLAY) {
                if (!gameplay) {
                    (void) hhuiPopScreen(&g_state);
                } else {
                    /* Stage 5: returning from inventory/targeting leaves the
                     * previous modal's hint behind unless it is restored here. */
                    strcpy(g_state.hint, HHUI_HINT_GAMEPLAY);
                }
            }
        }
    }

    screen = hhuiCurrentScreen(&g_state);
    if (screen == HHUI_SCREEN_NONE) return;   /* no overlay active yet */

    /* Stage 5 "core UI first": the core already renders a complete Brogue UI
     * (sidebar with visible-entity list, messages, button bar).  Our old
     * gameplay/status/targeting/menu/inventory panels duplicated it at a
     * different scale and clipped text.  Only the two genuinely gamepad-only
     * overlays are still drawn: the on-screen keyboard (text entry cannot be
     * driven by the controller otherwise). The Start menu is drawn by tiles.c
     * into a Brogue-style display buffer (Brogue glyph font, [] values). */
    if (screen != HHUI_SCREEN_TEXT_INPUT) {
        return;
    }

    /* Confirmations keep the core's own prompt visible: only the abstract
     * hint strip is added, since the prompt text is not exported.  Targeting
     * must not dim the map — the core-drawn cursor/trajectory stay visible. */
    if (screen != HHUI_SCREEN_GAMEPLAY && screen != HHUI_SCREEN_CONFIRM
        && screen != HHUI_SCREEN_TARGETING) {
        drawBackdrop(renderer);
    }

    switch (screen) {
        case HHUI_SCREEN_GAMEPLAY:
            drawMessageBand(renderer);
            drawStatusPanel(renderer);
            break;
        case HHUI_SCREEN_MENU:
            drawMainMenu(renderer, &g_state);
            break;
        case HHUI_SCREEN_TEXT_INPUT:
            drawTextInput(renderer, &g_state);
            break;
        case HHUI_SCREEN_IN_GAME_MENU:
            /* drawn by tiles.c into hhMenuBuffer with Brogue's own buttons */
            break;
        case HHUI_SCREEN_INVENTORY:
            drawInventory(renderer, &g_state);
            break;
        case HHUI_SCREEN_ITEM_ACTIONS:
            drawItemActions(renderer, &g_state);
            break;
        case HHUI_SCREEN_TARGETING:
            drawTargeting(renderer, &g_state);
            break;
        case HHUI_SCREEN_INFO:
            drawListPanel(renderer, &g_state);
            break;
        case HHUI_SCREEN_CONFIRM:
        default:
            break;
    }

    drawHintStrip(renderer, &g_state);
}
