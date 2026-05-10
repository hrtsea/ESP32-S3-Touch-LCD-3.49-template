#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sntp.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "lvgl.h"
#include "esp_lcd_axs15231b.h"
#include "esp_io_expander_tca9554.h"
#include "driver/i2c_master.h"

#include "user_config.h"
#include "i2c_bsp.h"
#include "i2c_equipment.h"
#include "lcd_bl_pwm_bsp.h"
#include "adc_bsp.h"
#include "sdcard_bsp.h"
#include "button_bsp.h"
#include "audio_min.h"
#include "radio.h"
#include "recorder.h"
#include "cli.h"
#include "i18n.h"
#include "landmask.h"
#include "tz_cities.h"

static const char *TAG = "skeleton";

/* Custom JetBrains Mono Bold fonts, generated via lv_font_conv. */
extern "C" const lv_font_t font_jbmono_48;
extern "C" const lv_font_t font_jbmono_64;
extern "C" const lv_font_t font_jbmono_96;

#define LCD_BIT_PER_PIXEL (16)

static SemaphoreHandle_t lvgl_mux        = NULL;
static SemaphoreHandle_t lvgl_flush_semap = NULL;
/* Two DMA-capable strip buffers so the next strip can be transformed
   while the prior strip's SPI DMA is still draining. */
static uint16_t         *lvgl_dma_bufs[2] = { NULL, NULL };

static volatile uint32_t fps_frame_count = 0;
static lv_obj_t         *fps_label       = NULL;
static lv_obj_t         *play_btn_label  = NULL;
static esp_lcd_panel_io_handle_t g_panel_io = NULL;
static esp_lcd_panel_handle_t    g_panel    = NULL;
/* rot_state: 0=0deg, 1=90deg CW, 2=180deg, 3=270deg CW.
   At 0/180 the canvas is portrait (172w x 640h);
   at 90/270 the canvas is landscape (640w x 172h). */
static int               rot_state       = 1;
static int               canvas_w        = UI_CANVAS_W;  /* 640 */
static int               canvas_h        = UI_CANVAS_H;  /* 172 */
static lv_disp_drv_t    *g_disp_drv      = NULL;
static char              g_status_text[256];

/* ---------------------- Settings (NVS-backed) ---------------------- */

typedef struct {
    uint16_t tz_idx;        /* index into k_tz_cities (see tz_cities.h) */
    uint8_t  brightness;    /* 0..255, applied to setUpduty (after invert) */
    uint16_t dim_s;         /* idle seconds before dim */
    uint16_t off_s;         /* idle seconds before backlight off */
    char     last_ssid[33]; /* last connected SSID */
    uint8_t  hour24;        /* 1 = 24-hour, 0 = 12-hour */
    uint8_t  date_fmt;      /* 0=YYYY.MM.DD, 1=DD.MM.YYYY, 2=MM.DD.YYYY */
    uint8_t  show_seconds;  /* 1 = show :SS digits */
    uint8_t  show_ms;       /* 1 = show .mmm digits */
    uint8_t  audio_enable;  /* 1 = audio playback allowed */
    uint8_t  audio_volume;  /* 0..100 */
    uint8_t  theme;         /* 0=dark, 1=light, 2=high-contrast */
    uint8_t  show_fps;      /* 1 = show FPS pill */
    uint8_t  wifi_autoconnect; /* 1 = auto-connect on boot */
    uint8_t  lang;             /* 0=en, 1=zh, 2=ja, 3=ko */
} app_cfg_t;

static app_cfg_t g_cfg = {
    .tz_idx     = TZ_DEFAULT_CITY_INDEX,
    .brightness = 255,
    .dim_s      = 8 * 3600,    /* 8 hours; 0 = never */
    .off_s      = 8 * 3600,    /* 8 hours; 0 = never */
    .last_ssid  = {0},
    .hour24     = 1,
    .date_fmt   = 0,
    .show_seconds = 1,
    .show_ms    = 1,
    .audio_enable = 1,
    .audio_volume = 70,
    .theme      = 0,
    .show_fps   = 1,
    .wifi_autoconnect = 1,
    .lang             = 0,
};

static void cfg_load(void);
static bool menu_input_blocked(void);
static void recorder_refresh_list(void);
static void cfg_save(void);
static void cfg_save_ssid_pass(const char *ssid, const char *pass);
static bool cfg_get_ssid_pass(const char *ssid, char *pass, size_t pass_len);

/* ---------------------- Wi-Fi ---------------------- */

#define WIFI_MAX_SCAN_AP 16
typedef struct {
    char    ssid[33];
    int8_t  rssi;
    uint8_t auth;  /* 0 = open */
} wifi_scan_ap_t;

static wifi_scan_ap_t g_wifi_scan[WIFI_MAX_SCAN_AP];
static uint16_t       g_wifi_scan_n = 0;
static bool           g_wifi_scanning = false;
static bool           g_wifi_inited = false;
static bool           g_wifi_connected = false;
static char           g_wifi_curr_ssid[33] = {0};
static uint8_t        g_wifi_last_reason = 0;     /* esp_wifi disconnect reason */
static int8_t         g_wifi_last_rssi   = 0;
static uint32_t       g_wifi_connect_started_ms = 0;
static bool           g_sntp_started     = false;
static time_t         g_last_sntp_sync   = 0;
static lv_obj_t      *g_clock_wifi_icon = NULL;
static lv_obj_t      *g_clock_bt_icon   = NULL;
static lv_timer_t    *g_status_timer    = NULL;

static void wifi_init_once(void);
static void wifi_start_scan(void);
static void wifi_connect(const char *ssid, const char *pass);
static void sntp_start_once(void);

/* ---------------------- Backlight + auto-dim ---------------------- */

static uint32_t g_last_activity_ms = 0;
static int      g_dim_state        = 0;  /* 0=full, 1=dim, 2=off */
static lv_timer_t *g_dim_timer     = NULL;

static void backlight_apply(uint8_t bri /*0..255, 255=full*/);
static void activity_kick(lv_event_t *e);
static void dim_timer_cb(lv_timer_t *t);

/* Tileview-based UI: hello | clock (start) | sunmap. Swipe horizontally. */
static lv_obj_t  *g_tileview         = NULL;
static lv_obj_t  *g_clock_time_label = NULL;
static lv_obj_t  *g_clock_ms_label   = NULL;
static lv_obj_t  *g_clock_date_label = NULL;
static lv_obj_t  *g_clock_tz_label   = NULL;
static lv_obj_t  *g_sunmap_canvas    = NULL;
static lv_color_t *g_sunmap_buf      = NULL;
static int        g_sunmap_w         = 0;
static int        g_sunmap_h         = 0;
static lv_timer_t *g_clock_timer     = NULL;
static lv_timer_t *g_clock_ms_timer  = NULL;
static lv_timer_t *g_sunmap_timer    = NULL;

/* Settings tile widgets (rebuilt on rotate). */
static lv_obj_t  *g_set_wifi_status = NULL;
static lv_obj_t  *g_set_wifi_list   = NULL;
static int        g_set_wifi_sel    = -1;  /* index into g_wifi_scan or -1 */
static lv_obj_t  *g_set_kb_overlay  = NULL;
static lv_obj_t  *g_set_kb_ta       = NULL;
static char       g_set_kb_ssid[33] = {0};

static void show_main_ui(const char *status_text);

static const axs15231b_lcd_init_cmd_t lcd_init_cmds[] = {
    {0x11, (uint8_t []){0x00}, 0, 100},
    {0x29, (uint8_t []){0x00}, 0, 100},
};


static bool lvgl_lock(int timeout_ms)
{
    const TickType_t to = (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(lvgl_mux, to) == pdTRUE;
}

static void lvgl_unlock(void)
{
    xSemaphoreGive(lvgl_mux);
}

static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t io,
                                    esp_lcd_panel_io_event_data_t *edata,
                                    void *user_ctx)
{
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(lvgl_flush_semap, &hp);
    return hp == pdTRUE;
}

static void lvgl_tick_inc_cb(void *arg)
{
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
    const int PW          = EXAMPLE_LCD_H_RES;     /* panel width  = 172 */
    const int PH          = EXAMPLE_LCD_V_RES;     /* panel height = 640 */
    const int CW          = canvas_w;
    const int CH          = canvas_h;
    const int rs          = rot_state;

    /* direct_mode: color_map points at the full canvas framebuffer
       (LVGL has only redrawn the dirty area, but the rest is already
       valid from the last frame). The AXS15231B in QSPI mode streams
       each frame as a contiguous block starting at y=0, so we must
       always send the whole panel even though only a sliver changed.
       The win is on the LVGL rasterize side: with direct_mode, LVGL
       skips re-rendering the unchanged pixels each frame. */
    (void)area;
    uint16_t *src = (uint16_t *)color_map;

    const int flush_count = (PH + LVGL_FLUSH_STRIP_ROWS - 1) / LVGL_FLUSH_STRIP_ROWS;
    const int offgap      = LVGL_FLUSH_STRIP_ROWS;
    int x1 = 0, y1 = 0, x2 = PW, y2 = offgap;

    int64_t t_frame_start = esp_timer_get_time();
    int64_t t_xform_total = 0;   /* per-pixel transform */
    int64_t t_wait_total  = 0;   /* waiting for a free DMA buffer */
    int64_t t_draw_total  = 0;   /* esp_lcd_panel_draw_bitmap submit time */

    int buf_idx = 0;
    for (int i = 0; i < flush_count; i++) {
        /* Take one slot from the counting semaphore -- we have 2 DMA
           buffers and the SPI-done callback gives a slot back. So this
           only blocks when both buffers are still in flight. */
        int64_t t_w0 = esp_timer_get_time();
        xSemaphoreTake(lvgl_flush_semap, portMAX_DELAY);
        t_wait_total += esp_timer_get_time() - t_w0;
        uint16_t *lvgl_dma_buf = lvgl_dma_bufs[buf_idx];
        buf_idx ^= 1;
        if (y2 > PH) y2 = PH;
        int rows = y2 - y1;
        int64_t t_x0 = esp_timer_get_time();
        /* Specialized inner loops per rotation. Landscape modes (90/270)
           must iterate `r` (panel row -> canvas column) on the INNER loop
           so each canvas row is read sequentially -- otherwise PSRAM
           cache misses on every pixel drop landscape FPS to ~2. */
        switch (rs) {
            case 0: {  /* 0 deg: portrait, identity copy. */
                int copy_w = (PW < CW ? PW : CW) * 2;
                for (int r = 0; r < rows; r++) {
                    int py = y1 + r;
                    uint16_t *drow = lvgl_dma_buf + (size_t)r * PW;
                    if (py < CH) {
                        memcpy(drow, src + (size_t)py * CW, copy_w);
                        if (PW > CW) memset(drow + CW, 0, (PW - CW) * 2);
                    } else {
                        memset(drow, 0, PW * 2);
                    }
                }
            } break;
            case 1: {  /* 90 CW: drow[px] = src[(CH-1-px)*CW + py].
                          Stage each canvas row span into internal RAM via
                          a single memcpy (sequential PSRAM read), then
                          scatter transposed into the DMA buffer. */
                uint16_t span[LVGL_FLUSH_STRIP_ROWS];
                for (int px = 0; px < PW; px++) {
                    int cy = CH - 1 - px;
                    if ((unsigned)cy >= (unsigned)CH) {
                        for (int r = 0; r < rows; r++)
                            lvgl_dma_buf[(size_t)r * PW + px] = 0;
                        continue;
                    }
                    const uint16_t *crow = src + (size_t)cy * CW + y1;
                    int span_n = (y1 + rows <= CW) ? rows : (CW - y1);
                    if (span_n < 0) span_n = 0;
                    if (span_n > 0) memcpy(span, crow, (size_t)span_n * 2);
                    for (int r = 0; r < span_n; r++)
                        lvgl_dma_buf[(size_t)r * PW + px] = span[r];
                    for (int r = span_n; r < rows; r++)
                        lvgl_dma_buf[(size_t)r * PW + px] = 0;
                }
            } break;
            case 2: {  /* 180: drow[px] = src[(CH-1-py)*CW + (CW-1-px)]. */
                for (int r = 0; r < rows; r++) {
                    int py = y1 + r;
                    int cy = CH - 1 - py;
                    uint16_t *drow = lvgl_dma_buf + (size_t)r * PW;
                    if ((unsigned)cy >= (unsigned)CH) {
                        memset(drow, 0, PW * 2);
                        continue;
                    }
                    const uint16_t *crow = src + (size_t)cy * CW;
                    for (int px = 0; px < PW; px++) {
                        int cx = CW - 1 - px;
                        drow[px] = ((unsigned)cx < (unsigned)CW) ? crow[cx] : 0;
                    }
                }
            } break;
            default: { /* 270 CW: drow[px] = src[px*CW + (CW-1-(y1+r))].
                          Stage canvas row span into internal RAM, then
                          scatter transposed in reverse order. */
                uint16_t span[LVGL_FLUSH_STRIP_ROWS];
                for (int px = 0; px < PW; px++) {
                    int cy = px;
                    if ((unsigned)cy >= (unsigned)CH) {
                        for (int r = 0; r < rows; r++)
                            lvgl_dma_buf[(size_t)r * PW + px] = 0;
                        continue;
                    }
                    int cx_end_excl = CW - y1;            /* exclusive */
                    int cx_start    = cx_end_excl - rows; /* may be < 0 */
                    int span_n = rows;
                    int dst_skip = 0;
                    if (cx_start < 0) {
                        dst_skip = -cx_start;
                        span_n -= dst_skip;
                        cx_start = 0;
                    }
                    if (span_n > 0) {
                        const uint16_t *crow = src + (size_t)cy * CW + cx_start;
                        memcpy(span, crow, (size_t)span_n * 2);
                    }
                    /* span[0..span_n) holds canvas pixels in increasing cx;
                       we need decreasing cx, so write in reverse. */
                    for (int r = 0; r < dst_skip; r++)
                        lvgl_dma_buf[(size_t)r * PW + px] = 0;
                    for (int r = 0; r < span_n; r++)
                        lvgl_dma_buf[(size_t)(dst_skip + r) * PW + px] = span[span_n - 1 - r];
                }
            } break;
        }
        t_xform_total += esp_timer_get_time() - t_x0;
        int64_t t_d0 = esp_timer_get_time();
        esp_lcd_panel_draw_bitmap(panel, x1, y1, x2, y2, lvgl_dma_buf);
        t_draw_total += esp_timer_get_time() - t_d0;
        y1 += offgap;
        y2 += offgap;
    }
    /* Drain both DMA buffers before reporting flush_ready to LVGL,
       otherwise LVGL may swap framebuffers while SPI is still sending. */
    int64_t t_w1 = esp_timer_get_time();
    xSemaphoreTake(lvgl_flush_semap, portMAX_DELAY);
    xSemaphoreTake(lvgl_flush_semap, portMAX_DELAY);
    /* Restore both slots for the next flush. */
    xSemaphoreGive(lvgl_flush_semap);
    xSemaphoreGive(lvgl_flush_semap);
    t_wait_total += esp_timer_get_time() - t_w1;
    int64_t t_total = esp_timer_get_time() - t_frame_start;
    fps_frame_count++;
    static uint32_t flush_log_div = 0;
    if ((flush_log_div++ % 120) == 0) {  /* ~every 5s at 24 FPS */
        ESP_LOGI(TAG,
                 "flush rs=%d C=%dx%d total=%lldus xform=%lldus draw=%lldus wait=%lldus",
                 rs, CW, CH,
                 (long long)t_total, (long long)t_xform_total,
                 (long long)t_draw_total, (long long)t_wait_total);
    }
    lv_disp_flush_ready(drv);
}

static void fps_timer_cb(lv_timer_t *t)
{
    static uint32_t last_tick = 0;
    uint32_t now = lv_tick_get();
    uint32_t dt  = now - last_tick;
    if (dt == 0) return;
    uint32_t frames = fps_frame_count;
    fps_frame_count = 0;
    last_tick = now;
    uint32_t fps_x10 = (frames * 10000U) / dt;
    if (fps_label) {
        lv_label_set_text_fmt(fps_label, "FPS %lu.%lu",
                              (unsigned long)(fps_x10 / 10),
                              (unsigned long)(fps_x10 % 10));
    }
    static uint32_t print_div = 0;
    if ((print_div++ & 3) == 0) {  /* ~every 2s */
        ESP_LOGI(TAG, "fps=%lu.%lu  (frames=%lu in %lu ms)",
                 (unsigned long)(fps_x10 / 10), (unsigned long)(fps_x10 % 10),
                 (unsigned long)frames, (unsigned long)dt);
    }
}

static void lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    /* Tiny dead-zone filter to suppress controller jitter while pressed.
       Held across calls so a stationary finger reports a stable coord. */
    static bool s_was_pressed = false;
    static int  s_hold_x = 0, s_hold_y = 0;

    uint8_t cmd[11] = {0xb5, 0xab, 0xa5, 0x5a, 0, 0, 0, 0x0e, 0, 0, 0};
    uint8_t buff[32] = {0};
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        i2c_master_write_read_dev(disp_touch_dev_handle, cmd, 11, buff, 32));

    uint16_t rx = (((uint16_t)buff[2] & 0x0f) << 8) | buff[3];
    uint16_t ry = (((uint16_t)buff[4] & 0x0f) << 8) | buff[5];

    if (buff[1] > 0 && buff[1] < 5) {
        if (rx > EXAMPLE_LCD_V_RES) rx = EXAMPLE_LCD_V_RES;
        if (ry > EXAMPLE_LCD_H_RES) ry = EXAMPLE_LCD_H_RES;
        /* Raw touch -> portrait panel coords (panel_px in [0,PW), panel_py in [0,PH)). */
        int panel_px = ry;
        int panel_py = EXAMPLE_LCD_V_RES - rx;
        const int PW = EXAMPLE_LCD_H_RES;
        const int PH = EXAMPLE_LCD_V_RES;
        const int CW = canvas_w;
        const int CH = canvas_h;
        int cx, cy;
        /* Inverse of the flush_cb mapping. */
        switch (rot_state) {
            case 0:  cx = panel_px;            cy = panel_py;              break;
            case 1:  cx = panel_py;            cy = CH - 1 - panel_px;     break;
            case 2:  cx = CW - 1 - panel_px;   cy = CH - 1 - panel_py;     break;
            default: cx = CW - 1 - panel_py;   cy = panel_px;              break;
        }
        (void)PW; (void)PH;
        if (cx < 0) cx = 0;
        if (cx >= CW) cx = CW - 1;
        if (cy < 0) cy = 0;
        if (cy >= CH) cy = CH - 1;
        /* While pressed, snap samples within 2 px of the last reported
           point back to that point. Kills the controller's 1-2 px jitter
           so taps don't drift into scroll territory. */
        const int DEAD = 2;
        if (s_was_pressed && abs(cx - s_hold_x) <= DEAD && abs(cy - s_hold_y) <= DEAD) {
            cx = s_hold_x;
            cy = s_hold_y;
        } else {
            s_hold_x = cx;
            s_hold_y = cy;
        }
        s_was_pressed = true;
        data->state = LV_INDEV_STATE_PR;
        data->point.x = cx;
        data->point.y = cy;
    } else {
        s_was_pressed = false;
        data->state = LV_INDEV_STATE_REL;
    }
}

