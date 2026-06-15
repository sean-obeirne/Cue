/*
 * battery.c — LiPo voltage → percentage using ADC1 oneshot.
 *
 * Ported from macro-blues/ble/battery.c.  The piecewise-linear
 * voltage-to-percent curve is copied verbatim because LiPo discharge
 * curves don't care which MCU is reading them.  The nRF52 SAADC code
 * is replaced with esp_adc_oneshot + esp_adc_cal.
 */

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

#include "board.h"
#include "battery.h"

static const char *TAG = "bat";

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t         s_cali;
static bool                      s_cali_ok;

static uint8_t  s_last_percent;
static uint16_t s_last_mv;

/* ---- LiPo voltage → percentage (5-segment approximation) ----
 * Copied verbatim from macro-blues. */
static uint8_t mv_to_percent(uint16_t mv)
{
    if (mv >= 4200) return 100;
    if (mv >= 4100) return 90 + (uint32_t)(mv - 4100) * 10  / 100;
    if (mv >= 3950) return 75 + (uint32_t)(mv - 3950) * 15  / 150;
    if (mv >= 3800) return 60 + (uint32_t)(mv - 3800) * 15  / 150;
    if (mv >= 3600) return 20 + (uint32_t)(mv - 3600) * 40  / 200;
    if (mv >= 3000) return       (uint32_t)(mv - 3000) * 20 / 600;
    return 0;
}

void battery_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = VBAT_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &s_adc));

    adc_oneshot_chan_cfg_t ch_cfg = {
        .atten    = ADC_ATTEN_DB_12,     /* full 0..~3.1 V range  */
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, VBAT_ADC_CHANNEL, &ch_cfg));

    /* Calibration — curve-fitting on S3, falls back to a rough linear
     * conversion if eFuse calibration data is missing. */
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = VBAT_ADC_UNIT,
        .chan     = VBAT_ADC_CHANNEL,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    s_cali_ok = (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali) == ESP_OK);
#endif
    if (!s_cali_ok) {
        ESP_LOGW(TAG, "ADC calibration unavailable, using raw mapping");
    }

    battery_update();   /* prime cached values */
}

void battery_update(void)
{
    int raw = 0;
    if (adc_oneshot_read(s_adc, VBAT_ADC_CHANNEL, &raw) != ESP_OK) return;

    int pin_mv;
    if (s_cali_ok) {
        adc_cali_raw_to_voltage(s_cali, raw, &pin_mv);
    } else {
        /* 12-bit, 12 dB atten → roughly 0..3100 mV.  Good enough for
         * a "battery low" warning even without calibration. */
        pin_mv = (raw * 3100) / 4095;
    }

    uint32_t vbat_mv = (uint32_t)pin_mv * BAT_DIVIDER_NUM / BAT_DIVIDER_DEN;
    if (vbat_mv > 0xFFFF) vbat_mv = 0xFFFF;

    s_last_mv      = (uint16_t)vbat_mv;
    s_last_percent = mv_to_percent(s_last_mv);
}

uint8_t  battery_percent(void)    { return s_last_percent; }
uint16_t battery_voltage_mv(void) { return s_last_mv;      }
int      battery_low(void)        { return s_last_mv < BAT_MV_LOW; }
