#ifndef CUE_DEBOUNCE_H
#define CUE_DEBOUNCE_H

/*
 * debounce.h — per-button debounce filter
 *
 * Direct port of macro-blues/input/debounce.h.  The matrix-style "all
 * keys at once" array is reused; on Cue NUM_INPUTS counts the discrete
 * iPod buttons + the encoder push, but the algorithm is identical.
 */

#include "board.h"

/* Total number of debounced digital inputs. */
#define NUM_INPUTS  (NUM_BUTTONS + 1)   /* + encoder push */

/* Samples (≈ scan periods) of agreement required to flip state.
 * At ~10 ms scan, 2 cycles = ~20 ms — enough for typical tactile sw. */
#define DEBOUNCE_CYCLES 2

void debounce_init(void);

/* Feed raw[NUM_INPUTS] (1 = pressed). Call once per scan cycle. */
void debounce_update(const int raw[NUM_INPUTS]);

/* Debounced (clean) state: 1 = pressed, 0 = released. */
int debounce_state(int idx);

/* Edge detectors — true exactly once on the cycle the change occurred. */
int debounce_fell(int idx);
int debounce_rose(int idx);

/* True while any input's debounce counter is non-zero. */
int debounce_settling(void);

#endif /* CUE_DEBOUNCE_H */