static void lvgl_port_task(void *arg)
{
    uint32_t delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
    for (;;) {
        if (lvgl_lock(-1)) {
            delay_ms = lv_timer_handler();
            lvgl_unlock();
        }
        if (delay_ms > EXAMPLE_LVGL_TASK_MAX_DELAY_MS) delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
        else if (delay_ms < EXAMPLE_LVGL_TASK_MIN_DELAY_MS) delay_ms = EXAMPLE_LVGL_TASK_MIN_DELAY_MS;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

static esp_lcd_panel_handle_t lcd_init(void)
{
    ESP_LOGI(TAG, "Init LCD reset GPIO");
    gpio_config_t io_conf = {};
    io_conf.intr_type    = GPIO_INTR_DISABLE;
    io_conf.mode         = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = ((uint64_t)1 << EXAMPLE_PIN_NUM_LCD_RST);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en   = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&io_conf));

    ESP_LOGI(TAG, "Init QSPI bus");
    spi_bus_config_t buscfg = {};
    buscfg.data0_io_num    = EXAMPLE_PIN_NUM_LCD_DATA0;
    buscfg.data1_io_num    = EXAMPLE_PIN_NUM_LCD_DATA1;
    buscfg.data2_io_num    = EXAMPLE_PIN_NUM_LCD_DATA2;
    buscfg.data3_io_num    = EXAMPLE_PIN_NUM_LCD_DATA3;
    buscfg.sclk_io_num     = EXAMPLE_PIN_NUM_LCD_PCLK;
    buscfg.max_transfer_sz = LVGL_DMA_BUFF_LEN;
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));
    esp_lcd_spi_bus_handle_t spi_bus = (esp_lcd_spi_bus_handle_t)(uintptr_t)LCD_HOST;

    esp_lcd_panel_io_handle_t panel_io = NULL;
    esp_lcd_panel_handle_t    panel    = NULL;

    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num         = EXAMPLE_PIN_NUM_LCD_CS;
    io_config.dc_gpio_num         = -1;
    io_config.spi_mode            = 3;
    io_config.pclk_hz             = 60 * 1000 * 1000;
    io_config.trans_queue_depth   = 10;
    io_config.on_color_trans_done = notify_lvgl_flush_ready;
    io_config.lcd_cmd_bits        = 32;
    io_config.lcd_param_bits      = 8;
    io_config.flags.quad_mode     = true;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(spi_bus, &io_config, &panel_io));
    g_panel_io = panel_io;

    axs15231b_vendor_config_t vendor_config = {};
    vendor_config.flags.use_qspi_interface = 1;
    vendor_config.init_cmds                = lcd_init_cmds;
    vendor_config.init_cmds_size           = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]);

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = -1;
    panel_config.rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = LCD_BIT_PER_PIXEL;
    panel_config.vendor_config  = &vendor_config;

    ESP_ERROR_CHECK(esp_lcd_new_panel_axs15231b(panel_io, &panel_config, &panel));

    ESP_ERROR_CHECK(gpio_set_level(EXAMPLE_PIN_NUM_LCD_RST, 1));
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_ERROR_CHECK(gpio_set_level(EXAMPLE_PIN_NUM_LCD_RST, 0));
    vTaskDelay(pdMS_TO_TICKS(250));
    ESP_ERROR_CHECK(gpio_set_level(EXAMPLE_PIN_NUM_LCD_RST, 1));
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    g_panel = panel;
    return panel;
}

static void lvgl_init(esp_lcd_panel_handle_t panel)
{
    static lv_disp_draw_buf_t disp_buf;
    static lv_disp_drv_t      disp_drv;
    static lv_indev_drv_t     indev_drv;

    /* Counting semaphore: 2 because we have 2 ping-pong DMA buffers.
       Each completed SPI transfer "gives" one slot back. */
    lvgl_flush_semap = xSemaphoreCreateCounting(2, 2);
    lv_init();

    lvgl_dma_bufs[0] = (uint16_t *)heap_caps_malloc(LVGL_DMA_BUFF_LEN, MALLOC_CAP_DMA);
    lvgl_dma_bufs[1] = (uint16_t *)heap_caps_malloc(LVGL_DMA_BUFF_LEN, MALLOC_CAP_DMA);
    assert(lvgl_dma_bufs[0] && lvgl_dma_bufs[1]);
    /* Put the LVGL framebuffer in internal RAM. Rasterizing 110 KB of
       16-bit pixels into internal RAM is ~10x faster than into SPIRAM,
       which was the dominant cost in lv_timer_handler. We only have
       headroom for one buffer here -- the other side is the SPI DMA
       which is paced by panel bandwidth, not memory. */
    lv_color_t *fb1 = (lv_color_t *)heap_caps_malloc(LVGL_SPIRAM_BUFF_LEN,
                                                     MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!fb1) {
        ESP_LOGW(TAG, "fb in internal RAM failed; falling back to SPIRAM");
        fb1 = (lv_color_t *)heap_caps_malloc(LVGL_SPIRAM_BUFF_LEN, MALLOC_CAP_SPIRAM);
    }
    assert(fb1);
    lv_disp_draw_buf_init(&disp_buf, fb1, NULL, UI_CANVAS_W * UI_CANVAS_H);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res      = canvas_w;
    disp_drv.ver_res      = canvas_h;
    disp_drv.flush_cb     = lvgl_flush_cb;
    disp_drv.draw_buf     = &disp_buf;
    /* direct_mode lets LVGL render only dirty regions into the existing
       full-canvas framebuffer instead of re-rasterizing every pixel each
       frame. flush_cb still gets called with the dirty area, but the
       framebuffer pointer it receives points at the WHOLE canvas, so we
       can read any pixel from it. We then send full panel-width strips
       to the AXS15231B (whose QSPI mode does not honour partial-width
       column windows reliably) but only for the rows the dirty rect
       intersects. */
    disp_drv.full_refresh = 0;
    disp_drv.direct_mode  = 1;
    disp_drv.user_data    = panel;
    lv_disp_drv_register(&disp_drv);
    g_disp_drv = &disp_drv;
    /* Force the LVGL refresh timer to fire every 5 ms so the flush rate is
       paced by SPI/PSRAM instead of LVGL's default ~30 ms cadence. */
    lv_disp_t *disp = lv_disp_get_default();
    if (disp && disp->refr_timer) {
        lv_timer_set_period(disp->refr_timer, 5);
    }

    esp_timer_create_args_t tick_args = {};
    tick_args.callback = &lvgl_tick_inc_cb;
    tick_args.name     = "lvgl_tick";
    esp_timer_handle_t tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));

    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_touch_cb;
    /* Touch tuning for iPhone/Android-feel:
       - The 2-px dead-zone in lvgl_touch_cb already kills the
         capacitive controller's stationary jitter, so a tap reports
         a stable point.
       - scroll_limit kept low (8 px) so a deliberate scroll cancels
         the click quickly. LVGL automatically suppresses CLICK on
         release when the press caused any scroll to start.
       - gesture_limit raised so an accidental sideways drift on a
         tap doesn't trigger the tileview swipe gesture.
       - long_press_time slightly raised so a finger that stays pressed
         a beat too long doesn't trigger auto-repeat. */
    indev_drv.scroll_limit            = 18;
    indev_drv.scroll_throw            = 25;
    indev_drv.gesture_limit           = 80;
    indev_drv.long_press_time         = 500;
    indev_drv.long_press_repeat_time  = 100;
    lv_indev_drv_register(&indev_drv);

    lvgl_mux = xSemaphoreCreateMutex();
    assert(lvgl_mux);
    xTaskCreatePinnedToCore(lvgl_port_task, "LVGL", 8192, NULL, 4, NULL, 0);
}

static void play_btn_event_cb(lv_event_t *e)
{
    (void)e;
    if (!g_cfg.audio_enable) {
        ESP_LOGI(TAG, "play ignored: audio disabled in settings");
        return;
    }
    bool now = !audio_min_is_playing();
    audio_min_play_midi(now);
    if (play_btn_label) {
        lv_label_set_text(play_btn_label, now ? LV_SYMBOL_STOP : LV_SYMBOL_PLAY);
    }
    ESP_LOGI(TAG, "play toggled -> %s", now ? "PLAY" : "STOP");
}

static void rotate_btn_event_cb(lv_event_t *e)
{
    (void)e;
    /* Cycle 0 -> 90 -> 180 -> 270 -> 0. The canvas dimensions swap
       between portrait (172x640) and landscape (640x172) so the UI
       fills the panel at every rotation. The framebuffer is reused
       (172*640 == 640*172 pixels) so no realloc is needed. */
    rot_state = (rot_state + 1) & 3;
    if (rot_state == 0 || rot_state == 2) {
        canvas_w = EXAMPLE_LCD_H_RES;   /* 172 */
        canvas_h = EXAMPLE_LCD_V_RES;   /* 640 */
    } else {
        canvas_w = UI_CANVAS_W;          /* 640 */
        canvas_h = UI_CANVAS_H;          /* 172 */
    }
    if (g_disp_drv) {
        g_disp_drv->hor_res = canvas_w;
        g_disp_drv->ver_res = canvas_h;
        lv_disp_drv_update(lv_disp_get_default(), g_disp_drv);
    }
    /* Wipe the active screen and rebuild widgets sized for the new canvas. */
    lv_obj_clean(lv_scr_act());
    fps_label = NULL;
    play_btn_label = NULL;
    g_tileview = NULL;
    g_clock_time_label = NULL;
    g_clock_ms_label = NULL;
    g_clock_date_label = NULL;
    g_clock_tz_label = NULL;
    g_sunmap_canvas = NULL;
    g_set_wifi_status = NULL;
    g_set_wifi_list   = NULL;
    g_set_kb_overlay  = NULL;
    g_set_kb_ta       = NULL;
    g_clock_wifi_icon = NULL;
    g_clock_bt_icon   = NULL;
    if (g_clock_timer)    { lv_timer_del(g_clock_timer);    g_clock_timer    = NULL; }
    if (g_clock_ms_timer) { lv_timer_del(g_clock_ms_timer); g_clock_ms_timer = NULL; }
    if (g_sunmap_timer)   { lv_timer_del(g_sunmap_timer);   g_sunmap_timer   = NULL; }
    if (g_dim_timer)      { lv_timer_del(g_dim_timer);      g_dim_timer      = NULL; }
    if (g_status_timer)   { lv_timer_del(g_status_timer);   g_status_timer   = NULL; }
    show_main_ui(g_status_text);
    ESP_LOGI(TAG, "rotate -> %d deg  canvas=%dx%d", rot_state * 90, canvas_w, canvas_h);
}

/* ---------------------- Settings: NVS impl ---------------------- */

#define NVS_NS_CFG  "cfg"
#define NVS_NS_WIFI "wifi"

/* Bump when defaults change so existing devices pick up the new
   values on the next flash instead of keeping stale NVS data. */
#define CFG_VERSION  6u

/* Optional compiled-in default Wi-Fi credential. The committed source
   defines empty defaults; if main/wifi_secret.h exists locally
   (gitignored, see wifi_secret.example.h) its values override them so
   the board auto-connects without on-screen-keyboard input. */
#if __has_include("wifi_secret.h")
#  include "wifi_secret.h"
#endif
#ifndef DEFAULT_WIFI_SSID
#define DEFAULT_WIFI_SSID  ""
#endif
#ifndef DEFAULT_WIFI_PASS
#define DEFAULT_WIFI_PASS  ""
#endif

static void cfg_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_CFG, NVS_READONLY, &h) != ESP_OK) return;
    uint8_t  ver = 0;
    uint16_t tzi = g_cfg.tz_idx;
    int8_t   tz_h_legacy = 0;
    bool     have_legacy_tz_h = false;
    uint8_t  br  = g_cfg.brightness;
    uint16_t ds  = g_cfg.dim_s;
    uint16_t os  = g_cfg.off_s;
    uint8_t  h24 = g_cfg.hour24;
    uint8_t  df  = g_cfg.date_fmt;
    uint8_t  ss  = g_cfg.show_seconds;
    uint8_t  sm  = g_cfg.show_ms;
    uint8_t  ae  = g_cfg.audio_enable;
    uint8_t  av  = g_cfg.audio_volume;
    uint8_t  th  = g_cfg.theme;
    uint8_t  sf  = g_cfg.show_fps;
    uint8_t  wac = g_cfg.wifi_autoconnect;
    uint8_t  lang= g_cfg.lang;
    size_t   sl  = sizeof(g_cfg.last_ssid);
    nvs_get_u8 (h, "ver",       &ver);
    if (nvs_get_u16(h, "tz_idx", &tzi) != ESP_OK) {
        if (nvs_get_i8(h, "tz_h", &tz_h_legacy) == ESP_OK) have_legacy_tz_h = true;
    }
    nvs_get_u8 (h, "bri",       &br);
    nvs_get_u16(h, "dim_s",     &ds);
    nvs_get_u16(h, "off_s",     &os);
    nvs_get_u8 (h, "h24",       &h24);
    nvs_get_u8 (h, "date_fmt",  &df);
    nvs_get_u8 (h, "show_sec",  &ss);
    nvs_get_u8 (h, "show_ms",   &sm);
    nvs_get_u8 (h, "aud_en",    &ae);
    nvs_get_u8 (h, "aud_vol",   &av);
    nvs_get_u8 (h, "theme",     &th);
    nvs_get_u8 (h, "show_fps",  &sf);
    nvs_get_u8 (h, "wifi_ac",   &wac);
    nvs_get_u8 (h, "lang",      &lang);
    nvs_get_str(h, "last_ssid", g_cfg.last_ssid, &sl);
    nvs_close(h);
    if (tzi >= TZ_CITY_COUNT) tzi = TZ_DEFAULT_CITY_INDEX;
    if (df > 2)  df  = 0;
    if (th > 2)  th  = 0;
    if (av > 100) av = 100;
    if (ver < CFG_VERSION) {
        ESP_LOGI(TAG, "cfg: migrating from v%u -> v%u",
                 (unsigned)ver, (unsigned)CFG_VERSION);
        if (have_legacy_tz_h) {
            ESP_LOGI(TAG, "cfg: dropping legacy tz_h=%d, defaulting to city %s",
                     (int)tz_h_legacy, k_tz_cities[TZ_DEFAULT_CITY_INDEX].name);
            tzi = TZ_DEFAULT_CITY_INDEX;
        }
        g_cfg.tz_idx     = tzi;
        g_cfg.brightness = br;
        if (DEFAULT_WIFI_SSID[0] && DEFAULT_WIFI_PASS[0]) {
            strncpy(g_cfg.last_ssid, DEFAULT_WIFI_SSID, sizeof(g_cfg.last_ssid) - 1);
            cfg_save_ssid_pass(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS);
        }
        cfg_save();
        return;
    }
    g_cfg.tz_idx     = tzi;
    g_cfg.brightness = br;
    g_cfg.dim_s      = ds;
    g_cfg.off_s      = os;
    g_cfg.hour24     = h24 ? 1 : 0;
    g_cfg.date_fmt   = df;
    g_cfg.show_seconds = ss ? 1 : 0;
    g_cfg.show_ms    = sm ? 1 : 0;
    g_cfg.audio_enable = ae ? 1 : 0;
    g_cfg.audio_volume = av;
    g_cfg.theme      = th;
    g_cfg.show_fps   = sf ? 1 : 0;
    g_cfg.wifi_autoconnect = wac ? 1 : 0;
    g_cfg.lang = lang;
}

static void cfg_save(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_CFG, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8 (h, "ver",       CFG_VERSION);
    nvs_set_u16(h, "tz_idx",    g_cfg.tz_idx);
    nvs_set_u8 (h, "bri",       g_cfg.brightness);
    nvs_set_u8 (h, "h24",       g_cfg.hour24);
    nvs_set_u8 (h, "date_fmt",  g_cfg.date_fmt);
    nvs_set_u8 (h, "show_sec",  g_cfg.show_seconds);
    nvs_set_u8 (h, "show_ms",   g_cfg.show_ms);
    nvs_set_u8 (h, "aud_en",    g_cfg.audio_enable);
    nvs_set_u8 (h, "aud_vol",   g_cfg.audio_volume);
    nvs_set_u8 (h, "theme",     g_cfg.theme);
    nvs_set_u8 (h, "show_fps",  g_cfg.show_fps);
    nvs_set_u8 (h, "wifi_ac",   g_cfg.wifi_autoconnect);
    nvs_set_u8 (h, "lang",      g_cfg.lang);
    nvs_set_u16(h, "dim_s",     g_cfg.dim_s);
    nvs_set_u16(h, "off_s",     g_cfg.off_s);
    nvs_set_str(h, "last_ssid", g_cfg.last_ssid);
    nvs_commit(h);
    nvs_close(h);
}

static void cfg_save_ssid_pass(const char *ssid, const char *pass)
{
    if (!ssid || !*ssid) return;
    /* Use first 15 chars of SSID as the key (NVS key max 15 bytes). */
    char key[16] = {0};
    strncpy(key, ssid, 15);
    nvs_handle_t h;
    if (nvs_open(NVS_NS_WIFI, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, key, pass ? pass : "");
    nvs_commit(h);
    nvs_close(h);
}

static bool cfg_get_ssid_pass(const char *ssid, char *pass, size_t pass_len)
{
    if (!ssid || !*ssid || !pass) return false;
    char key[16] = {0};
    strncpy(key, ssid, 15);
    nvs_handle_t h;
    if (nvs_open(NVS_NS_WIFI, NVS_READONLY, &h) != ESP_OK) return false;
    size_t l = pass_len;
    esp_err_t er = nvs_get_str(h, key, pass, &l);
    nvs_close(h);
    return er == ESP_OK;
}

/* ---------------------- Wi-Fi impl ---------------------- */

static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT) {
        switch (id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "wifi: STA_START");
                break;
            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t *d =
                    (wifi_event_sta_disconnected_t *)data;
                g_wifi_last_reason = d ? d->reason : 0;
                ESP_LOGW(TAG, "wifi: disconnected reason=%u",
                         (unsigned)g_wifi_last_reason);
                g_wifi_connected = false;
                /* Auto-reconnect with backoff: try once if we have a target
                   SSID configured AND we're not in the middle of a user-
                   initiated scan (scan can't run if a connect is racing). */
                if (g_wifi_curr_ssid[0] && !g_wifi_scanning) {
                    esp_wifi_connect();
                }
                break;
            }
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "wifi: connected to %s", g_wifi_curr_ssid);
                g_wifi_connected = true;
                g_wifi_last_reason = 0;
                /* Persist this SSID as the auto-connect target. */
                strncpy(g_cfg.last_ssid, g_wifi_curr_ssid, sizeof(g_cfg.last_ssid) - 1);
                cfg_save();
                break;
            case WIFI_EVENT_SCAN_DONE: {
                /* Keep recs[] off the sys_evt task stack -- it's only
                   ~2 KB and a wifi_ap_record_t array of 16 entries is
                   ~1.7 KB on its own, blowing the stack. */
                static wifi_ap_record_t recs[WIFI_MAX_SCAN_AP];
                uint16_t apc = WIFI_MAX_SCAN_AP;
                if (esp_wifi_scan_get_ap_records(&apc, recs) == ESP_OK) {
                    g_wifi_scan_n = apc;
                    for (int i = 0; i < apc; i++) {
                        strncpy(g_wifi_scan[i].ssid, (const char *)recs[i].ssid,
                                sizeof(g_wifi_scan[i].ssid) - 1);
                        g_wifi_scan[i].ssid[sizeof(g_wifi_scan[i].ssid) - 1] = 0;
                        g_wifi_scan[i].rssi = recs[i].rssi;
                        g_wifi_scan[i].auth = (uint8_t)recs[i].authmode;
                    }
                } else {
                    g_wifi_scan_n = 0;
                }
                ESP_LOGI(TAG, "wifi: scan done, n=%u", (unsigned)g_wifi_scan_n);
                g_wifi_scanning = false;
                /* If a connect target is configured, resume reconnect now
                   that the scan has finished. */
                if (g_wifi_curr_ssid[0] && !g_wifi_connected) {
                    esp_wifi_connect();
                }
                break;
            }
            default: break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "wifi: got IP");
        /* Start SNTP once we actually have routable network. */
        sntp_start_once();
    }
}

static void wifi_init_once(void)
{
    if (g_wifi_inited) return;
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_start());
    g_wifi_inited = true;
}

static void wifi_start_scan(void)
{
    wifi_init_once();
    /* Disconnect any in-flight association first; the radio can't scan
       while it's repeatedly retrying a failed connect (NO_AP_FOUND etc).
       g_wifi_scanning suppresses the disconnect-handler's auto-reconnect
       so we don't fight ourselves. */
    g_wifi_scanning = true;
    esp_wifi_disconnect();
    wifi_scan_config_t sc = {};
    sc.show_hidden = false;
    sc.scan_type   = WIFI_SCAN_TYPE_ACTIVE;
    esp_err_t er = esp_wifi_scan_start(&sc, false);
    if (er != ESP_OK) {
        ESP_LOGW(TAG, "wifi: scan_start=%s", esp_err_to_name(er));
        g_wifi_scanning = false;  /* failed to start; allow reconnects */
    }
}

static void wifi_connect(const char *ssid, const char *pass)
{
    wifi_init_once();
    wifi_config_t wc = {};
    strncpy((char *)wc.sta.ssid,     ssid, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, pass ? pass : "", sizeof(wc.sta.password) - 1);
    /* If a password was supplied accept anything WPA2-and-above; if
       none, allow open APs only. The hard-coded OPEN threshold from
       earlier silently rejected every protected AP. */
    wc.sta.threshold.authmode = (pass && pass[0])
                                    ? WIFI_AUTH_WPA2_PSK
                                    : WIFI_AUTH_OPEN;
    wc.sta.pmf_cfg.capable    = true;
    wc.sta.pmf_cfg.required   = false;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    strncpy(g_wifi_curr_ssid, ssid, sizeof(g_wifi_curr_ssid) - 1);
    g_wifi_curr_ssid[sizeof(g_wifi_curr_ssid) - 1] = 0;
    g_wifi_connect_started_ms = lv_tick_get();
    g_wifi_last_reason = 0;
    esp_wifi_disconnect();
    esp_err_t er = esp_wifi_connect();
    ESP_LOGI(TAG, "wifi: connect %s pass_len=%u auth=%d -> %s",
             ssid, (unsigned)(pass ? strlen(pass) : 0),
             (int)wc.sta.threshold.authmode, esp_err_to_name(er));
}

