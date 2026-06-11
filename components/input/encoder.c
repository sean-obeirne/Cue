/*
 * encoder.c — quadrature rotary encoder, port of macro-blues/input/encoder.c
 *
 * Original implementation used nRF52 GPIOTE channels firing on either
 * edge.  Here we register a per-pin GPIO ISR with the shared ESP-IDF
 * gpio_install_isr_service.  The gray-code state machine and quad_table
 * are unchanged — that part is hardware-agnostic.
 *
 * Position is accumulated in a volatile int from the ISR; main loop
 * polls encoder_poll() which atomically drains it.
 */

#include <stdint.h>

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"

#include "board.h"
#include "encoder.h"

static const char *TAG = "enc";

/* CW gray sequence: AB 11 → 01 → 00 → 10 → 11. */
static const int8_t quad_table[4][4] = {
    /*               cur 00  01  10  11 */
    /* prev 00 */ { 0, +1, -1,  0},
    /* prev 01 */ {-1,  0,  0, +1},
    /* prev 10 */ {+1,  0,  0, -1},
    /* prev 11 */ { 0, -1, +1,  0},
};

static volatile uint8_t s_prev_state;
static volatile int32_t s_position;

static IRAM_ATTR uint8_t read_state(void)
{
    int a = gpio_get_level(PIN_ENC_A);
    int b = gpio_get_level(PIN_ENC_B);
    return (uint8_t)((a << 1) | b);
}

static void IRAM_ATTR encoder_isr(void *arg)
{
    (void)arg;
    uint8_t cur = read_state();
    if (cur != s_prev_state) {
        s_position   += quad_table[s_prev_state][cur];
        s_prev_state  = cur;
    }
}

void encoder_init(void)
{
    /* Configure A/B as inputs with pull-ups and any-edge interrupts. */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_ENC_A) | (1ULL << PIN_ENC_B),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&io);

    s_prev_state = read_state();
    s_position   = 0;

    /* gpio_install_isr_service may have been called by another
     * component already; treat ESP_ERR_INVALID_STATE as success. */
    esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "gpio_install_isr_service: %s", esp_err_to_name(err));
    }

    gpio_isr_handler_add(PIN_ENC_A, encoder_isr, NULL);
    gpio_isr_handler_add(PIN_ENC_B, encoder_isr, NULL);
}

int encoder_poll(void)
{
    /* 32-bit reads/writes are atomic on Xtensa LX7 — fine without a lock. */
    int steps = s_position;
    if (steps) s_position = 0;
    return steps;
}
