/*
 * storage.c — microSD stub.
 *
 * Real implementation will spin up SPI2 with the pins from board.h
 * and mount FATFS at /sdcard via esp_vfs_fat_sdspi_mount().  Today it
 * just logs and reports "not mounted" so the player path can no-op
 * cleanly until the card slot is wired up.
 */

#include <stdbool.h>

#include "driver/spi_common.h"  /* SPI2_HOST enum */
#include "esp_log.h"
#include "storage.h"
#include "board.h"

static const char *TAG = "sd";
static bool s_mounted;

esp_err_t storage_init(void)
{
    ESP_LOGI(TAG, "stub: would mount SD on SPI%d (MOSI=%d MISO=%d SCK=%d CS=%d) at %s",
             (int)SD_SPI_HOST, PIN_SD_MOSI, PIN_SD_MISO, PIN_SD_SCK,
             PIN_SD_CS, CUE_SD_MOUNT_POINT);
    s_mounted = false;
    return ESP_OK;
}

bool storage_mounted(void) { return s_mounted; }
