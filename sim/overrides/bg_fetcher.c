/* sim/overrides/bg_fetcher.c - 后台图片抓取器 no-op */
#include "bg_fetcher.h"

void bg_fetcher_ensure(void) {
    /* PC 模拟器无网络背景图抓取 */
}
