/* sim/main.c - PC LVGL 模拟器入口
 *
 * 复用项目所有 UI 源码（main/ui/, main/utils/, main/data/, main/config/），
 * 用 SDL2 替换 LCD 硬件、用内存版配置/事件总线替换 FreeRTOS/NVS 实现，
 * 用 mock_client 动态生成 NAS 数据，在 Windows PC 上预览全部 UI 屏幕。
 *
 * 启动流程：
 *   config_load → app_cfg_init → event_bus_init → lv_init → sdl_driver_init →
 *   ui_init(Boot) → data_source_init(mock) → data_source_connect →
 *   ui_events_start_time_timer (时钟显示) → ui_events_start_dim_timer →
 *   ui_events_start_tile_monitor
 *
 * 主循环：
 *   sdl_driver_poll (鼠标/退出) → lv_tick_inc → lv_timer_handler →
 *   按 poll_sec 周期：data_source_poll → event_bus_publish_nas_data →
 *   sim_update_screens (直接调用屏幕 update 函数)
 *
 * 编译：参见 sim/CMakeLists.txt
 * 运行：./nas_monitor_sim.exe
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

/* 不使用 SDL2main 的 WinMain 入口转换：用户 main 直接作为 console 入口。
 * 这样 -mconsole 下无需链接 libSDL2main.a，避免静态库链接顺序导致的
 * "undefined reference to WinMain" 问题。 */
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include "lvgl.h"

/* ESP-IDF stubs */
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"

/* 项目配置 */
#include "config.h"
#include "app_cfg.h"
#include "event_bus.h"
#include "disp_driver.h"
#include "data_source.h"
#include "nas_data.h"
#include "fan_control.h"
#include "ui_helpers.h"
#include "ui_events.h"
#include "ui.h"

/* 屏幕 update API */
#include "screens/ui_Screen_Overview.h"
#include "screens/ui_Screen_Storage.h"
#include "screens/ui_Screen_DiskDetail.h"
#include "screens/ui_Screen_SystemDetail.h"
#include "screens/ui_Screen_NetDetail.h"

/* WiFi adapter（用于读取当前 IP） */
#include "wifi_adapter.h"

/* SDL 后端 */
#include "sdl/sdl_driver.h"

/* ui_events.c 中非头文件暴露的辅助函数 */
extern void ui_events_start_time_timer(void);

static const char *TAG = "sim";

