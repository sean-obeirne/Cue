#ifndef CUE_SSD1309_H
#define CUE_SSD1309_H

#include <stdint.h>
#include "esp_err.h"

/*
 * Low-level SSD1309 (SSD1306-compatible) driver.
 *
 * The 2.42" MC242GW we target presents the same 0x3C I2C address and
 * command set as a 128x64 SSD1306 — the only delta is that SSD1309
 * does not implement an internal charge pump (the 0x8D 0x14 command
 * is treated as a no-op).  The init sequence below is therefore safe
 * on both panels.
 */

esp_err_t ssd1309_init(void);

/* Push a 128x64 framebuffer (1024 bytes, 8 horizontal pages of 128) */
esp_err_t ssd1309_write_framebuffer(const uint8_t *fb);

/* Power the panel on/off without touching the framebuffer. */
esp_err_t ssd1309_display_on(void);
esp_err_t ssd1309_display_off(void);

#endif /* CUE_SSD1309_H */
