#include "esp_log.h"
#include "bt_audio.h"

static const char *TAG = "bt";

esp_err_t bt_audio_init(void)
{
    ESP_LOGI(TAG, "stub: BT disabled in sdkconfig (A2DP source pending)");
    return ESP_OK;
}
