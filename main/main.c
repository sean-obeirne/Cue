/*
 * main.c — Cue firmware entry point.
 *
 * Structural lineage: this file mirrors macro-blues/main.c — the
 * three-phase init pattern (drivers → wireless → post-init), the
 * always-scan / debounce / poll-encoder main loop, and the periodic
 * battery check are all carried over directly.  What changed:
 *
 *   - No SoftDevice / no BLE HID.  Wireless is deferred (bt_audio
 *     stub) and Cue isn't a keyboard.
 *   - The 4x3 key matrix is replaced with five discrete buttons
 *     (buttons.c), but the debounce machinery is identical.
 *   - Drawing is done by the ui component, which renders an iPod-
 *     style menu on the 128x64 SSD1309 instead of hand-rolling a
 *     status strip in main().
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "esp_rom_sys.h"   /* esp_rom_delay_us */

#include "board.h"
#include "display.h"
#include "buttons.h"
#include "encoder.h"
#include "debounce.h"
#include "battery.h"
#include "storage.h"
#include "audio.h"
#include "bt_audio.h"
#include "ui.h"

static const char *TAG = "cue";

/* ~30 s @ 10 ms loop tick. */
#define BAT_CHECK_TICKS  3000

/* Temporary boot-time I2C scanner.  Probes 0x03..0x77 on the configured
 * SDA/SCL pins, logs every address that ACKs, then tears the bus down
 * so display_init() can rebuild it normally.  Remove once hardware is
 * verified. */
static void i2c_scan(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .i2c_port                     = I2C_NUM_0,
        .scl_io_num                   = PIN_I2C_SCL,
        .sda_io_num                   = PIN_I2C_SDA,
        .glitch_ignore_cnt            = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    if (i2c_new_master_bus(&bus_cfg, &bus) != ESP_OK) {
        ESP_LOGE(TAG, "i2c scan: bus create failed");
        return;
    }
    ESP_LOGI(TAG, "i2c scan on SDA=%d SCL=%d:", PIN_I2C_SDA, PIN_I2C_SCL);
    int found = 0;
    for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
        if (i2c_master_probe(bus, addr, 50 /* ms */) == ESP_OK) {
            ESP_LOGI(TAG, "  ACK @ 0x%02X", addr);
            found++;
        }
    }
    if (!found) ESP_LOGW(TAG, "  no devices responded");
    i2c_del_master_bus(bus);
}

/* ---------------------------------------------------------------------------
 * Bit-banged ("manual") I2C scan + bus-health probe.
 *
 * Bypasses the ESP-IDF I2C peripheral entirely and toggles SDA/SCL as plain
 * open-drain GPIOs.  Think of it as a software multimeter on the bus: it
 * reports the idle line levels, whether each line can actually be driven low
 * and springs back high (pull-up check), then bit-bangs START + address + ACK
 * for every 7-bit address.  Use it when the hardware scan just says
 * "no devices responded" and we need to know *why*.  Remove with i2c_scan()
 * once the hardware is verified.
 * ------------------------------------------------------------------------- */
#define BB_DELAY_US   20    /* ~25 kHz half-period; slow on purpose */
#define BB_TAG        "i2cbb"

static inline void bb_sda_rel(void) { gpio_set_level(PIN_I2C_SDA, 1); }
static inline void bb_sda_low(void) { gpio_set_level(PIN_I2C_SDA, 0); }
static inline void bb_scl_rel(void) { gpio_set_level(PIN_I2C_SCL, 1); }
static inline void bb_scl_low(void) { gpio_set_level(PIN_I2C_SCL, 0); }
static inline int  bb_sda(void)     { return gpio_get_level(PIN_I2C_SDA); }
static inline int  bb_scl(void)     { return gpio_get_level(PIN_I2C_SCL); }
static inline void bb_wait(void)    { esp_rom_delay_us(BB_DELAY_US); }

static void bb_pins_config(bool internal_pullup)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_I2C_SDA) | (1ULL << PIN_I2C_SCL),
        .mode         = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en   = internal_pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    bb_sda_rel();
    bb_scl_rel();
    bb_wait();
}

/* Release SCL and wait for it to actually go high (honours clock stretching).
 * Returns false if it stays low past the timeout. */
static bool bb_scl_release_wait(void)
{
    bb_scl_rel();
    for (int i = 0; i < 100; i++) {
        if (bb_scl()) return true;
        bb_wait();
    }
    return false;
}

static void bb_start(void)
{
    bb_sda_rel(); bb_scl_rel(); bb_wait();
    bb_sda_low(); bb_wait();
    bb_scl_low(); bb_wait();
}

static void bb_stop(void)
{
    bb_sda_low(); bb_wait();
    bb_scl_release_wait(); bb_wait();
    bb_sda_rel(); bb_wait();
}

/* Clock out one byte MSB-first; returns 0 if the device ACKed, 1 on NACK. */
static int bb_write_byte(uint8_t b)
{
    for (int i = 0; i < 8; i++) {
        if (b & 0x80) bb_sda_rel(); else bb_sda_low();
        b <<= 1;
        bb_wait();
        bb_scl_release_wait(); bb_wait();
        bb_scl_low(); bb_wait();
    }
    /* 9th clock: release SDA and sample the ACK bit. */
    bb_sda_rel(); bb_wait();
    bb_scl_release_wait(); bb_wait();
    int ack = bb_sda();          /* 0 = ACK (device pulled SDA low) */
    bb_scl_low(); bb_wait();
    return ack;
}

