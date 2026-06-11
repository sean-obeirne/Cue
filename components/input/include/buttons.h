#ifndef CUE_BUTTONS_H
#define CUE_BUTTONS_H

/*
 * buttons.h — discrete iPod-style buttons + encoder push
 *
 * Replaces macro-blues' 4x3 matrix scanner.  All five inputs are
 * configured as active-low with internal pull-ups; buttons_scan()
 * fills an array in the order below so it can be fed straight to
 * debounce_update().
 */

#include "debounce.h"

enum cue_button {
    BTN_MENU = 0,
    BTN_PREV,
    BTN_NEXT,
    BTN_PLAY,
    BTN_SELECT,   /* encoder push */
    BTN_COUNT
};

_Static_assert(BTN_COUNT == NUM_INPUTS, "BTN_COUNT must equal NUM_INPUTS");

void buttons_init(void);

/* Fill state[NUM_INPUTS] with raw active-low reads (1 = pressed). */
void buttons_scan(int state[NUM_INPUTS]);

/* Returns 1 if any button is currently held. */
int buttons_any_pressed(void);

#endif /* CUE_BUTTONS_H */
