/* sim/stubs/nvs.h - NVS 存储 stub（所有 get 返回 NOT_FOUND） */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_err.h"

typedef void *nvs_handle_t;
typedef int32_t nvs_iterator_t;

#define NVS_READONLY   0
#define NVS_READWRITE  1
#define NVS_TYPE_U8    0x11
#define NVS_TYPE_I8    0x12
#define NVS_TYPE_U16   0x21
#define NVS_TYPE_I16   0x22
#define NVS_TYPE_U32   0x31
#define NVS_TYPE_I32   0x32
#define NVS_TYPE_U64   0x41
#define NVS_TYPE_I64   0x42
#define NVS_TYPE_STR   0x51
#define NVS_TYPE_BLOB  0x61

static inline esp_err_t nvs_open(const char *name, unsigned int mode, nvs_handle_t *handle) {
    (void)name; (void)mode;
    if (handle) *handle = (nvs_handle_t)1;
    return ESP_ERR_NVS_NOT_FOUND;
}
static inline esp_err_t nvs_open_from_partition(const char *part, const char *name, unsigned int mode, nvs_handle_t *h) {
    (void)part; (void)name; (void)mode; if (h) *h = (nvs_handle_t)1; return ESP_ERR_NVS_NOT_FOUND;
}
static inline void nvs_close(nvs_handle_t handle) { (void)handle; }

static inline esp_err_t nvs_get_u8(nvs_handle_t h, const char *k, uint8_t *out)  { (void)h; (void)k; (void)out; return ESP_ERR_NVS_NOT_FOUND; }
static inline esp_err_t nvs_get_i8(nvs_handle_t h, const char *k, int8_t *out)   { (void)h; (void)k; (void)out; return ESP_ERR_NVS_NOT_FOUND; }
static inline esp_err_t nvs_get_u16(nvs_handle_t h, const char *k, uint16_t *out) { (void)h; (void)k; (void)out; return ESP_ERR_NVS_NOT_FOUND; }
static inline esp_err_t nvs_get_i16(nvs_handle_t h, const char *k, int16_t *out) { (void)h; (void)k; (void)out; return ESP_ERR_NVS_NOT_FOUND; }
static inline esp_err_t nvs_get_u32(nvs_handle_t h, const char *k, uint32_t *out) { (void)h; (void)k; (void)out; return ESP_ERR_NVS_NOT_FOUND; }
static inline esp_err_t nvs_get_i32(nvs_handle_t h, const char *k, int32_t *out) { (void)h; (void)k; (void)out; return ESP_ERR_NVS_NOT_FOUND; }
static inline esp_err_t nvs_get_u64(nvs_handle_t h, const char *k, uint64_t *out) { (void)h; (void)k; (void)out; return ESP_ERR_NVS_NOT_FOUND; }
static inline esp_err_t nvs_get_i64(nvs_handle_t h, const char *k, int64_t *out) { (void)h; (void)k; (void)out; return ESP_ERR_NVS_NOT_FOUND; }
static inline esp_err_t nvs_get_str(nvs_handle_t h, const char *k, char *out, size_t *len) { (void)h; (void)k; (void)out; (void)len; return ESP_ERR_NVS_NOT_FOUND; }
static inline esp_err_t nvs_get_blob(nvs_handle_t h, const char *k, void *out, size_t *len) { (void)h; (void)k; (void)out; (void)len; return ESP_ERR_NVS_NOT_FOUND; }

static inline esp_err_t nvs_set_u8(nvs_handle_t h, const char *k, uint8_t v)   { (void)h; (void)k; (void)v; return ESP_OK; }
static inline esp_err_t nvs_set_i8(nvs_handle_t h, const char *k, int8_t v)    { (void)h; (void)k; (void)v; return ESP_OK; }
static inline esp_err_t nvs_set_u16(nvs_handle_t h, const char *k, uint16_t v) { (void)h; (void)k; (void)v; return ESP_OK; }
static inline esp_err_t nvs_set_i16(nvs_handle_t h, const char *k, int16_t v)  { (void)h; (void)k; (void)v; return ESP_OK; }
static inline esp_err_t nvs_set_u32(nvs_handle_t h, const char *k, uint32_t v) { (void)h; (void)k; (void)v; return ESP_OK; }
static inline esp_err_t nvs_set_i32(nvs_handle_t h, const char *k, int32_t v)  { (void)h; (void)k; (void)v; return ESP_OK; }
static inline esp_err_t nvs_set_u64(nvs_handle_t h, const char *k, uint64_t v) { (void)h; (void)k; (void)v; return ESP_OK; }
static inline esp_err_t nvs_set_i64(nvs_handle_t h, const char *k, int64_t v)  { (void)h; (void)k; (void)v; return ESP_OK; }
static inline esp_err_t nvs_set_str(nvs_handle_t h, const char *k, const char *v) { (void)h; (void)k; (void)v; return ESP_OK; }
static inline esp_err_t nvs_set_blob(nvs_handle_t h, const char *k, const void *v, size_t len) { (void)h; (void)k; (void)v; (void)len; return ESP_OK; }

static inline esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }
static inline esp_err_t nvs_erase_key(nvs_handle_t h, const char *k) { (void)h; (void)k; return ESP_OK; }
static inline esp_err_t nvs_erase_all(nvs_handle_t h) { (void)h; return ESP_OK; }
