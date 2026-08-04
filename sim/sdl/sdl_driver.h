/* sim/sdl/sdl_driver.h - PC SDL2 显示驱动接口
 *
 * 在 PC 上模拟 640x172 RGB565 显示屏，使用 SDL2 创建窗口并
 * 将 LVGL framebuffer 渲染到 SDL 纹理。鼠标坐标按 SIM_SCALE
 * 缩放后传递给 LVGL 的 indev，模拟触摸输入。
 *
 * 使用 MinGW + SDL2 静态链接，无需安装额外运行时。
 */
#ifndef SIM_SDL_DRIVER_H
#define SIM_SDL_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 屏幕 UX 分辨率（与硬件一致） */
#define SIM_HOR_RES   640
#define SIM_VER_RES   172

/* 显示窗口缩放系数（鼠标坐标反向除以该值映射回 LVGL 坐标） */
#define SIM_SCALE     2

/* 初始化 SDL 窗口 + LVGL display driver + 鼠标 indev。
 * 成功返回 0，失败返回 -1。 */
int sdl_driver_init(void);

/* 处理 SDL 事件队列（鼠标移动、按键、窗口关闭）。
 * 收到 SDL_QUIT 时返回 false，主循环应退出。 */
bool sdl_driver_poll(void);

/* 关闭 SDL，释放资源 */
void sdl_driver_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* SIM_SDL_DRIVER_H */
