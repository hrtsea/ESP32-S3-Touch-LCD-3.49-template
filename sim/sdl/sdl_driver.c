/* sim/sdl/sdl_driver.c - SDL2 显示驱动实现
 *
 * 渲染流程：
 *   LVGL flush_cb → SDL_UpdateTexture(RGB565 纹理) → SDL_RenderCopy(2x 缩放) → SDL_RenderPresent
 *
 * 输入：
 *   SDL_MOUSEMOTION/SDL_MOUSEBUTTONDOWN/UP → lv_indev 鼠标坐标更新
 *   SDL_QUIT → 返回 false，主循环退出
 *
 * 配色：黑色背景（640x172 在 1280x344 窗口中居中显示）
 */
#include "sdl_driver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "lvgl.h"

static SDL_Window   *s_window   = NULL;
static SDL_Renderer *s_renderer  = NULL;
static SDL_Texture  *s_texture  = NULL;
static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t   *s_buf1     = NULL;
static lv_color_t   *s_buf2     = NULL;
static lv_indev_t   *s_mouse     = NULL;
static bool          s_mouse_pressed = false;
static int16_t       s_mouse_x = 0;
static int16_t       s_mouse_y = 0;

/* LVGL flush 回调：全屏双缓冲 + SDL_UpdateTexture 避免画面撕裂
 *
 * LVGL 使用 LV_DISP_RENDER_MODE_DIRECT 模式（buffer 为全屏大小），
 * 每帧仅调用一次 flush_cb。SDL_UpdateTexture 内部处理 pitch，
 * 避免 SDL_LockTexture 返回的整行 pitch 与 LVGL 紧凑排列不匹配。
 */
static void sdl_disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *colors)
{
    (void)drv;
    if (!area || !colors || !s_texture) {
        if (drv && drv->draw_buf) lv_disp_flush_ready(drv);
        return;
    }

    SDL_Rect rect = {
        .x = area->x1,
        .y = area->y1,
        .w = area->x2 - area->x1 + 1,
        .h = area->y2 - area->y1 + 1,
    };

    /* SDL_UpdateTexture 自动处理 pitch，避免手动 LockTexture 的 stride 问题
     * 第二参数是每行字节数 = 区域宽度 × 每像素字节数（RGB565 = 2 字节） */
    int pitch = rect.w * (int)sizeof(lv_color_t);
    SDL_UpdateTexture(s_texture, &rect, colors, pitch);

    /* 每帧仅调用一次 RenderPresent，彻底消除撕裂 */
    SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, 255);
    SDL_RenderClear(s_renderer);
    SDL_Rect dst = { .x = 0, .y = 0, .w = SIM_HOR_RES * SIM_SCALE, .h = SIM_VER_RES * SIM_SCALE };
    SDL_RenderCopy(s_renderer, s_texture, NULL, &dst);
    SDL_RenderPresent(s_renderer);

    lv_disp_flush_ready(drv);
}

/* LVGL 鼠标读回调 */
static void sdl_mouse_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    data->point.x = s_mouse_x;
    data->point.y = s_mouse_y;
    data->state = s_mouse_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

int sdl_driver_init(void)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    /* 创建窗口：物理尺寸 = 屏幕尺寸 × SIM_SCALE */
    s_window = SDL_CreateWindow(
        "NAS Monitor Simulator (640x172)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SIM_HOR_RES * SIM_SCALE, SIM_VER_RES * SIM_SCALE,
        SDL_WINDOW_SHOWN);
    if (!s_window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return -1;
    }

    s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!s_renderer) {
        /* 失败后回退到软件渲染 */
        s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!s_renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return -1;
    }
    SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, 255);
    SDL_RenderClear(s_renderer);

    /* RGB565 纹理：与 LVGL LV_COLOR_DEPTH 16 完全一致 */
    s_texture = SDL_CreateTexture(s_renderer,
        SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
        SIM_HOR_RES, SIM_VER_RES);
    if (!s_texture) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        return -1;
    }

    /* 分配 LVGL 全屏双 framebuffer（DIRECT 模式：每帧仅一次 flush_cb） */
    size_t buf_pixels = SIM_HOR_RES * SIM_VER_RES;
    s_buf1 = malloc(buf_pixels * sizeof(lv_color_t));
    s_buf2 = malloc(buf_pixels * sizeof(lv_color_t));
    if (!s_buf1 || !s_buf2) {
        fprintf(stderr, "malloc draw buffer failed (need %zu bytes each)\n",
                buf_pixels * sizeof(lv_color_t));
        return -1;
    }
    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2, buf_pixels);

    /* 注册 LVGL display driver */
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = SIM_HOR_RES;
    disp_drv.ver_res = SIM_VER_RES;
    disp_drv.draw_buf = &s_draw_buf;
    disp_drv.flush_cb = sdl_disp_flush_cb;
    disp_drv.antialiasing = 1;
    lv_disp_drv_register(&disp_drv);

    /* 注册 LVGL 鼠标 indev */
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = sdl_mouse_read_cb;
    s_mouse = lv_indev_drv_register(&indev_drv);

    fprintf(stdout, "[sdl] window %dx%d (scale=%d), RGB565, mouse indev=%p\n",
            SIM_HOR_RES, SIM_VER_RES, SIM_SCALE, (void *)s_mouse);
    return 0;
}

bool sdl_driver_poll(void)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            return false;
        case SDL_MOUSEMOTION:
            s_mouse_x = (int16_t)(ev.motion.x / SIM_SCALE);
            s_mouse_y = (int16_t)(ev.motion.y / SIM_SCALE);
            if (s_mouse_x < 0) s_mouse_x = 0;
            if (s_mouse_y < 0) s_mouse_y = 0;
            if (s_mouse_x >= SIM_HOR_RES) s_mouse_x = SIM_HOR_RES - 1;
            if (s_mouse_y >= SIM_VER_RES) s_mouse_y = SIM_VER_RES - 1;
            break;
        case SDL_MOUSEBUTTONDOWN:
            s_mouse_pressed = true;
            s_mouse_x = (int16_t)(ev.button.x / SIM_SCALE);
            s_mouse_y = (int16_t)(ev.button.y / SIM_SCALE);
            fprintf(stderr, "[sdl] BUTTONDOWN at %d,%d\n", s_mouse_x, s_mouse_y);
            fflush(stderr);
            break;
        case SDL_MOUSEBUTTONUP:
            s_mouse_pressed = false;
            fprintf(stderr, "[sdl] BUTTONUP at %d,%d\n", s_mouse_x, s_mouse_y);
            fflush(stderr);
            break;
        case SDL_KEYDOWN:
            if (ev.key.keysym.sym == SDLK_ESCAPE) {
                return false;
            }
            break;
        default:
            break;
        }
    }
    return true;
}

void sdl_driver_deinit(void)
{
    if (s_texture)  { SDL_DestroyTexture(s_texture);  s_texture = NULL; }
    if (s_renderer) { SDL_DestroyRenderer(s_renderer); s_renderer = NULL; }
    if (s_window)   { SDL_DestroyWindow(s_window);    s_window = NULL; }
    if (s_buf1) { free(s_buf1); s_buf1 = NULL; }
    if (s_buf2) { free(s_buf2); s_buf2 = NULL; }
    SDL_Quit();
}
