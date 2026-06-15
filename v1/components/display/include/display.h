#ifndef CUE_DISPLAY_H
#define CUE_DISPLAY_H

#include <stdint.h>
#include "board.h"

/*
 * display.h — high-level API for the 128x64 SSD1309 OLED.
 *
 * Coordinate system (natural landscape):
 *   (0, 0) = top-left corner
 *   x grows rightward, 0 .. DISPLAY_W-1   (0..127)
 *   y grows downward,  0 .. DISPLAY_H-1   (0..63)
 *
 * A single 1024-byte framebuffer lives in RAM.  Drawing operates on
 * that buffer; display_flush() ships it to the panel over I2C.
 *
 * Font: the 5x7 Adafruit GLCD font from glyphs.c, 5 columns + 1 gap.
 */

#define FB_PAGES       (DISPLAY_H / 8)        /* 8 pages of 128 px */
#define FB_SIZE        (DISPLAY_W * FB_PAGES) /* 1024 bytes */

extern uint8_t fb[FB_SIZE];

void display_init(void);
void display_clear(void);
void display_flush(void);

void display_pixel(int x, int y, int on);
void display_rect(int x, int y, int w, int h, int on);

/* Draw an ASCII character at pixel (x, y), 5x7 cell scaled by `scale`. */
void display_char(int x, int y, char c, int scale);

/* Draw a null-terminated string starting at (x, y). Wraps to the next
 * 9*scale-pixel row when it would overflow horizontally. */
void display_string(int x, int y, const char *s, int scale);

/* Centred string convenience: horizontally centres `s` at row y. */
void display_string_centred(int y, const char *s, int scale);

/* Power management without touching the framebuffer. */
void display_on(void);
void display_off(void);

#endif /* CUE_DISPLAY_H */
