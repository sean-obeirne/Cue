#ifndef CUE_AUDIO_H
#define CUE_AUDIO_H

#include <stdbool.h>
#include "esp_err.h"

/*
 * audio.h — I2S playback engine (stub).
 *
 * Once implemented:
 *   - Owns the I2S0 peripheral on PIN_I2S_BCK / LRCK / DOUT.
 *   - Pulls compressed frames from a decoder (MP3/FLAC/AAC).
 *   - Mixes against a software volume control.
 *
 * Today this exists so the UI layer can compile against a stable API.
 */

typedef enum {
    AUDIO_STOPPED = 0,
    AUDIO_PLAYING,
    AUDIO_PAUSED,
} audio_state_t;

esp_err_t      audio_init(void);

esp_err_t      audio_play_path(const char *path);
void           audio_toggle_pause(void);
void           audio_stop(void);
void           audio_skip_next(void);
void           audio_skip_prev(void);

audio_state_t  audio_state(void);
const char    *audio_now_playing(void);   /* track title / file name */
int            audio_progress_pct(void);  /* 0..100, -1 if unknown   */

void           audio_set_volume(int pct); /* 0..100 */
int            audio_get_volume(void);

#endif /* CUE_AUDIO_H */
