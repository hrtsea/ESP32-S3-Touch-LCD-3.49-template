/* sim/stubs/nvs_flash.h - NVS Flash stub */
#pragma once
#include "esp_err.h"
#include "nvs.h"

static inline esp_err_t nvs_flash_init(void) { return ESP_OK; }
static inline esp_err_t nvs_flash_init_partition(const char *label) { (void)label; return ESP_OK; }
static inline esp_err_t nvs_flash_erase(void) { return ESP_OK; }
static inline esp_err_t nvs_flash_erase_partition(const char *label) { (void)label; return ESP_OK; }
static inline esp_err_t nvs_flash_deinit(void) { return ESP_OK; }
static inline esp_err_t nvs_flash_deinit_partition(const char *label) { (void)label; return ESP_OK; }
