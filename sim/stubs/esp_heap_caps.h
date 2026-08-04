/* sim/stubs/esp_heap_caps.h - ESP-IDF 堆内存 stub */
#pragma once
#include <stdint.h>
#include <stdlib.h>

#define MALLOC_CAP_DEFAULT    (1 << 0)
#define MALLOC_CAP_8BIT       (1 << 1)
#define MALLOC_CAP_32BIT      (1 << 2)
#define MALLOC_CAP_DMA        (1 << 3)
#define MALLOC_CAP_SPIRAM     (1 << 4)
#define MALLOC_CAP_INTERNAL   (1 << 5)
#define MALLOC_CAP_DEFAULT_8BIT  (1 << 6)

static inline void *heap_caps_malloc(size_t size, uint32_t caps) { (void)caps; return malloc(size); }
static inline void *heap_caps_calloc(size_t n, size_t sz, uint32_t caps) { (void)caps; return calloc(n, sz); }
static inline void *heap_caps_realloc(void *p, size_t sz, uint32_t caps) { (void)caps; return realloc(p, sz); }
static inline void  heap_caps_free(void *p) { free(p); }
static inline size_t heap_caps_get_free_size(uint32_t caps) { (void)caps; return 1024 * 1024 * 32; }
static inline size_t heap_caps_get_largest_free_block(uint32_t caps) { (void)caps; return 1024 * 1024 * 4; }
static inline size_t heap_caps_get_minimum_free_size(uint32_t caps) { (void)caps; return 1024 * 1024 * 8; }
