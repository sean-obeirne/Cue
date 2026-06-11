/*
 * buttons.c — discrete-button scanner (replaces macro-blues key matrix).
 *
 * Active-low; internal pull-ups enabled.  buttons_scan() does plain
 * GPIO reads — no row/col strobing needed because Cue has only five
 * dedicated buttons rather than a 12-key matrix.
 */

#include "driver/gpio.h"

#include "board.h"
#include "buttons.h"

static const gpio_num_t s_pins[BTN_COUNT] = {
    [BTN_MENU]   = PIN_BTN_MENU,
    [BTN_PREV]   = PIN_BTN_PREV,
    [BTN_NEXT]   = PIN_BTN_NEXT,
    [BTN_PLAY]   = PIN_BTN_PLAY,
    [BTN_SELECT] = PIN_ENC_BTN,
};

void buttons_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 0,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    for (int i = 0; i < BTN_COUNT; i++)
        io.pin_bit_mask |= (1ULL << s_pins[i]);
    gpio_config(&io);
}

void buttons_scan(int state[NUM_INPUTS])
{
    for (int i = 0; i < BTN_COUNT; i++)
        state[i] = (gpio_get_level(s_pins[i]) == 0) ? 1 : 0;
}

int buttons_any_pressed(void)
{
    for (int i = 0; i < BTN_COUNT; i++)
        if (gpio_get_level(s_pins[i]) == 0) return 1;
    return 0;
}
