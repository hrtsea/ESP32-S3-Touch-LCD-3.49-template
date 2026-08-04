# 5. 关键 API 参考

本文档列出项目对外暴露的核心 API，按模块分组。所有 API 均为 C ABI（除非注明 C++）。

---

## 5.1 入口与生命周期

### app_main（[main.cpp](../main/main.cpp#L111)）

```cpp
extern "C" void app_main(void);
```

启动顺序：`log_init` → `event_bus_init` → `app_cfg_load` → `hw_init` → `network_init` → `nas_event_loop_start` → `http_timer_init/start` → `ui_init` → `cli_start` → `system_monitor_start`。

---

## 5.2 配置层 API

### app_cfg（新体系，命名空间 `"cfg"`）

文件：[app_cfg.h](../main/config/app_cfg.h)

#### 生命周期
```c
void  app_cfg_init(void);
void  app_cfg_load(void);       // 别名
void  app_cfg_save(void);       // 全量写 NVS
size_t app_cfg_get_last_ssid(char *buf, size_t buf_len);   // 线程安全
```

#### Getter（节选）
```c
uint8_t       app_cfg_get_lang(void);
uint8_t       app_cfg_get_brightness(void);
uint16_t      app_cfg_get_dim_s(void);
uint16_t      app_cfg_get_off_s(void);
int16_t       app_cfg_get_clock_x(void);
int16_t       app_cfg_get_clock_y(void);
uint8_t       app_cfg_get_clock_size(void);
uint32_t      app_cfg_get_clock_rgba(void);
uint8_t       app_cfg_get_show_ms(void);
uint8_t       app_cfg_get_show_seconds(void);
uint8_t       app_cfg_get_show_clock(void);
const char*   app_cfg_get_clock_text(void);          // 内部指针，非线程安全
uint8_t       app_cfg_get_bg_mode(void);
uint16_t      app_cfg_get_bg_refresh_s(void);
const char*   app_cfg_get_bg_url(void);              // 内部指针
uint32_t      app_cfg_get_bg_color(void);
uint16_t      app_cfg_get_canvas_w(void);            // 转发到 disp_driver
uint16_t      app_cfg_get_canvas_h(void);
const char*   app_cfg_get_quotes_sym_l(void);
const char*   app_cfg_get_quotes_sym_r(void);
uint16_t      app_cfg_get_quotes_refresh_s(void);
uint32_t      app_cfg_get_quotes_up_rgba(void);
uint32_t      app_cfg_get_quotes_down_rgba(void);
```

#### Setter（每个内置 publish + save）

| 分类 | API |
|------|-----|
| 时钟 | `app_cfg_set_show_seconds(uint8_t)` / `set_show_clock(uint8_t)` / `set_clock_text(const char*)` / `set_clock_pos(int16_t x, int16_t y)` / `set_clock_size(uint8_t)` / `set_clock_rgba(uint32_t)` / `set_show_ms(uint8_t)` |
| 背景 | `app_cfg_set_bg_mode(uint8_t)` / `set_bg_url(const char*)` / `set_bg_color(uint32_t)` / `set_bg_refresh_s(uint16_t)` / `clock_bg_reload(void)` / `bg_fetch_now(void)` |
| 行情 | `app_cfg_set_quotes_sym_l(const char*)` / `set_quotes_sym_r(const char*)` / `set_quotes_refresh_s(uint16_t)` / `set_quotes_up_rgba(uint32_t)` / `set_quotes_down_rgba(uint32_t)` |
| 显示 | `app_cfg_set_lang(uint8_t)` / `set_brightness(uint8_t)` / `set_dim_off(uint16_t dim_s, uint16_t off_s)` / `set_show_fps(uint8_t)` / `set_audio_enable(uint8_t)` / `set_audio_volume(uint8_t)` / `set_theme(uint8_t)` |
| 时间 | `app_cfg_set_tz_idx(uint16_t)` / `set_hour24(uint8_t)` / `set_date_fmt(uint8_t)` |
| WiFi | `app_cfg_set_wifi_autoconnect(uint8_t)` / `wifi_connect_save(const char* ssid, const char* pass)` / `set_last_ssid(const char*)` |
| Tile | `app_cfg_set_active_tile(int idx)` |

### config（旧体系，命名空间 `"nasmon"`）

