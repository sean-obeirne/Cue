/*
 * ui.c — iPod-flavoured menu UI on the 128x64 SSD1309.
 *
 * Two screens for v0:
 *   SCREEN_MENU   — vertical list of items, encoder scrolls, SELECT
 *                   activates, MENU/back pops (this is the home).
 *   SCREEN_NOWPLAY — minimal now-playing view; MENU returns home.
 *
 * The drawing primitives (display_clear/string/rect) and battery
 * indicator are inherited from macro-blues — only the layout was
 * redesigned for portrait-of-information rather than a 128x32 strip.
 */

#include <string.h>
#include <stdio.h>

#include "display.h"
#include "battery.h"
#include "audio.h"
#include "ui.h"

typedef enum { SCREEN_MENU, SCREEN_NOWPLAY } screen_t;

typedef struct {
    const char *label;
    void (*activate)(void);
} menu_item_t;

/* ---- Item actions ---- */
static void act_nowplaying(void);
static void act_dummy(void) { /* placeholder */ }

static const menu_item_t s_items[] = {
    { "Music",        act_dummy       },
    { "Playlists",    act_dummy       },
    { "Artists",      act_dummy       },
    { "Albums",       act_dummy       },
    { "Songs",        act_dummy       },
    { "Now Playing",  act_nowplaying  },
    { "Settings",     act_dummy       },
};
#define ITEM_COUNT ((int)(sizeof(s_items) / sizeof(s_items[0])))

static screen_t s_screen;
static int      s_sel;       /* selected menu index   */
static int      s_top;       /* topmost rendered item */

#define ROW_H       12          /* 8 px glyph + 4 padding              */
#define HEADER_H    12          /* top status bar                      */
#define VISIBLE     ((DISPLAY_H - HEADER_H) / ROW_H)   /* 4 rows shown */

void ui_init(void)
{
    s_screen = SCREEN_MENU;
    s_sel    = 0;
    s_top    = 0;
}

/* ---- Navigation handlers ---- */
void ui_handle_scroll(int delta)
{
    if (s_screen != SCREEN_MENU) return;
    s_sel += delta;
    if (s_sel < 0)             s_sel = 0;
    if (s_sel >= ITEM_COUNT)   s_sel = ITEM_COUNT - 1;
    if (s_sel < s_top)         s_top = s_sel;
    if (s_sel >= s_top + VISIBLE) s_top = s_sel - VISIBLE + 1;
}

void ui_handle_select(void)
{
    if (s_screen == SCREEN_MENU && s_items[s_sel].activate)
        s_items[s_sel].activate();
}

void ui_handle_back(void)
{
    if (s_screen != SCREEN_MENU) s_screen = SCREEN_MENU;
}

void ui_handle_play(void)  { audio_toggle_pause(); }
void ui_handle_prev(void)  { audio_skip_prev();    }
void ui_handle_next(void)  { audio_skip_next();    }

/* ---- Drawing ---- */
static void draw_header(const char *title)
{
    /* Title left, battery glyph right.  Battery layout adapted from
     * macro-blues main.c: 24x10 outer, 22x8 inner, 2x4 nub.        */
    display_string(2, 2, title, 1);

    int x = DISPLAY_W - 26;
    int y = 2;
    display_rect(x,     y,     24, 8, 1);   /* outer */
    display_rect(x + 1, y + 1, 22, 6, 0);   /* hollow */
    display_rect(x + 24, y + 2, 2, 4, 1);   /* nub */

    int pct = battery_percent();
    int fill_w = (20 * pct) / 100;
    if (fill_w > 0)
        display_rect(x + 2, y + 2, fill_w, 4, 1);

    /* hairline under header */
    display_rect(0, HEADER_H - 2, DISPLAY_W, 1, 1);
}

static void draw_menu(void)
{
    draw_header("Cue");

    for (int i = 0; i < VISIBLE; i++) {
        int idx = s_top + i;
        if (idx >= ITEM_COUNT) break;
        int y = HEADER_H + i * ROW_H;
        int selected = (idx == s_sel);

        /* Label is always drawn the same — left-margin reserved for
         * the cursor glyph keeps text positions stable as the
         * selection moves. */
        display_string(8, y + 2, s_items[idx].label, 1);

        /* Right-pointing chevron in the left margin marks the
         * current selection, no row inversion. */
        if (selected) {
            display_pixel(2, y + 3, 1);
            display_pixel(2, y + 4, 1);
            display_pixel(3, y + 4, 1);
            display_pixel(3, y + 5, 1);
            display_pixel(4, y + 5, 1);
            display_pixel(3, y + 6, 1);
            display_pixel(3, y + 7, 1);
            display_pixel(2, y + 7, 1);
            display_pixel(2, y + 8, 1);
        }
    }

    /* simple scroll thumb on the right edge */
    int track_h = DISPLAY_H - HEADER_H;
    int thumb_h = (track_h * VISIBLE) / ITEM_COUNT;
    if (thumb_h < 4) thumb_h = 4;
    int thumb_y = HEADER_H + (track_h - thumb_h) * s_top / (ITEM_COUNT - VISIBLE > 0 ? ITEM_COUNT - VISIBLE : 1);
    display_rect(DISPLAY_W - 2, thumb_y, 2, thumb_h, 1);
}

static void draw_nowplay(void)
{
    draw_header("Now Playing");

    const char *t = audio_now_playing();
    if (!t || !*t) t = "(idle)";
    display_string_centred(20, t, 1);

    /* play / pause glyph, centred */
    int cx = DISPLAY_W / 2;
    int cy = 40;
    switch (audio_state()) {
    case AUDIO_PLAYING:
        /* two vertical bars */
        display_rect(cx - 6, cy - 6, 3, 12, 1);
        display_rect(cx + 3, cy - 6, 3, 12, 1);
        break;
    case AUDIO_PAUSED:
    case AUDIO_STOPPED:
    default:
        /* play triangle */
        for (int i = 0; i < 12; i++) {
            int w = 12 - i;
            display_rect(cx - 5, cy - 6 + i, w / 2 + 1, 1, 1);
        }
        break;
    }

    /* volume bar across the bottom */
    int v = audio_get_volume();
    display_rect(8,  DISPLAY_H - 6, DISPLAY_W - 16, 4, 0);
    display_rect(8,  DISPLAY_H - 6, ((DISPLAY_W - 16) * v) / 100, 4, 1);
}

void ui_render(void)
{
    display_clear();
    if (s_screen == SCREEN_NOWPLAY) draw_nowplay();
    else                            draw_menu();
}

static void act_nowplaying(void)
{
    s_screen = SCREEN_NOWPLAY;
}
