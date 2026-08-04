/* sim/stubs/driver/sdmmc_host.h - SDMMC 主机 stub（仅 sdcard_bsp.h 引用） */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SDMMC_HOST_DEFAULT = 0,
} sdmmc_slot_t;

typedef struct {
    int flags;
    int slot;
    int max_freq_khz;
} sdmmc_host_t;

#ifdef __cplusplus
}
#endif