文件：[config.h](../main/config/config.h)

```c
void config_load(void);
void config_save(void);
void config_save_wifi(const char* ssid, const char* pass);
void config_save_nas(const char* type, const char* ip, uint16_t port,
                     const char* user, const char* pass, bool https);
void config_save_display(uint16_t rotation_angle, uint8_t brightness, uint8_t autodim);
void config_save_fan(const FanConfig* fan);
void config_save_disk_config(uint8_t sata, uint8_t m2);   // 发布 EVENT_DISK_CONFIG_CHANGED
void config_reset(void);
void config_save_backup(void);
void config_restore_backup(void);

// 内联辅助
static inline uint8_t config_get_total_disk_slots(void);
static inline bool    config_is_sata_slot(uint8_t idx);
static inline bool    config_is_m2_slot(uint8_t idx);
```

---

## 5.3 事件总线 API

文件：[event_bus.h](../main/utils/event_bus.h)

```c
// 初始化（创建 mutex + 队列 + 1Hz/10Hz 心跳 esp_timer）
void event_bus_init(void);

// 异步发布（投递到队列，非阻塞）
void event_bus_publish(event_id_t id, const void *data, size_t len);

// 同步+异步混合发布（先同步调 handler，再入队）
void event_bus_publish_nas_data(const NasData *data);

// 阻塞式从队列取事件
bool event_bus_receive(event_t *evt, uint32_t timeout_ms);

// 已废弃（仅打日志，推荐用 receive）
void event_bus_subscribe(event_id_t id, event_handler_t cb, void *user_data);
void event_bus_unsubscribe(event_id_t id, event_handler_t cb);

// 事件名查询
const char* event_bus_name(event_id_t id);
```

