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
#include "webui.h"
#include "cli.h"
#include "i18n.h"
#include "landmask.h"
#include "tz_cities.h"

#include "ui_common.h"
#include "ui_radio.h"

static const char *TAG = "skeleton";

extern "C" const lv_font_t font_jbmono_24;
extern "C" const lv_font_t font_jbmono_48;
extern "C" const lv_font_t font_jbmono_64;
extern "C" const lv_font_t font_jbmono_96;

extern "C" void tz_apply_current(void);
extern "C" const char *tz_current_city_name(void);
extern "C" void disp_driver_init(void);

/* ---------------------- Wi-Fi ---------------------- */

wifi_scan_ap_t g_wifi_scan[WIFI_MAX_SCAN_AP];
uint16_t       g_wifi_scan_n = 0;
bool           g_wifi_scanning = false;
static bool    g_wifi_inited = false;
bool           g_wifi_connected = false;
char           g_wifi_curr_ssid[33] = {0};
uint8_t        g_wifi_last_reason = 0;
int8_t         g_wifi_last_rssi = 0;
uint32_t       g_wifi_connect_started_ms = 0;
static bool    g_sntp_started = false;
static time_t  g_last_sntp_sync = 0;

static void wifi_init_once(void);
static void sntp_start_once(void);

/* ---------------------- Backlight + auto-dim ---------------------- */

uint32_t g_last_activity_ms = 0;
int      g_dim_state = 0;

/* ---------------------- IP overlay tag ---------------------- */

static lv_obj_t *g_ip_label = NULL;

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

static void ip_label_set(const char *text)
{
    if (!lvgl_lock(50)) return;
    ip_label_ensure();
    if (text && *text) {
        lv_label_set_text(g_ip_label, text);
        lv_obj_clear_flag(g_ip_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_ip_label, LV_OBJ_FLAG_HIDDEN);
    }
    lvgl_unlock();
}

/* ---------------------- Wi-Fi impl ---------------------- */

static uint8_t g_wifi_fail_count = 0;
static bool    g_wifi_roaming_scan = false;
#define WIFI_FAILS_BEFORE_ROAM 3

static bool wifi_has_remembered(const char *ssid)
{
    if (!ssid || !*ssid) return false;
    char key[16] = {0};
    strncpy(key, ssid, 15);
    nvs_handle_t h;
    if (nvs_open(NVS_NS_WIFI, NVS_READONLY, &h) != ESP_OK) return false;
    size_t l = 0;
    esp_err_t er = nvs_get_str(h, key, NULL, &l);
    nvs_close(h);
    return er == ESP_OK;
}

static void wifi_kick_roam_scan(void)
{
    if (g_wifi_scanning) return;
    wifi_scan_config_t sc = {};
    sc.show_hidden = false;
    g_wifi_roaming_scan = true;
    g_wifi_scanning = true;
    esp_err_t er = esp_wifi_scan_start(&sc, false);
    ESP_LOGI(TAG, "wifi: roaming scan -> %s", esp_err_to_name(er));
    if (er != ESP_OK) {
        g_wifi_scanning = false;
        g_wifi_roaming_scan = false;
    }
}

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
                ip_label_set(NULL);
                g_wifi_fail_count++;
                if (g_wifi_curr_ssid[0] && !g_wifi_scanning &&
                    g_wifi_fail_count < WIFI_FAILS_BEFORE_ROAM) {
                    esp_wifi_connect();
                } else if (g_cfg.wifi_autoconnect && !g_wifi_scanning) {
                    wifi_kick_roam_scan();
                }
                break;
            }
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "wifi: connected to %s", g_wifi_curr_ssid);
                g_wifi_connected = true;
                g_wifi_last_reason = 0;
                g_wifi_fail_count = 0;
                strncpy(g_cfg.last_ssid, g_wifi_curr_ssid, sizeof(g_cfg.last_ssid) - 1);
                cfg_save();
                break;
            case WIFI_EVENT_SCAN_DONE: {
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
                if (g_wifi_roaming_scan) {
                    g_wifi_roaming_scan = false;
                    int best_i = -1;
                    int best_rssi = -127;
                    for (int i = 0; i < g_wifi_scan_n; i++) {
                        if (!wifi_has_remembered(g_wifi_scan[i].ssid)) continue;
                        if (g_wifi_scan[i].rssi > best_rssi) {
                            best_rssi = g_wifi_scan[i].rssi;
                            best_i = i;
                        }
                    }
                    if (best_i >= 0) {
                        const char *ssid = g_wifi_scan[best_i].ssid;
                        char pass[65] = {0};
                        cfg_get_ssid_pass(ssid, pass, sizeof(pass));
                        ESP_LOGI(TAG, "wifi: roaming to known %s rssi=%d",
                                 ssid, best_rssi);
                        g_wifi_fail_count = 0;
                        wifi_connect(ssid, pass);
                        break;
                    } else {
                        ESP_LOGI(TAG, "wifi: no remembered AP visible (%u seen)",
                                 (unsigned)g_wifi_scan_n);
                        g_wifi_fail_count = 0;
                    }
                }
                if (g_wifi_curr_ssid[0] && !g_wifi_connected) {
                    esp_wifi_connect();
                }
                break;
            }
            default: break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "wifi: got IP " IPSTR, IP2STR(&ev->ip_info.ip));
        sntp_start_once();
        char buf[40];
        snprintf(buf, sizeof(buf), IPSTR, IP2STR(&ev->ip_info.ip));
        ip_label_set(buf);
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

