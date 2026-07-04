#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

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
void wifi_ip_label_set(const char *text);

#ifdef __cplusplus
}
#endif

#endif