/* Public language getters/setters used by i18n.c. Defined here because
   g_cfg is otherwise file-static. */
extern "C" int  app_cfg_get_lang(void) { return g_cfg.lang; }
extern "C" void app_cfg_set_lang(int lang)
{
    if (lang < 0) lang = 0;
    if (lang >= I18N_LANG_COUNT) lang = 0;
    g_cfg.lang = (uint8_t)lang;
    cfg_save();
}

extern "C" void app_cfg_set_brightness(int v)
{
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    g_cfg.brightness = (uint8_t)v;
    g_dim_state = 0;
    g_last_activity_ms = lv_tick_get();
    backlight_apply(g_cfg.brightness);
    cfg_save();
}

extern "C" void app_cfg_set_dim_off(int dim_s, int off_s)
{
    if (dim_s < 0) dim_s = 0;
    if (off_s < 0) off_s = 0;
    g_cfg.dim_s = (uint16_t)dim_s;
    g_cfg.off_s = (uint16_t)off_s;
    g_dim_state = 0;
    g_last_activity_ms = lv_tick_get();
    backlight_apply(g_cfg.brightness);
    cfg_save();
}

/* Public helper for cli.c: persist creds and kick a connect attempt. */
extern "C" void app_wifi_connect_save(const char *ssid, const char *pass)
{
    if (!ssid || !*ssid) return;
    cfg_save_ssid_pass(ssid, pass ? pass : "");
    strncpy(g_cfg.last_ssid, ssid, sizeof(g_cfg.last_ssid) - 1);
    g_cfg.last_ssid[sizeof(g_cfg.last_ssid) - 1] = 0;
    cfg_save();
    wifi_connect(ssid, pass ? pass : "");
}

/* ---------------------- SNTP ---------------------- */

/* Re-sync interval (ms). 4 hours by default. */
#define SNTP_SYNC_INTERVAL_MS  (4UL * 3600UL * 1000UL)

static void sntp_sync_notification_cb(struct timeval *tv)
{
    if (!tv) return;
    g_last_sntp_sync = tv->tv_sec;
    struct tm tm;
    gmtime_r(&g_last_sntp_sync, &tm);
    ESP_LOGI(TAG, "sntp: synced to %04d-%02d-%02d %02d:%02d:%02d UTC",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
    /* Write the corrected time back to the PCF85063 RTC so the
       battery-backed clock survives power cycles in sync. */
    i2c_rtc_setTime((uint16_t)(tm.tm_year + 1900),
                    (uint8_t)(tm.tm_mon + 1),
                    (uint8_t)tm.tm_mday,
                    (uint8_t)tm.tm_hour,
                    (uint8_t)tm.tm_min,
                    (uint8_t)tm.tm_sec);
}

static void sntp_start_once(void)
{
    if (g_sntp_started) return;
    g_sntp_started = true;
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    sntp_set_time_sync_notification_cb(sntp_sync_notification_cb);
    sntp_set_sync_interval(SNTP_SYNC_INTERVAL_MS);
    esp_sntp_init();
    ESP_LOGI(TAG, "sntp: started, server=pool.ntp.org, interval=%lus",
             (unsigned long)(SNTP_SYNC_INTERVAL_MS / 1000));
}

static const char *wifi_reason_str(uint8_t r)
{
    switch (r) {
        case 0:                              return "";
        case WIFI_REASON_AUTH_EXPIRE:        return "auth expired";
        case WIFI_REASON_AUTH_LEAVE:         return "auth leave";
        case WIFI_REASON_ASSOC_EXPIRE:       return "assoc expired";
        case WIFI_REASON_ASSOC_TOOMANY:      return "AP full";
        case WIFI_REASON_NOT_AUTHED:         return "not authed";
        case WIFI_REASON_NOT_ASSOCED:        return "not assoced";
        case WIFI_REASON_ASSOC_LEAVE:        return "assoc leave";
        case WIFI_REASON_ASSOC_NOT_AUTHED:   return "assoc not authed";
        case WIFI_REASON_DISASSOC_PWRCAP_BAD:return "pwrcap bad";
        case WIFI_REASON_BEACON_TIMEOUT:     return "beacon timeout";
        case WIFI_REASON_NO_AP_FOUND:        return "AP not found";
        case WIFI_REASON_AUTH_FAIL:          return "auth fail (wrong pass?)";
        case WIFI_REASON_ASSOC_FAIL:         return "assoc fail";
        case WIFI_REASON_HANDSHAKE_TIMEOUT:  return "handshake timeout";
        case WIFI_REASON_CONNECTION_FAIL:    return "connection fail";
        case WIFI_REASON_AP_TSF_RESET:       return "AP tsf reset";
        case WIFI_REASON_ROAMING:            return "roaming";
        default:                             return "disconnected";
    }
}

static void status_timer_cb(lv_timer_t *t)
{
    (void)t;
    /* Wi-Fi status icon on the clock tile. */
    if (g_clock_wifi_icon) {
        if (g_wifi_connected) {
            lv_obj_set_style_text_color(g_clock_wifi_icon,
                                         lv_color_make(0x80, 0xff, 0x80), 0);
        } else if (g_wifi_curr_ssid[0]) {
            /* trying or failed */
            lv_obj_set_style_text_color(g_clock_wifi_icon,
                                         lv_color_make(0xff, 0xa0, 0x40), 0);
        } else {
            lv_obj_set_style_text_color(g_clock_wifi_icon,
                                         lv_color_make(0x40, 0x40, 0x40), 0);
        }
    }
    /* BT icon: we don't have BT enabled yet, dim it. */
    if (g_clock_bt_icon) {
        lv_obj_set_style_text_color(g_clock_bt_icon,
                                     lv_color_make(0x40, 0x40, 0x40), 0);
    }
    /* Settings tile: live Wi-Fi status text + connect timeout/reason. */
    if (g_set_wifi_status) {
        if (g_wifi_connected) {
            lv_label_set_text_fmt(g_set_wifi_status, LV_SYMBOL_OK " %s",
                                  g_wifi_curr_ssid);
        } else if (g_wifi_curr_ssid[0]) {
            uint32_t elapsed = lv_tick_elaps(g_wifi_connect_started_ms);
            if (g_wifi_last_reason) {
                lv_label_set_text_fmt(g_set_wifi_status,
                                      LV_SYMBOL_WARNING " %s: %s",
                                      g_wifi_curr_ssid,
                                      wifi_reason_str(g_wifi_last_reason));
            } else if (elapsed > 15000) {
                lv_label_set_text_fmt(g_set_wifi_status,
                                      LV_SYMBOL_WARNING " %s: timed out",
                                      g_wifi_curr_ssid);
            } else {
                lv_label_set_text_fmt(g_set_wifi_status,
                                      tr(I18N_WIFI_CONNECTING_N),
                                      g_wifi_curr_ssid,
                                      (unsigned)(elapsed / 1000));
            }
        } else {
            lv_label_set_text(g_set_wifi_status, tr(I18N_WIFI_NOT_CONN));
        }
    }
}

/* ---------------------- Backlight + auto-dim impl ---------------------- */

static void backlight_apply(uint8_t bri)
{
    /* setUpduty takes the inverted PWM value: 0xFF = off, 0x00 = full.
       g_cfg.brightness uses 0..255 with 255 = full, so invert. */
    setUpduty((uint16_t)(0xFF - bri));
}

static void activity_kick(lv_event_t *e)
{
    (void)e;
    g_last_activity_ms = lv_tick_get();
    if (g_dim_state != 0) {
        g_dim_state = 0;
        backlight_apply(g_cfg.brightness);
    }
}

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
        if (want == 0) backlight_apply(g_cfg.brightness);
        else if (want == 1) backlight_apply(g_cfg.brightness / 8 + 4);
        else backlight_apply(0);
    }
}

/* ---------------------- Theme palette ---------------------- */

typedef struct {
    lv_color_t bg;          /* tile background */
    lv_color_t text;        /* primary text */
    lv_color_t menu_surf;   /* settings menu surface */
    lv_color_t menu_hdr;    /* settings header strip */
    lv_color_t menu_btn;    /* back button */
    lv_color_t sunmap_water_n; /* night water */
    lv_color_t sunmap_water_d; /* day water */
    lv_color_t sunmap_land_n;  /* night land */
    lv_color_t sunmap_land_d;  /* day land */
} theme_palette_t;

static theme_palette_t theme_get(void)
{
    theme_palette_t p;
    switch (g_cfg.theme) {
    case 1:  /* light */
        p.bg             = lv_color_make(0xf0, 0xf0, 0xf4);
        p.text           = lv_color_make(0x10, 0x10, 0x18);
        p.menu_surf      = lv_color_make(0xe8, 0xe8, 0xee);
        p.menu_hdr       = lv_color_make(0xc0, 0xc0, 0xcc);
        p.menu_btn       = lv_color_make(0x90, 0x90, 0xa0);
        p.sunmap_water_n = lv_color_make(0xb0, 0xb8, 0xc8);
        p.sunmap_water_d = lv_color_make(0xe0, 0xe4, 0xf0);
        p.sunmap_land_n  = lv_color_make(0x60, 0x70, 0x80);
        p.sunmap_land_d  = lv_color_make(0x20, 0x30, 0x40);
        break;
    case 2:  /* high contrast */
        p.bg             = lv_color_black();
        p.text           = lv_color_make(0xff, 0xff, 0x00);
        p.menu_surf      = lv_color_black();
        p.menu_hdr       = lv_color_make(0xff, 0xff, 0x00);
        p.menu_btn       = lv_color_white();
        p.sunmap_water_n = lv_color_black();
        p.sunmap_water_d = lv_color_make(0x40, 0x40, 0x00);
        p.sunmap_land_n  = lv_color_make(0x80, 0x80, 0x00);
        p.sunmap_land_d  = lv_color_make(0xff, 0xff, 0x00);
        break;
    default: /* dark */
        p.bg             = lv_color_black();
        p.text           = lv_color_white();
        p.menu_surf      = lv_color_make(0x20, 0x20, 0x28);
        p.menu_hdr       = lv_color_make(0x30, 0x30, 0x3c);
        p.menu_btn       = lv_color_make(0x50, 0x50, 0x60);
        p.sunmap_water_n = lv_color_black();
        p.sunmap_water_d = lv_color_make(0x20, 0x20, 0x20);
        p.sunmap_land_n  = lv_color_make(0x40, 0x40, 0x40);
        p.sunmap_land_d  = lv_color_make(0x90, 0x90, 0x90);
        break;
    }
    return p;
}

/* ---------------------- Clock tile ---------------------- */

static void tz_apply_current(void)
{
    uint16_t i = g_cfg.tz_idx;
    if (i >= TZ_CITY_COUNT) i = TZ_DEFAULT_CITY_INDEX;
    setenv("TZ", k_tz_cities[i].posix_tz, 1);
    tzset();
}

static const char *tz_current_city_name(void)
{
    uint16_t i = g_cfg.tz_idx;
    if (i >= TZ_CITY_COUNT) i = TZ_DEFAULT_CITY_INDEX;
    return k_tz_cities[i].name;
}

static void get_display_time(struct tm *out)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t t = tv.tv_sec;
    localtime_r(&t, out);
}

