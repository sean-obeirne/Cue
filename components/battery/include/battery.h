#ifndef CUE_BATTERY_H
#define CUE_BATTERY_H

/*
 * battery.h — LiPo monitoring on the ESP32-S3.
 *
 * Port of macro-blues/ble/battery.{c,h}.  The BLE Battery Service
 * pieces are dropped — they'll live in the future bt_audio component
 * if BLE comes back.
 *
 * Hardware assumption: a 2:1 resistor divider from VBAT down to
 * PIN_VBAT_SENSE (GPIO1, ADC1_CH0) so the ADC sees half of VBAT.
 * Override BAT_DIVIDER_NUM/DEN in board.h if your wiring differs.
 *
 * Call order:
 *   battery_init();              once after boot
 *   battery_update();            every ~30 s from the main loop
 *   battery_percent() / mv()     anytime
 */

#include <stdint.h>

void     battery_init(void);
void     battery_update(void);
uint8_t  battery_percent(void);
uint16_t battery_voltage_mv(void);
int      battery_low(void);

#endif /* CUE_BATTERY_H */