事件 ID 完整列表见 [事件总线与数据流 § 事件 ID](06-事件总线与数据流.md#事件-id-一览)。

---

## 5.4 数据源 API

文件：[data_source.h](../main/data/data_source.h)

### 工厂与生命周期
```c
bool        data_source_init(const char* type_id);
bool        data_source_connect(void);
void        data_source_disconnect(void);
bool        data_source_poll(void);
bool        data_source_is_connected(void);
const NasData* data_source_get_data(void);
const char* data_source_get_type_name(void);
const char* data_source_get_conn_icon(void);          // "wifi" / "usb" / "none"
bool        data_source_switch(const char* type_id);  // 运行时切换
```

### VTable 包装（inline）
```c
static inline bool ds_init(DataSource* s);
static inline bool ds_connect(DataSource* s);
static inline void ds_disconnect(DataSource* s);
static inline bool ds_poll(DataSource* s);
static inline bool ds_is_connected(DataSource* s);
static inline const NasData* ds_get_data(DataSource* s);
static inline const char* ds_get_type_name(DataSource* s);
static inline const char* ds_get_conn_icon(DataSource* s);
static inline NasTypeConfig ds_get_config(DataSource* s);
static inline void ds_destroy(DataSource* s);
```

### 类型查询
```c
NasType        nas_type_from_string(const char* str);
const char*    nas_type_to_string(NasType t);
const char*    get_display_type_name(NasType t);
NasTypeConfig  nas_type_config_get_defaults(NasType t);
NasTypeConfig  nas_type_config_get_defaults_by_id(const char* type_id);
float          data_source_get_rx_speed_mbps(void);
float          data_source_get_tx_speed_mbps(void);
```

### NAS 事件循环
文件：[nas_event_loop.h](../main/data/nas_event_loop.h)
```c
void      nas_event_loop_start(void);
void      nas_event_loop_stop(void);
bool      nas_event_loop_is_running(void);
esp_err_t nas_event_loop_switch_source(const char* nas_type_id);
```

### JSON 序列化
文件：[nas_data_json.h](../main/data/nas_data_json.h)
```c
cJSON* nas_data_to_json(const NasData* data);
bool   nas_json_to_data(cJSON* root, NasData* data);
void   nas_data_free_json(cJSON* root);
```

---

## 5.5 UI 层 API

### 主入口
文件：[ui.h](../main/ui/ui.h)
```c
void ui_init(void);
```

### UI 状态管理
文件：[ui_helpers.h](../main/ui/ui_helpers.h)
```c
lv_obj_t*   ui_helpers_get_tileview(void);
void        ui_helpers_set_tileview(lv_obj_t* tv);              // 当前未使用

char*       ui_helpers_get_status_text(void);
void        ui_helpers_set_status_text(const char* txt);

uint32_t    ui_helpers_get_last_activity_ms(void);
void        ui_helpers_set_last_activity_ms(uint32_t ms);

uint8_t     ui_helpers_get_dim_state(void);
void        ui_helpers_set_dim_state(uint8_t state);            // 0=正常 1=变暗 2=熄屏

uint32_t    ui_helpers_get_last_scroll_ms(void);
void        ui_helpers_set_last_scroll_ms(uint32_t ms);

uint32_t    ui_helpers_get_menu_block_until_ms(void);
void        ui_helpers_set_menu_block_until_ms(uint32_t ms);
bool        ui_helpers_menu_input_blocked(void);                // 去抖检查（350ms）

void        ui_helpers_notify_activity(void);                   // 更新活动时间 + 恢复亮度
void        ui_helpers_backlight_apply(uint8_t bri);            // 设置 PWM 占空比（反相）
```

### 事件循环
文件：[ui_events.h](../main/ui/ui_events.h)
```c
void ui_events_start(void);
void ui_events_stop(void);
void ui_events_register_screen_events(lv_obj_t* scr);
void ui_events_register_tileview_events(lv_obj_t* tv);          // 保留未启用
void ui_events_start_tile_monitor(lv_obj_t* tv);                // 保留未启用
void ui_events_start_dim_timer(void);
void ui_events_start_time_timer(void);
void ui_events_wifi_status_cb(bool connected, const char* ip);  // 在 lv_layer_top 创建/更新 IP 标签
```

### Screen 创建/销毁 API

| Screen | 创建 | 销毁 |
|--------|------|------|
| Boot | `ui_Screen_Boot_screen_init(void)` | `ui_Screen_Boot_screen_cleanup(void)` |
| Overview | `ui_Screen_Overview_screen_init(void)` | `ui_Screen_Overview_screen_destroy(void)` |
| Storage | `ui_Screen_Storage_screen_init(void)` | `ui_Screen_Storage_screen_destroy(void)` |
| Settings | `ui_Screen_Settings_screen_init(void)` | `ui_Screen_Settings_screen_destroy(void)` |
| WifiConfig | `ui_Screen_WifiConfig_screen_init(void)` | `ui_Screen_WifiConfig_screen_cleanup(void)` |

### Screen 更新 API（节选）

#### Overview
```c
void overview_screen_update_time(const char* time_str);
void overview_screen_update_network(uint32_t rx_bps, uint32_t tx_bps);
void overview_screen_update_ip(const char* ip);
void overview_screen_update_wifi(bool connected);
void overview_screen_update_cpu(float cpu_pct);
void overview_screen_update_temp(float temp_c);
void overview_screen_update_mem(float mem_pct);
void overview_screen_update_disk(float disk_pct);
void overview_screen_update_hdd_led(uint8_t idx, uint8_t health, bool online);
void overview_screen_update_hdd_name(uint8_t idx, const char* name);
```

#### Storage
```c
void storage_screen_update_time(const char* time_str);
void storage_screen_update_network(uint32_t rx_bps, uint32_t tx_bps);
void storage_screen_update_ip(const char* ip);
void storage_screen_update_hdd_name(uint8_t idx, const char* name);
void storage_screen_update_hdd_bar(uint8_t idx, uint8_t used_pct, uint8_t health);
void storage_screen_update_hdd_temp(uint8_t idx, int16_t temp);
void storage_screen_update_hdd_online(uint8_t idx, bool online);
```

### Settings Tab API

每个 Tab 提供：
- `ui_Screen_Settings_<Tab>_create(lv_obj_t* parent)`：创建 Tab 内容
- `ui_Screen_Settings_<Tab>_cleanup(void)`：清理全局控件指针（事件订阅 unsubscribe）

Tab 列表：`WifiTab` / `NasTab` / `ScreenTab` / `StationTab` / `MusicTab` / `RegionTab` / `GuideTab`。

---

## 5.6 工具层 API

### 硬件初始化
文件：[hw_init.h](../main/utils/hw_init.h)
```c
void hw_init(void);
void system_monitor_start(void);
void system_time_init(void);
```

### 后台抓取
文件：[bg_fetcher.h](../main/utils/bg_fetcher.h)
```c
void bg_fetcher_ensure(void);   // 启动或唤醒任务
```

### HTTP 定时器
文件：[http_timer.h](../main/utils/http_timer.h)
```c
void     http_timer_init(void);
void     http_timer_start(void);
void     http_timer_stop(void);
void     http_timer_set_interval_ms(uint32_t ms);
uint32_t http_timer_get_interval_ms(void);
bool     http_timer_is_running(void);
```

### WiFi 桥接
文件：[wifi_bridge.h](../main/utils/wifi_bridge.h)
```c
void wifi_bridge_init(void);    // 必须在 wifi_cfg_init() 之后、ui_init() 之前
```

### WiFi 适配
文件：[wifi_adapter.h](../main/utils/wifi_adapter.h)
```c
void wifi_start_scan(void);
void wifi_disconnect(void);
void wifi_set_last_reason(uint8_t reason);
uint8_t wifi_get_last_reason(void);
// ... 详见文件
```

### 国际化
文件：[i18n.h](../main/utils/i18n.h)
```c
const char*    tr(i18n_key_t key);
uint8_t        i18n_lang(void);
void           i18n_set_lang(uint8_t idx);
const lv_font_t* i18n_font(void);
void           tz_apply_current(void);
const char*    tz_current_city_name(void);
```

### 主题
文件：[theme.h](../main/utils/theme.h)
```c
theme_palette_t theme_get(void);   // 按 g_cfg.theme 返回调色板
```

### CLI
文件：[cli.h](../main/utils/cli.h)
```c
void cli_start(void);
```

---

## 5.7 组件层 API（仅主应用实际使用的）

### i2c_bsp
文件：[i2c_bsp.h](../components/i2c_bsp/i2c_bsp.h)
```c
extern i2c_master_dev_handle_t disp_touch_dev_handle;
extern i2c_master_dev_handle_t rtc_dev_handle;
extern i2c_master_dev_handle_t imu_dev_handle;
extern i2c_master_bus_handle_t esp_i2c_bus_handle;

void    i2c_master_Init(void);
uint8_t i2c_writr_buff(i2c_master_dev_handle_t dev, int reg, uint8_t *buf, uint8_t len);
uint8_t i2c_read_buff(i2c_master_dev_handle_t dev, int reg, uint8_t *buf, uint8_t len);
```

### i2c_equipment
文件：[i2c_equipment.h](../components/i2c_equipment/i2c_equipment.h)
```c
void          i2c_rtc_setup(void);
void          i2c_rtc_setTime(uint16_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi, uint8_t s);
RtcDateTime_t i2c_rtc_get(void);
void          i2c_imu_setup(void);
ImuDate_t     i2c_imu_get(void);
```

### adc_bsp
```c
void adc_bsp_init(void);
void adc_get_value(float *value, int *data);
```

### button_bsp
```c
extern EventGroupHandle_t boot_groups;
extern EventGroupHandle_t pwr_groups;
void   button_Init(void);
uint8_t user_button_get_repeat_count(void);
uint8_t user_boot_get_repeat_count(void);
```

### lcd_bl_pwm_bsp
```c
void lcd_bl_pwm_bsp_init(uint16_t duty);
void setUpduty(uint16_t duty);
```

### audio_min
```c
esp_err_t audio_min_init(void);
void      audio_min_play_midi(bool play);
bool      audio_min_is_playing(void);
void      audio_min_set_volume(uint8_t vol_0_100);
bool      audio_min_is_available(void);
void      audio_min_shutdown(void);
```

---

## 5.8 显示驱动 API

文件：[disp_driver.h](../main/drivers/disp_driver.h)
```c
void disp_driver_init(void);                     // LCD 面板 + LVGL + 触摸任务

uint16_t disp_driver_get_canvas_w(void);
uint16_t disp_driver_get_canvas_h(void);

void disp_driver_set_fps_label(lv_obj_t* label); // 注册 FPS 标签
void disp_driver_fps_timer_cb(lv_timer_t* t);    // 3 秒周期 FPS 采样

// LVGL 锁（非 LVGL 任务操作 UI 时必须使用）
bool lvgl_lock(uint32_t timeout_ms);
void lvgl_unlock(void);
```