static void clock_ms_update_cb(lv_timer_t *t)
{
    (void)t;
    if (!g_clock_ms_label) return;
    if (!g_cfg.show_ms) {
        if (!lv_obj_has_flag(g_clock_ms_label, LV_OBJ_FLAG_HIDDEN))
            lv_obj_add_flag(g_clock_ms_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (lv_obj_has_flag(g_clock_ms_label, LV_OBJ_FLAG_HIDDEN))
        lv_obj_clear_flag(g_clock_ms_label, LV_OBJ_FLAG_HIDDEN);
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int ms = (int)(tv.tv_usec / 1000);
    if (ms < 0) ms = 0;
    if (ms > 999) ms = 999;
    char buf[8];
    snprintf(buf, sizeof(buf), ".%03d", ms);
    lv_label_set_text(g_clock_ms_label, buf);
}

static void clock_update_cb(lv_timer_t *t)
{
    (void)t;
    if (!g_clock_time_label || !g_clock_date_label) return;
    struct tm tm;
    get_display_time(&tm);
    char buf[64];
    int yyyy = tm.tm_year + 1900;
    int mm   = tm.tm_mon + 1;
    int dd   = tm.tm_mday;
    if (yyyy < 0) yyyy = 0;
    if (yyyy > 9999) yyyy = 9999;
    if (mm < 1) mm = 1;
    if (mm > 12) mm = 12;
    if (dd < 1) dd = 1;
    if (dd > 31) dd = 31;
    switch (g_cfg.date_fmt) {
    case 1:  snprintf(buf, sizeof(buf), "%02d.%02d.%04d", dd, mm, yyyy); break;
    case 2:  snprintf(buf, sizeof(buf), "%02d.%02d.%04d", mm, dd, yyyy); break;
    default: snprintf(buf, sizeof(buf), "%04d.%02d.%02d", yyyy, mm, dd); break;
    }
    lv_label_set_text(g_clock_date_label, buf);
    int hh = tm.tm_hour, mi = tm.tm_min, ss = tm.tm_sec;
    if (hh < 0) hh = 0;
    if (hh > 23) hh = 23;
    if (mi < 0) mi = 0;
    if (mi > 59) mi = 59;
    if (ss < 0) ss = 0;
    if (ss > 60) ss = 60;
    int disp_h = hh;
    if (!g_cfg.hour24) {
        disp_h = hh % 12;
        if (disp_h == 0) disp_h = 12;
    }
    if (g_cfg.show_seconds) {
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", disp_h, mi, ss);
    } else {
        snprintf(buf, sizeof(buf), "%02d:%02d",       disp_h, mi);
    }
    lv_label_set_text(g_clock_time_label, buf);
}

/* Forward decl: sunmap functions used by clock tile. */
static void sunmap_redraw(void);
static void sunmap_update_cb(lv_timer_t *t);

static void build_clock_tile(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 0, 0);

    /* Daylight map: stretch to fill the full screen as the clock
       background. Aspect may not match 2:1 (real Earth) -- we accept
       the squash because the map's job here is mostly atmospheric. */
    int W = canvas_w;
    int H = canvas_h;
    g_sunmap_w = W;
    g_sunmap_h = H;
    if (g_sunmap_buf) { lv_mem_free(g_sunmap_buf); g_sunmap_buf = NULL; }
    g_sunmap_buf = (lv_color_t *)lv_mem_alloc((size_t)W * H * sizeof(lv_color_t));
    if (g_sunmap_buf) {
        g_sunmap_canvas = lv_canvas_create(parent);
        lv_canvas_set_buffer(g_sunmap_canvas, g_sunmap_buf, W, H, LV_IMG_CF_TRUE_COLOR);
        lv_obj_align(g_sunmap_canvas, LV_ALIGN_CENTER, 0, 0);
        sunmap_redraw();
        if (!g_sunmap_timer) {
            g_sunmap_timer = lv_timer_create(sunmap_update_cb, SUNMAP_RECOMPUTE_MS, NULL);
        }
    }

    /* Date at the top center, transparent bar over the map. */
    g_clock_date_label = lv_label_create(parent);
    lv_label_set_text(g_clock_date_label, "----.--.--");
    lv_obj_set_style_text_color(g_clock_date_label, lv_color_make(0xd0, 0xd0, 0xd0), 0);
    lv_obj_set_style_text_font(g_clock_date_label, i18n_font(), 0);
    lv_obj_set_style_bg_color(g_clock_date_label, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_clock_date_label, LV_OPA_70, 0);
    lv_obj_set_style_pad_hor(g_clock_date_label, 6, 0);
    lv_obj_set_style_radius(g_clock_date_label, 3, 0);
    lv_obj_align(g_clock_date_label, LV_ALIGN_TOP_MID, 0, 4);

    /* Big time in JetBrains Mono Bold 96, dead center of the screen.
       Shifted slightly left to leave room for the millisecond field. */
    g_clock_time_label = lv_label_create(parent);
    lv_label_set_text(g_clock_time_label, "--:--:--");
    lv_obj_set_style_text_color(g_clock_time_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(g_clock_time_label, &font_jbmono_96, 0);
    lv_obj_set_style_bg_opa(g_clock_time_label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(g_clock_time_label, 0, 0);
    lv_obj_align(g_clock_time_label, LV_ALIGN_CENTER, -52, 0);

    /* Milliseconds in JBMono 48 (half size of the time face), aligned to
       sit on the bottom of the time so the digits share a baseline. */
    g_clock_ms_label = lv_label_create(parent);
    lv_label_set_text(g_clock_ms_label, ".000");
    lv_obj_set_style_text_color(g_clock_ms_label, lv_color_make(0xc0, 0xc0, 0xc0), 0);
    lv_obj_set_style_text_font(g_clock_ms_label, &font_jbmono_48, 0);
    lv_obj_set_style_bg_opa(g_clock_ms_label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(g_clock_ms_label, 0, 0);
    lv_obj_align_to(g_clock_ms_label, g_clock_time_label,
                    LV_ALIGN_OUT_RIGHT_BOTTOM, 0, -8);

    /* Timezone hint, bottom right -- shows the active city name. */
    g_clock_tz_label = lv_label_create(parent);
    lv_label_set_text(g_clock_tz_label, tz_current_city_name());
    lv_obj_set_style_text_color(g_clock_tz_label, lv_color_make(0xa0, 0xa0, 0xa0), 0);
    lv_obj_set_style_text_font(g_clock_tz_label, i18n_font(), 0);
    lv_obj_set_style_bg_color(g_clock_tz_label, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_clock_tz_label, LV_OPA_60, 0);
    lv_obj_set_style_pad_hor(g_clock_tz_label, 4, 0);
    lv_obj_align(g_clock_tz_label, LV_ALIGN_BOTTOM_RIGHT, -6, -4);

    /* Wi-Fi + BT icons, top right (left of FPS pill which sits at -4,4). */
    g_clock_wifi_icon = lv_label_create(parent);
    lv_label_set_text(g_clock_wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(g_clock_wifi_icon,
                                 lv_color_make(0x40, 0x40, 0x40), 0);
    lv_obj_set_style_text_font(g_clock_wifi_icon, i18n_font(), 0);
    lv_obj_set_style_bg_color(g_clock_wifi_icon, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_clock_wifi_icon, LV_OPA_60, 0);
    lv_obj_set_style_pad_hor(g_clock_wifi_icon, 3, 0);
    lv_obj_align(g_clock_wifi_icon, LV_ALIGN_TOP_RIGHT, -4, 4);

    g_clock_bt_icon = lv_label_create(parent);
    lv_label_set_text(g_clock_bt_icon, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_color(g_clock_bt_icon,
                                 lv_color_make(0x40, 0x40, 0x40), 0);
    lv_obj_set_style_text_font(g_clock_bt_icon, i18n_font(), 0);
    lv_obj_set_style_bg_color(g_clock_bt_icon, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_clock_bt_icon, LV_OPA_60, 0);
    lv_obj_set_style_pad_hor(g_clock_bt_icon, 3, 0);
    lv_obj_align(g_clock_bt_icon, LV_ALIGN_TOP_RIGHT, -28, 4);

    if (!g_clock_timer) {
        g_clock_timer = lv_timer_create(clock_update_cb, 500, NULL);
    }
    if (!g_clock_ms_timer) {
        /* 16 ms tick = ~60 Hz, drives the visible refresh rate. */
        g_clock_ms_timer = lv_timer_create(clock_ms_update_cb, 16, NULL);
    }
    clock_update_cb(NULL);
    clock_ms_update_cb(NULL);
}

/* ---------------------- Sunmap tile ---------------------- */

/* Continents are sourced from main/landmask.h, a 1-bit equirectangular
   raster (640x172, stretched to fill the canvas) generated offline from
   Natural Earth 1:110m land polygons. See scripts/gen_landmask.py. */

static void sunmap_redraw(void)
{
    if (!g_sunmap_buf || !g_sunmap_canvas) return;
    const int W = g_sunmap_w;
    const int H = g_sunmap_h;
    /* Four-tone palette: ocean/land x night/day. The mask is sized for
       the canvas (LANDMASK_W x LANDMASK_H = 640 x 172) and indexed 1:1. */
    theme_palette_t pal = theme_get();
    const lv_color_t c_water_n = pal.sunmap_water_n;
    const lv_color_t c_water_d = pal.sunmap_water_d;
    const lv_color_t c_land_n  = pal.sunmap_land_n;
    const lv_color_t c_land_d  = pal.sunmap_land_d;

    /* Subsolar point. */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t now = tv.tv_sec;
    struct tm tm;
    gmtime_r(&now, &tm);
    int doy = tm.tm_yday;  /* 0..365 */
    float utc_hours = tm.tm_hour + tm.tm_min / 60.0f + tm.tm_sec / 3600.0f;
    float sun_lon_deg = -15.0f * (utc_hours - 12.0f);
    float sun_lat_deg = 23.44f * sinf(2.0f * (float)M_PI * (doy - 81) / 365.0f);
    float sl     = sun_lat_deg * (float)M_PI / 180.0f;
    float so     = sun_lon_deg * (float)M_PI / 180.0f;
    float sin_sl = sinf(sl);
    float cos_sl = cosf(sl);

    for (int y = 0; y < H; y++) {
        float lat = (90.0f - (y + 0.5f) * (180.0f / H)) * (float)M_PI / 180.0f;
        float sin_p = sinf(lat);
        float cos_p = cosf(lat);
        const float lon0 = -(float)M_PI;
        const float dlon = 2.0f * (float)M_PI / W;
        for (int x = 0; x < W; x++) {
            float lon = lon0 + (x + 0.5f) * dlon;
            float c = sin_sl * sin_p + cos_sl * cos_p * cosf(lon - so);
            int land = landmask_get(x, y);
            lv_color_t color;
            if (c > 0) color = land ? c_land_d : c_water_d;
            else       color = land ? c_land_n : c_water_n;
            g_sunmap_buf[y * W + x] = color;
        }
    }

    lv_obj_invalidate(g_sunmap_canvas);
}

static void sunmap_update_cb(lv_timer_t *t)
{
    (void)t;
    sunmap_redraw();
}

/* ---------------------- Radio tile ---------------------- */

static lv_obj_t  *g_radio_status_lbl = NULL;   /* "Connecting...", "Playing", etc. */
static lv_obj_t  *g_radio_now_lbl    = NULL;   /* current station name + genre */
static lv_obj_t  *g_radio_btn_lbl    = NULL;   /* play / stop glyph */
static lv_obj_t  *g_radio_list       = NULL;   /* scrollable station list */
static lv_obj_t  *g_radio_vol_lbl    = NULL;   /* "Vol N" indicator */
static bool       g_radio_engine_up  = false;
static lv_timer_t *g_radio_poll_timer = NULL;
static int        g_radio_pending_idx = -1;    /* set by the LVGL click cb */

typedef enum {
    RADIO_CMD_NONE = 0,
    RADIO_CMD_PLAY_INDEX,   /* g_radio_pending_idx tells which */
    RADIO_CMD_STOP,
    RADIO_CMD_INIT_ENGINE,  /* boot path: init engine without playing yet */
} radio_cmd_t;

static volatile radio_cmd_t  g_radio_cmd        = RADIO_CMD_NONE;
static char                  g_radio_status[80] = "Idle";
static SemaphoreHandle_t     g_radio_status_mtx = NULL;
static TaskHandle_t          g_radio_worker     = NULL;

static void radio_set_status(const char *s)
{
    if (!g_radio_status_mtx) return;
    xSemaphoreTake(g_radio_status_mtx, portMAX_DELAY);
    strncpy(g_radio_status, s, sizeof(g_radio_status) - 1);
    g_radio_status[sizeof(g_radio_status) - 1] = 0;
    xSemaphoreGive(g_radio_status_mtx);
}

static bool radio_engine_ensure_up(void)
{
    if (g_radio_engine_up) return true;
    radio_set_status(tr(I18N_RADIO_ENGINE_INIT));
    audio_min_shutdown();
    if (radio_init() != ESP_OK) {
        radio_set_status(tr(I18N_RADIO_ENGINE_FAIL));
        return false;
    }
    g_radio_engine_up = true;
    return true;
}

static void radio_worker_task(void *arg)
{
    (void)arg;
    while (1) {
        radio_cmd_t cmd = g_radio_cmd;
        if (cmd == RADIO_CMD_INIT_ENGINE) {
            g_radio_cmd = RADIO_CMD_NONE;
            radio_engine_ensure_up();
        } else if (cmd == RADIO_CMD_PLAY_INDEX) {
            g_radio_cmd = RADIO_CMD_NONE;
            int idx = g_radio_pending_idx;
            if (!radio_engine_ensure_up()) continue;
            char buf[80];
            snprintf(buf, sizeof(buf), tr(I18N_RADIO_CONNECTING), radio_station_name(idx));
            radio_set_status(buf);
            if (radio_play_index(idx) == ESP_OK) {
                snprintf(buf, sizeof(buf), tr(I18N_RADIO_PLAYING), radio_station_name(idx));
                radio_set_status(buf);
            } else {
                radio_set_status(tr(I18N_RADIO_PLAY_FAIL));
            }
        } else if (cmd == RADIO_CMD_STOP) {
            g_radio_cmd = RADIO_CMD_NONE;
            if (g_radio_engine_up) radio_stop();
            radio_set_status(tr(I18N_RADIO_STOPPED));
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void radio_worker_ensure(void)
{
    if (!g_radio_status_mtx) g_radio_status_mtx = xSemaphoreCreateMutex();
    if (!g_radio_worker) {
        xTaskCreatePinnedToCore(radio_worker_task, "radio_wrk", 6 * 1024,
                                NULL, 4, &g_radio_worker, 1);
    }
}

/* Called from app_main once Wi-Fi is up so the engine is warm by the time
   the user picks a station -- removes the ~200 ms codec/I2S setup from the
   first-play latency. */
static void radio_engine_warm_at_boot(void)
{
    radio_worker_ensure();
    g_radio_cmd = RADIO_CMD_INIT_ENGINE;
}

static void radio_status_poll_cb(lv_timer_t *t)
{
    (void)t;
    if (!g_radio_status_lbl || !g_radio_status_mtx) return;
    char snap[80];
    xSemaphoreTake(g_radio_status_mtx, portMAX_DELAY);
    strncpy(snap, g_radio_status, sizeof(snap));
    xSemaphoreGive(g_radio_status_mtx);
    lv_label_set_text(g_radio_status_lbl, snap);
    if (g_radio_btn_lbl) {
        bool playing = g_radio_engine_up && radio_is_playing();
        lv_label_set_text(g_radio_btn_lbl, playing ? LV_SYMBOL_STOP : LV_SYMBOL_PLAY);
    }
    if (g_radio_now_lbl) {
        int idx = radio_current_index();
        if (idx >= 0) {
            char buf[80];
            snprintf(buf, sizeof(buf), "%s  -  %s",
                     radio_station_name(idx), radio_station_genre(idx));
            lv_label_set_text(g_radio_now_lbl, buf);
        }
    }
}

static void radio_vol_step(int delta)
{
    int v = radio_get_volume() + delta;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    radio_set_volume(v);
    if (g_radio_vol_lbl) lv_label_set_text_fmt(g_radio_vol_lbl, tr(I18N_VOL_N), v);
}

static void radio_vol_dn_cb(lv_event_t *e) { (void)e; radio_vol_step(-5); }
static void radio_vol_up_cb(lv_event_t *e) { (void)e; radio_vol_step(+5); }

static void radio_play_btn_cb(lv_event_t *e)
{
    (void)e;
    radio_worker_ensure();
    if (g_radio_engine_up && radio_is_playing()) {
        g_radio_cmd = RADIO_CMD_STOP;
    } else {
        /* Resume the most recent station, or fall back to station 0. */
        int idx = radio_current_index();
        g_radio_pending_idx = idx >= 0 ? idx : 0;
        g_radio_cmd = RADIO_CMD_PLAY_INDEX;
    }
}

static void radio_station_pick_cb(lv_event_t *e)
{
    if (menu_input_blocked()) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= radio_station_count()) return;
    radio_worker_ensure();
    g_radio_pending_idx = idx;
    g_radio_cmd = RADIO_CMD_PLAY_INDEX;
}

static void build_radio_tile(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_make(0x10, 0x10, 0x18), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 0, 0);

    /* Split horizontally: scrollable station list on the left, now-playing
       info + transport on the right. The 640 px canvas gives ~62/38 split. */
    const int LIST_W = 400;
    const int INFO_W = canvas_w - LIST_W;

    /* ---------- Left: station list ---------- */
    g_radio_list = lv_obj_create(parent);
    lv_obj_remove_style_all(g_radio_list);
    lv_obj_set_size(g_radio_list, LIST_W, canvas_h);
    lv_obj_align(g_radio_list, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_layout(g_radio_list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(g_radio_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g_radio_list, 2, 0);
    lv_obj_set_style_pad_all(g_radio_list, 4, 0);
    lv_obj_set_style_bg_color(g_radio_list, lv_color_make(0x10, 0x10, 0x18), 0);
    lv_obj_set_style_bg_opa(g_radio_list, LV_OPA_COVER, 0);
    lv_obj_set_scroll_dir(g_radio_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_radio_list, LV_SCROLLBAR_MODE_AUTO);

    for (int i = 0; i < radio_station_count(); i++) {
        lv_obj_t *row = lv_btn_create(g_radio_list);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, 26);
        lv_obj_set_style_bg_color(row, lv_color_make(0x20, 0x20, 0x30), 0);
        lv_obj_set_style_pad_all(row, 4, 0);
        lv_obj_add_event_cb(row, radio_station_pick_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text_fmt(lbl, "%s  -  %s",
                              radio_station_name(i),
                              radio_station_genre(i));
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl, i18n_font(), 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
    }

    /* ---------- Right: info panel ---------- */
    lv_obj_t *info = lv_obj_create(parent);
    lv_obj_remove_style_all(info);
    lv_obj_set_size(info, INFO_W, canvas_h);
    lv_obj_align(info, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(info, lv_color_make(0x18, 0x18, 0x24), 0);
    lv_obj_set_style_bg_opa(info, LV_OPA_COVER, 0);
    lv_obj_clear_flag(info, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(info, 6, 0);

    lv_obj_t *header = lv_label_create(info);
    lv_label_set_text_fmt(header, LV_SYMBOL_AUDIO "  %s", tr(I18N_RADIO_NOW_PLAYING));
    lv_obj_set_style_text_color(header, lv_color_make(0xa0, 0xa0, 0xc0), 0);
    lv_obj_set_style_text_font(header, i18n_font(), 0);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 0, 0);

    g_radio_now_lbl = lv_label_create(info);
    lv_label_set_long_mode(g_radio_now_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_radio_now_lbl, INFO_W - 12);
    lv_label_set_text(g_radio_now_lbl, tr(I18N_RADIO_NO_STATION));
    lv_obj_set_style_text_color(g_radio_now_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(g_radio_now_lbl, i18n_font(), 0);
    lv_obj_align(g_radio_now_lbl, LV_ALIGN_TOP_LEFT, 0, 18);

    g_radio_status_lbl = lv_label_create(info);
    lv_label_set_long_mode(g_radio_status_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_radio_status_lbl, INFO_W - 12);
    lv_label_set_text(g_radio_status_lbl, tr(I18N_IDLE));
    lv_obj_set_style_text_color(g_radio_status_lbl, lv_color_make(0xc0, 0xc0, 0xc0), 0);
    lv_obj_set_style_text_font(g_radio_status_lbl, i18n_font(), 0);
    lv_obj_align(g_radio_status_lbl, LV_ALIGN_TOP_LEFT, 0, 70);

    /* Bottom row: [-]  Vol N  [+]                             [play] */
    lv_obj_t *vol_dn = lv_btn_create(info);
    lv_obj_set_size(vol_dn, 32, 32);
    lv_obj_align(vol_dn, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_radius(vol_dn, 4, 0);
    lv_obj_set_style_bg_color(vol_dn, lv_color_make(0x40, 0x40, 0x60), 0);
    lv_obj_add_event_cb(vol_dn, radio_vol_dn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *vol_dn_l = lv_label_create(vol_dn);
    lv_label_set_text(vol_dn_l, LV_SYMBOL_MINUS);
    lv_obj_set_style_text_color(vol_dn_l, lv_color_white(), 0);
    lv_obj_set_style_text_font(vol_dn_l, i18n_font(), 0);
    lv_obj_center(vol_dn_l);

    g_radio_vol_lbl = lv_label_create(info);
    lv_label_set_text_fmt(g_radio_vol_lbl, tr(I18N_VOL_N), radio_get_volume());
    lv_obj_set_style_text_color(g_radio_vol_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(g_radio_vol_lbl, i18n_font(), 0);
    lv_obj_align(g_radio_vol_lbl, LV_ALIGN_BOTTOM_LEFT, 36, -10);

    lv_obj_t *vol_up = lv_btn_create(info);
    lv_obj_set_size(vol_up, 32, 32);
    lv_obj_align(vol_up, LV_ALIGN_BOTTOM_LEFT, 80, 0);
    lv_obj_set_style_radius(vol_up, 4, 0);
    lv_obj_set_style_bg_color(vol_up, lv_color_make(0x40, 0x40, 0x60), 0);
    lv_obj_add_event_cb(vol_up, radio_vol_up_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *vol_up_l = lv_label_create(vol_up);
    lv_label_set_text(vol_up_l, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_color(vol_up_l, lv_color_white(), 0);
    lv_obj_set_style_text_font(vol_up_l, i18n_font(), 0);
    lv_obj_center(vol_up_l);

    lv_obj_t *btn = lv_btn_create(info);
    lv_obj_set_size(btn, 50, 38);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_bg_color(btn, lv_color_make(0x20, 0x80, 0x40), 0);
    lv_obj_add_event_cb(btn, radio_play_btn_cb, LV_EVENT_CLICKED, NULL);
    g_radio_btn_lbl = lv_label_create(btn);
    lv_label_set_text(g_radio_btn_lbl, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_color(g_radio_btn_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(g_radio_btn_lbl, i18n_font(), 0);
    lv_obj_center(g_radio_btn_lbl);

    if (!g_radio_poll_timer) {
        g_radio_poll_timer = lv_timer_create(radio_status_poll_cb, 250, NULL);
    }
}

/* ---------------------- Hello tile (legacy demo) ---------------------- */

static void build_hello_tile(lv_obj_t *parent, const char *status_text)
{
    lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hello = lv_label_create(parent);
    lv_label_set_long_mode(hello, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(hello, canvas_w / 3);
    lv_label_set_text(hello, "Hello World  *  Hello World  *  Hello World  *  ");
    lv_obj_set_style_text_color(hello, lv_color_white(), 0);
    lv_obj_set_style_text_font(hello, i18n_font(), 0);
    lv_obj_set_style_anim_speed(hello, 40, 0);
    lv_obj_set_style_text_align(hello, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(hello, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t *status = lv_label_create(parent);
    lv_label_set_long_mode(status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(status, canvas_w - 20);
    lv_label_set_text(status, status_text);
    lv_obj_set_style_text_color(status, lv_color_make(0xa0, 0xa0, 0xa0), 0);
    lv_obj_set_style_text_font(status, i18n_font(), 0);
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(status, LV_ALIGN_CENTER, 0, 0);

    /* Play / Stop button */
    lv_obj_t *play_btn = lv_btn_create(parent);
    lv_obj_set_size(play_btn, 50, 50);
    lv_obj_align(play_btn, LV_ALIGN_BOTTOM_LEFT, 16, -8);
    lv_obj_set_style_radius(play_btn, 25, 0);
    lv_obj_set_style_bg_color(play_btn, lv_color_make(0x20, 0x80, 0x40), 0);
    lv_obj_add_event_cb(play_btn, play_btn_event_cb, LV_EVENT_CLICKED, NULL);
    play_btn_label = lv_label_create(play_btn);
    lv_label_set_text(play_btn_label, audio_min_is_playing() ? LV_SYMBOL_STOP : LV_SYMBOL_PLAY);
    lv_obj_set_style_text_color(play_btn_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(play_btn_label, i18n_font(), 0);
    lv_obj_center(play_btn_label);

    /* Rotate button */
    lv_obj_t *rot_btn = lv_btn_create(parent);
    lv_obj_set_size(rot_btn, 50, 50);
    lv_obj_align(rot_btn, LV_ALIGN_BOTTOM_RIGHT, -16, -8);
    lv_obj_set_style_radius(rot_btn, 30, 0);
    lv_obj_set_style_bg_color(rot_btn, lv_color_make(0x40, 0x40, 0x80), 0);
    lv_obj_add_event_cb(rot_btn, rotate_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rot_label = lv_label_create(rot_btn);
    lv_label_set_text(rot_label, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(rot_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(rot_label, i18n_font(), 0);
    lv_obj_center(rot_label);

}

/* ---------------------- Settings tile ---------------------- */

static void set_render_wifi_list(void);

/* Keyboard overlay for password entry. */
static void kb_close(void)
{
    if (g_set_kb_overlay) {
        lv_obj_del_async(g_set_kb_overlay);  /* deferred to avoid use-
                                                after-free inside the
                                                lv_keyboard event chain */
        g_set_kb_overlay = NULL;
        g_set_kb_ta      = NULL;
    }
    if (g_clock_ms_timer) lv_timer_resume(g_clock_ms_timer);
}

static void kb_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *kb = lv_event_get_target(e);
    if (code == LV_EVENT_READY) {
        /* Read the password BEFORE deleting the textarea. We snapshot
           the strings here because lv_keyboard_def_event_cb will keep
           dereferencing keyboard->ta after we return; we use
           lv_obj_del_async so the actual deletion happens after that
           function unwinds. */
        const char *pass = g_set_kb_ta ? lv_textarea_get_text(g_set_kb_ta) : "";
        char pass_copy[65] = {0};
        if (pass) strncpy(pass_copy, pass, sizeof(pass_copy) - 1);
        char ssid[33] = {0};
        strncpy(ssid, g_set_kb_ssid, sizeof(ssid) - 1);
        ESP_LOGI(TAG, "kb: connect ssid=%s pass_len=%u", ssid,
                 (unsigned)strlen(pass_copy));
        cfg_save_ssid_pass(ssid, pass_copy);
        /* Promote this SSID to "last_ssid" so auto-connect picks it up on
           the next boot. Without this the password gets stored but the
           auto-connect path logs "no credentials yet". */
        strncpy(g_cfg.last_ssid, ssid, sizeof(g_cfg.last_ssid) - 1);
        g_cfg.last_ssid[sizeof(g_cfg.last_ssid) - 1] = 0;
        cfg_save();
        wifi_connect(ssid, pass_copy);
        if (g_set_wifi_status) lv_label_set_text_fmt(g_set_wifi_status, tr(I18N_WIFI_CONNECTING), ssid);
        kb_close();
    } else if (code == LV_EVENT_CANCEL) {
        kb_close();
    } else {
        (void)kb;
    }
}

/* Mac-style 5-row keymap: digit row always visible, QWERTY, ASDF, ZXCV with
   punctuation, then a compact bottom row with a narrower space bar so we
   have room for "1#" (-> symbols mode), arrows, "_", "-" and OK.
   Magic strings ("ABC", "abc", "1#") are recognised by lv_keyboard for
   mode toggles -- everything else types literally.
   Width units sum per row; LVGL renders each button proportionally. */
static const char *kKbMapLower[] = {
    /* row 1: 10 digits + backspace(2)  -> 12 */
    "1","2","3","4","5","6","7","8","9","0", LV_SYMBOL_BACKSPACE, "\n",
    /* row 2: 10 letters                 -> 10 */
    "q","w","e","r","t","y","u","i","o","p", "\n",
    /* row 3: 9 letters                  -> 9 */
    "a","s","d","f","g","h","j","k","l", "\n",
    /* row 4: shift(1.5) + 7 letters + , + .  -> 11.5 */
    "ABC", "z","x","c","v","b","n","m",",",".", "\n",
    /* row 5: 1#(2) + @ + . + left + space(4) + right + _ + - + OK(2) -> 13 */
    "1#", "@",".", LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, "_","-", LV_SYMBOL_OK, ""
};
/* Widths (per button) for kKbMapLower. CTRL_BTN_FLAGS marks a control key
   that isn't inserted as text. CLICK_TRIG | NO_REPEAT on character keys
   makes them fire on release only and disables auto-repeat -- prevents
   "caaaattt" from a slightly-too-long press or a jittery touch. Backspace
   keeps default behaviour so hold-to-delete still works. */
#define KB_CHAR  (LV_BTNMATRIX_CTRL_CLICK_TRIG | LV_BTNMATRIX_CTRL_NO_REPEAT | 1)
#define KB_CTRL2 (LV_KEYBOARD_CTRL_BTN_FLAGS | LV_BTNMATRIX_CTRL_CLICK_TRIG | LV_BTNMATRIX_CTRL_NO_REPEAT | 2)
#define KB_CTRL1 (LV_KEYBOARD_CTRL_BTN_FLAGS | LV_BTNMATRIX_CTRL_CLICK_TRIG | LV_BTNMATRIX_CTRL_NO_REPEAT | 1)
static const lv_btnmatrix_ctrl_t kKbCtrlLower[] = {
    KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 2,  /* backspace: auto-repeat allowed */
    KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,
    KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,
    LV_BTNMATRIX_CTRL_CHECKED | KB_CTRL2,
    KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,
    KB_CTRL2, KB_CHAR, KB_CHAR,
    KB_CTRL1, 4 | LV_BTNMATRIX_CTRL_CLICK_TRIG | LV_BTNMATRIX_CTRL_NO_REPEAT, KB_CTRL1,
    KB_CHAR, KB_CHAR, KB_CTRL2,
};
static const char *kKbMapUpper[] = {
    "1","2","3","4","5","6","7","8","9","0", LV_SYMBOL_BACKSPACE, "\n",
    "Q","W","E","R","T","Y","U","I","O","P", "\n",
    "A","S","D","F","G","H","J","K","L", "\n",
    "abc", "Z","X","C","V","B","N","M",",",".", "\n",
    "1#", "@",".", LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, "_","-", LV_SYMBOL_OK, ""
};
static const lv_btnmatrix_ctrl_t kKbCtrlUpper[] = {
    KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 2,
    KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,
    KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,
    LV_BTNMATRIX_CTRL_CHECKED | KB_CTRL2,
    KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,KB_CHAR,
    KB_CTRL2, KB_CHAR, KB_CHAR,
    KB_CTRL1, 4 | LV_BTNMATRIX_CTRL_CLICK_TRIG | LV_BTNMATRIX_CTRL_NO_REPEAT, KB_CTRL1,
    KB_CHAR, KB_CHAR, KB_CTRL2,
};

static void kb_open_for_ssid(const char *ssid)
{
    strncpy(g_set_kb_ssid, ssid, sizeof(g_set_kb_ssid) - 1);
    g_set_kb_ssid[sizeof(g_set_kb_ssid) - 1] = 0;

    /* Pause the 60 Hz ms-clock while typing -- otherwise every
       keystroke contends with the ms label invalidation and the
       extra compositor passes can blow the LVGL task stack. */
    if (g_clock_ms_timer) lv_timer_pause(g_clock_ms_timer);

    lv_obj_t *scr = lv_scr_act();
    g_set_kb_overlay = lv_obj_create(scr);
    lv_obj_remove_style_all(g_set_kb_overlay);
    lv_obj_set_size(g_set_kb_overlay, canvas_w, canvas_h);
    lv_obj_set_style_bg_color(g_set_kb_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_set_kb_overlay, LV_OPA_90, 0);
    lv_obj_clear_flag(g_set_kb_overlay, LV_OBJ_FLAG_SCROLLABLE);

    /* Single-line text area at the top + eye toggle on the right edge.
       Pad-tight so the keyboard below gets the rest of the canvas. */
    const int TA_H  = 20;
    const int EYE_W = 28;
    g_set_kb_ta = lv_textarea_create(g_set_kb_overlay);
    lv_textarea_set_one_line(g_set_kb_ta, true);
    lv_textarea_set_password_mode(g_set_kb_ta, true);
    char ph[64];
    snprintf(ph, sizeof(ph), tr(I18N_WIFI_PASS_FOR), ssid);
    lv_textarea_set_placeholder_text(g_set_kb_ta, ph);
    lv_obj_set_size(g_set_kb_ta, canvas_w - EYE_W, TA_H);
    lv_obj_align(g_set_kb_ta, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_font(g_set_kb_ta, i18n_font(), 0);
    lv_obj_set_style_pad_top(g_set_kb_ta, 1, 0);
    lv_obj_set_style_pad_bottom(g_set_kb_ta, 1, 0);
    lv_obj_set_style_pad_left(g_set_kb_ta, 6, 0);
    lv_obj_set_style_pad_right(g_set_kb_ta, 6, 0);
    lv_obj_set_style_border_width(g_set_kb_ta, 0, 0);
    lv_obj_set_style_radius(g_set_kb_ta, 0, 0);

    /* Eye toggle: tap to show/hide the password text. Defaults to hidden
       (eye-closed glyph), since the textarea boots in password mode. */
    lv_obj_t *eye = lv_btn_create(g_set_kb_overlay);
    lv_obj_set_size(eye, EYE_W, TA_H);
    lv_obj_align(eye, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_radius(eye, 0, 0);
    lv_obj_set_style_pad_all(eye, 0, 0);
    lv_obj_set_style_bg_color(eye, lv_color_make(0x30, 0x30, 0x40), 0);
    lv_obj_t *eye_lbl = lv_label_create(eye);
    lv_label_set_text(eye_lbl, LV_SYMBOL_EYE_CLOSE);
    lv_obj_set_style_text_color(eye_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(eye_lbl, i18n_font(), 0);
    lv_obj_center(eye_lbl);
    lv_obj_add_event_cb(eye, [](lv_event_t *e) {
        lv_obj_t *btn = lv_event_get_target(e);
        lv_obj_t *lbl = lv_obj_get_child(btn, 0);
        if (!g_set_kb_ta) return;
        bool was_pw = lv_textarea_get_password_mode(g_set_kb_ta);
        lv_textarea_set_password_mode(g_set_kb_ta, !was_pw);
        if (lbl) lv_label_set_text(lbl, was_pw ? LV_SYMBOL_EYE_OPEN
                                               : LV_SYMBOL_EYE_CLOSE);
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *kb = lv_keyboard_create(g_set_kb_overlay);
    lv_obj_set_width(kb, canvas_w);
    lv_obj_set_height(kb, canvas_h - TA_H);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    /* Tight padding inside the keyboard so each row gets more vertical
       space; gives ~30 px tall keys on the 172 px canvas. */
    lv_obj_set_style_pad_all(kb, 2, 0);
    lv_obj_set_style_pad_row(kb, 2, 0);
    lv_obj_set_style_pad_column(kb, 2, 0);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_LOWER,
                        (const char **)kKbMapLower, kKbCtrlLower);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_UPPER,
                        (const char **)kKbMapUpper, kKbCtrlUpper);
    lv_keyboard_set_textarea(kb, g_set_kb_ta);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_READY,  NULL);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_CANCEL, NULL);
}

/* Tap-to-select: tapping an AP row just highlights it. The right-side
   Connect button uses g_set_wifi_sel to drive the actual association. */
static void wifi_ap_clicked_cb(lv_event_t *e)
{
    if (menu_input_blocked()) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= (int)g_wifi_scan_n) return;
    g_set_wifi_sel = idx;
    set_render_wifi_list();
}

/* Connect the currently selected AP. Open APs go straight; saved-password
   APs reuse the stored password; unknown-password APs open the keyboard. */
static void wifi_connect_selected_cb(lv_event_t *e)
{
    (void)e;
    if (menu_input_blocked()) return;
    int idx = g_set_wifi_sel;
    if (idx < 0 || idx >= (int)g_wifi_scan_n) return;
    const wifi_scan_ap_t *ap = &g_wifi_scan[idx];
    if (ap->auth == 0) {
        cfg_save_ssid_pass(ap->ssid, "");
        strncpy(g_cfg.last_ssid, ap->ssid, sizeof(g_cfg.last_ssid) - 1);
        g_cfg.last_ssid[sizeof(g_cfg.last_ssid) - 1] = 0;
        cfg_save();
        wifi_connect(ap->ssid, "");
        if (g_set_wifi_status) lv_label_set_text_fmt(g_set_wifi_status, tr(I18N_WIFI_CONNECTING), ap->ssid);
        return;
    }
    char pass[65] = {0};
    if (cfg_get_ssid_pass(ap->ssid, pass, sizeof(pass))) {
        strncpy(g_cfg.last_ssid, ap->ssid, sizeof(g_cfg.last_ssid) - 1);
        g_cfg.last_ssid[sizeof(g_cfg.last_ssid) - 1] = 0;
        cfg_save();
        wifi_connect(ap->ssid, pass);
        if (g_set_wifi_status) lv_label_set_text_fmt(g_set_wifi_status, tr(I18N_WIFI_CONNECTING), ap->ssid);
        return;
    }
    kb_open_for_ssid(ap->ssid);
}

/* Forget the selected AP's saved password and clear last_ssid if it
   matches. Keeps the AP visible in the list. */
static void wifi_forget_selected_cb(lv_event_t *e)
{
    (void)e;
    int idx = g_set_wifi_sel;
    if (idx < 0 || idx >= (int)g_wifi_scan_n) return;
    const wifi_scan_ap_t *ap = &g_wifi_scan[idx];
    /* nvs_erase_key on the per-SSID record + the auto-connect ssid if
       it points here. */
    nvs_handle_t h;
    if (nvs_open(NVS_NS_WIFI, NVS_READWRITE, &h) == ESP_OK) {
        char key[16] = {0};
        strncpy(key, ap->ssid, sizeof(key) - 1);
        nvs_erase_key(h, key);
        nvs_commit(h);
        nvs_close(h);
    }
    if (strncmp(ap->ssid, g_cfg.last_ssid, sizeof(g_cfg.last_ssid)) == 0) {
        g_cfg.last_ssid[0] = 0;
        cfg_save();
        esp_wifi_disconnect();
        g_wifi_connected = false;
    }
    set_render_wifi_list();
    if (g_set_wifi_status) lv_label_set_text(g_set_wifi_status, tr(I18N_WIFI_NOT_CONN));
}

static void set_render_wifi_list(void)
{
    if (!g_set_wifi_list) return;
    lv_obj_clean(g_set_wifi_list);
    if (g_wifi_scan_n == 0) {
        lv_obj_t *empty = lv_label_create(g_set_wifi_list);
        lv_label_set_text(empty, tr(I18N_WIFI_NO_APS));
        lv_obj_set_style_text_color(empty, lv_color_make(0xa0, 0xa0, 0xa0), 0);
        lv_obj_set_style_text_font(empty, i18n_font(), 0);
        return;
    }
    for (int i = 0; i < g_wifi_scan_n; i++) {
        const wifi_scan_ap_t *ap = &g_wifi_scan[i];
        lv_obj_t *btn = lv_btn_create(g_set_wifi_list);
        lv_obj_set_width(btn, lv_pct(100));
        lv_obj_set_height(btn, 22);
        bool is_connected = g_wifi_connected &&
                            strncmp(ap->ssid, g_wifi_curr_ssid,
                                    sizeof(g_wifi_curr_ssid)) == 0;
        bool is_selected  = (i == g_set_wifi_sel);
        char dummy_pass[2];
        bool is_saved = cfg_get_ssid_pass(ap->ssid, dummy_pass, sizeof(dummy_pass));
        lv_obj_set_style_bg_color(btn,
            is_selected ? lv_color_make(0x40, 0x40, 0x60)
                        : lv_color_make(0x20, 0x20, 0x30), 0);
        lv_obj_set_style_pad_all(btn, 2, 0);
        lv_obj_add_event_cb(btn, wifi_ap_clicked_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *l = lv_label_create(btn);
        /* Prefix glyphs: ✓ if currently connected; otherwise * if we have
           a saved password but aren't connected. Lock indicates encryption. */
        const char *prefix = "";
        if (is_connected)      prefix = LV_SYMBOL_OK " ";
        else if (is_saved)     prefix = "* ";
        lv_label_set_text_fmt(l, "%s%s%s  %ddB",
                              prefix,
                              ap->auth == 0 ? "" : LV_SYMBOL_KEYBOARD " ",
                              ap->ssid[0] ? ap->ssid : "(hidden)",
                              ap->rssi);
        lv_color_t col = lv_color_white();
        if (is_connected) col = lv_color_make(0x40, 0xc0, 0x80);
        else if (is_saved) col = lv_color_make(0xa0, 0xc0, 0xff);
        lv_obj_set_style_text_color(l, col, 0);
        lv_obj_set_style_text_font(l, i18n_font(), 0);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    }
}

static void scan_btn_cb(lv_event_t *e)
{
    (void)e;
    if (g_set_wifi_status) lv_label_set_text(g_set_wifi_status, tr(I18N_WIFI_SCANNING));
    wifi_start_scan();
    /* Schedule a one-shot UI refresh ~3s later when scan_done has fired. */
    lv_timer_t *t = lv_timer_create([](lv_timer_t *tt) {
        if (g_set_wifi_status) {
            lv_label_set_text_fmt(g_set_wifi_status, "Found %u networks", (unsigned)g_wifi_scan_n);
        }
        set_render_wifi_list();
        lv_timer_del(tt);
    }, 3000, NULL);
    (void)t;
}

/* Block touch-driven menu actions for a short window after a back press,
   so the same gesture that pops a page doesn't also pick the item that
   slides in under the finger. Stamped by the back-button click handler
   below; tested by every action callback that mutates state. */
static uint32_t g_menu_input_block_until_ms = 0;
static uint32_t g_last_scroll_ms = 0;
#define MENU_BACK_DEBOUNCE_MS  350
#define SCROLL_CLICK_SUPPRESS_MS 250

static bool menu_input_blocked(void)
{
    uint32_t now = lv_tick_get();
    /* Block if we're inside the back-press debounce window. */
    if ((int32_t)(g_menu_input_block_until_ms - now) > 0) return true;
    /* iOS/Android-style: also block if the last scroll motion was within
       SCROLL_CLICK_SUPPRESS_MS. Stops a fling-then-release from firing a
       click on the row the finger happened to lift on. */
    if (g_last_scroll_ms != 0 &&
        lv_tick_elaps(g_last_scroll_ms) < SCROLL_CLICK_SUPPRESS_MS) {
        return true;
    }
    return false;
}

/* Transparent shield placed above the menu for MENU_BACK_DEBOUNCE_MS
   after a back-press. It eats every click that would otherwise reach
   the menu item that scrolled in under the finger. */
static lv_obj_t *g_menu_shield = NULL;

static void menu_shield_drop_cb(lv_timer_t *t)
{
    if (g_menu_shield) {
        lv_obj_del(g_menu_shield);
        g_menu_shield = NULL;
    }
    lv_timer_del(t);
}

static void menu_back_clicked_cb(lv_event_t *e)
{
    (void)e;
    g_menu_input_block_until_ms = lv_tick_get() + MENU_BACK_DEBOUNCE_MS;
    if (g_menu_shield) return;  /* already shielded */
    /* Parent to the active screen so the shield covers the menu and any
       header/back-button that might still be sitting under the finger. */
    g_menu_shield = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(g_menu_shield);
    lv_obj_set_size(g_menu_shield, canvas_w, canvas_h);
    lv_obj_align(g_menu_shield, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(g_menu_shield, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(g_menu_shield, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_menu_shield, LV_OBJ_FLAG_CLICKABLE);
    /* Move to top z-order so subsequent clicks land on the shield. */
    lv_obj_move_foreground(g_menu_shield);
    lv_timer_t *t = lv_timer_create(menu_shield_drop_cb, MENU_BACK_DEBOUNCE_MS, NULL);
    (void)t;
}

static void tz_city_pick_cb(lv_event_t *e)
{
    if (menu_input_blocked()) return;
    /* user_data carries the city index packed into a void*. */
    uintptr_t idx = (uintptr_t)lv_event_get_user_data(e);
    if (idx >= TZ_CITY_COUNT) return;
    g_cfg.tz_idx = (uint16_t)idx;
    tz_apply_current();
    cfg_save();
    if (g_clock_tz_label) lv_label_set_text(g_clock_tz_label, tz_current_city_name());
    /* Force an immediate clock-face refresh so the user sees the change. */
    clock_update_cb(NULL);
}

static void bri_slider_cb(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    int v = lv_slider_get_value(s);
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    g_cfg.brightness = (uint8_t)v;
    if (g_dim_state == 0) backlight_apply(g_cfg.brightness);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) cfg_save();
}

static void fmt_duration(char *buf, size_t buflen, uint32_t total_s)
{
    if (total_s == 0)              snprintf(buf, buflen, "%s", tr(I18N_NEVER));
    else if (total_s < 60)         snprintf(buf, buflen, "%us", (unsigned)total_s);
    else if (total_s < 3600) {
        unsigned m = total_s / 60, s = total_s % 60;
        if (s) snprintf(buf, buflen, "%um %us", m, s);
        else   snprintf(buf, buflen, "%um", m);
    } else {
        unsigned h = total_s / 3600, m = (total_s % 3600) / 60;
        if (m) snprintf(buf, buflen, "%uh %um", h, m);
        else   snprintf(buf, buflen, "%uh", h);
    }
}

#define IDLE_SLIDER_MAX (8 * 3600)

static void dim_s_cb(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    int v = lv_slider_get_value(s);
    if (v < 0) v = 0;
    if (v > IDLE_SLIDER_MAX) v = IDLE_SLIDER_MAX;
    g_cfg.dim_s = (uint16_t)v;
    lv_obj_t *lbl = (lv_obj_t *)lv_event_get_user_data(e);
    if (lbl) {
        char d[24];
        fmt_duration(d, sizeof(d), g_cfg.dim_s);
        if (g_cfg.dim_s == 0) lv_label_set_text(lbl, tr(I18N_DIM_NEVER));
        else                  lv_label_set_text_fmt(lbl, tr(I18N_DIM_AFTER), d);
    }
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) cfg_save();
}

/* ---------- Display sub-page callbacks (12/24h, date fmt, secs/ms, FPS) ---------- */

static void hour_fmt_cb(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    g_cfg.hour24 = lv_obj_has_state(s, LV_STATE_CHECKED) ? 1 : 0;
    cfg_save();
    clock_update_cb(NULL);
}

static void show_sec_cb(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    g_cfg.show_seconds = lv_obj_has_state(s, LV_STATE_CHECKED) ? 1 : 0;
    cfg_save();
    clock_update_cb(NULL);
}

static void show_ms_cb(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    g_cfg.show_ms = lv_obj_has_state(s, LV_STATE_CHECKED) ? 1 : 0;
    cfg_save();
}

static void show_fps_cb(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    g_cfg.show_fps = lv_obj_has_state(s, LV_STATE_CHECKED) ? 1 : 0;
    cfg_save();
    if (fps_label) {
        if (g_cfg.show_fps) lv_obj_clear_flag(fps_label, LV_OBJ_FLAG_HIDDEN);
        else                lv_obj_add_flag(fps_label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void date_fmt_cb(lv_event_t *e)
{
    lv_obj_t *r = lv_event_get_target(e);
    int sel = lv_dropdown_get_selected(r);
    if (sel < 0) sel = 0;
    if (sel > 2) sel = 2;
    g_cfg.date_fmt = (uint8_t)sel;
    cfg_save();
    clock_update_cb(NULL);
}

/* ---------- Sound sub-page callbacks (enable + volume) ---------- */

static void audio_en_cb(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    g_cfg.audio_enable = lv_obj_has_state(s, LV_STATE_CHECKED) ? 1 : 0;
    if (!g_cfg.audio_enable && audio_min_is_playing()) audio_min_play_midi(false);
    cfg_save();
}

static void audio_vol_cb(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    int v = lv_slider_get_value(s);
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    g_cfg.audio_volume = (uint8_t)v;
    audio_min_set_volume(g_cfg.audio_volume);
    lv_obj_t *lbl = (lv_obj_t *)lv_event_get_user_data(e);
    if (lbl) lv_label_set_text_fmt(lbl, tr(I18N_VOLUME_PCT), (unsigned)v);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) cfg_save();
}

/* ---------- Theme + Wi-Fi auto-connect ---------- */

static void theme_cb(lv_event_t *e)
{
    lv_obj_t *r = lv_event_get_target(e);
    int sel = lv_dropdown_get_selected(r);
    if (sel < 0) sel = 0;
    if (sel > 2) sel = 2;
    g_cfg.theme = (uint8_t)sel;
    cfg_save();
    /* Sunmap can be re-themed live; menu colors apply on next boot. */
    sunmap_redraw();
}

static void wifi_ac_cb(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    g_cfg.wifi_autoconnect = lv_obj_has_state(s, LV_STATE_CHECKED) ? 1 : 0;
    cfg_save();
}

/* ---------- Reset to defaults ---------- */

static void reset_confirm_cb(lv_event_t *e)
{
    (void)e;
    if (menu_input_blocked()) return;
    nvs_handle_t h;
    if (nvs_open(NVS_NS_CFG, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    if (nvs_open(NVS_NS_WIFI, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "settings reset, rebooting");
    esp_restart();
}

/* ---------- Auto-dim sliders ---------- */

static void off_s_cb(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    int v = lv_slider_get_value(s);
    if (v < 0) v = 0;
    if (v > IDLE_SLIDER_MAX) v = IDLE_SLIDER_MAX;
    g_cfg.off_s = (uint16_t)v;
    lv_obj_t *lbl = (lv_obj_t *)lv_event_get_user_data(e);
    if (lbl) {
        char d[24];
        fmt_duration(d, sizeof(d), g_cfg.off_s);
        if (g_cfg.off_s == 0) lv_label_set_text(lbl, tr(I18N_SLEEP_NEVER));
        else                  lv_label_set_text_fmt(lbl, tr(I18N_SLEEP_AFTER), d);
    }
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) cfg_save();
}

/* Build a sub-page that the menu can navigate to. Returns the page so
   the caller can attach it via lv_menu_set_load_page_event. */

static lv_obj_t *build_subpage_wifi(lv_obj_t *menu)
{
    lv_obj_t *page = lv_menu_page_create(menu, (char *)tr(I18N_SET_WIFI));

    /* Outer cont laid out as a flex row: left column = AP list, right
       column = status + Scan / Connect / Forget / auto-connect. */
    lv_obj_t *cont = lv_menu_cont_create(page);
    lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(cont, 4, 0);
    lv_obj_set_style_pad_all(cont, 2, 0);
    lv_obj_set_height(cont, lv_pct(100));

    /* ---------- Left: AP list ---------- */
    g_set_wifi_list = lv_obj_create(cont);
    lv_obj_set_width(g_set_wifi_list, lv_pct(60));
    lv_obj_set_height(g_set_wifi_list, lv_pct(100));
    lv_obj_set_layout(g_set_wifi_list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(g_set_wifi_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g_set_wifi_list, 2, 0);
    lv_obj_set_style_pad_all(g_set_wifi_list, 2, 0);
    lv_obj_set_style_bg_color(g_set_wifi_list, lv_color_make(0x10, 0x10, 0x14), 0);
    lv_obj_set_scroll_dir(g_set_wifi_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_set_wifi_list, LV_SCROLLBAR_MODE_AUTO);
    set_render_wifi_list();

    /* ---------- Right: actions + status ---------- */
    lv_obj_t *side = lv_obj_create(cont);
    lv_obj_remove_style_all(side);
    lv_obj_set_width(side, lv_pct(38));
    lv_obj_set_height(side, lv_pct(100));
    lv_obj_set_layout(side, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(side, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(side, 4, 0);
    lv_obj_clear_flag(side, LV_OBJ_FLAG_SCROLLABLE);

    g_set_wifi_status = lv_label_create(side);
    lv_label_set_long_mode(g_set_wifi_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_set_wifi_status, lv_pct(100));
    lv_label_set_text_fmt(g_set_wifi_status, "%s",
                          g_wifi_connected ? g_wifi_curr_ssid : tr(I18N_WIFI_NOT_CONN));
    lv_obj_set_style_text_color(g_set_wifi_status, lv_color_make(0xc0, 0xc0, 0xc0), 0);
    lv_obj_set_style_text_font(g_set_wifi_status, i18n_font(), 0);

    /* Scan button */
    lv_obj_t *scan_btn = lv_btn_create(side);
    lv_obj_set_size(scan_btn, lv_pct(100), 24);
    lv_obj_t *scan_l = lv_label_create(scan_btn);
    lv_label_set_text(scan_l, tr(I18N_WIFI_SCAN_BTN));
    lv_obj_set_style_text_font(scan_l, i18n_font(), 0);
    lv_obj_center(scan_l);
    lv_obj_add_event_cb(scan_btn, scan_btn_cb, LV_EVENT_CLICKED, NULL);

    /* Connect button (acts on selected list row) */
    lv_obj_t *conn_btn = lv_btn_create(side);
    lv_obj_set_size(conn_btn, lv_pct(100), 24);
    lv_obj_set_style_bg_color(conn_btn, lv_color_make(0x20, 0x80, 0x40), 0);
    lv_obj_t *conn_l = lv_label_create(conn_btn);
    lv_label_set_text(conn_l, tr(I18N_WIFI_CONNECT_BTN));
    lv_obj_set_style_text_color(conn_l, lv_color_white(), 0);
    lv_obj_set_style_text_font(conn_l, i18n_font(), 0);
    lv_obj_center(conn_l);
    lv_obj_add_event_cb(conn_btn, wifi_connect_selected_cb, LV_EVENT_CLICKED, NULL);

    /* Forget button */
    lv_obj_t *forget_btn = lv_btn_create(side);
    lv_obj_set_size(forget_btn, lv_pct(100), 24);
    lv_obj_set_style_bg_color(forget_btn, lv_color_make(0x80, 0x40, 0x20), 0);
    lv_obj_t *forget_l = lv_label_create(forget_btn);
    lv_label_set_text(forget_l, tr(I18N_WIFI_FORGET_BTN));
    lv_obj_set_style_text_color(forget_l, lv_color_white(), 0);
    lv_obj_set_style_text_font(forget_l, i18n_font(), 0);
    lv_obj_center(forget_l);
    lv_obj_add_event_cb(forget_btn, wifi_forget_selected_cb, LV_EVENT_CLICKED, NULL);

    return page;
}

static lv_obj_t *build_subpage_tz_city_list(lv_obj_t *menu, uint16_t first, uint16_t last,
                                              const char *title)
{
    lv_obj_t *page = lv_menu_page_create(menu, (char *)title);
    lv_obj_set_scroll_dir(page, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_AUTO);
    for (uint16_t i = first; i < last; i++) {
        lv_obj_t *cont = lv_menu_cont_create(page);
        lv_obj_t *l    = lv_label_create(cont);
        lv_label_set_text(l, k_tz_cities[i].name);
        lv_obj_set_style_text_font(l, i18n_font(), 0);
        if (i == g_cfg.tz_idx) {
            lv_obj_set_style_text_color(l, lv_color_make(0x40, 0xc0, 0x80), 0);
        }
        lv_obj_add_flag(cont, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(cont, tz_city_pick_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);
    }
    return page;
}

static lv_obj_t *build_subpage_tz(lv_obj_t *menu)
{
    /* Build the per-continent city pages first, then a continent-list
       root page that links to them. Two-level navigation matches the
       OpenWRT-style "Continent / City" picker. */
    lv_obj_t *page = lv_menu_page_create(menu, (char *)tr(I18N_SET_TZ));
    lv_obj_set_scroll_dir(page, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_AUTO);
    for (uint16_t c = 0; c < TZ_CONTINENT_COUNT; c++) {
        lv_obj_t *city_page = build_subpage_tz_city_list(menu,
            k_tz_continents[c].first, k_tz_continents[c].last,
            k_tz_continents[c].name);
        lv_obj_t *cont = lv_menu_cont_create(page);
        lv_obj_t *l    = lv_label_create(cont);
        lv_label_set_text(l, k_tz_continents[c].name);
        lv_obj_set_style_text_font(l, i18n_font(), 0);
        lv_menu_set_load_page_event(menu, cont, city_page);
        lv_obj_add_event_cb(cont, menu_back_clicked_cb, LV_EVENT_CLICKED, NULL);
    }
    return page;
}

static lv_obj_t *build_subpage_brightness(lv_obj_t *menu)
{
    lv_obj_t *page = lv_menu_page_create(menu, (char *)tr(I18N_SET_BRIGHTNESS));
    lv_obj_t *cont = lv_menu_cont_create(page);
    lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cont, 6, 0);

    lv_obj_t *l = lv_label_create(cont);
    lv_label_set_text(l, tr(I18N_BACKLIGHT_LEVEL));
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_set_style_text_font(l, i18n_font(), 0);

    lv_obj_t *bri_s = lv_slider_create(cont);
    lv_obj_set_width(bri_s, lv_pct(95));
    lv_slider_set_range(bri_s, 8, 255);
    lv_slider_set_value(bri_s, g_cfg.brightness, LV_ANIM_OFF);
    lv_obj_add_event_cb(bri_s, bri_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(bri_s, bri_slider_cb, LV_EVENT_RELEASED,      NULL);
    lv_obj_clear_flag(bri_s, LV_OBJ_FLAG_GESTURE_BUBBLE);

    return page;
}

static lv_obj_t *build_subpage_autodim(lv_obj_t *menu)
{
    lv_obj_t *page = lv_menu_page_create(menu, (char *)tr(I18N_SET_AUTODIM));
    lv_obj_t *cont = lv_menu_cont_create(page);
    lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cont, 6, 0);

    lv_obj_t *dim_lbl = lv_label_create(cont);
    {
        char d[24];
        fmt_duration(d, sizeof(d), g_cfg.dim_s);
        if (g_cfg.dim_s == 0) lv_label_set_text(dim_lbl, tr(I18N_DIM_NEVER));
        else                  lv_label_set_text_fmt(dim_lbl, tr(I18N_DIM_AFTER), d);
    }
    lv_obj_set_style_text_color(dim_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(dim_lbl, i18n_font(), 0);
    lv_obj_t *dim_s = lv_slider_create(cont);
    lv_obj_set_width(dim_s, lv_pct(95));
    lv_slider_set_range(dim_s, 0, IDLE_SLIDER_MAX);
    lv_slider_set_value(dim_s, g_cfg.dim_s, LV_ANIM_OFF);
    /* Don't let horizontal slider drags bubble up and trigger a tileview
       page swipe. Same for the gesture flag. */
    lv_obj_clear_flag(dim_s, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(dim_s, dim_s_cb, LV_EVENT_VALUE_CHANGED, dim_lbl);
    lv_obj_add_event_cb(dim_s, dim_s_cb, LV_EVENT_RELEASED,      dim_lbl);

    lv_obj_t *off_lbl = lv_label_create(cont);
    {
        char d[24];
        fmt_duration(d, sizeof(d), g_cfg.off_s);
        if (g_cfg.off_s == 0) lv_label_set_text(off_lbl, tr(I18N_SLEEP_NEVER));
        else                  lv_label_set_text_fmt(off_lbl, tr(I18N_SLEEP_AFTER), d);
    }
    lv_obj_set_style_text_color(off_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(off_lbl, i18n_font(), 0);
    lv_obj_t *off_s = lv_slider_create(cont);
    lv_obj_set_width(off_s, lv_pct(95));
    lv_slider_set_range(off_s, 0, IDLE_SLIDER_MAX);
    lv_slider_set_value(off_s, g_cfg.off_s, LV_ANIM_OFF);
    lv_obj_clear_flag(off_s, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(off_s, off_s_cb, LV_EVENT_VALUE_CHANGED, off_lbl);
    lv_obj_add_event_cb(off_s, off_s_cb, LV_EVENT_RELEASED,      off_lbl);

    return page;
}

/* Helper: a labelled toggle row (label on the left, switch on the right). */
static lv_obj_t *add_toggle_row(lv_obj_t *parent, const char *label,
                                bool checked, lv_event_cb_t cb)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *l = lv_label_create(row);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_font(l, i18n_font(), 0);
    lv_obj_t *sw = lv_switch_create(row);
    if (checked) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return row;
}

static void lang_pick_cb(lv_event_t *e)
{
    if (menu_input_blocked()) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "lang_pick: %d", idx);
    i18n_set_lang(idx);
    /* Repaint every label in this language sub-page so the green tick
       moves to the newly-selected language immediately. */
    lv_obj_t *cont = lv_event_get_current_target(e);
    /* Climb to the menu_page (event came from the row, parent is the page). */
    lv_obj_t *page = cont ? lv_obj_get_parent(cont) : NULL;
    if (page) {
        for (uint32_t i = 0; i < lv_obj_get_child_cnt(page); i++) {
            lv_obj_t *row = lv_obj_get_child(page, i);
            if (!row || lv_obj_get_child_cnt(row) == 0) continue;
            lv_obj_t *lbl = lv_obj_get_child(row, 0);
            if (!lbl) continue;
            lv_obj_set_style_text_color(lbl,
                ((int)i == idx) ? lv_color_make(0x40, 0xc0, 0x80)
                                : lv_color_white(), 0);
        }
    }
}

static lv_obj_t *build_subpage_language(lv_obj_t *menu)
{
    lv_obj_t *page = lv_menu_page_create(menu, (char *)tr(I18N_SET_LANGUAGE));
    lv_obj_set_scroll_dir(page, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_AUTO);
    int cur = i18n_lang();
    /* One row per supported language; tap to switch. */
    for (int i = 0; i < I18N_LANG_COUNT; i++) {
        lv_obj_t *cont = lv_menu_cont_create(page);
        lv_obj_t *l    = lv_label_create(cont);
        lv_label_set_text(l, k_i18n_lang_names[i]);
        lv_obj_set_style_text_font(l, i18n_font(), 0);
        if (i == cur) {
            lv_obj_set_style_text_color(l, lv_color_make(0x40, 0xc0, 0x80), 0);
        }
        lv_obj_add_flag(cont, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(cont, lang_pick_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
    }
    return page;
}

static lv_obj_t *build_subpage_display(lv_obj_t *menu)
{
    lv_obj_t *page = lv_menu_page_create(menu, (char *)tr(I18N_SET_DISPLAY));
    lv_obj_set_scroll_dir(page, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_t *cont = lv_menu_cont_create(page);
    lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cont, 6, 0);

    add_toggle_row(cont, tr(I18N_HOUR_24), g_cfg.hour24, hour_fmt_cb);
    add_toggle_row(cont, tr(I18N_SHOW_SECONDS), g_cfg.show_seconds, show_sec_cb);
    add_toggle_row(cont, tr(I18N_SHOW_MS), g_cfg.show_ms, show_ms_cb);
    add_toggle_row(cont, tr(I18N_SHOW_FPS), g_cfg.show_fps, show_fps_cb);

    lv_obj_t *df_l = lv_label_create(cont);
    lv_label_set_text(df_l, tr(I18N_DATE_FORMAT));
    lv_obj_set_style_text_font(df_l, i18n_font(), 0);
    lv_obj_t *df = lv_dropdown_create(cont);
    lv_dropdown_set_options_static(df,
        "YYYY.MM.DD\nDD.MM.YYYY\nMM.DD.YYYY");
    lv_dropdown_set_selected(df, g_cfg.date_fmt);
    lv_obj_add_event_cb(df, date_fmt_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *th_l = lv_label_create(cont);
    lv_label_set_text(th_l, tr(I18N_THEME));
    lv_obj_set_style_text_font(th_l, i18n_font(), 0);
    lv_obj_t *th = lv_dropdown_create(cont);
    /* Build a "Dark\nLight\nHigh contrast"-style options string in a static
       buffer so the dropdown can keep using it. Refreshed on each entry to
       this sub-page so it picks up the active language. */
    static char theme_opts[96];
    snprintf(theme_opts, sizeof(theme_opts), "%s\n%s\n%s",
             tr(I18N_THEME_DARK), tr(I18N_THEME_LIGHT), tr(I18N_THEME_HICONTRAST));
    lv_dropdown_set_options_static(th, theme_opts);
    lv_dropdown_set_selected(th, g_cfg.theme);
    lv_obj_add_event_cb(th, theme_cb, LV_EVENT_VALUE_CHANGED, NULL);

    return page;
}

static lv_obj_t *build_subpage_sound(lv_obj_t *menu)
{
    lv_obj_t *page = lv_menu_page_create(menu, (char *)tr(I18N_SET_SOUND));
    lv_obj_t *cont = lv_menu_cont_create(page);
    lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cont, 6, 0);

    add_toggle_row(cont, tr(I18N_SOUND_ENABLED), g_cfg.audio_enable, audio_en_cb);

    lv_obj_t *vol_lbl = lv_label_create(cont);
    lv_label_set_text_fmt(vol_lbl, tr(I18N_VOLUME_PCT), (unsigned)g_cfg.audio_volume);
    lv_obj_set_style_text_font(vol_lbl, i18n_font(), 0);

    lv_obj_t *vol_s = lv_slider_create(cont);
    lv_obj_set_width(vol_s, lv_pct(95));
    lv_slider_set_range(vol_s, 0, 100);
    lv_slider_set_value(vol_s, g_cfg.audio_volume, LV_ANIM_OFF);
    lv_obj_clear_flag(vol_s, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(vol_s, audio_vol_cb, LV_EVENT_VALUE_CHANGED, vol_lbl);
    lv_obj_add_event_cb(vol_s, audio_vol_cb, LV_EVENT_RELEASED,      vol_lbl);

    return page;
}

static lv_obj_t *build_subpage_reset(lv_obj_t *menu)
{
    lv_obj_t *page = lv_menu_page_create(menu, (char *)tr(I18N_SET_RESET));
    lv_obj_t *cont = lv_menu_cont_create(page);
    lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cont, 6, 0);

    lv_obj_t *l = lv_label_create(cont);
    lv_label_set_text(l, tr(I18N_RESET_WARN));
    lv_obj_set_style_text_font(l, i18n_font(), 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, lv_pct(95));

    lv_obj_t *btn = lv_btn_create(cont);
    lv_obj_set_height(btn, 32);
    lv_obj_set_width(btn, 160);
    lv_obj_set_style_bg_color(btn, lv_color_make(0xa0, 0x20, 0x20), 0);
    lv_obj_t *bl = lv_label_create(btn);
    lv_label_set_text(bl, tr(I18N_RESET_BTN));
    lv_obj_set_style_text_color(bl, lv_color_white(), 0);
    lv_obj_set_style_text_font(bl, i18n_font(), 0);
    lv_obj_center(bl);
    lv_obj_add_event_cb(btn, reset_confirm_cb, LV_EVENT_CLICKED, NULL);

    return page;
}

/* Storage page widgets we update after a format finishes. */
static lv_obj_t *g_storage_info_lbl = NULL;
static lv_obj_t *g_storage_btn_lbl  = NULL;

static void storage_info_refresh(void)
{
    if (!g_storage_info_lbl) return;
    if (!sdcard_is_mounted()) {
        lv_label_set_text(g_storage_info_lbl, "SD: not mounted");
        return;
    }
    uint64_t total = 0, free = 0;
    if (esp_vfs_fat_info("/sdcard", &total, &free) == ESP_OK && total > 0) {
        lv_label_set_text_fmt(g_storage_info_lbl, "SD: %llu MB free of %llu MB",
                              (unsigned long long)(free / (1024 * 1024)),
                              (unsigned long long)(total / (1024 * 1024)));
    } else {
        lv_label_set_text(g_storage_info_lbl, "SD: read err");
    }
}

static void sd_format_worker(void *arg)
{
    (void)arg;
    /* Stop any ongoing recording first; the file handle would be invalid
       across an unmount/reformat. */
    if (recorder_is_recording()) recorder_stop();
    esp_err_t r = sdcard_format();
    ESP_LOGI(TAG, "sd format result: %s", esp_err_to_name(r));
    /* mkdir the recordings folder back; format leaves the root empty. */
    mkdir("/sdcard/recordings", 0775);
    /* LVGL touch: must run on the LVGL task. We borrow the safe
       cross-task path by setting flags and letting the LVGL timer pick
       them up. Simpler: just call from the worker since lv_label_set
       is OK if no-one is reading on the LVGL side simultaneously --
       but the cleanest is to set a "needs refresh" flag and let the
       recorder tile's poll handle it. We log here and let the next
       poll repaint. */
    storage_info_refresh();
    if (g_storage_btn_lbl) {
        lv_label_set_text(g_storage_btn_lbl,
                          r == ESP_OK ? "Format SD card"
                                      : "Format failed");
    }
    recorder_refresh_list();
    vTaskDelete(NULL);
}

static void sd_format_cb(lv_event_t *e)
{
    (void)e;
    if (menu_input_blocked()) return;
    if (g_storage_btn_lbl) lv_label_set_text(g_storage_btn_lbl, "Formatting...");
    /* Format on a worker so the LVGL task keeps drawing. 64GB FAT32
       format takes ~30-60s on this controller. */
    xTaskCreatePinnedToCore(sd_format_worker, "sd_fmt", 4 * 1024,
                            NULL, 4, NULL, 1);
}

static lv_obj_t *build_subpage_storage(lv_obj_t *menu)
{
    lv_obj_t *page = lv_menu_page_create(menu, (char *)tr(I18N_SET_STORAGE));
    lv_obj_t *cont = lv_menu_cont_create(page);
    lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cont, 6, 0);

    g_storage_info_lbl = lv_label_create(cont);
    lv_obj_set_style_text_font(g_storage_info_lbl, i18n_font(), 0);
    lv_obj_set_style_text_color(g_storage_info_lbl, lv_color_white(), 0);
    /* Don't call esp_vfs_fat_info at tile-build time -- on some cards it
       hangs the SDMMC driver for many seconds and wedges boot. Show a
       static placeholder; storage_info_refresh() is invoked from the
       sd_format_worker after a format completes. */
    lv_label_set_text(g_storage_info_lbl,
                      sdcard_is_mounted() ? "SD: mounted" : "SD: not mounted");

    /* Format button: full FAT32 reformat via esp_vfs_fat_sdcard_format.
       Wipes everything on the card. Runs on a worker task so the UI
       stays responsive (~30-60s for 64GB). */
    lv_obj_t *fmt_btn = lv_btn_create(cont);
    lv_obj_set_size(fmt_btn, 200, 32);
    lv_obj_set_style_bg_color(fmt_btn, lv_color_make(0xa0, 0x20, 0x20), 0);
    g_storage_btn_lbl = lv_label_create(fmt_btn);
    lv_label_set_text(g_storage_btn_lbl, "Format SD card");
    lv_obj_set_style_text_color(g_storage_btn_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(g_storage_btn_lbl, i18n_font(), 0);
    lv_obj_center(g_storage_btn_lbl);
    lv_obj_add_event_cb(fmt_btn, sd_format_cb, LV_EVENT_CLICKED, NULL);
    return page;
}

/* Wi-Fi sub-page is built earlier; add the auto-connect toggle there. */
static void wifi_subpage_add_autoconnect(lv_obj_t *page)
{
    /* page contents: menu_cont -> [list, side]. Toggle goes inside side. */
    lv_obj_t *menu_cont = lv_obj_get_child(page, 0);
    if (!menu_cont || lv_obj_get_child_cnt(menu_cont) < 2) return;
    lv_obj_t *side = lv_obj_get_child(menu_cont, 1);
    if (!side) return;
    add_toggle_row(side, tr(I18N_WIFI_AUTOCONNECT), g_cfg.wifi_autoconnect, wifi_ac_cb);
}

static void build_settings_tile(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 0, 0);

    theme_palette_t pal = theme_get();
    lv_obj_t *menu = lv_menu_create(parent);
    lv_obj_set_size(menu, lv_pct(100), lv_pct(100));
    /* Show a back button on sub-pages, none on the root list. */
    lv_menu_set_mode_header(menu, LV_MENU_HEADER_TOP_FIXED);
    lv_menu_set_mode_root_back_btn(menu, LV_MENU_ROOT_BACK_BTN_DISABLED);
    lv_obj_set_style_bg_color(menu, pal.menu_surf, 0);
    lv_obj_set_style_text_color(menu, pal.text, 0);
    /* Use the i18n font (CJK glyphs at 14 px with Latin fallback) so labels
       in zh/ja/ko render. Latin-only labels still pick the Montserrat
       fallback, so this is safe even in en mode. */
    lv_obj_set_style_text_font(menu, i18n_font(), 0);

    /* Header bar: title fills the middle, back button anchored right. */
    lv_obj_t *hdr = lv_menu_get_main_header(menu);
    if (hdr) {
        lv_obj_set_style_bg_color(hdr, pal.menu_hdr, 0);
        lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(hdr, 4, 0);
        lv_obj_set_style_pad_gap(hdr, 6, 0);
        /* Children stay packed at the start; the title gets flex-grow so it
           expands to fill the middle and the back button rides at the end. */
        lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    }
    lv_obj_t *title_lbl = lv_menu_get_main_header(menu)
                            ? lv_obj_get_child(lv_menu_get_main_header(menu), 1)
                            : NULL;
    if (title_lbl) {
        lv_obj_set_flex_grow(title_lbl, 1);
        lv_obj_set_style_text_align(title_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(title_lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(title_lbl, i18n_font(), 0);
    }
    lv_obj_t *back = lv_menu_get_main_header_back_btn(menu);
    if (back) {
        lv_obj_set_size(back, 60, 32);
        lv_obj_set_style_bg_color(back, pal.menu_btn, 0);
        lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(back, 4, 0);
        /* Stamp a debounce window so the same touch that pops the page
           doesn't also trigger the city/wifi/reset action it lands on. */
        lv_obj_add_event_cb(back, menu_back_clicked_cb, LV_EVENT_CLICKED, NULL);
        /* lv_menu already added an lv_img arrow as the first child; hide it
           so we don't render two stacked arrows. */
        if (lv_obj_get_child_cnt(back) > 0) {
            lv_obj_add_flag(lv_obj_get_child(back, 0), LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_t *txt = lv_label_create(back);
        lv_label_set_text_fmt(txt, LV_SYMBOL_LEFT " %s", tr(I18N_BACK));
        lv_obj_set_style_text_color(txt, lv_color_white(), 0);
        lv_obj_set_style_text_font(txt, i18n_font(), 0);
        lv_obj_center(txt);
    }

    /* Build sub-pages first; the main page links them. */
    lv_obj_t *p_wifi = build_subpage_wifi(menu);
    wifi_subpage_add_autoconnect(p_wifi);
    lv_obj_t *p_tz    = build_subpage_tz(menu);
    lv_obj_t *p_bri   = build_subpage_brightness(menu);
    lv_obj_t *p_dim   = build_subpage_autodim(menu);
    lv_obj_t *p_disp  = build_subpage_display(menu);
    lv_obj_t *p_snd   = build_subpage_sound(menu);
    lv_obj_t *p_lang  = build_subpage_language(menu);
    lv_obj_t *p_reset = build_subpage_reset(menu);
    lv_obj_t *p_storage = build_subpage_storage(menu);

    /* Main (root) page: list of menu items. Scrolls vertically if
       there are more entries than fit on the 172 px tall canvas. */
    lv_obj_t *main_page = lv_menu_page_create(menu, (char *)tr(I18N_MENU_TITLE));
    lv_obj_set_scroll_dir(main_page, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(main_page, LV_SCROLLBAR_MODE_AUTO);
    struct { const char *icon; i18n_key_t key; lv_obj_t *page; } rows[] = {
        { LV_SYMBOL_WIFI,     I18N_SET_WIFI,       p_wifi  },
        { LV_SYMBOL_BELL,     I18N_SET_TZ,         p_tz    },
        { LV_SYMBOL_IMAGE,    I18N_SET_DISPLAY,    p_disp  },
        { LV_SYMBOL_AUDIO,    I18N_SET_SOUND,      p_snd   },
        { LV_SYMBOL_EYE_OPEN, I18N_SET_BRIGHTNESS, p_bri   },
        { LV_SYMBOL_POWER,    I18N_SET_AUTODIM,    p_dim   },
        { LV_SYMBOL_SD_CARD,  I18N_SET_STORAGE,    p_storage },
        { LV_SYMBOL_TRASH,    I18N_SET_RESET,      p_reset },
        { LV_SYMBOL_KEYBOARD, I18N_SET_LANGUAGE,   p_lang  },
    };
    for (size_t i = 0; i < sizeof(rows)/sizeof(rows[0]); i++) {
        lv_obj_t *cont = lv_menu_cont_create(main_page);
        lv_obj_t *l    = lv_label_create(cont);
        lv_label_set_text_fmt(l, "%s  %s", rows[i].icon, tr(rows[i].key));
        lv_obj_set_style_text_font(l, i18n_font(), 0);
        lv_menu_set_load_page_event(menu, cont, rows[i].page);
        /* Same input-shield trick as the back button: stamp the debounce
           when a row is clicked so the touch that loaded the new page
           can't also fire a click on whatever item lands under the
           finger on that new page. */
        lv_obj_add_event_cb(cont, menu_back_clicked_cb, LV_EVENT_CLICKED, NULL);
    }

    lv_menu_set_page(menu, main_page);
}

/* ---------------------- Recorder tile ---------------------- */

/* Recorder tile widgets. Single full-width view:
   - Top: status text ("Idle" / "Recording  Ns")
   - Middle: big round REC/STOP button
   - Below button: "Recordings ▶" button to open the list overlay
   - Bottom row: stereo VU bars  L |||||||----    ----|||||||| R
   The list of recordings is a separate full-tile overlay shown over
   the parent tile when the user taps "Recordings"; closes on a
   "Back" button. No split screen. */
static lv_obj_t *g_rec_tile         = NULL;
static lv_obj_t *g_rec_status       = NULL;
static lv_obj_t *g_rec_btn_lbl      = NULL;
static lv_obj_t *g_rec_vu_l         = NULL;   /* left bar (grows toward left) */
static lv_obj_t *g_rec_vu_r         = NULL;   /* right bar (grows toward right) */
static lv_obj_t *g_rec_list_overlay = NULL;
static lv_obj_t *g_rec_list         = NULL;
static lv_timer_t *g_rec_poll       = NULL;
static int        g_rec_vu_l_smooth = 0;
static int        g_rec_vu_r_smooth = 0;

static void recorder_refresh_list(void);

static void rec_play_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) return;
    if (menu_input_blocked()) return;
    const char *name = (const char *)lv_event_get_user_data(e);
    if (!name || !*name) return;
    char path[128];
    snprintf(path, sizeof(path), "file://sdcard/recordings/%s", name);
    if (radio_is_playing()) radio_stop();
    radio_play(path);
}

static void rec_delete_cb(lv_event_t *e)
{
    if (menu_input_blocked()) return;
    const char *name = (const char *)lv_event_get_user_data(e);
    recorder_delete(name);
    recorder_refresh_list();
}

static void recorder_refresh_list(void)
{
    if (!g_rec_list) return;
    lv_obj_clean(g_rec_list);
    static char names[16][64];
    int n = recorder_list(names, 16);
    if (n == 0) {
        lv_obj_t *l = lv_label_create(g_rec_list);
        lv_label_set_text(l, "(no recordings)");
        lv_obj_set_style_text_color(l, lv_color_make(0xa0, 0xa0, 0xa0), 0);
        lv_obj_set_style_text_font(l, i18n_font(), 0);
        return;
    }
    for (int i = 0; i < n; i++) {
        /* Tappable row: tap row body = play; tap trash = delete.
           Filename on the left (big), size/duration beneath (small),
           trash icon on the right. Whole row is itself the click
           target so the touch target is huge and we don't fight LVGL
           about which child eats the click. */
        lv_obj_t *row = lv_btn_create(g_rec_list);
        lv_obj_set_size(row, lv_pct(100), 48);
        lv_obj_set_style_bg_color(row, lv_color_make(0x18, 0x40, 0x28), 0);
        lv_obj_set_style_pad_all(row, 6, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(row, rec_play_cb, LV_EVENT_CLICKED,
                            (void *)names[i]);

        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, names[i]);
        lv_obj_set_style_text_color(name, lv_color_white(), 0);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 0);

        lv_obj_t *meta = lv_label_create(row);
        uint32_t bytes = 0, dur_ms = 0;
        recorder_file_info(names[i], &bytes, &dur_ms);
        char meta_buf[48];
        if (bytes >= 1024 * 1024) {
            snprintf(meta_buf, sizeof(meta_buf), "%.1f MB  %u.%01us",
                     bytes / (1024.0 * 1024.0),
                     (unsigned)(dur_ms / 1000),
                     (unsigned)((dur_ms / 100) % 10));
        } else {
            snprintf(meta_buf, sizeof(meta_buf), "%u KB  %u.%01us",
                     (unsigned)(bytes / 1024),
                     (unsigned)(dur_ms / 1000),
                     (unsigned)((dur_ms / 100) % 10));
        }
        lv_label_set_text(meta, meta_buf);
        lv_obj_set_style_text_color(meta, lv_color_make(0xc0, 0xc0, 0xc0), 0);
        lv_obj_set_style_text_font(meta, &lv_font_montserrat_12, 0);
        lv_obj_align(meta, LV_ALIGN_BOTTOM_LEFT, 0, 0);

        /* Trash button: anchored right, eats its own click event so the
           row's play handler doesn't also fire on delete. */
        lv_obj_t *del = lv_btn_create(row);
        lv_obj_set_size(del, 40, 32);
        lv_obj_align(del, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_color(del, lv_color_make(0x80, 0x40, 0x20), 0);
        lv_obj_t *dl = lv_label_create(del);
        lv_label_set_text(dl, LV_SYMBOL_TRASH);
        lv_obj_set_style_text_color(dl, lv_color_white(), 0);
        lv_obj_set_style_text_font(dl, &lv_font_montserrat_16, 0);
        lv_obj_center(dl);
        lv_obj_add_event_cb(del, rec_delete_cb, LV_EVENT_CLICKED,
                            (void *)names[i]);
    }
}

static void rec_btn_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "rec_btn tap: blocked=%d recording=%d",
             (int)menu_input_blocked(), (int)recorder_is_recording());
    if (menu_input_blocked()) return;
    if (recorder_is_recording()) {
        esp_err_t r = recorder_stop();
        ESP_LOGI(TAG, "rec_btn stop -> %s", esp_err_to_name(r));
        recorder_refresh_list();   /* no-op if overlay isn't open */
    } else {
        const char *path = NULL;
        esp_err_t r = recorder_start(&path);
        ESP_LOGI(TAG, "rec_btn start -> %s path=%s",
                 esp_err_to_name(r), path ? path : "(null)");
    }
}

static int peak_to_pct(uint16_t peak)
{
    if (peak == 0) return 0;
    int log2v = 0;
    uint32_t v = peak;
    while (v >>= 1) log2v++;          /* log2(32767) ~= 15 */
    int t = (log2v * 100) / 15;
    return t > 100 ? 100 : t;
}

static void rec_poll_cb(lv_timer_t *t)
{
    (void)t;
    /* Retry monitor start: at boot time the radio engine is warmed
       *after* show_main_ui, so build_recorder_tile's first call to
       recorder_monitor_start() fails (radio engine not yet up). The
       worker is what feeds peak data to the VU; without it the bars
       sit at zero. recorder_monitor_start is idempotent. */
    static bool s_monitor_running = false;
    if (!s_monitor_running) {
        if (recorder_monitor_start() == ESP_OK) s_monitor_running = true;
    }
    bool recording = recorder_is_recording();
    bool playing   = radio_is_playing();
    if (g_rec_btn_lbl) {
        lv_label_set_text(g_rec_btn_lbl, recording ? LV_SYMBOL_STOP : "REC");
    }
    if (g_rec_status) {
        if (recording) {
            lv_label_set_text_fmt(g_rec_status, LV_SYMBOL_AUDIO " REC  %us",
                                  recorder_elapsed_s());
            lv_obj_set_style_text_color(g_rec_status, lv_color_make(0xff, 0x40, 0x40), 0);
        } else if (playing) {
            /* Show "▶ Playing <basename>" so the user can tell that
               radio_play actually fired even if the speaker output
               sounds quiet. The VU bars below also flip to the
               decoder's output level so they reflect playback, not
               the mic. */
            const char *uri = radio_current_uri();
            const char *base = uri ? uri : "stream";
            const char *slash = NULL;
            if (uri) {
                for (const char *p = uri; *p; p++) if (*p == '/') slash = p;
                if (slash) base = slash + 1;
            }
            lv_label_set_text_fmt(g_rec_status, LV_SYMBOL_PLAY " Playing %s", base);
            lv_obj_set_style_text_color(g_rec_status, lv_color_make(0x40, 0xc0, 0xff), 0);
        } else {
            lv_label_set_text(g_rec_status, "Idle");
            lv_obj_set_style_text_color(g_rec_status, lv_color_white(), 0);
        }
    }
    /* Stereo L/R VU. Source flips between mic input and decoder
       output: when playback is active, show the decoded PCM peak so
       the user can confirm the player is actually emitting samples
       (independent of speaker volume); otherwise show mic input from
       the recorder's monitor loop. Always read both peak buffers so
       neither accumulates stale residue across modes. */
    uint16_t mic_l = 0, mic_r = 0;
    uint16_t out_l = 0, out_r = 0;
    recorder_peak_lr(&mic_l, &mic_r);
    radio_out_peak(&out_l, &out_r);
    uint16_t pl = playing ? out_l : mic_l;
    uint16_t pr = playing ? out_r : mic_r;
    int tl = peak_to_pct(pl);
    int tr = peak_to_pct(pr);
    if (tl > g_rec_vu_l_smooth) g_rec_vu_l_smooth = tl;
    else g_rec_vu_l_smooth = (g_rec_vu_l_smooth * 7 + tl) / 8;
    if (tr > g_rec_vu_r_smooth) g_rec_vu_r_smooth = tr;
    else g_rec_vu_r_smooth = (g_rec_vu_r_smooth * 7 + tr) / 8;
    /* Visual hint: tint the indicator blue while playback is active,
       green during normal mic monitoring. */
    if (g_rec_vu_l) {
        lv_obj_set_style_bg_color(g_rec_vu_l,
            playing ? lv_color_make(0x40, 0xa0, 0xff)
                    : lv_color_make(0x30, 0xc0, 0x40),
            LV_PART_INDICATOR);
        lv_bar_set_value(g_rec_vu_l, g_rec_vu_l_smooth, LV_ANIM_OFF);
    }
    if (g_rec_vu_r) {
        lv_obj_set_style_bg_color(g_rec_vu_r,
            playing ? lv_color_make(0x40, 0xa0, 0xff)
                    : lv_color_make(0x30, 0xc0, 0x40),
            LV_PART_INDICATOR);
        lv_bar_set_value(g_rec_vu_r, g_rec_vu_r_smooth, LV_ANIM_OFF);
    }
}

static void rec_list_close_cb(lv_event_t *e)
{
    (void)e;
    if (g_rec_list_overlay) {
        lv_obj_del(g_rec_list_overlay);
        g_rec_list_overlay = NULL;
        g_rec_list = NULL;
    }
}

static void rec_list_open_cb(lv_event_t *e)
{
    (void)e;
    if (menu_input_blocked()) return;
    if (g_rec_list_overlay) return;
    /* Mount the overlay on the active screen's TOP LAYER so the
       tileview can't capture our taps as start-of-swipe gestures.
       Top layer is above the tileview in the input dispatch order. */
    g_rec_list_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_rec_list_overlay);
    lv_obj_set_size(g_rec_list_overlay, canvas_w, canvas_h);
    lv_obj_align(g_rec_list_overlay, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(g_rec_list_overlay, lv_color_make(0x10, 0x10, 0x18), 0);
    lv_obj_set_style_bg_opa(g_rec_list_overlay, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g_rec_list_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(g_rec_list_overlay, 4, 0);

    lv_obj_t *back = lv_btn_create(g_rec_list_overlay);
    lv_obj_set_size(back, 56, 22);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(back, lv_color_make(0x40, 0x40, 0x60), 0);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(bl, lv_color_white(), 0);
    lv_obj_set_style_text_font(bl, i18n_font(), 0);
    lv_obj_center(bl);
    lv_obj_add_event_cb(back, rec_list_close_cb, LV_EVENT_CLICKED, NULL);

    g_rec_list = lv_obj_create(g_rec_list_overlay);
    lv_obj_remove_style_all(g_rec_list);
    /* Fill the overlay below the back bar (back is 22 px + 4 px pad). */
    lv_obj_set_size(g_rec_list, lv_pct(100), lv_pct(85));
    lv_obj_align(g_rec_list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_layout(g_rec_list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(g_rec_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g_rec_list, 2, 0);
    lv_obj_set_style_pad_all(g_rec_list, 4, 0);
    lv_obj_set_style_bg_opa(g_rec_list, LV_OPA_TRANSP, 0);
    lv_obj_set_scroll_dir(g_rec_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_rec_list, LV_SCROLLBAR_MODE_AUTO);
    recorder_refresh_list();
}

static void build_recorder_tile(lv_obj_t *parent)
{
    g_rec_tile = parent;
    lv_obj_set_style_bg_color(parent, lv_color_make(0x10, 0x10, 0x18), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 4, 0);

    /* Tile is 172 px wide (rotated) x 640 px tall in our LVGL coord
       system. Layout from top: status, REC button, recordings button,
       VU at the very bottom. */

    /* The display is rotated landscape: 640 wide x 172 tall. We lay
       the recorder tile out left -> right:
         [ status + Recordings button ]   [ big REC ]   [ L/R VU ]
       Status text is on the left.
       REC button is in the center.
       L/R VU stacked on the right. */

    /* Left column: status + recordings button. */
    g_rec_status = lv_label_create(parent);
    lv_label_set_text(g_rec_status, "Idle");
    lv_obj_set_style_text_color(g_rec_status, lv_color_white(), 0);
    lv_obj_set_style_text_font(g_rec_status, i18n_font(), 0);
    lv_obj_align(g_rec_status, LV_ALIGN_LEFT_MID, 12, -32);

    lv_obj_t *list_btn = lv_btn_create(parent);
    lv_obj_set_size(list_btn, 150, 36);
    lv_obj_align(list_btn, LV_ALIGN_LEFT_MID, 12, 24);
    lv_obj_set_style_bg_color(list_btn, lv_color_make(0x40, 0x40, 0x60), 0);
    lv_obj_t *lbl = lv_label_create(list_btn);
    lv_label_set_text(lbl, "Recordings " LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, i18n_font(), 0);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(list_btn, rec_list_open_cb, LV_EVENT_CLICKED, NULL);

    /* Center: big round REC button. Tile is 172 px tall so the button
       can be ~120 px without clipping; height is the constraint. */
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 130, 130);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(btn, 65, 0);
    lv_obj_set_style_bg_color(btn, lv_color_make(0xc0, 0x30, 0x30), 0);
    lv_obj_add_event_cb(btn, rec_btn_cb, LV_EVENT_CLICKED, NULL);
    g_rec_btn_lbl = lv_label_create(btn);
    lv_label_set_text(g_rec_btn_lbl, "REC");
    lv_obj_set_style_text_color(g_rec_btn_lbl, lv_color_white(), 0);
    /* User asked for 2x bigger than the default i18n font (~14 px).
       Only Montserrat 12/14/16/48 are compiled in this build, so 48 is
       the closest "much bigger". */
    lv_obj_set_style_text_font(g_rec_btn_lbl, &lv_font_montserrat_48, 0);
    lv_obj_center(g_rec_btn_lbl);

    /* Right side: stereo L/R VU stacked vertically.
         L: ████████░░░░ (top bar)
         R: ████████░░░░ (bottom bar)
       Bars are normal LTR-fill. Width 130 px, fits comfortably in the
       remaining ~150 px on the right. */
    lv_obj_t *l_lbl = lv_label_create(parent);
    lv_label_set_text(l_lbl, "L");
    lv_obj_set_style_text_color(l_lbl, lv_color_make(0xa0, 0xa0, 0xa0), 0);
    lv_obj_set_style_text_font(l_lbl, i18n_font(), 0);
    lv_obj_align(l_lbl, LV_ALIGN_RIGHT_MID, -150, -22);

    g_rec_vu_l = lv_bar_create(parent);
    lv_obj_set_size(g_rec_vu_l, 130, 14);
    lv_obj_align(g_rec_vu_l, LV_ALIGN_RIGHT_MID, -8, -22);
    lv_bar_set_range(g_rec_vu_l, 0, 100);
    lv_obj_set_style_bg_color(g_rec_vu_l, lv_color_make(0x30, 0x30, 0x30), 0);
    lv_obj_set_style_bg_color(g_rec_vu_l, lv_color_make(0x30, 0xc0, 0x40), LV_PART_INDICATOR);
    lv_obj_set_style_radius(g_rec_vu_l, 3, 0);
    lv_obj_set_style_radius(g_rec_vu_l, 3, LV_PART_INDICATOR);

    lv_obj_t *r_lbl = lv_label_create(parent);
    lv_label_set_text(r_lbl, "R");
    lv_obj_set_style_text_color(r_lbl, lv_color_make(0xa0, 0xa0, 0xa0), 0);
    lv_obj_set_style_text_font(r_lbl, i18n_font(), 0);
    lv_obj_align(r_lbl, LV_ALIGN_RIGHT_MID, -150, 22);

    g_rec_vu_r = lv_bar_create(parent);
    lv_obj_set_size(g_rec_vu_r, 130, 14);
    lv_obj_align(g_rec_vu_r, LV_ALIGN_RIGHT_MID, -8, 22);
    lv_bar_set_range(g_rec_vu_r, 0, 100);
    lv_obj_set_style_bg_color(g_rec_vu_r, lv_color_make(0x30, 0x30, 0x30), 0);
    lv_obj_set_style_bg_color(g_rec_vu_r, lv_color_make(0x30, 0xc0, 0x40), LV_PART_INDICATOR);
    lv_obj_set_style_radius(g_rec_vu_r, 3, 0);
    lv_obj_set_style_radius(g_rec_vu_r, 3, LV_PART_INDICATOR);

    if (!g_rec_poll) {
        g_rec_poll = lv_timer_create(rec_poll_cb, 100, NULL);
    }
    /* Live VU regardless of recording state. */
    if (recorder_monitor_start() != ESP_OK) {
        ESP_LOGW(TAG, "recorder monitor start failed");
    }
}

/* ---------------------- Top-level UI builder ---------------------- */

static void build_main_ui(const char *status_text)
{
    static bool fps_timer_created = false;

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    g_tileview = lv_tileview_create(scr);
    lv_obj_set_size(g_tileview, canvas_w, canvas_h);
    lv_obj_set_style_bg_color(g_tileview, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_tileview, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(g_tileview, LV_SCROLLBAR_MODE_OFF);

    /* 4-tile loop: Clock <-> Settings <-> Radio <-> Recorder <-> Clock.
       LVGL tileview doesn't natively wrap; the wrap-around between tile 0
       and tile 3 is handled by the gesture cb installed below. */
    lv_obj_t *t_clock  = lv_tileview_add_tile(g_tileview, 0, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_t *t_set    = lv_tileview_add_tile(g_tileview, 1, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_t *t_radio  = lv_tileview_add_tile(g_tileview, 2, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_t *t_record = lv_tileview_add_tile(g_tileview, 3, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    (void)status_text;

    build_clock_tile(t_clock);
    build_settings_tile(t_set);
    build_radio_tile(t_radio);
    build_recorder_tile(t_record);

    /* Wrap-around: when the user swipes left on tile 0 or right on tile 2,
       LVGL's tileview just snaps back. Catch the indev gesture on the
       active screen and jump to the opposite end so the loop feels like
       a true ring. */
    lv_obj_add_event_cb(g_tileview, [](lv_event_t *e) {
        lv_indev_t *indev = lv_indev_get_act();
        if (!indev) return;
        lv_dir_t dir = lv_indev_get_gesture_dir(indev);
        lv_obj_t *tv  = lv_event_get_target(e);
        lv_coord_t x = lv_obj_get_scroll_x(tv);
        lv_coord_t w = lv_obj_get_width(tv);
        int idx = (w > 0) ? (x + w / 2) / w : 0;
        if (dir == LV_DIR_LEFT && idx == 3) {
            lv_obj_set_tile_id(tv, 0, 0, LV_ANIM_OFF);
        } else if (dir == LV_DIR_RIGHT && idx == 0) {
            lv_obj_set_tile_id(tv, 3, 0, LV_ANIM_OFF);
        }
    }, LV_EVENT_GESTURE, NULL);

    /* iOS-style commit threshold: hold the tileview at its current snap
       position until the X drift exceeds 50 px. Up to that point, every
       SCROLL event is reverted to the locked-in tile so a sloppy tap or a
       small accidental drift can never drag the page. Once the drift is
       large enough, the user is clearly committing to a swipe and we let
       LVGL's native snap take over. */
    lv_obj_add_event_cb(g_tileview, [](lv_event_t *e) {
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
            lv_point_t p; lv_indev_get_point(id, &p);
            int dx = (int)p.x - (int)s_press_x;
            if (dx > 50 || dx < -50) {
                s_committed = true;
            } else {
                /* Snap back to the locked tile mid-drag. */
                lv_obj_scroll_to_x(tv, s_locked_x, LV_ANIM_OFF);
            }
        } else if (c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST) {
            s_committed = false;
        }
    }, LV_EVENT_ALL, NULL);

    /* FPS overlay: parented to the screen (not the tileview) so it
       floats above every tile. */
    fps_label = lv_label_create(scr);
    lv_label_set_text(fps_label, "FPS --");
    lv_obj_set_style_text_color(fps_label, lv_color_make(0x00, 0xff, 0x80), 0);
    lv_obj_set_style_text_font(fps_label, i18n_font(), 0);
    lv_obj_set_style_bg_color(fps_label, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(fps_label, LV_OPA_60, 0);
    lv_obj_set_style_pad_hor(fps_label, 3, 0);
    lv_obj_align(fps_label, LV_ALIGN_TOP_LEFT, 4, 4);
    lv_obj_clear_flag(fps_label, LV_OBJ_FLAG_CLICKABLE);
    if (!g_cfg.show_fps) lv_obj_add_flag(fps_label, LV_OBJ_FLAG_HIDDEN);

    if (!fps_timer_created) {
        lv_timer_create(fps_timer_cb, 3000, NULL);
        fps_timer_created = true;
    }

    /* Wi-Fi/BT status icons + settings page status text -- 1 Hz. */
    if (!g_status_timer) {
        g_status_timer = lv_timer_create(status_timer_cb, 1000, NULL);
    }

    /* Activity wake: any touch on the screen kicks the dim timer. */
    lv_obj_add_event_cb(scr, activity_kick, LV_EVENT_PRESSED,  NULL);
    lv_obj_add_event_cb(scr, activity_kick, LV_EVENT_RELEASED, NULL);

    /* iOS/Android-style scroll-then-click suppression. Any scroll on any
       descendant bubbles SCROLL_BEGIN / SCROLL / SCROLL_END up to the
       screen. We stamp the time and consult it from menu_input_blocked()
       and the action callbacks so a click that lands within 250 ms of a
       scroll motion is ignored. */
    lv_obj_add_event_cb(scr, [](lv_event_t *e) {
        lv_event_code_t c = lv_event_get_code(e);
        /* Only stamp on SCROLL (mid-motion) -- not BEGIN/END, which can
           fire even on no-op presses and would suppress all clicks. */
        if (c == LV_EVENT_SCROLL) {
            g_last_scroll_ms = lv_tick_get();
        }
    }, LV_EVENT_ALL, NULL);
    g_last_activity_ms = lv_tick_get();
    if (!g_dim_timer) {
        g_dim_timer = lv_timer_create(dim_timer_cb, 1000, NULL);
    }

    /* Start on the clock. */
    lv_obj_set_tile_id(g_tileview, 0, 0, LV_ANIM_OFF);
}

static void show_main_ui(const char *status_text)
{
    /* Cache for redraw on rotation. */
    if (status_text != g_status_text) {
        strncpy(g_status_text, status_text, sizeof(g_status_text) - 1);
        g_status_text[sizeof(g_status_text) - 1] = '\0';
    }
    bool need_lock = (xSemaphoreGetMutexHolder(lvgl_mux) != xTaskGetCurrentTaskHandle());
    if (need_lock && !lvgl_lock(-1)) return;
    build_main_ui(g_status_text);
    if (need_lock) lvgl_unlock();
}

extern "C" void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
    esp_log_level_set("lcd_panel.axs15231b", ESP_LOG_VERBOSE);
    esp_log_level_set("lcd_panel.io.spi", ESP_LOG_VERBOSE);

    /* Persisted user settings: load early so brightness/timezone are
       applied before the UI builds. NVS init must happen before
       Wi-Fi (esp_wifi requires it) and before any cfg_load(). */
    {
        esp_err_t er = nvs_flash_init();
        if (er == ESP_ERR_NVS_NO_FREE_PAGES || er == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK(nvs_flash_erase());
            er = nvs_flash_init();
        }
        ESP_ERROR_CHECK(er);
    }
    cfg_load();
    ESP_LOGI(TAG, "cfg: tz=%s (%s) bri=%u dim=%us off=%us last_ssid=%s",
             tz_current_city_name(), k_tz_cities[g_cfg.tz_idx].posix_tz,
             g_cfg.brightness, g_cfg.dim_s, g_cfg.off_s,
             g_cfg.last_ssid[0] ? g_cfg.last_ssid : "(none)");
    ESP_LOGI(TAG, "===== 12_HelloWorld_Skeleton boot =====");
    ESP_LOGI(TAG, "H_RES=%d V_RES=%d  DMA=%d SPIRAM=%d",
             EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES,
             LVGL_DMA_BUFF_LEN, LVGL_SPIRAM_BUFF_LEN);

    char status[256];
    int  pos = 0;
    pos += snprintf(status + pos, sizeof(status) - pos, "Drivers:\n");

    ESP_LOGI(TAG, "[1/9] I2C buses");
    i2c_master_Init();
    ESP_LOGI(TAG, "      esp_i2c_bus_handle=%p", esp_i2c_bus_handle);
    pos += snprintf(status + pos, sizeof(status) - pos, "I2C OK\n");

    ESP_LOGI(TAG, "[2/9] TCA9554 power rails P6+P7=HIGH");
    {
        esp_io_expander_handle_t io_expander = NULL;
        esp_err_t er = esp_io_expander_new_i2c_tca9554(
            esp_i2c_bus_handle, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &io_expander);
        ESP_LOGI(TAG, "      tca9554 new=%s handle=%p", esp_err_to_name(er), io_expander);
        ESP_ERROR_CHECK(esp_io_expander_set_dir(io_expander,
            IO_EXPANDER_PIN_NUM_7 | IO_EXPANDER_PIN_NUM_6, IO_EXPANDER_OUTPUT));
        ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander,
            IO_EXPANDER_PIN_NUM_7 | IO_EXPANDER_PIN_NUM_6, 1));
        esp_io_expander_print_state(io_expander);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    pos += snprintf(status + pos, sizeof(status) - pos, "TCA9554 OK\n");

    ESP_LOGI(TAG, "[3/9] LCD backlight PWM (from cfg)");
    lcd_bl_pwm_bsp_init((uint16_t)(0xFF - g_cfg.brightness));
    pos += snprintf(status + pos, sizeof(status) - pos, "BL OK\n");

    ESP_LOGI(TAG, "[4/9] LCD panel init");
    esp_lcd_panel_handle_t panel = lcd_init();
    ESP_LOGI(TAG, "      panel handle=%p", panel);

    ESP_LOGI(TAG, "[5/9] LVGL init");
    lvgl_init(panel);
    pos += snprintf(status + pos, sizeof(status) - pos, "LCD/Touch OK\n");

    ESP_LOGI(TAG, "[6/9] RTC + IMU");
    i2c_rtc_setup();
    i2c_imu_setup();
    pos += snprintf(status + pos, sizeof(status) - pos, "RTC/IMU OK\n");

    ESP_LOGI(TAG, "[7/9] ADC battery");
    adc_bsp_init();
    pos += snprintf(status + pos, sizeof(status) - pos, "ADC OK\n");

    ESP_LOGI(TAG, "[8/9] Audio (audio_min: ES8311 + I2S)");
    if (audio_min_init() == ESP_OK) {
        audio_min_set_volume(g_cfg.audio_volume);
        pos += snprintf(status + pos, sizeof(status) - pos, "Audio OK\n");
    } else {
        pos += snprintf(status + pos, sizeof(status) - pos, "Audio FAIL\n");
    }

    ESP_LOGI(TAG, "[9/9] SD card + Buttons");
    _sdcard_init();
    button_Init();
    pos += snprintf(status + pos, sizeof(status) - pos, "SD/Btn OK");

    /* If the RTC is older than the firmware's build time, write the
       build time into the RTC. Each rebuild therefore reseeds the
       clock to within a few seconds of wall time (treated as UTC),
       which avoids the year-2000 default and means the user does
       not have to set the clock manually after a flash. A real-time
       source (NTP, GPS) can later overwrite this. */
    setenv("TZ", "UTC0", 1);
    tzset();
    {
        /* BUILD_EPOCH_UTC is injected by CMake (string(TIMESTAMP ... UTC)) so
           the seed is real UTC regardless of the build machine's timezone. */
        time_t build_epoch = (time_t)BUILD_EPOCH_UTC;
        struct tm bt = {};
        gmtime_r(&build_epoch, &bt);
        int b_y  = bt.tm_year + 1900;
        int b_mo = bt.tm_mon + 1;
        int b_d  = bt.tm_mday;
        int b_h  = bt.tm_hour;
        int b_mi = bt.tm_min;
        int b_s  = bt.tm_sec;

        RtcDateTime_t r = i2c_rtc_get();
        struct tm rt = {};
        rt.tm_year = (int)r.year - 1900;
        rt.tm_mon  = (int)r.month - 1;
        rt.tm_mday = r.day;
        rt.tm_hour = r.hour;
        rt.tm_min  = r.minute;
        rt.tm_sec  = r.second;
        time_t rtc_epoch = mktime(&rt);

        if (rtc_epoch < build_epoch) {
            ESP_LOGI(TAG, "RTC (%04d-%02d-%02d %02d:%02d:%02d) older than build "
                          "(%04d-%02d-%02d %02d:%02d:%02d), reseeding",
                     r.year, r.month, r.day, r.hour, r.minute, r.second,
                     b_y, b_mo, b_d, b_h, b_mi, b_s);
            i2c_rtc_setTime((uint16_t)b_y, (uint8_t)b_mo, (uint8_t)b_d,
                            (uint8_t)b_h, (uint8_t)b_mi, (uint8_t)b_s);
            r = i2c_rtc_get();
        }

        struct tm tm = {};
        tm.tm_year = (int)r.year - 1900;
        tm.tm_mon  = (int)r.month - 1;
        tm.tm_mday = r.day;
        tm.tm_hour = r.hour;
        tm.tm_min  = r.minute;
        tm.tm_sec  = r.second;
        time_t t = mktime(&tm);
        struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "RTC seed: %04d-%02d-%02d %02d:%02d:%02d UTC -> epoch %lld",
                 r.year, r.month, r.day, r.hour, r.minute, r.second, (long long)t);
    }

    /* Switch from the UTC0 used during RTC seeding to the user's selected
       city; localtime_r() now returns local wall-clock for the clock face. */
    tz_apply_current();

    /* Bring up Wi-Fi *before* the LVGL widget tree is built. esp_wifi_init
       needs ~5 DMA-capable rx buffers from internal RAM; with ~250 KiB total
       and the menu's many sub-pages it ran out of buffers if we initialised
       LVGL first. The connect itself still happens later. */
    wifi_init_once();

    show_main_ui(status);
    ESP_LOGI(TAG, "===== All drivers initialized =====");
    cli_start();

    /* Warm up the radio engine in the background so first-tap latency is
       just HTTP+decode, not also codec/I2S setup. Drops audio_min's MIDI
       playback (the I2S channel and ES8311 are now owned by the radio
       engine), but the Hello tile is gone so that demo wasn't reachable
       anyway. */
    radio_engine_warm_at_boot();

    /* Auto-connect at boot using whatever NVS has stored, unless the
       user disabled it in Settings. */
    if (!g_cfg.wifi_autoconnect) {
        ESP_LOGI(TAG, "auto-connect: disabled in settings");
    } else {
        if (!g_cfg.last_ssid[0] && DEFAULT_WIFI_SSID[0]) {
            strncpy(g_cfg.last_ssid, DEFAULT_WIFI_SSID,
                    sizeof(g_cfg.last_ssid) - 1);
        }
        char pass[65] = {0};
        bool have_pass = cfg_get_ssid_pass(g_cfg.last_ssid, pass, sizeof(pass));
        if (!have_pass && DEFAULT_WIFI_SSID[0] && DEFAULT_WIFI_PASS[0] &&
            strcmp(g_cfg.last_ssid, DEFAULT_WIFI_SSID) == 0) {
            strncpy(pass, DEFAULT_WIFI_PASS, sizeof(pass) - 1);
            cfg_save_ssid_pass(g_cfg.last_ssid, pass);
            have_pass = true;
        }
        if (g_cfg.last_ssid[0] && have_pass) {
            ESP_LOGI(TAG, "auto-connect: %s (pass_len=%u)",
                     g_cfg.last_ssid, (unsigned)strlen(pass));
            wifi_connect(g_cfg.last_ssid, pass);
        } else {
            ESP_LOGI(TAG, "auto-connect: no credentials yet (use Settings -> Wi-Fi)");
        }
    }



    uint32_t heartbeat = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        size_t free8  = heap_caps_get_free_size(MALLOC_CAP_8BIT);
        size_t freedma = heap_caps_get_free_size(MALLOC_CAP_DMA);
        size_t freespi = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        ESP_LOGI(TAG, "alive #%lu  frames=%lu  heap8=%u dma=%u spiram=%u",
                 (unsigned long)heartbeat++,
                 (unsigned long)fps_frame_count,
                 (unsigned)free8, (unsigned)freedma, (unsigned)freespi);
    }
}
