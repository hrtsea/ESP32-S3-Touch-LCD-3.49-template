/* sim/stubs/esp_netif.h - 网络接口 stub */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_netif_ip_info_t {
    uint32_t ip;
    uint32_t netmask;
    uint32_t gw;
} esp_netif_ip_info_t;

typedef void *esp_netif_handle_t;
typedef int esp_netif_ip6_type_t;

#ifdef __cplusplus
}
#endif
