/* sim/stubs/esp_vfs_fat.h - VFS FAT stub（仅 sdcard/cli 使用，已 override） */
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *sdmmc_card_t;
typedef struct {
    void *format_if_mount_failed;
    int max_files;
    int allocation_unit_size;
} esp_vfs_fat_sdmmc_mount_config_t;

#ifdef __cplusplus
}
#endif
