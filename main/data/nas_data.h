#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "../config/config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum NasType {
    NAS_SYNOLOGY,
    NAS_QNAP,
    NAS_TRUENAS,
    NAS_FNOS,
    NAS_UNRAID,
    NET_LINUX_HTTP,
    NET_LINUX_SERIAL,
    NET_WINDOWS,
    NET_NETDATA,
    NET_SNMP,
    NAS_MOCK,
    NAS_TYPE_ENUM_COUNT
} NasType;

typedef enum HealthStatus {
    HEALTH_OK,
    HEALTH_WARNING,
    HEALTH_CRITICAL,
    HEALTH_UNKNOWN
} HealthStatus;

typedef struct NasSystemInfo {
    char hostname[32];
    char model[32];
    uint32_t uptime_s;
    float cpu_pct;
    float ram_pct;
    uint32_t ram_total_mb;
    uint32_t ram_used_mb;
    uint32_t ram_free_mb;
    uint32_t ram_cached_mb;
    uint32_t swap_total_mb;
    uint32_t swap_used_mb;
    int16_t temp_cpu;
    int16_t temp_sys;
    float cpu_cores[MAX_CPU_CORES];
    uint8_t cpu_core_count;
    float load_avg[3];
} NasSystemInfo;

typedef struct NasDiskInfo {
    char name[16];
    char device[32];
    char model_name[32];
    char mount[32];
    char disk_type[8];
    int16_t temp;
    HealthStatus health;
    uint32_t size_gb;
    uint32_t used_gb;
    uint8_t used_pct;
    uint32_t read_kbps;
    uint32_t write_kbps;
} NasDiskInfo;

typedef struct NasVolumeInfo {
    char name[32];
    uint32_t total_gb;
    uint32_t used_gb;
    uint8_t used_pct;
    char raid[16];
    char status[16];
} NasVolumeInfo;

typedef struct NasServiceInfo {
    char name[32];
    bool running;
    bool is_docker;
} NasServiceInfo;

#define MAX_NETWORK_INTERFACES 4

typedef struct NasNetworkInfo {
    char interface[16];
    char ip[16];
    uint32_t rx_bps;
    uint32_t tx_bps;
} NasNetworkInfo;

typedef struct NasInterfaceInfo {
    char name[16];
    char ip[16];
    uint32_t rx_bps;
    uint32_t tx_bps;
    bool active;
} NasInterfaceInfo;

typedef struct FanStatus {
    uint16_t rpm;
    uint8_t  pwm_pct;
    int16_t  ctrl_temp;
    bool     stall_alarm;
    bool     enabled;
} FanStatus;

typedef struct NasData {
    NasSystemInfo   system;
    NasDiskInfo     disks[MAX_DISKS];
    uint8_t         disk_count;
    NasVolumeInfo   volumes[MAX_VOLUMES];
    uint8_t         volume_count;
    NasServiceInfo  services[MAX_SERVICES];
    uint8_t         service_count;
    NasNetworkInfo  network;
    NasInterfaceInfo interfaces[MAX_NETWORK_INTERFACES];
    uint8_t         interface_count;
    uint8_t         active_interface_idx;
    FanStatus       fan;
    uint32_t        last_update_ms;
    bool            is_online;
    bool            has_update;
} NasData;

#ifdef __cplusplus
}
#endif