/* 模拟器数据驱动：将 mock 数据推送到所有屏幕的 update 函数 */
static void sim_update_screens(const NasData *data)
{
    if (!data || !data->is_online) return;

    /* Overview 屏幕 */
    if (ui_Screen_Overview != NULL) {
        overview_screen_update_cpu((int)data->system.cpu_pct);
        overview_screen_update_temp(data->system.temp_cpu);
        overview_screen_update_mem((int)data->system.ram_pct);
        overview_screen_update_disk((int)data->system.disk_pct);

        uint8_t total_slots = config_get_total_disk_slots();
        for (int i = 0; i < total_slots; i++) {
            if (i < data->disk_slot_count) {
                const NasDiskInfo *disk = &data->disks[i];
                overview_screen_update_hdd_led(i, disk->online, disk->health);
                overview_screen_update_hdd_name(i, disk->name);
            }
        }

        overview_screen_update_network((int)(data->network.tx_bps / 1000),
                                       (int)(data->network.rx_bps / 1000));
    }

    /* Storage 屏幕 */
    if (ui_Screen_Storage != NULL) {
        for (int i = 0; i < data->disk_slot_count; i++) {
            const NasDiskInfo *disk = &data->disks[i];
            storage_screen_update_hdd_name(i, disk->name);
            storage_screen_update_hdd_bar(i, (int)disk->used_pct, disk->health);
            storage_screen_update_hdd_temp(i, disk->temp);
            storage_screen_update_hdd_online(i, disk->online);
        }
        storage_screen_update_network((int)(data->network.tx_bps / 1000),
                                      (int)(data->network.rx_bps / 1000));
    }

    /* 详情屏（仅当用户进入时存在） */
    if (ui_Screen_DiskDetail != NULL) {
        ui_Screen_DiskDetail_update_data(data);
        ui_Screen_DiskDetail_update_network((int)(data->network.tx_bps / 1000),
                                            (int)(data->network.rx_bps / 1000));
    }
    if (ui_Screen_SystemDetail != NULL) {
        ui_Screen_SystemDetail_update_data(data);
        ui_Screen_SystemDetail_update_network((int)(data->network.tx_bps / 1000),
                                              (int)(data->network.rx_bps / 1000));
    }

    /* NetDetail 屏幕（仅当用户进入时存在） */
    if (ui_Screen_NetDetail != NULL) {
        netdetail_screen_update_network(data->network.tx_bps, data->network.rx_bps);
    }

    /* WiFi 状态/IP 显示 */
    char ip_buf[16] = {0};
    wifi_cfg_get_current_ip(ip_buf, sizeof(ip_buf));
    if (ui_Screen_Overview) overview_screen_update_ip(ip_buf);
    if (ui_Screen_Storage)  storage_screen_update_ip(ip_buf);
    if (ui_Screen_DiskDetail) ui_Screen_DiskDetail_update_ip(ip_buf);
    if (ui_Screen_SystemDetail) ui_Screen_SystemDetail_update_ip(ip_buf);
    if (ui_Screen_NetDetail) netdetail_screen_update_ip(ip_buf);
    if (ui_Screen_Overview) overview_screen_update_wifi(wifi_cfg_is_connected());
    if (ui_Screen_Storage)  storage_screen_update_wifi(wifi_cfg_is_connected());
    if (ui_Screen_DiskDetail) ui_Screen_DiskDetail_update_wifi(wifi_cfg_is_connected());
    if (ui_Screen_SystemDetail) ui_Screen_SystemDetail_update_wifi(wifi_cfg_is_connected());
    if (ui_Screen_NetDetail) netdetail_screen_update_wifi(wifi_cfg_is_connected());

    /* 推送温度给风扇控制 */
    fan_control_on_nas_data(data);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    /* 告知 SDL2 我们已自行处理 main 入口（SDL_MAIN_HANDLED），
     * 避免内部 main-ready 标志未设置导致 SDL_Init 失败。 */
    SDL_SetMainReady();

    fprintf(stdout, "================================================\n");
    fprintf(stdout, "  NAS Monitor LVGL Simulator (PC)\n");
    fprintf(stdout, "  Screen: %dx%d, scale=%dx, RGB565\n",
            SIM_HOR_RES, SIM_VER_RES, SIM_SCALE);
    fprintf(stdout, "  ESP-IDF stubs + SDL2 backend + mock data\n");
    fprintf(stdout, "================================================\n");

    /* 1. 配置初始化（内存版，nvs stub 返回 NOT_FOUND） */
    config_load();
    g_config.poll_sec = 1;  /* 测试：1 秒轮询，加速 sparkline 更新 */
    ESP_LOGI(TAG, "config loaded: nas_type=%s sata=%u m2=%u poll=%us",
             g_config.nas_type, g_config.sata_disk_count,
             g_config.m2_disk_count, g_config.poll_sec);

    /* 2. 应用配置（g_cfg，内存版） */
    app_cfg_init();
    /* 关闭 FPS 显示 */
    g_cfg.show_fps = 0;

    /* 3. 事件总线（同步版） */
    event_bus_init();

    /* 4. LVGL 核心 + SDL 显示驱动（disp_driver_init 内部调用 sdl_driver_init） */
    lv_init();
    disp_driver_init();

    /* 5. UI 初始化（Boot 屏 + FPS 计时器） */
    ui_init();

    /* 6. 启动时间/变暗/Tile 监控定时器（不启动 ui_events 任务） */
    ui_events_start_time_timer();
    ui_events_start_dim_timer();
    ui_events_start_tile_monitor();

    /* 7. 数据源：mock client */
    if (!data_source_init("mock")) {
        ESP_LOGE(TAG, "data_source_init(mock) failed");
        return 1;
    }
    if (!data_source_connect()) {
        ESP_LOGE(TAG, "data_source_connect failed");
        return 1;
    }
    ESP_LOGI(TAG, "mock data source connected");

    /* 8. 风扇控制初始化（内存版） */
    fan_control_init();

    /* 9. 主循环 */
    uint32_t last_poll_ms = 0;
    uint32_t poll_interval_ms = (uint32_t)g_config.poll_sec * 1000U;
    if (poll_interval_ms == 0) poll_interval_ms = 5000U;

    while (1) {
        /* SDL 事件处理（鼠标、退出） */
        if (!sdl_driver_poll()) {
            ESP_LOGI(TAG, "SDL quit received, exiting");
            break;
        }

        /* LVGL 定时器（驱动 Boot 进度、FPS、时钟显示）
         * 时基由 LV_TICK_CUSTOM 直接读取 SDL_GetTicks()，无需 lv_tick_inc。 */
        lv_timer_handler();

        /* 周期性拉取 mock 数据并更新屏幕 */
        uint32_t now = lv_tick_get();
        if (now - last_poll_ms >= poll_interval_ms) {
            last_poll_ms = now;
            if (data_source_poll()) {
                const NasData *data = data_source_get_data();
                if (data && data->is_online) {
                    /* 发布事件（同步通知订阅者，若有） */
                    event_bus_publish_nas_data(data);
                    /* 直接驱动屏幕（绕过未启动的 ui_events 任务） */
                    sim_update_screens(data);
                    ESP_LOGI(TAG, "data polled: cpu=%.1f%% mem=%.1f%% temp=%d disk_count=%u",
                             data->system.cpu_pct, data->system.ram_pct,
                             data->system.temp_cpu, data->disk_count);
                }
            } else {
                ESP_LOGW(TAG, "data_source_poll failed");
            }
        }

        /* 5ms 延迟（约 200fps 上限，降低 CPU 占用） */
        SDL_Delay(5);
    }

    /* 清理 */
    sdl_driver_deinit();
    data_source_disconnect();
    ESP_LOGI(TAG, "simulator exited cleanly");
    return 0;
}
