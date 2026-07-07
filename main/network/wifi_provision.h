#ifndef WIFI_PROVISION_H
#define WIFI_PROVISION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

typedef enum {
    WIFI_PROV_IDLE,
    WIFI_PROV_PROVISIONING,
    WIFI_PROV_CONNECTING,
    WIFI_PROV_CONNECTED,
} wifi_prov_state_t;

#define WIFI_PROV_AP_SSID "NAS-Monitor"
#define WIFI_PROV_AP_PASS "12345678"

void wifi_provision_init(void);
void wifi_provision_start(const char *ap_ssid, const char *ap_pass);
void wifi_provision_stop(void);
wifi_prov_state_t wifi_provision_get_state(void);
void wifi_provision_on_config(const char *ssid, const char *pass);

#ifdef __cplusplus
}
#endif

#endif
