/**
 * @file ui_main.c
 * @brief UI主模块 - 负责顶层UI构建、TileView管理、背光控制和状态管理
 * 
 * 本模块是整个UI系统的入口，负责：
 * - 创建和管理TileView（6个页面的滑动容器）
 * - 管理背光自动调光功能
 * - 显示FPS和WiFi状态信息
 * - 处理屏幕旋转和手势操作
 * - 协调各子UI模块（时钟、行情、设置、收音机、录音器、演示页）
 */

#include "ui_main.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "lcd_bl_pwm_bsp.h"

#include "ui_clock.h"
#include "ui_quotes.h"
#include "ui_radio.h"
#include "ui_settings.h"
#include "ui_recorder.h"
#include "ui_hello.h"
#include "ui_audio_test.h"
#include "audio_min.h"
#include "wifi_manager.h"

static const char *TAG = "ui_main";

static void build_main_ui(const char *status_text);

/* ---------------------- 全局变量定义 ---------------------- */

lv_obj_t      *g_tileview       = NULL;     /* TileView实例，管理6个页面 */
lv_timer_t    *g_status_timer   = NULL;     /* 状态更新定时器（1Hz） */
char           g_status_text[256];          /* 状态文本缓存，用于旋转重绘 */

uint32_t       g_last_activity_ms = 0;       /* 最后活动时间戳（用于自动调光） */
int            g_dim_state = 0;             /* 调光状态（0=正常, 1=变暗, 2=关闭） */
uint32_t       g_last_scroll_ms = 0;         /* 最后滚动时间戳 */
uint32_t       g_menu_input_block_until_ms = 0; /* 菜单输入阻塞截止时间 */

static lv_timer_t *g_dim_timer = NULL;      /* 自动调光定时器（1Hz） */

static lv_obj_t   *g_ip_label = NULL;        /* 底部IP地址标签（显示WiFi连接状态） */

/* ---------------------- IP标签UI ---------------------- */

/**
 * @brief 确保IP标签已创建
 * 
 * 如果g_ip_label为空，则创建一个位于屏幕底部左侧的半透明标签
 */