void wifi_start_scan(void)
{
    wifi_init_once();
    g_wifi_scanning = true;
    esp_wifi_disconnect();
    wifi_scan_config_t sc = {};
    sc.show_hidden = false;
    sc.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    esp_err_t er = esp_wifi_scan_start(&sc, false);
    if (er != ESP_OK) {
        ESP_LOGW(TAG, "wifi: scan_start=%s", esp_err_to_name(er));
        g_wifi_scanning = false;
    }
}

void wifi_connect(const char *ssid, const char *pass)
{
    wifi_init_once();
    wifi_config_t wc = {};
    strncpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, pass ? pass : "", sizeof(wc.sta.password) - 1);
    wc.sta.threshold.authmode = (pass && pass[0])
                                    ? WIFI_AUTH_WPA2_PSK
                                    : WIFI_AUTH_OPEN;
    wc.sta.pmf_cfg.capable = true;
    wc.sta.pmf_cfg.required = false;
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

/* ---------------------- webui snapshot ---------------------- */

extern "C" int webui_snapshot_fb(void *out, size_t cap)
{
    if (!out) return -1;
    size_t need = (size_t)UI_CANVAS_W * UI_CANVAS_H * 2;
    if (cap < need) return -1;
    lv_disp_t *disp = lv_disp_get_default();
    if (!disp || !disp->driver || !disp->driver->draw_buf) return -1;
    const void *src = disp->driver->draw_buf->buf1;
    if (!src) return -1;
    if (!lvgl_lock(50)) return -1;
    memcpy(out, src, need);
    lvgl_unlock();
    return (int)need;
}

/* ---------------------- SNTP ---------------------- */

#define SNTP_SYNC_INTERVAL_MS (4UL * 3600UL * 1000UL)

