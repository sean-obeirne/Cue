#ifndef CUE_BOARD_H
#define CUE_BOARD_H

/*
 * board.h — Cue / ESP32-S3-N16R8 (YD-ESP32-S3) pin map
 *
 * Centralised so every other module references symbolic names.
 * Pins are picked from the YD-ESP32-S3 pin headers (P1 / P2),
 * avoiding:
 *   - GPIO35/36/37 — used for octal SPI flash + PSRAM, not exposed.
 *   - GPIO19/20    — USB D-/D+ on the "USB" Type-C connector.
 *   - GPIO0/45/46  — strapping pins (used unless absolutely needed).
 *   - GPIO43/44    — UART0 TX/RX, kept for `idf.py monitor`.
 */

#include "driver/gpio.h"

/* ---- Status / debug LED ----
 * The on-board WS2812 hangs off GPIO48 only when the "RGB" pad is
 * bridged. Leave a symbol so a future RMT-based driver can use it. */
#define PIN_STATUS_RGB     GPIO_NUM_48

/* ---- I2C0 (SSD1309 OLED, future sensors) ---- */
#define PIN_I2C_SDA        GPIO_NUM_8
#define PIN_I2C_SCL        GPIO_NUM_9
#define I2C_HZ             400000

/* SSD1309 / SSD1306-compatible 2.42" 128×64 panel */
#define DISPLAY_I2C_ADDR   0x3C
#define DISPLAY_W          128
#define DISPLAY_H          64

/* ---- Rotary encoder + select press (the "click wheel" surrogate) ----
 * RTC_GPIOs so they can wake the chip from light/deep sleep. */
#define PIN_ENC_A          GPIO_NUM_4
#define PIN_ENC_B          GPIO_NUM_5
#define PIN_ENC_BTN        GPIO_NUM_6

/* ---- Discrete iPod-style buttons (active low, internal pull-up) ----
 * MENU goes "up" the navigation stack; PLAY toggles play/pause;
 * PREV / NEXT skip tracks (long-press = scrub). */
#define PIN_BTN_MENU       GPIO_NUM_10
#define PIN_BTN_PREV       GPIO_NUM_11
#define PIN_BTN_NEXT       GPIO_NUM_12
#define PIN_BTN_PLAY       GPIO_NUM_13

#define NUM_BUTTONS        4

/* ---- microSD over SPI (storage component) ---- */
#define PIN_SD_MOSI        GPIO_NUM_38
#define PIN_SD_MISO        GPIO_NUM_40
#define PIN_SD_SCK         GPIO_NUM_42
#define PIN_SD_CS          GPIO_NUM_21
#define SD_SPI_HOST        SPI2_HOST

/* ---- I2S audio out (audio component, not yet implemented) ---- */
#define PIN_I2S_BCK        GPIO_NUM_47
#define PIN_I2S_LRCK       GPIO_NUM_45
#define PIN_I2S_DOUT       GPIO_NUM_46

/* ---- Battery sensing ----
 * Assumes an external 2:1 divider between VBAT and PIN_VBAT_SENSE.
 * GPIO1 == ADC1_CH0 on the ESP32-S3. */
#define PIN_VBAT_SENSE     GPIO_NUM_1
#define VBAT_ADC_UNIT      ADC_UNIT_1
#define VBAT_ADC_CHANNEL   ADC_CHANNEL_0
#define BAT_DIVIDER_NUM    2
#define BAT_DIVIDER_DEN    1

/* LiPo voltage thresholds (millivolts) */
#define BAT_MV_FULL        4200
#define BAT_MV_NOMINAL     3700
#define BAT_MV_LOW         3300
#define BAT_MV_CUTOFF      3000

#endif /* CUE_BOARD_H */
