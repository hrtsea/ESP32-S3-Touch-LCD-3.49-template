/* sim/stubs/esp_system.h - ESP-IDF 系统 stub */
#pragma once
#include <stdint.h>
#include <time.h>

/* esp_log_timestamp 定义在 esp_log.h（避免重复定义） */

static inline void esp_restart(void) { exit(0); }
static inline uint32_t esp_get_free_heap_size(void) { return 1024 * 1024 * 32; }
static inline uint32_t esp_get_free_internal_heap_size(void) { return 1024 * 1024 * 16; }
static inline uint32_t esp_get_minimum_free_heap_size(void) { return 1024 * 1024 * 8; }
static inline const char *esp_get_idf_version(void) { return "v5.5.4-sim"; }
static inline void esp_chip_info(void *info) { (void)info; }

#include <stdlib.h>
