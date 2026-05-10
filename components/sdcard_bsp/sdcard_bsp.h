#ifndef SDCARD_BSP_H
#define SDCARD_BSP_H
#include "driver/sdmmc_host.h"


typedef struct
{
  float sdcard_size;
}sdcard_bsp_t;

extern sdcard_bsp_t user_sdcard_bsp;
extern EventGroupHandle_t sdcard_even_;
/* True while sdcard_format() is running (volume is unmounted). UI code
   that touches /sdcard via opendir/statvfs/esp_vfs_fat_info should
   short-circuit when this is set. */
#include <stdbool.h>
extern volatile bool sdcard_busy;

#ifdef __cplusplus
extern "C" {
#endif


void _sdcard_init(void);
esp_err_t sdcard_file_write(const char *path, const char *data);
esp_err_t sdcard_file_read(const char *path, char *buffer, size_t *out_len);
/* Format the mounted SD card to FAT32 in place. Wipes everything.
   Returns the underlying esp_vfs_fat_sdcard_format result. */
esp_err_t sdcard_format(void);
/* True when /sdcard is mounted and not in the middle of a format. UI
   code should gate esp_vfs_fat_info / opendir on this to avoid driver
   spam ("Failed to get number of free clusters") on a missing card. */
bool sdcard_is_mounted(void);

#ifdef __cplusplus
}
#endif

#endif