static void ip_label_ensure(void)
{
    if (g_ip_label) return;
    g_ip_label = lv_label_create(lv_layer_top());
    lv_label_set_text(g_ip_label, "");
    lv_obj_set_style_text_color(g_ip_label, lv_color_make(0xa0, 0xa0, 0xa0), 0);
    lv_obj_set_style_text_font(g_ip_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_bg_color(g_ip_label, lv_color_make(0x00, 0x00, 0x00), 0);
    lv_obj_set_style_bg_opa(g_ip_label, LV_OPA_50, 0);
    lv_obj_set_style_pad_left(g_ip_label, 4, 0);
    lv_obj_set_style_pad_right(g_ip_label, 4, 0);
    lv_obj_set_style_pad_top(g_ip_label, 1, 0);
    lv_obj_set_style_pad_bottom(g_ip_label, 1, 0);
    lv_obj_set_style_radius(g_ip_label, 3, 0);
    lv_obj_align(g_ip_label, LV_ALIGN_BOTTOM_LEFT, 2, -2);
    lv_obj_add_flag(g_ip_label, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief WiFi状态变化回调函数
 * 
 * 当WiFi连接状态变化时，更新底部IP标签的显示
 * 
 * @param connected 是否已连接
 * @param ip_addr IP地址字符串
 */
static void wifi_status_cb(bool connected, const char *ip_addr)
{
    if (!lvgl_lock(50)) return;
    ip_label_ensure();
    if (connected && ip_addr && *ip_addr) {
        lv_label_set_text(g_ip_label, ip_addr);
        lv_obj_clear_flag(g_ip_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_ip_label, LV_OBJ_FLAG_HIDDEN);
    }
    lvgl_unlock();
}

/* ---------------------- 状态更新定时器 ---------------------- */

/**
 * @brief 状态更新定时器回调（1Hz）
 * 
 * 更新以下UI元素：
 * - 时钟页面的WiFi/BT图标颜色
 * - 设置页面的WiFi状态文本（连接中/已连接/断开原因）
 */
static void status_timer_cb(lv_timer_t *t)
{
    (void)t;

    char ssid_buf[33];
    wifi_get_curr_ssid(ssid_buf, sizeof(ssid_buf));

    /* 更新时钟页面的WiFi图标颜色 */
    if (wifi_is_connected()) {
        clock_set_wifi_icon_color(lv_color_make(0x80, 0xff, 0x80));
    } else if (ssid_buf[0]) {
        clock_set_wifi_icon_color(lv_color_make(0xff, 0xa0, 0x40));
    } else {
        clock_set_wifi_icon_color(lv_color_make(0x40, 0x40, 0x40));
    }

    /* 更新时钟页面的BT图标颜色（暂时禁用，显示为灰色） */
    clock_set_bt_icon_color(lv_color_make(0x40, 0x40, 0x40));

    /* 更新设置页面的WiFi状态文本 */
    if (g_set_wifi_status) {
        if (wifi_is_connected()) {
            lv_label_set_text_fmt(g_set_wifi_status, LV_SYMBOL_OK " %s",
                                  ssid_buf);
        } else if (ssid_buf[0]) {
            uint32_t elapsed = lv_tick_elaps(wifi_get_connect_started_ms());
            uint8_t reason = wifi_get_last_reason();
            if (reason) {
                lv_label_set_text_fmt(g_set_wifi_status,
                                      LV_SYMBOL_WARNING " %s: %s",
                                      ssid_buf,
                                      wifi_reason_str(reason));
            } else if (elapsed > 15000) {
                lv_label_set_text_fmt(g_set_wifi_status,
                                      LV_SYMBOL_WARNING " %s: timed out",
                                      ssid_buf);
            } else {
                lv_label_set_text_fmt(g_set_wifi_status,
                                      tr(I18N_WIFI_CONNECTING_N),
                                      ssid_buf,
                                      (unsigned)(elapsed / 1000));
            }
        } else {
            lv_label_set_text(g_set_wifi_status, tr(I18N_WIFI_NOT_CONN));
        }
    }
}

/* ---------------------- 背光控制与自动调光 ---------------------- */

/**
 * @brief 应用背光亮度
 * 
 * 通过PWM设置背光亮度，亮度值0-255，实际PWM占空比取反
 * 
 * @param bri 亮度值（0=最暗，255=最亮）
 */
void backlight_apply(uint8_t bri)
{
    setUpduty((uint16_t)(0xFF - bri));
}

/**
 * @brief 用户活动触发函数
 * 
 * 当用户触摸屏幕时重置活动时间戳，并恢复正常亮度
 * 
 * @param e LVGL事件参数
 */
static void activity_kick(lv_event_t *e)
{
    (void)e;
    g_last_activity_ms = lv_tick_get();
    if (g_dim_state != 0) {
        g_dim_state = 0;
        backlight_apply(g_cfg.brightness);
    }
}

/**
 * @brief 自动调光定时器回调（1Hz）
 * 
 * 根据空闲时间判断是否需要进入变暗或关闭状态：
 * - 空闲时间 >= off_s秒 → 关闭背光（状态2）
 * - 空闲时间 >= dim_s秒 → 变暗背光（状态1）
 * - 其他 → 正常亮度（状态0）
 * 
 * @param t LVGL定时器参数
 */
static void dim_timer_cb(lv_timer_t *t)
{
    (void)t;
    uint32_t idle_ms = lv_tick_elaps(g_last_activity_ms);
    int want = 0;

    if (g_cfg.off_s > 0 && idle_ms >= (uint32_t)g_cfg.off_s * 1000) {
        want = 2;
    } else if (g_cfg.dim_s > 0 && idle_ms >= (uint32_t)g_cfg.dim_s * 1000) {
        want = 1;
    }

    if (want != g_dim_state) {
        g_dim_state = want;
        if (want == 0) {
            backlight_apply(g_cfg.brightness);
        } else if (want == 1) {
            backlight_apply(g_cfg.brightness / 8 + 4);
        } else {
            backlight_apply(0);
        }
    }
}

/* ---------------------- FPS显示定时器 ---------------------- */

/**
 * @brief FPS计算和显示定时器回调（3秒）
 * 
 * 计算最近3秒内的平均帧率，并更新FPS标签显示
 * 
 * @param t LVGL定时器参数
 */
void fps_timer_cb(lv_timer_t *t)
{
    (void)t;
    static uint32_t last_tick = 0;
    uint32_t now = lv_tick_get();
    uint32_t dt  = now - last_tick;

    if (dt == 0) return;

    uint32_t frames = disp_driver_get_and_reset_fps_frames();
    last_tick = now;

    uint32_t fps_x10 = (frames * 10000U) / dt;

    lv_obj_t *fps_lbl = disp_driver_get_fps_label();
    if (fps_lbl) {
        lv_label_set_text_fmt(fps_lbl, "FPS %lu.%lu",
                              (unsigned long)(fps_x10 / 10),
                              (unsigned long)(fps_x10 % 10));
    }

    static uint32_t print_div = 0;
    if ((print_div++ & 3) == 0) {
        ESP_LOGI(TAG, "fps=%lu.%lu  (frames=%lu in %lu ms)",
                 (unsigned long)(fps_x10 / 10), (unsigned long)(fps_x10 % 10),
                 (unsigned long)frames, (unsigned long)dt);
    }
}

/**
 * @brief 屏幕旋转按钮事件回调
 * 
 * 循环切换屏幕旋转角度（0°→90°→180°→270°→0°），并重建整个UI
 * 
 * @param e LVGL事件参数
 */
void rotate_btn_event_cb(lv_event_t *e)
{
    (void)e;

    /* 循环切换旋转状态 */
    int new_rot = (disp_driver_get_rot_state() + 1) & 3;
    disp_driver_set_rot_state(new_rot);

    /* 根据旋转状态切换画布尺寸 */
    if (new_rot == 0 || new_rot == 2) {
        disp_driver_set_canvas_size(172, 640);
    } else {
        disp_driver_set_canvas_size(640, 172);
    }

    /* 更新显示驱动分辨率 */
    extern void disp_driver_update_resolution(void);
    disp_driver_update_resolution();

    /* 清空当前屏幕，重置所有UI指针 */
    lv_obj_clean(lv_scr_act());
    disp_driver_set_fps_label(NULL);
    g_hello_play_btn_label = NULL;
    g_tileview = NULL;

    /* 重置时钟页面 */
    clock_cleanup();

    /* 重置设置页面指针 */
    g_set_wifi_status = NULL;
    g_set_wifi_list   = NULL;
    g_set_kb_overlay  = NULL;
    g_set_kb_ta       = NULL;

    /* 重置行情页面 */
    quotes_cleanup();

    /* 重置收音机页面指针 */
    g_radio_status_lbl = NULL; g_radio_now_lbl = NULL;
    g_radio_btn_lbl = NULL; g_radio_list = NULL; g_radio_vol_lbl = NULL;

    /* 重置录音器页面指针 */
    g_rec_status = NULL; g_rec_btn_lbl = NULL;
    g_rec_vu_l = NULL; g_rec_vu_r = NULL;
    g_rec_list_overlay = NULL; g_rec_list = NULL; g_rec_overlay_status = NULL;

    /* 删除所有定时器（clock 定时器已在 clock_cleanup 中处理） */
    if (g_dim_timer)      { lv_timer_del(g_dim_timer);      g_dim_timer      = NULL; }
    if (g_status_timer)   { lv_timer_del(g_status_timer);   g_status_timer   = NULL; }
    if (g_radio_poll_timer) { lv_timer_del(g_radio_poll_timer); g_radio_poll_timer = NULL; }
    if (g_rec_poll) { lv_timer_del(g_rec_poll); g_rec_poll = NULL; }

    /* 直接调用build_main_ui（已在LVGL任务中，无需再次获取锁） */
    build_main_ui(g_status_text);

    ESP_LOGI(TAG, "rotate -> %d deg  canvas=%dx%d",
             new_rot * 90, disp_driver_get_canvas_w(), disp_driver_get_canvas_h());
}

/* ---------------------- TileView手势处理器 ---------------------- */

/**
 * @brief TileView循环滑动手势回调
 * 
 * 实现循环滚动：在最后一页向左滑动跳转到第0页，在第0页向右滑动跳转到最后一页
 * 
 * @param e LVGL事件参数
 */
static void tileview_gesture_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;

    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    lv_obj_t *tv  = lv_event_get_target(e);
    lv_coord_t x = lv_obj_get_scroll_x(tv);
    lv_coord_t w = lv_obj_get_width(tv);
    int idx = (w > 0) ? (x + w / 2) / w : 0;
    const int last = N_TILES - 1;

    if (dir == LV_DIR_LEFT && idx == last) {
        lv_obj_set_tile_id(tv, 0, 0, LV_ANIM_OFF);
    } else if (dir == LV_DIR_RIGHT && idx == 0) {
        lv_obj_set_tile_id(tv, last, 0, LV_ANIM_OFF);
    }
}

/**
 * @brief TileView提交阈值回调
 * 
 * 按住TileView直到X方向偏移超过20像素才确认切换页面，否则回弹到当前页面
 * 小屏幕上降低阈值便于操作
 * 
 * @param e LVGL事件参数
 */
static void tileview_commit_cb(lv_event_t *e)
{
    static lv_coord_t s_press_x = 0;
    static lv_coord_t s_locked_x = 0;
    static bool       s_committed = false;

    lv_event_code_t c = lv_event_get_code(e);
    lv_obj_t *tv = lv_event_get_target(e);

    if (c == LV_EVENT_PRESSED) {
        lv_indev_t *id = lv_indev_get_act();
        lv_point_t p; lv_indev_get_point(id, &p);
        s_press_x = p.x;
        s_locked_x = lv_obj_get_scroll_x(tv);
        s_committed = false;
    } else if (c == LV_EVENT_SCROLL && !s_committed) {
        lv_indev_t *id = lv_indev_get_act();
        if (!id) return;
        lv_point_t p; lv_indev_get_point(id, &p);
        int dx = (int)p.x - (int)s_press_x;
        if (dx > 20 || dx < -20) {
            s_committed = true;
        } else {
            lv_obj_scroll_to_x(tv, s_locked_x, LV_ANIM_OFF);
        }
    } else if (c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST) {
        s_committed = false;
    }
}

/**
 * @brief 屏幕滚动时间戳回调
 * 
 * 在SCROLL事件发生时记录时间戳，用于实现"先滚动后点击"的抑制逻辑
 * 
 * @param e LVGL事件参数
 */
static void screen_scroll_stamp_cb(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_SCROLL) {
        g_last_scroll_ms = lv_tick_get();
    }
}

static int s_current_tile_idx = 0;

static void tile_monitor_cb(lv_timer_t *t)
{
    (void)t;
    if (!g_tileview) return;
    lv_coord_t x = lv_obj_get_scroll_x(g_tileview);
    lv_coord_t w = lv_obj_get_width(g_tileview);
    int idx = (w > 0) ? (x + w / 2) / w : 0;
    if (idx < 0) idx = 0;
    if (idx >= N_TILES) idx = N_TILES - 1;
    if (idx != s_current_tile_idx) {
        int old = s_current_tile_idx;
        s_current_tile_idx = idx;
        if (old == 4) recorder_tile_on_leave();
        if (idx == 4) recorder_tile_on_enter();
    }
}

/* ---------------------- 顶层UI构建器 ---------------------- */

/**
 * @brief 构建主UI界面
 * 
 * 创建TileView并添加6个页面：时钟、行情、设置、收音机、录音器、演示页
 * 设置各种事件回调和定时器
 * 
 * @param status_text 状态文本（显示在演示页）
 */
static void build_main_ui(const char *status_text)
{
    static bool fps_timer_created = false;

    /* 获取当前屏幕并设置背景 */
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* 创建TileView */
    g_tileview = lv_tileview_create(scr);
    lv_obj_set_size(g_tileview, disp_driver_get_canvas_w(), disp_driver_get_canvas_h());
    lv_obj_set_style_bg_color(g_tileview, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_tileview, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(g_tileview, LV_SCROLLBAR_MODE_OFF);

    /* 创建7个页面Tile */
    lv_obj_t *t_clock  = lv_tileview_add_tile(g_tileview, 0, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_t *t_quotes = lv_tileview_add_tile(g_tileview, 1, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_t *t_set    = lv_tileview_add_tile(g_tileview, 2, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_t *t_radio  = lv_tileview_add_tile(g_tileview, 3, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_t *t_record = lv_tileview_add_tile(g_tileview, 4, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_t *t_audio_test = lv_tileview_add_tile(g_tileview, 5, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_t *t_hello  = lv_tileview_add_tile(g_tileview, 6, 0, LV_DIR_LEFT | LV_DIR_RIGHT);

    (void)status_text;

    /* 构建各页面内容 */
    build_clock_tile(t_clock);
    build_quotes_tile(t_quotes);
    build_settings_tile(t_set);
    build_radio_tile(t_radio);
    build_recorder_tile(t_record);
    build_audio_test_tile(t_audio_test);
    build_hello_tile(t_hello, g_status_text);

    /* 设置TileView循环滑动手势 */
    lv_obj_add_event_cb(g_tileview, tileview_gesture_cb, LV_EVENT_GESTURE, NULL);

    /* 设置iOS风格提交阈值 */
    lv_obj_add_event_cb(g_tileview, tileview_commit_cb, LV_EVENT_ALL, NULL);

    /* 创建FPS显示标签（浮动在所有页面之上） */
    lv_obj_t *fps_lbl = lv_label_create(scr);
    disp_driver_set_fps_label(fps_lbl);
    lv_label_set_text(fps_lbl, "FPS --");
    lv_obj_set_style_text_color(fps_lbl, lv_color_make(0x00, 0xff, 0x80), 0);
    lv_obj_set_style_text_font(fps_lbl, i18n_font(), 0);
    lv_obj_set_style_bg_color(fps_lbl, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(fps_lbl, LV_OPA_60, 0);
    lv_obj_set_style_pad_hor(fps_lbl, 3, 0);
    lv_obj_align(fps_lbl, LV_ALIGN_TOP_LEFT, 4, 4);
    lv_obj_clear_flag(fps_lbl, LV_OBJ_FLAG_CLICKABLE);
    if (!g_cfg.show_fps) lv_obj_add_flag(fps_lbl, LV_OBJ_FLAG_HIDDEN);

    /* 创建FPS定时器（只创建一次） */
    if (!fps_timer_created) {
        lv_timer_create(fps_timer_cb, 3000, NULL);
        fps_timer_created = true;
    }

    /* 创建状态更新定时器 */
    if (!g_status_timer) {
        g_status_timer = lv_timer_create(status_timer_cb, 1000, NULL);
    }

    /* 创建Tile切换监控定时器（用于页面enter/leave回调） */
    static bool tile_monitor_created = false;
    if (!tile_monitor_created) {
        lv_timer_create(tile_monitor_cb, 100, NULL);
        tile_monitor_created = true;
    }

    /* 设置活动唤醒回调（触摸屏幕时重置调光定时器） */
    lv_obj_add_event_cb(scr, activity_kick, LV_EVENT_PRESSED,  NULL);
    lv_obj_add_event_cb(scr, activity_kick, LV_EVENT_RELEASED, NULL);

    /* 设置滚动时间戳回调（用于滚动后点击抑制） */
    lv_obj_add_event_cb(scr, screen_scroll_stamp_cb, LV_EVENT_ALL, NULL);

    /* 初始化活动时间戳和调光定时器 */
    g_last_activity_ms = lv_tick_get();
    if (!g_dim_timer) {
        g_dim_timer = lv_timer_create(dim_timer_cb, 1000, NULL);
    }

    /* 默认显示时钟页面 */
    lv_obj_set_tile_id(g_tileview, 0, 0, LV_ANIM_OFF);
}

/**
 * @brief 显示主UI界面（带锁保护）
 * 
 * 这是UI的入口函数，会获取LVGL互斥锁后调用build_main_ui
 * 
 * @param status_text 状态文本（缓存用于旋转重绘）
 */
void show_main_ui(const char *status_text)
{
    /* 缓存状态文本用于旋转重绘 */
    if (status_text != g_status_text) {
        strncpy(g_status_text, status_text, sizeof(g_status_text) - 1);
        g_status_text[sizeof(g_status_text) - 1] = '\0';
    }

    /* 注册WiFi状态回调 */
    wifi_manager_register_status_cb(wifi_status_cb);

    /* 获取LVGL互斥锁并构建UI */
    if (!lvgl_lock(-1)) return;
    build_main_ui(g_status_text);
    lvgl_unlock();

    /* 初始化音频测试模块的worker任务 */
    audio_test_ui_init();
}
