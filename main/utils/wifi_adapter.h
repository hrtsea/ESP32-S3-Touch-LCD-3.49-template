#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_wifi_config.h"

typedef wifi_scan_result_t wifi_scan_ap_t;

const char *wifi_cfg_get_current_ssid(char *buf, size_t buf_size);

void wifi_cfg_clear_credentials(void);

void wifi_start_provisioning(void);

uint16_t wifi_cfg_get_scan_count(void);

const wifi_scan_ap_t *wifi_cfg_get_scan_ap(uint16_t idx);

void wifi_cfg_scan_adapter(void);

bool wifi_has_credentials(void);

void wifi_connect(const char *ssid, const char *password);

void wifi_disconnect(void);

void wifi_start_scan(void);

bool wifi_is_connected(void);

uint32_t wifi_get_connect_started_ms(void);

void wifi_set_last_reason(uint8_t reason);

uint8_t wifi_get_last_reason(void);
