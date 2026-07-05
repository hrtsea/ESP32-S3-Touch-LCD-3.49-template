#include "data_source.h"
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static NasData g_nas_data;
static bool g_data_source_initialized = false;

static void generate_mock_data(void)
{
    memset(&g_nas_data, 0, sizeof(NasData));

    strlcpy(g_nas_data.system.hostname, "Mock NAS", sizeof(g_nas_data.system.hostname));
    strlcpy(g_nas_data.system.model, "ZotLab Z6", sizeof(g_nas_data.system.model));
    g_nas_data.system.uptime_s = time(NULL);
    g_nas_data.system.cpu_pct = 25.0f + (float)(rand() % 30);
    g_nas_data.system.ram_pct = 45.0f + (float)(rand() % 20);
    g_nas_data.system.ram_total_mb = 32768;
    g_nas_data.system.ram_used_mb = (uint32_t)(g_nas_data.system.ram_pct * g_nas_data.system.ram_total_mb / 100.0f);
    g_nas_data.system.ram_free_mb = g_nas_data.system.ram_total_mb - g_nas_data.system.ram_used_mb;
    g_nas_data.system.temp_cpu = 35 + (rand() % 15);
    g_nas_data.system.temp_sys = 33 + (rand() % 12);
    g_nas_data.system.cpu_core_count = 8;
    for (int i = 0; i < g_nas_data.system.cpu_core_count && i < MAX_CPU_CORES; i++) {
        g_nas_data.system.cpu_cores[i] = 20.0f + (float)(rand() % 40);
    }

    g_nas_data.disk_count = 4;
    for (int i = 0; i < g_nas_data.disk_count; i++) {
        snprintf(g_nas_data.disks[i].name, sizeof(g_nas_data.disks[i].name), "Disk %d", i + 1);
        snprintf(g_nas_data.disks[i].device, sizeof(g_nas_data.disks[i].device), "/dev/sd%c", 'a' + i);
        strlcpy(g_nas_data.disks[i].model_name, "SSD 4TB", sizeof(g_nas_data.disks[i].model_name));
        snprintf(g_nas_data.disks[i].mount, sizeof(g_nas_data.disks[i].mount), "/mnt/disk%d", i + 1);
        strlcpy(g_nas_data.disks[i].disk_type, "SSD", sizeof(g_nas_data.disks[i].disk_type));
        g_nas_data.disks[i].temp = 30 + (rand() % 10);
        g_nas_data.disks[i].health = HEALTH_OK;
        g_nas_data.disks[i].size_gb = 4096;
        g_nas_data.disks[i].used_gb = 1024 + (rand() % 2048);
        g_nas_data.disks[i].used_pct = (uint8_t)(g_nas_data.disks[i].used_gb * 100 / g_nas_data.disks[i].size_gb);
        g_nas_data.disks[i].read_kbps = rand() % 10000;
        g_nas_data.disks[i].write_kbps = rand() % 5000;
    }

    g_nas_data.volume_count = 1;
    strlcpy(g_nas_data.volumes[0].name, "Volume1", sizeof(g_nas_data.volumes[0].name));
    g_nas_data.volumes[0].total_gb = 12288;
    g_nas_data.volumes[0].used_gb = 5120 + (rand() % 3000);
    g_nas_data.volumes[0].used_pct = (uint8_t)(g_nas_data.volumes[0].used_gb * 100 / g_nas_data.volumes[0].total_gb);
    strlcpy(g_nas_data.volumes[0].raid, "RAID 5", sizeof(g_nas_data.volumes[0].raid));
    strlcpy(g_nas_data.volumes[0].status, "Healthy", sizeof(g_nas_data.volumes[0].status));

    strlcpy(g_nas_data.network.interface, "eth0", sizeof(g_nas_data.network.interface));
    strlcpy(g_nas_data.network.ip, g_config.nas_ip, sizeof(g_nas_data.network.ip));
    g_nas_data.network.rx_bps = (uint32_t)((rand() % 100) * 1000000);
    g_nas_data.network.tx_bps = (uint32_t)((rand() % 50) * 1000000);

    g_nas_data.interface_count = 1;
    strlcpy(g_nas_data.interfaces[0].name, "eth0", sizeof(g_nas_data.interfaces[0].name));
    strlcpy(g_nas_data.interfaces[0].ip, g_config.nas_ip, sizeof(g_nas_data.interfaces[0].ip));
    g_nas_data.interfaces[0].rx_bps = g_nas_data.network.rx_bps;
    g_nas_data.interfaces[0].tx_bps = g_nas_data.network.tx_bps;
    g_nas_data.interfaces[0].active = true;
    g_nas_data.active_interface_idx = 0;

    g_nas_data.fan.rpm = 1500 + (rand() % 500);
    g_nas_data.fan.pwm_pct = 30 + (rand() % 20);
    g_nas_data.fan.ctrl_temp = g_nas_data.system.temp_cpu;
    g_nas_data.fan.stall_alarm = false;
    g_nas_data.fan.enabled = true;

    g_nas_data.last_update_ms = (uint32_t)(time(NULL) * 1000);
    g_nas_data.is_online = true;
    g_nas_data.has_update = false;
}

