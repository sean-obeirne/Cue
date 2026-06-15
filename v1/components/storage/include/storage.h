#ifndef CUE_STORAGE_H
#define CUE_STORAGE_H

#include "esp_err.h"

/*
 * storage.h — microSD card mounted at /sdcard.
 *
 * The implementation is a thin wrapper around esp_vfs_fat_sdspi_mount
 * using the SPI host + pins from board.h.  Currently stubbed: it logs
 * its intent and returns ESP_OK so the rest of the firmware can run
 * on a bare dev board.
 */

#define CUE_SD_MOUNT_POINT "/sdcard"

esp_err_t storage_init(void);

/* Returns true once a card has been successfully mounted. */
bool storage_mounted(void);

#endif /* CUE_STORAGE_H */
