/* sim/stubs/bsp/sdcard_bsp.h - SD 卡 stub */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float sdcard_size;
} sdcard_bsp_t;

extern sdcard_bsp_t user_sdcard_bsp;
extern volatile bool sdcard_busy;

void _sdcard_init(void);
esp_err_t sdcard_file_write(const char *path, const char *data);
esp_err_t sdcard_file_read(const char *path, char *buffer, size_t *out_len);
esp_err_t sdcard_format(void);
bool sdcard_is_mounted(void);

#ifdef __cplusplus
}
#endif
