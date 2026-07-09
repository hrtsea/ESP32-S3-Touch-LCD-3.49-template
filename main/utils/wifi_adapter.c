#include "wifi_adapter.h"
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"

static wifi_scan_result_t s_scan_results[32];
static size_t s_scan_count = 0;
static uint32_t s_connect_started_ms = 0;
static uint8_t s_last_reason = 0;

const char *wifi_cfg_get_current_ssid(char *buf, size_t buf_size) {
    wifi_status_t status;
    if (wifi_cfg_get_status(&status) == ESP_OK) {
        strncpy(buf, status.ssid, buf_size - 1);
        buf[buf_size - 1] = '\0';
        return buf;
    }
    buf[0] = '\0';
    return buf;
}

void wifi_cfg_clear_credentials(void) {
    wifi_cfg_factory_reset();
}

void wifi_start_provisioning(void) {
    wifi_cfg_start_ap(NULL);
}

uint16_t wifi_cfg_get_scan_count(void) {
    return (uint16_t)s_scan_count;
}

const wifi_scan_ap_t *wifi_cfg_get_scan_ap(uint16_t idx) {
    if (idx >= s_scan_count) return NULL;
    return &s_scan_results[idx];
}

void wifi_cfg_scan_adapter(void) {
    s_scan_count = 0;
    wifi_cfg_scan(s_scan_results, sizeof(s_scan_results) / sizeof(s_scan_results[0]), &s_scan_count);
}

bool wifi_has_credentials(void) {
    wifi_network_t networks[8];
    size_t count = 0;
    wifi_cfg_list_networks(networks, sizeof(networks) / sizeof(networks[0]), &count);
    return count > 0;
}

void wifi_connect(const char *ssid, const char *password) {
    s_connect_started_ms = esp_timer_get_time() / 1000;
    wifi_network_t network = {0};
    strncpy(network.ssid, ssid, sizeof(network.ssid) - 1);
    if (password && *password) {
        strncpy(network.password, password, sizeof(network.password) - 1);
    }
    network.priority = 10;
    wifi_cfg_add_network(&network);
    wifi_cfg_connect(ssid);
}

void wifi_disconnect(void) {
    wifi_cfg_disconnect();
}

void wifi_start_scan(void) {
    wifi_cfg_scan_adapter();
}

bool wifi_is_connected(void) {
    return wifi_cfg_is_connected();
}

uint32_t wifi_get_connect_started_ms(void) {
    return s_connect_started_ms;
}

void wifi_set_last_reason(uint8_t reason) {
    s_last_reason = reason;
}

uint8_t wifi_get_last_reason(void) {
    return s_last_reason;
}

