/*
 * display.c — framebuffer-backed 128x64 OLED helpers.
 *
 * Ported from macro-blues/output/display.c.  The original code drew
 * in portrait coordinates because the 128x32 panel was mounted
 * sideways; we collapse that back to natural landscape (x, y) since
 * the 2.42" SSD1309 is in its normal orientation.
 *
 * Pixel layout: the panel is paged — each byte covers 8 vertical
 * pixels (bit0 = top of page).  fb[page*W + x] holds the column.
 */

#include <string.h>

#include "display.h"
#include "ssd1309.h"
#include "glyphs.h"

uint8_t fb[FB_SIZE];

void display_init(void)
{
    ssd1309_init();
    display_clear();
    display_flush();
}

void display_clear(void)
{
    memset(fb, 0, sizeof(fb));
}

void display_flush(void)
{
    ssd1309_write_framebuffer(fb);
}

void display_pixel(int x, int y, int on)
{
    if (x < 0 || x >= DISPLAY_W || y < 0 || y >= DISPLAY_H) return;
    int page = y / 8;
    int bit  = y % 8;
    int idx  = page * DISPLAY_W + x;
    if (on)
        fb[idx] |=  (1u << bit);
    else
        fb[idx] &= ~(1u << bit);
}

void display_rect(int x, int y, int w, int h, int on)
{
    for (int i = 0; i < w; i++)
        for (int j = 0; j < h; j++)
            display_pixel(x + i, y + j, on);
}

void display_char(int x, int y, char c, int scale)
{
    const uint8_t *g = glyph(c);
    for (int col = 0; col < 5; col++) {
        for (int row = 0; row < 8; row++) {
            int on = (g[col] >> row) & 1;
            for (int sx = 0; sx < scale; sx++)
                for (int sy = 0; sy < scale; sy++)
                    display_pixel(x + col * scale + sx,
                                  y + row * scale + sy,
                                  on);
        }
    }
}

void display_string(int x, int y, const char *s, int scale)
{
    int char_w = 5 * scale + 1;
    int char_h = 8 * scale + 1;
    while (*s) {
        display_char(x, y, *s++, scale);
        x += char_w;
        if (x + char_w > DISPLAY_W) {
            x  = 0;
            y += char_h;
            if (y + char_h > DISPLAY_H) break;
        }
    }
}

void display_string_centred(int y, const char *s, int scale)
{
    int char_w = 5 * scale + 1;
    int len    = 0;
    for (const char *p = s; *p; p++) len++;
    int total  = len * char_w - 1;     /* drop trailing gap */
    int x      = (DISPLAY_W - total) / 2;
    if (x < 0) x = 0;
    display_string(x, y, s, scale);
}

void display_on(void)  { ssd1309_display_on();  }
void display_off(void) { ssd1309_display_off(); }
