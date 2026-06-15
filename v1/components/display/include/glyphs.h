#pragma once
#include <stdint.h>

/* Returns a pointer to the 5-byte glyph for ASCII character c.
 * Index each column byte as glyph(c)[0..4]. */
const uint8_t *glyph(char c);
