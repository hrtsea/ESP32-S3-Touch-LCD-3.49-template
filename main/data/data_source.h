#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "nas_data.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NasTypeConfig {
    NasType type;
    const char* default_ip;
    uint32_t default_port;
    const char* default_user;
    bool need_password;
    bool need_apiurl;
    bool need_snmp;
    bool need_serial;
} NasTypeConfig;

struct NasTypeEntry {
    const char* id;
    const char* display_name;
    NasType nas_type_enum;
    bool implemented;
};

static const struct NasTypeEntry NAS_TYPES[] = {
    {"synology",     "Synology DSM",   NAS_SYNOLOGY,    true},
    {"qnap",         "QNAP QTS",       NAS_QNAP,        true},
    {"truenas",      "TrueNAS",        NAS_TRUENAS,     true},
    {"fnos",         "FNOS",           NAS_FNOS,        true},
    {"unraid",       "Unraid",         NAS_UNRAID,      true},
    {"netdata",      "Netdata",        NET_NETDATA,     true},
    {"snmp",         "SNMP",           NET_SNMP,        true},
    {"linux_http",   "Linux (HTTP)",   NET_LINUX_HTTP,  true},
    {"linux_serial", "Linux (Serial)", NET_LINUX_SERIAL,true},
    {"windows",      "Windows",        NET_WINDOWS,     true},
    {"mock",         "Mock (测试)",    NAS_MOCK,        true},
};

static const int DATA_TYPE_COUNT = sizeof(NAS_TYPES) / sizeof(NAS_TYPES[0]);

bool data_source_init(const char* nas_type_id);
bool data_source_connect(void);
void data_source_disconnect(void);
bool data_source_poll(void);
bool data_source_is_connected(void);
const NasData* data_source_get_data(void);
const char* data_source_get_type_name(void);
const char* data_source_get_conn_icon(void);
bool data_source_switch(const char* nas_type_id);

float data_source_get_rx_speed_mbps(void);
float data_source_get_tx_speed_mbps(void);

const char* get_display_type_name(const char* nas_type_id);
NasType nas_type_from_string(const char* nas_type_id);
const char* nas_type_to_string(NasType type);
struct NasTypeConfig nas_type_config_get_defaults(NasType type);
struct NasTypeConfig nas_type_config_get_defaults_by_id(const char* type_id);

#ifdef __cplusplus
}
#endif