static bool bb_probe(uint8_t addr7)
{
    bb_start();
    int ack = bb_write_byte((uint8_t)((addr7 << 1) | 0));  /* write bit */
    bb_stop();
    return ack == 0;
}

static void i2c_scan_manual(void)
{
    /* 1. External pull-up check: lines released, internal PU OFF. */
    bb_pins_config(false);
    int sda_ext = bb_sda();
    int scl_ext = bb_scl();
    ESP_LOGI(BB_TAG, "idle, no internal PU:  SDA=%d SCL=%d  (1,1 => external pull-ups present)",
             sda_ext, scl_ext);
    if (!sda_ext || !scl_ext)
        ESP_LOGW(BB_TAG, "  -> line low without internal PU: missing external pull-up OR shorted to GND");

    /* 2. Re-check with the chip's internal pull-ups enabled. */
    bb_pins_config(true);
    int sda_idle = bb_sda();
    int scl_idle = bb_scl();
    ESP_LOGI(BB_TAG, "idle, internal PU on:  SDA=%d SCL=%d  (expect 1,1)", sda_idle, scl_idle);

    /* 3. Drive-low / release controllability test. */
    bb_sda_low(); bb_wait(); int sda_drv = bb_sda(); bb_sda_rel(); bb_wait(); int sda_rec = bb_sda();
    bb_scl_low(); bb_wait(); int scl_drv = bb_scl(); bb_scl_rel(); bb_wait(); int scl_rec = bb_scl();
    ESP_LOGI(BB_TAG, "SDA drive-low=%d release=%d   SCL drive-low=%d release=%d  (expect 0 then 1)",
             sda_drv, sda_rec, scl_drv, scl_rec);
    if (sda_drv) ESP_LOGW(BB_TAG, "  -> SDA will not go low: shorted to 3V3?");
    if (scl_drv) ESP_LOGW(BB_TAG, "  -> SCL will not go low: shorted to 3V3?");
    if (!sda_rec) ESP_LOGW(BB_TAG, "  -> SDA will not recover high: stuck low / no pull-up");
    if (!scl_rec) ESP_LOGW(BB_TAG, "  -> SCL will not recover high: stuck low / no pull-up");

    if (!sda_idle || !scl_idle) {
        ESP_LOGE(BB_TAG, "bus not idle-high; skipping address sweep");
        gpio_reset_pin(PIN_I2C_SDA);
        gpio_reset_pin(PIN_I2C_SCL);
        return;
    }

    /* 4. Bit-banged address sweep. */
    ESP_LOGI(BB_TAG, "bit-bang sweep 0x03..0x77 on SDA=%d SCL=%d:", PIN_I2C_SDA, PIN_I2C_SCL);
    int found = 0;
    for (uint8_t a = 0x03; a <= 0x77; a++) {
        if (bb_probe(a)) { ESP_LOGI(BB_TAG, "  ACK @ 0x%02X", a); found++; }
    }
    ESP_LOGI(BB_TAG, "target 0x%02X: %s", DISPLAY_I2C_ADDR,
             bb_probe(DISPLAY_I2C_ADDR) ? "ACK (alive!)" : "no ACK");
    if (!found) ESP_LOGW(BB_TAG, "  no devices ACKed on the bit-bang scan either");

    gpio_reset_pin(PIN_I2C_SDA);
    gpio_reset_pin(PIN_I2C_SCL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Cue booting on ESP32-S3-N16R8");

    i2c_scan_manual();
    i2c_scan();

    /* ---- Phase 1: I/O peripherals ---- */
    display_init();
    buttons_init();
    encoder_init();
    debounce_init();

    /* ---- Phase 2: wireless (stub today) ---- */
    bt_audio_init();

    /* ---- Phase 3: post-init drivers ---- */
    battery_init();
    storage_init();
    audio_init();

    /* ---- Phase 4: UI ---- */
    ui_init();

    int  raw[NUM_INPUTS];
    int  bat_counter = 0;

    while (true) {
        /* --- Input --- */
        buttons_scan(raw);
        debounce_update(raw);

        int enc = encoder_poll();
        if (enc) ui_handle_scroll(enc > 0 ? 1 : -1);  /* one detent at a time */

        if (debounce_fell(BTN_SELECT)) { ESP_LOGI(TAG, "btn: SELECT"); ui_handle_select(); }
        if (debounce_fell(BTN_MENU))   { ESP_LOGI(TAG, "btn: MENU");   ui_handle_back();   }
        if (debounce_fell(BTN_PLAY))   { ESP_LOGI(TAG, "btn: PLAY");   ui_handle_play();   }
        if (debounce_fell(BTN_PREV))   { ESP_LOGI(TAG, "btn: PREV");   ui_handle_scroll(-1); }
        if (debounce_fell(BTN_NEXT))   { ESP_LOGI(TAG, "btn: NEXT");   ui_handle_scroll(+1); }

        /* --- Render --- */
        ui_render();
        display_flush();

        /* --- Periodic battery sample --- */
        if (++bat_counter >= BAT_CHECK_TICKS) {
            bat_counter = 0;
            battery_update();
            if (battery_low())
                ESP_LOGW(TAG, "battery low: %u mV (%u%%)",
                         (unsigned)battery_voltage_mv(),
                         (unsigned)battery_percent());
        }

        /* Same dual-rate idea as macro-blues — fast poll while keys
         * are settling, otherwise relax. */
        if (buttons_any_pressed() || debounce_settling())
            vTaskDelay(pdMS_TO_TICKS(10));
        else
            vTaskDelay(pdMS_TO_TICKS(30));
    }
}
