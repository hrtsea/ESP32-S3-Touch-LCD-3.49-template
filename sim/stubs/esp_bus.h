/* sim/stubs/esp_bus.h - esp_bus stub（空） */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*esp_bus_evt_fn)(const char *event, const void *data, size_t len, void *ctx);

/* 无 esp_bus 实现 */
static inline void esp_bus_init(void) {}
static inline int esp_bus_subscribe(const char *event, void *cb, void *ctx) {
    (void)event; (void)cb; (void)ctx; return 0;
}
static inline int esp_bus_sub(const char *pattern, esp_bus_evt_fn handler, void *ctx) {
    (void)pattern; (void)handler; (void)ctx; return 0;
}
static inline int esp_bus_connect(const char *evt, const char *action, void *data, size_t len) {
    (void)evt; (void)action; (void)data; (void)len; return 0;
}

#ifdef __cplusplus
}
#endif
