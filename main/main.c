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

void app_main(void)
{
    ESP_LOGI(TAG, "Cue booting on ESP32-S3-N16R8");

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
