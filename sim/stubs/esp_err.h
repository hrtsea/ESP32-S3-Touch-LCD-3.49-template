/* sim/stubs/esp_err.h - ESP-IDF 错误码 stub */
#pragma once
#include <stdint.h>

typedef int esp_err_t;

#define ESP_OK          0
#define ESP_FAIL        -1
#define ESP_ERR_NO_MEM  0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FOUND 0x104
#define ESP_ERR_NOT_SUPPORTED 0x106

/* NVS 错误码 */
#define ESP_ERR_NVS_NOT_FOUND       0x1104
#define ESP_ERR_NVS_NOT_INITIALIZED 0x1101
#define ESP_ERR_NVS_NO_FREE_PAGES  0x1102
#define ESP_ERR_NVS_NEW_VERSION_FOUND 0x1103

#define ESP_ERROR_CHECK(x) do { esp_err_t __err_rc = (x); if (__err_rc != ESP_OK) { fprintf(stderr, "ESP_ERROR_CHECK failed: %d\n", __err_rc); } } while(0)

#include <stdio.h>
