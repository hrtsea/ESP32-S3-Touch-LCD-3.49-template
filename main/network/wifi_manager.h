#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define WIFI_MAX_SCAN_AP 16
typedef struct {
    char    ssid[33];
    int8_t  rssi;
    uint8_t auth;
} wifi_scan_ap_t;

extern wifi_scan_ap_t g_wifi_scan[WIFI_MAX_SCAN_AP];
extern bool     g_wifi_connected;
extern char     g_wifi_curr_ssid[33];
extern uint8_t  g_wifi_last_reason;
extern int8_t   g_wifi_last_rssi;
extern uint32_t g_wifi_connect_started_ms;
extern bool     g_wifi_scanning;
extern uint16_t g_wifi_scan_n;

void wifi_manager_init(void);
void wifi_start_scan(void);
void wifi_connect(const char *ssid, const char *pass);

typedef void (*wifi_status_cb_t)(bool connected, const char *ip_addr);
void wifi_manager_register_status_cb(wifi_status_cb_t cb);
const char *wifi_reason_str(uint8_t reason);

bool     wifi_is_connected(void);
void     wifi_get_curr_ssid(char *buf, size_t buf_len);
uint8_t  wifi_get_last_reason(void);
int8_t   wifi_get_last_rssi(void);
uint32_t wifi_get_connect_started_ms(void);
bool     wifi_is_scanning(void);
uint16_t wifi_get_scan_count(void);
const wifi_scan_ap_t *wifi_get_scan_ap(uint16_t idx);

#ifdef __cplusplus
}
#endif

#endif
