#ifndef CUE_BT_AUDIO_H
#define CUE_BT_AUDIO_H

#include "esp_err.h"

/*
 * bt_audio.h — Bluetooth A2DP transport (stub placeholder).
 *
 * Cue's eventual goal is to act as an A2DP source streaming local
 * playback to BT headphones.  For now this file exists so the build
 * graph + UI hooks are in place; bt_audio_init() is intentionally a
 * no-op until BT_ENABLED is turned on in sdkconfig.defaults.
 *
 * NB: macro-blues' BLE HID/Bond/GATT layers are explicitly *not*
 * ported — Cue is not a keyboard.
 */

esp_err_t bt_audio_init(void);

#endif /* CUE_BT_AUDIO_H */
