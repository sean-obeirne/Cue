/*
 * ssd1309.c — I2C driver for the SSD1309 / SSD1306-compatible OLED.
 *
 * Ported from macro-blues/drivers/ssd1306.c.  The bare-metal nRF52 TWIM
 * paths are replaced with the ESP-IDF "new" I2C master driver
 * (driver/i2c_master.h, available in IDF >= 5.2), but the actual
 * controller command sequence is unchanged except for:
 *   - MUX ratio   : 0x3F   (64 rows, was 0x1F for 32)
 *   - COM pins    : 0x12   (alternative, no remap; 128x64 layout)
 *   - Charge pump : retained — SSD1309 ignores it harmlessly.
 */

#include <string.h>

#include "driver/i2c_master.h"
#include "esp_log.h"

#include "board.h"
#include "display.h"   /* FB_PAGES, FB_SIZE */
#include "ssd1309.h"

static const char *TAG = "ssd1309";

/* Co=0, D/C=0 → command stream. Co=0, D/C=1 → data stream. */
#define CTRL_CMD   0x00
#define CTRL_DATA  0x40

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;

/* Bounce buffer for cmd writes — max ~30 bytes including control byte. */
static uint8_t s_cmd_buf[40];

/* Bounce buffer for one full page row (128 data bytes + ctrl byte). */
static uint8_t s_data_chunk[1 + 128];

static esp_err_t ssd1309_cmd(const uint8_t *cmds, size_t len)
{
    if (len + 1 > sizeof(s_cmd_buf)) return ESP_ERR_INVALID_SIZE;
    s_cmd_buf[0] = CTRL_CMD;
    memcpy(&s_cmd_buf[1], cmds, len);
    return i2c_master_transmit(s_dev, s_cmd_buf, len + 1, 100 /* ms */);
}

esp_err_t ssd1309_init(void)
{
    /* ---- I2C bus + device ---- */
    i2c_master_bus_config_t bus_cfg = {
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .i2c_port                     = I2C_NUM_0,
        .scl_io_num                   = PIN_I2C_SCL,
        .sda_io_num                   = PIN_I2C_SDA,
        .glitch_ignore_cnt            = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) { ESP_LOGE(TAG, "i2c bus: %s", esp_err_to_name(err)); return err; }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = DISPLAY_I2C_ADDR,
        .scl_speed_hz    = I2C_HZ,
    };
    err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) { ESP_LOGE(TAG, "i2c dev: %s", esp_err_to_name(err)); return err; }

    /* ---- Panel init (SSD1306-style sequence, 128x64) ----
     * Refs: SSD1306 datasheet §8, SSD1309 datasheet §8.1, U8g2 ssd1309. */
    static const uint8_t init_cmds[] = {
        0xAE,             /* Display OFF                                       */
        0xD5, 0x80,       /* Display clock: default oscillator, divide ratio 1 */
        0xA8, 0x3F,       /* MUX ratio: 64 rows                                */
        0xD3, 0x00,       /* Display offset: 0                                 */
        0x40,             /* Display start line: 0                             */
        0x8D, 0x14,       /* Charge pump on (no-op on SSD1309, OK on SSD1306) */
        0x20, 0x00,       /* Memory addressing mode: horizontal                */
        0xA1,             /* Segment re-map: column 127 → SEG0                 */
        0xC8,             /* COM scan: remapped (bottom-to-top)                */
        0xDA, 0x12,       /* COM pins: alternative, no L/R remap (128x64)     */
        0x81, 0xCF,       /* Contrast: 0xCF (bright)                          */
        0xD9, 0xF1,       /* Pre-charge period                                 */
        0xDB, 0x40,       /* VCOMH deselect                                    */
        0xA4,             /* Resume from RAM                                   */
        0xA6,             /* Normal (non-inverted) display                     */
        0x2E,             /* Deactivate any prior hardware scroll              */
        0xAF,             /* Display ON                                        */
    };
    err = ssd1309_cmd(init_cmds, sizeof(init_cmds));
    if (err != ESP_OK) return err;

    /* Wipe internal RAM so power-on garbage doesn't show. */
    uint8_t blank[FB_PAGES * DISPLAY_W];
    memset(blank, 0, sizeof(blank));
    return ssd1309_write_framebuffer(blank);
}

esp_err_t ssd1309_write_framebuffer(const uint8_t *fbuf)
{
    /* Set column range 0..127 */
    const uint8_t col_cmd[] = {0x21, 0x00, DISPLAY_W - 1};
    esp_err_t err = ssd1309_cmd(col_cmd, sizeof(col_cmd));
    if (err != ESP_OK) return err;

    /* Set page range 0..(FB_PAGES-1) */
    const uint8_t page_cmd[] = {0x22, 0x00, FB_PAGES - 1};
    err = ssd1309_cmd(page_cmd, sizeof(page_cmd));
    if (err != ESP_OK) return err;

    /* Ship one page (128 bytes) per I2C transaction.  Splitting keeps
     * transactions short and matches the macro-blues behaviour. */
    s_data_chunk[0] = CTRL_DATA;
    for (int p = 0; p < FB_PAGES; p++) {
        memcpy(&s_data_chunk[1], &fbuf[p * DISPLAY_W], DISPLAY_W);
        err = i2c_master_transmit(s_dev, s_data_chunk, 1 + DISPLAY_W, 100);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

esp_err_t ssd1309_display_on(void)
{
    static const uint8_t cmd[] = {0xAF};
    return ssd1309_cmd(cmd, sizeof(cmd));
}

esp_err_t ssd1309_display_off(void)
{
    static const uint8_t cmd[] = {0xAE};
    return ssd1309_cmd(cmd, sizeof(cmd));
}
