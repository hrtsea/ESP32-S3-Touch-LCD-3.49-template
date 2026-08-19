#include "data_source.h"
#include "client/mock_client.h"
#include "client/api_client.h"
#include "client/synology_client.h"
#include "client/netdata_client.h"
#include "client/truenas_client.h"
#include "client/qnap_client.h"
#include "client/serial_client.h"
#include "client/snmp_client.h"
#include "client/unraid_client.h"
#include "esp_log.h"
#include "event_bus.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "data_source";

const NasTypeEntry NAS_TYPES[] = {
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

const int DATA_TYPE_COUNT = sizeof(NAS_TYPES) / sizeof(NAS_TYPES[0]);

static const NasTypeConfig s_nas_type_configs[] = {
    {NAS_SYNOLOGY,     "192.168.1.100", 5000,   "admin", true,  true,  false, false},
    {NAS_QNAP,         "192.168.1.100", 8080,   "admin", true,  true,  false, false},
    {NAS_TRUENAS,      "192.168.1.100", 80,     "root",  true,  true,  false, false},
    {NAS_FNOS,         "192.168.1.100", 3000,   "",      false, true,  false, false},
    {NAS_UNRAID,       "192.168.1.100", 80,     "",      true,  false, false, false},
    {NET_LINUX_HTTP,   "192.168.1.100", 8099,   "",      false, false, false, false},
    {NET_LINUX_SERIAL, "/dev/ttyUSB0",  115200, "",      false, false, false, true},
    {NET_NETDATA,      "192.168.1.100", 19999,  "",      false, true,  false, false},
    {NET_SNMP,         "192.168.1.100", 161,    "",      false, false, true,  false},
    {NET_WINDOWS,      "192.168.1.100", 0,      "admin", true,  false, false, false},
    {NAS_MOCK,         "",              0,      "",      false, false, false, false},
};
static const int s_nas_type_configs_count = sizeof(s_nas_type_configs) / sizeof(s_nas_type_configs[0]);

NasTypeConfig nas_type_config_get_defaults(NasType type)
{
    for (int i = 0; i < s_nas_type_configs_count; i++) {
        if (s_nas_type_configs[i].type == type) {
            return s_nas_type_configs[i];
        }
    }
    NasTypeConfig empty = {0};
    return empty;
}

NasTypeConfig nas_type_config_get_defaults_by_id(const char* type_id)
{
    NasType type = nas_type_from_string(type_id);
    return nas_type_config_get_defaults(type);
}

const char* get_display_type_name(const char* nas_type_id)
{
    for (int i = 0; i < DATA_TYPE_COUNT; i++) {
        if (strcmp(NAS_TYPES[i].id, nas_type_id) == 0) {
            return NAS_TYPES[i].display_name;
        }
    }
    return "Unknown";
}

NasType nas_type_from_string(const char* nas_type_id)
{
    if (nas_type_id == NULL) return NET_LINUX_HTTP;
    for (int i = 0; i < DATA_TYPE_COUNT; i++) {
        if (strcmp(NAS_TYPES[i].id, nas_type_id) == 0) {
            return NAS_TYPES[i].nas_type_enum;
        }
    }
    return NET_LINUX_HTTP;
}

const char* nas_type_to_string(NasType type)
{
    for (int i = 0; i < DATA_TYPE_COUNT; i++) {
        if (NAS_TYPES[i].nas_type_enum == type) {
            return NAS_TYPES[i].id;
        }
    }
    return "linux_http";
}

static DataSource* g_data_source = NULL;

static DataSource* ds_create_by_type(NasType type)
{
    switch (type) {
        case NAS_SYNOLOGY:     return synology_client_create();
        case NAS_QNAP:         return qnap_client_create();
        case NAS_TRUENAS:      return truenas_client_create();
        case NAS_FNOS:         return mock_client_create_with_type(NAS_FNOS, "FNOS", "wifi");
        case NAS_UNRAID:       return unraid_client_create();
        case NET_NETDATA:      return netdata_client_create();
        case NET_SNMP:         return snmp_client_create();
        case NET_LINUX_HTTP:   return api_client_create(NET_LINUX_HTTP);
        case NET_LINUX_SERIAL: return serial_client_create();
        case NET_WINDOWS:      return api_client_create(NET_WINDOWS);
        case NAS_MOCK:         return mock_client_create();
        default:               return NULL;
    }
}

DataSource* data_source_create(const char* nas_type_id)
{
    for (int i = 0; i < DATA_TYPE_COUNT; i++) {
        if (strcmp(NAS_TYPES[i].id, nas_type_id) == 0) {
            return ds_create_by_type(NAS_TYPES[i].nas_type_enum);
        }
    }

    ESP_LOGW(TAG, "Unsupported type: %s, fallback to mock", nas_type_id);
    return mock_client_create();
}

static bool ds_create_and_init(const char* nas_type_id)
{
    g_data_source = data_source_create(nas_type_id);
    if (g_data_source == NULL) {
        ESP_LOGE(TAG, "Failed to create data source for type: %s", nas_type_id);
        return false;
    }

    if (!ds_init(g_data_source)) {
        ESP_LOGE(TAG, "Failed to init data source");
        ds_destroy(g_data_source);
        g_data_source = NULL;
        return false;
    }

    return true;
}

bool data_source_init(const char* nas_type_id)
{
    if (g_data_source != NULL) {
        ESP_LOGW(TAG, "Data source already initialized, switch first");
        return false;
    }

    return ds_create_and_init(nas_type_id);
}

bool data_source_connect(void)
{
    if (g_data_source == NULL) return false;
    return ds_connect(g_data_source);
}

void data_source_disconnect(void)
{
    if (g_data_source == NULL) return;
    ds_disconnect(g_data_source);
}

bool data_source_poll(void)
{
    if (g_data_source == NULL) return false;
    return ds_poll(g_data_source);
}

bool data_source_is_connected(void)
{
    if (g_data_source == NULL) return false;
    return ds_is_connected(g_data_source);
}

const NasData* data_source_get_data(void)
{
    if (g_data_source == NULL) return NULL;
    return ds_get_data(g_data_source);
}

const char* data_source_get_type_name(void)
{
    if (g_data_source == NULL) return "None";
    return ds_get_type_name(g_data_source);
}

const char* data_source_get_conn_icon(void)
{
    if (g_data_source == NULL) return "none";
    return ds_get_conn_icon(g_data_source);
}

bool data_source_switch(const char* nas_type_id)
{
    if (g_data_source != NULL) {
        ESP_LOGI(TAG, "Switching from %s, disconnecting...", ds_get_type_name(g_data_source));
        ds_disconnect(g_data_source);
        ds_destroy(g_data_source);
        g_data_source = NULL;
    }

    if (nas_type_id == NULL || strlen(nas_type_id) == 0 ||
        strcmp(nas_type_id, "none") == 0) {
        ESP_LOGI(TAG, "Data source cleared (no type specified)");
        return true;
    }

    ESP_LOGI(TAG, "Creating new data source for type: %s", nas_type_id);
    if (!ds_create_and_init(nas_type_id)) {
        return false;
    }

    if (!ds_connect(g_data_source)) {
        ESP_LOGW(TAG, "Failed to connect data source for type: %s", nas_type_id);
        return false;
    }

    return true;
}
