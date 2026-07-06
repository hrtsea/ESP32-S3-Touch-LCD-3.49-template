#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_NAME           "ZotLab NAS Monitor"
#define APP_HOSTNAME       "NAS Monitor"
#define APP_SERIAL_PREFIX  "<< " APP_NAME " >>"
#define APP_VERSION        "1.1.0"

#define NAS_LOGO           "ZotLab"
#define NAS_TYPE           "Z6"
#define APP_SUBTITLE       "System Monitor"

#ifndef TFT_HEIGHT
#define TFT_HEIGHT          172
#endif

#ifndef TFT_WIDTH
#define TFT_WIDTH           640
#endif

#define SERIAL_STX             0x02
#define SERIAL_ETX             0x03
#define SERIAL_BUF_SIZE        2048
#define SERIAL_TIMEOUT_MS      3000

#define MAX_DISKS              16
#define MAX_VOLUMES            8
#define MAX_SERVICES           16
#define MAX_CPU_CORES          8
#define NET_HISTORY_POINTS     30

#define STATUS_BAR_H           24
#define PAGE_DOT_H             8
#define CONTENT_H              (TFT_HEIGHT - STATUS_BAR_H - PAGE_DOT_H)

#define TOTAL_PAGES            7

#define COLOR_OK        0x07E0
#define COLOR_WARN      0xFFE0
#define COLOR_DANGER    0xF800
#define COLOR_INFO      0x07FF
#define COLOR_BG        0x0000
#define COLOR_TEXT      0xFFFF
#define COLOR_DIM       0x4208

#ifdef __cplusplus
}
#endif