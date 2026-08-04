/* sim/stubs/esp_sntp.h - SNTP stub（仅 sntp_manager.c 使用，已 override） */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*sntp_sync_time_cb_t)(struct timeval *tv);
typedef int sntp_operatingmode_t;
typedef void *sntp_impl_t;

static inline void sntp_setoperatingmode(sntp_operatingmode_t mode) { (void)mode; }
static inline void sntp_setservername(int idx, const char *name) { (void)idx; (void)name; }
static inline void sntp_init(void) {}
static inline void sntp_stop(void) {}

#ifdef __cplusplus
}
#endif