bool data_source_init(const char* nas_type_id)
{
    (void)nas_type_id;
    if (!g_data_source_initialized) {
        generate_mock_data();
        g_data_source_initialized = true;
    }
    return true;
}

bool data_source_connect(void)
{
    return true;
}

void data_source_disconnect(void)
{
}

bool data_source_poll(void)
{
    generate_mock_data();
    return true;
}

bool data_source_is_connected(void)
{
    return g_data_source_initialized;
}

const NasData* data_source_get_data(void)
{
    if (!g_data_source_initialized) {
        generate_mock_data();
        g_data_source_initialized = true;
    }
    return &g_nas_data;
}

const char* data_source_get_type_name(void)
{
    return g_config.nas_type;
}

const char* data_source_get_conn_icon(void)
{
    return "wifi";
}

bool data_source_switch(const char* nas_type_id)
{
    (void)nas_type_id;
    g_data_source_initialized = false;
    return data_source_init(nas_type_id);
}

float data_source_get_rx_speed_mbps(void)
{
    if (!g_data_source_initialized) {
        generate_mock_data();
        g_data_source_initialized = true;
    }
    return (float)g_nas_data.network.rx_bps / 8000000.0f;
}

float data_source_get_tx_speed_mbps(void)
{
    if (!g_data_source_initialized) {
        generate_mock_data();
        g_data_source_initialized = true;
    }
    return (float)g_nas_data.network.tx_bps / 8000000.0f;
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
    if (strcmp(nas_type_id, "synology") == 0) return NAS_SYNOLOGY;
    if (strcmp(nas_type_id, "qnap") == 0) return NAS_QNAP;
    if (strcmp(nas_type_id, "truenas") == 0) return NAS_TRUENAS;
    if (strcmp(nas_type_id, "fnos") == 0) return NAS_FNOS;
    if (strcmp(nas_type_id, "unraid") == 0) return NAS_UNRAID;
    if (strcmp(nas_type_id, "netdata") == 0) return NET_NETDATA;
    if (strcmp(nas_type_id, "snmp") == 0) return NET_SNMP;
    if (strcmp(nas_type_id, "linux_http") == 0) return NET_LINUX_HTTP;
    if (strcmp(nas_type_id, "linux_serial") == 0) return NET_LINUX_SERIAL;
    if (strcmp(nas_type_id, "windows") == 0) return NET_WINDOWS;
    if (strcmp(nas_type_id, "mock") == 0) return NAS_MOCK;
    return NET_LINUX_HTTP;
}

const char* nas_type_to_string(NasType type)
{
    switch (type) {
        case NAS_SYNOLOGY: return "synology";
        case NAS_QNAP: return "qnap";
        case NAS_TRUENAS: return "truenas";
        case NAS_FNOS: return "fnos";
        case NAS_UNRAID: return "unraid";
        case NET_NETDATA: return "netdata";
        case NET_SNMP: return "snmp";
        case NET_LINUX_HTTP: return "linux_http";
        case NET_LINUX_SERIAL: return "linux_serial";
        case NET_WINDOWS: return "windows";
        case NAS_MOCK: return "mock";
        default: return "linux_http";
    }
}

struct NasTypeConfig nas_type_config_get_defaults(NasType type)
{
    static const struct NasTypeConfig defaults[] = {
        {NAS_SYNOLOGY,     "192.168.1.100", 5000,  "admin", true,  true,  false, false},
        {NAS_QNAP,         "192.168.1.100", 8080,  "admin", true,  true,  false, false},
        {NAS_TRUENAS,      "192.168.1.100", 80,    "root",  true,  true,  false, false},
        {NAS_FNOS,         "192.168.1.100", 3000,  "",      false, true,  false, false},
        {NAS_UNRAID,       "192.168.1.100", 80,    "root",  true,  false, false, false},
        {NET_LINUX_HTTP,   "192.168.1.100", 8099,  "",      false, false, false, false},
        {NET_LINUX_SERIAL, "/dev/ttyUSB0",  115200,"",      false, false, false, true},
        {NET_NETDATA,      "192.168.1.100", 19999, "",      false, true,  false, false},
        {NET_SNMP,         "192.168.1.100", 161,   "",      false, false, true,  false},
        {NET_WINDOWS,      "192.168.1.100", 0,     "admin", true,  false, false, false},
        {NAS_MOCK,         "",              0,     "",      false, false, false, false},
    };

    for (int i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++) {
        if (defaults[i].type == type) {
            return defaults[i];
        }
    }
    return (struct NasTypeConfig){0};
}

struct NasTypeConfig nas_type_config_get_defaults_by_id(const char* type_id)
{
    NasType type = nas_type_from_string(type_id);
    return nas_type_config_get_defaults(type);
}