static void sntp_sync_notification_cb(struct timeval *tv)
{
    if (!tv) return;
    g_last_sntp_sync = tv->tv_sec;
    struct tm tm;
    gmtime_r(&g_last_sntp_sync, &tm);
    ESP_LOGI(TAG, "sntp: synced to %04d-%02d-%02d %02d:%02d:%02d UTC",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
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

/* ---------------------- Backlight + auto-dim impl ---------------------- */

void backlight_apply(uint8_t bri)
{
    setUpduty((uint16_t)(0xFF - bri));
}

/* ---------------------- Theme palette ---------------------- */

theme_palette_t theme_get(void)
{
    theme_palette_t p;
    switch (g_cfg.theme) {
    case 1:
        p.bg = lv_color_make(0xf0, 0xf0, 0xf4);
        p.text = lv_color_make(0x10, 0x10, 0x18);
        p.menu_surf = lv_color_make(0xe8, 0xe8, 0xee);
        p.menu_hdr = lv_color_make(0xc0, 0xc0, 0xcc);
        p.menu_btn = lv_color_make(0x90, 0x90, 0xa0);
        p.sunmap_water_n = lv_color_make(0xb0, 0xb8, 0xc8);
        p.sunmap_water_d = lv_color_make(0xe0, 0xe4, 0xf0);
        p.sunmap_land_n = lv_color_make(0x60, 0x70, 0x80);
        p.sunmap_land_d = lv_color_make(0x20, 0x30, 0x40);
        break;
    case 2:
        p.bg = lv_color_black();
        p.text = lv_color_make(0xff, 0xff, 0x00);
        p.menu_surf = lv_color_black();
        p.menu_hdr = lv_color_make(0xff, 0xff, 0x00);
        p.menu_btn = lv_color_white();
        p.sunmap_water_n = lv_color_black();
        p.sunmap_water_d = lv_color_make(0x40, 0x40, 0x00);
        p.sunmap_land_n = lv_color_make(0x80, 0x80, 0x00);
        p.sunmap_land_d = lv_color_make(0xff, 0xff, 0x00);
        break;
    default:
        p.bg = lv_color_black();
        p.text = lv_color_white();
        p.menu_surf = lv_color_make(0x20, 0x20, 0x28);
        p.menu_hdr = lv_color_make(0x30, 0x30, 0x3c);
        p.menu_btn = lv_color_make(0x50, 0x50, 0x60);
        p.sunmap_water_n = lv_color_black();
        p.sunmap_water_d = lv_color_make(0x20, 0x20, 0x20);
        p.sunmap_land_n = lv_color_make(0x40, 0x40, 0x40);
        p.sunmap_land_d = lv_color_make(0x90, 0x90, 0x90);
        break;
    }
    return p;
}

/* ---------------------- app_cfg setters that need backlight/wifi ---------------------- */

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

extern "C" void app_wifi_connect_save(const char *ssid, const char *pass)
{
    if (!ssid || !*ssid) return;
    cfg_save_ssid_pass(ssid, pass ? pass : "");
    strncpy(g_cfg.last_ssid, ssid, sizeof(g_cfg.last_ssid) - 1);
    g_cfg.last_ssid[sizeof(g_cfg.last_ssid) - 1] = 0;
    cfg_save();
    wifi_connect(ssid, pass ? pass : "");
}

/* ---------------------- app_main ---------------------- */

extern "C" void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
    esp_log_level_set("lcd_panel.axs15231b", ESP_LOG_VERBOSE);
    esp_log_level_set("lcd_panel.io.spi", ESP_LOG_VERBOSE);

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

    int pos = 0;
    pos += snprintf(g_status_text + pos, sizeof(g_status_text) - pos, "Drivers:\n");

    ESP_LOGI(TAG, "[1/9] I2C buses");
    i2c_master_Init();
    ESP_LOGI(TAG, "      esp_i2c_bus_handle=%p", esp_i2c_bus_handle);
    pos += snprintf(g_status_text + pos, sizeof(g_status_text) - pos, "I2C OK\n");

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
    pos += snprintf(g_status_text + pos, sizeof(g_status_text) - pos, "TCA9554 OK\n");

    ESP_LOGI(TAG, "[3/9] LCD backlight PWM (from cfg)");
    lcd_bl_pwm_bsp_init((uint16_t)(0xFF - g_cfg.brightness));
    pos += snprintf(g_status_text + pos, sizeof(g_status_text) - pos, "BL OK\n");

    ESP_LOGI(TAG, "[4/9] LCD panel + LVGL");
    disp_driver_init();
    pos += snprintf(g_status_text + pos, sizeof(g_status_text) - pos, "LCD/Touch OK\n");

    ESP_LOGI(TAG, "[5/9] RTC + IMU");
    i2c_rtc_setup();
    i2c_imu_setup();
    pos += snprintf(g_status_text + pos, sizeof(g_status_text) - pos, "RTC/IMU OK\n");

    ESP_LOGI(TAG, "[6/9] ADC battery");
    adc_bsp_init();
    pos += snprintf(g_status_text + pos, sizeof(g_status_text) - pos, "ADC OK\n");

    ESP_LOGI(TAG, "[7/9] Audio (audio_min: ES8311 + I2S)");
    if (audio_min_init() == ESP_OK) {
        audio_min_set_volume(g_cfg.audio_volume);
        pos += snprintf(g_status_text + pos, sizeof(g_status_text) - pos, "Audio OK\n");
    } else {
        pos += snprintf(g_status_text + pos, sizeof(g_status_text) - pos, "Audio FAIL\n");
    }

    ESP_LOGI(TAG, "[8/9] SD card + Buttons");
    _sdcard_init();
    button_Init();
    pos += snprintf(g_status_text + pos, sizeof(g_status_text) - pos, "SD/Btn OK");

    setenv("TZ", "UTC0", 1);
    tzset();
    {
        time_t build_epoch = (time_t)BUILD_EPOCH_UTC;
        struct tm bt = {};
        gmtime_r(&build_epoch, &bt);
        int b_y = bt.tm_year + 1900;
        int b_mo = bt.tm_mon + 1;
        int b_d = bt.tm_mday;
        int b_h = bt.tm_hour;
        int b_mi = bt.tm_min;
        int b_s = bt.tm_sec;

        RtcDateTime_t r = i2c_rtc_get();
        struct tm rt = {};
        rt.tm_year = (int)r.year - 1900;
        rt.tm_mon = (int)r.month - 1;
        rt.tm_mday = r.day;
        rt.tm_hour = r.hour;
        rt.tm_min = r.minute;
        rt.tm_sec = r.second;
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
        tm.tm_mon = (int)r.month - 1;
        tm.tm_mday = r.day;
        tm.tm_hour = r.hour;
        tm.tm_min = r.minute;
        tm.tm_sec = r.second;
        time_t t = mktime(&tm);
        struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "RTC seed: %04d-%02d-%02d %02d:%02d:%02d UTC -> epoch %lld",
                 r.year, r.month, r.day, r.hour, r.minute, r.second, (long long)t);
    }

    tz_apply_current();
    wifi_init_once();

    if (webui_start() != ESP_OK) {
        ESP_LOGW(TAG, "webui_start failed");
    }

    show_main_ui(g_status_text);
    ESP_LOGI(TAG, "===== All drivers initialized =====");
    cli_start();

    radio_engine_warm_at_boot();
    bg_fetcher_ensure();

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
        size_t free8 = heap_caps_get_free_size(MALLOC_CAP_8BIT);
        size_t freedma = heap_caps_get_free_size(MALLOC_CAP_DMA);
        size_t freespi = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        ESP_LOGI(TAG, "alive #%lu  frames=%lu  heap8=%u dma=%u spiram=%u",
                 (unsigned long)heartbeat++,
                 (unsigned long)g_fps_frame_count,
                 (unsigned)free8, (unsigned)freedma, (unsigned)freespi);
    }
}
