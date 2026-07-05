#pragma once

#include <stdint.h>

#ifndef TFT_HEIGHT
#define TFT_HEIGHT          172
#endif

#ifndef TFT_WIDTH
#define TFT_WIDTH           640
#endif

#define APP_NAME           "ZotLab NAS Monitor"
#define APP_HOSTNAME       "NAS Monitor"
#define APP_SERIAL_PREFIX  "<< " APP_NAME " >>"

#define NAS_LOGO           "ZotLab"
#define NAS_TYPE           "Z6"
#define APP_SUBTITLE       "System Monitor"

#define APP_VERSION        "1.1.0"

#define DEFAULT_HTTP_PORT      8099
#define DEFAULT_SYNOLOGY_PORT  5000
#define DEFAULT_QNAP_PORT     8080
#define DEFAULT_TRUENAS_PORT   80
#define DEFAULT_NETDATA_PORT   19999
#define DEFAULT_SNMP_PORT      161
#define DEFAULT_SERIAL_BAUD    115200

#define DEFAULT_POLL_SEC       5
#define MIN_POLL_SEC           1
#define MAX_POLL_SEC           30

#define SERIAL_STX             0x02
#define SERIAL_ETX             0x03
#define SERIAL_BUF_SIZE        2048
#define SERIAL_TIMEOUT_MS      3000

#define MAX_DISKS              16
#define MAX_VOLUMES            8
#define MAX_SERVICES           16
#define MAX_CPU_CORES          8
#define NET_HISTORY_POINTS     30

#ifndef DEFAULT_SATA_DISK_COUNT
#define DEFAULT_SATA_DISK_COUNT     6
#endif

#ifndef DEFAULT_M2_DISK_COUNT
#define DEFAULT_M2_DISK_COUNT       3
#endif

#define STATUS_BAR_H           24
#define PAGE_DOT_H             8
#define CONTENT_H              (TFT_HEIGHT - STATUS_BAR_H - PAGE_DOT_H)

#define TOTAL_PAGES            7

#define FAN_CURVE_POINTS       5
#define FAN_DEFAULT_HYSTERESIS 3
#define FAN_DEFAULT_MIN_PWM    20
#define FAN_DEFAULT_EMERGENCY  55
#define FAN_DEFAULT_RAMP_MS    2000
#define FAN_DEFAULT_STALL_SEC  5
#define FAN_ENABLED            true

#define COLOR_OK        0x07E0
#define COLOR_WARN      0xFFE0
#define COLOR_DANGER    0xF800
#define COLOR_INFO      0x07FF
#define COLOR_BG        0x0000
#define COLOR_TEXT      0xFFFF
#define COLOR_DIM       0x4208