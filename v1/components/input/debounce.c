/*
 * debounce.c — verbatim port of macro-blues/input/debounce.c, renamed
 * from NUM_KEYS to NUM_INPUTS to reflect the discrete-button layout.
 */

#include "debounce.h"

static int accepted[NUM_INPUTS];
static int counter[NUM_INPUTS];
static int fell_flag[NUM_INPUTS];
static int rose_flag[NUM_INPUTS];

void debounce_init(void)
{
    for (int i = 0; i < NUM_INPUTS; i++) {
        accepted[i]  = 0;
        counter[i]   = 0;
        fell_flag[i] = 0;
        rose_flag[i] = 0;
    }
}

void debounce_update(const int raw[NUM_INPUTS])
{
    for (int i = 0; i < NUM_INPUTS; i++) {
        fell_flag[i] = 0;
        rose_flag[i] = 0;

        if (raw[i] != accepted[i]) {
            counter[i]++;
            if (counter[i] >= DEBOUNCE_CYCLES) {
                accepted[i] = raw[i];
                counter[i]  = 0;
                if (accepted[i]) fell_flag[i] = 1;   /* just pressed  */
                else             rose_flag[i] = 1;   /* just released */
            }
        } else {
            counter[i] = 0;
        }
    }
}

int debounce_state(int idx) { return accepted[idx];  }
int debounce_fell(int idx)  { return fell_flag[idx]; }
int debounce_rose(int idx)  { return rose_flag[idx]; }

int debounce_settling(void)
{
    for (int i = 0; i < NUM_INPUTS; i++)
        if (counter[i] != 0) return 1;
    return 0;
}
