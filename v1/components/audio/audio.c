/*
 * audio.c — playback engine stub.
 *
 * Holds state only; no real I2S output yet.  The transport functions
 * mutate s_state / s_volume so the UI can render plausible feedback
 * during bring-up.
 */

#include <string.h>

#include "esp_log.h"
#include "audio.h"

static const char *TAG = "audio";

static audio_state_t s_state;
static int           s_volume = 60;
static char          s_track[64];

esp_err_t audio_init(void)
{
    ESP_LOGI(TAG, "stub: audio engine ready (no I2S yet)");
    s_state = AUDIO_STOPPED;
    s_track[0] = '\0';
    return ESP_OK;
}

esp_err_t audio_play_path(const char *path)
{
    if (path) {
        strncpy(s_track, path, sizeof(s_track) - 1);
        s_track[sizeof(s_track) - 1] = '\0';
    }
    s_state = AUDIO_PLAYING;
    ESP_LOGI(TAG, "play %s", s_track);
    return ESP_OK;
}

void audio_toggle_pause(void)
{
    if (s_state == AUDIO_PLAYING) s_state = AUDIO_PAUSED;
    else if (s_state == AUDIO_PAUSED) s_state = AUDIO_PLAYING;
}

void audio_stop(void)       { s_state = AUDIO_STOPPED; }
void audio_skip_next(void)  { ESP_LOGI(TAG, "skip next"); }
void audio_skip_prev(void)  { ESP_LOGI(TAG, "skip prev"); }

audio_state_t  audio_state(void)         { return s_state;  }
const char    *audio_now_playing(void)   { return s_track;  }
int            audio_progress_pct(void)  { return -1;       }

void audio_set_volume(int pct)
{
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    s_volume = pct;
}
int  audio_get_volume(void) { return s_volume; }
