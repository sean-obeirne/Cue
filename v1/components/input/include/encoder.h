#ifndef CUE_ENCODER_H
#define CUE_ENCODER_H

/*
 * encoder.h — quadrature rotary encoder (the Cue "click wheel" stand-in)
 *
 * Same public API as macro-blues/input/encoder.h.  Button state is now
 * read alongside the iPod-style buttons in buttons.c, so encoder_btn_*
 * is intentionally absent here — use debounce_fell(BTN_SELECT) instead.
 */

void encoder_init(void);

/* Returns accumulated detents since last call (+ CW, − CCW, 0 idle). */
int encoder_poll(void);

#endif /* CUE_ENCODER_H */
