#ifndef UTILS_FORMAT_H
#define UTILS_FORMAT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 将 bps(bits per second) 格式化为紧凑的速率字符串 */
void fmt_bps(char *buf, size_t n, uint32_t bps);

/* 将 kbps(kilobytes/s) 格式化为紧凑的速率字符串，带方向箭头 */
void fmt_bps_arrow(char *buf, size_t n, uint32_t bps, bool is_upload);

#ifdef __cplusplus
}
#endif

#endif /* UTILS_FORMAT_H */
