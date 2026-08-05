/* main/utils/format.c
 *
 * 统一的速率格式化函数：将 bps(bits per second) 转换为紧凑的可读字符串。
 *
 * 规则：
 *   1. 先转成 KB/s：bps / 8 / 1024
 *   2. < 1 KB/s          → "xB/s"        (x = bytes/s = bps/8)
 *   3. < 1024 KB/s       → "xKB/s"       (整数 KB/s)
 *   4. >= 1024 KB/s      → "x.xMB/s"     (1 位小数)
 *   5. >= 1024*1024 KB/s → "x.xGB/s"     (1 位小数)
 *
 * fmt_bps_arrow 在前面加 "^ "(上传) 或 "v "(下载) 方向箭头。
 */

#include "format.h"
#include <stdio.h>

void fmt_bps(char *buf, size_t n, uint32_t bps)
{
    if (!buf || n == 0) return;

    /* 转 KB/s（整数除法） */
    uint32_t kbs = bps / 8U / 1024U;

    if (kbs < 1U) {
        /* 不足 1 KB/s，按字节/秒显示 */
        uint32_t bs = bps / 8U;
        snprintf(buf, n, "%luB/s", (unsigned long)bs);
    } else if (kbs < 1024U) {
        /* 1 ~ 1023 KB/s，整数显示 */
        snprintf(buf, n, "%luKB/s", (unsigned long)kbs);
    } else if (kbs < 1024U * 1024U) {
        /* 1 MB/s ~ 1023 MB/s，1 位小数 */
        snprintf(buf, n, "%.1fMB/s", (double)kbs / 1024.0);
    } else {
        /* >= 1 GB/s，1 位小数 */
        snprintf(buf, n, "%.1fGB/s", (double)kbs / (1024.0 * 1024.0));
    }
}

void fmt_bps_arrow(char *buf, size_t n, uint32_t bps, bool is_upload)
{
    if (!buf || n == 0) return;

    /* 箭头 + 速率（留出前缀空间） */
    char tmp[16];
    fmt_bps(tmp, sizeof(tmp), bps);
    snprintf(buf, n, "%s %s", is_upload ? "^" : "v", tmp);
}
