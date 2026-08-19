# 12. config 目录梳理与整合

> 本文档梳理 `main/config/` 目录的双轨配置现状，厘清两套系统的职责边界、重叠/冲突字段与调用关系，并给出整合建议。
>
> 前置：事件总线见 [06](./06-事件总线与数据流.md)，数据流与 `app_main` 启动顺序见 [10](./10-事件执行流程.md)。

---

## 12.1 目录构成

```
main/config/
├── app_info.h       # 纯常量/宏：APP_NAME、屏幕尺寸、MAX_DISKS=16 等（被两者共享）
├── config.h/.c       # 老配置系统：g_config (AppConfig) + config_load/save_xxx
└── app_cfg.h/.c      # 新配置系统：g_cfg (app_cfg_t) + app_cfg_init/save/set_xxx
```

`app_info.h` 无状态、只有 `#define`，是两套系统的**公共常量来源**（如 `MAX_DISKS` 定义在 `app_info.h`，`config.h` 转发引用）。

---

## 12.2 双轨对比

| 维度 | 老系统 `config.c` / `g_config` | 新系统 `app_cfg.c` / `g_cfg` |
|------|------|------|
| NVS 命名空间 | `"nasmon"` | `"cfg"`（`NVS_NS_CFG`，`app_cfg.h:13`） |
| 版本机制 | `AppConfig.version`，**无迁移逻辑** | `app_cfg_t.version` + `CFG_VERSION=7`（`app_cfg.h:14`，有 `cfg_apply_migration`） |
| 加载入口 | `config_load()`（`main.cpp:122` 调用） | `app_cfg_init()`（`main.cpp` 调用） |
| 保存机制 | `config_save()` **全量**逐 key 写 | `app_cfg_save()` **增量**（只写脏字段 `+CFG_MIGRATED` 标记） |
| 变更通知 | 仅 `config_save_fan`→`EVENT_FAN_CONFIG_CHANGED`、`config_save_disk_config`→`EVENT_DISK_CONFIG_CHANGED` | `cfg_publish(field)`→`EVENT_CFG_CHANGED`，字段级过滤（`cfg_field_t`） |
| 管理的配置域 | WiFi 凭证、NAS 连接（IP/端口/类型/key）、风扇、磁盘槽位、亮度（死字段）、旋转角、autodim | 时钟（位置/字号/颜色/文本/开关）、背景（模式/URL/颜色/刷新）、行情（符号/刷新/涨跌色）、时区、主题、语言、亮度、音频、FPS、自动变暗/关闭 |
| 主要消费者 | `nas_event_loop`、`unraid_client`/`api_client` 等、`fan_control.c`、`ui_Screen_Settings_NasTab/FanTab` | `ui_clock`、`ui_events`（背光/变暗）、`ui_settings`、`bg_fetcher` |

> 两系统**都活跃**：`main.cpp` 同时调 `config_load()` 和 `app_cfg_init()`。不是死代码，而是**按功能域分裂的两套配置**，各自占一个 NVS 命名空间。

---

## 12.3 重叠与冲突字段（整合重点）

### ① 背光亮度 — 真实冲突，老系统为死字段

- **新系统 `g_cfg.brightness`（`app_cfg.h:91`）是活动源**：`ui_helpers.c:59` 初始化背光、`ui_events.c:91/93/95/257` 调光/息屏都用 `g_cfg.brightness` 经 `ui_helpers_backlight_apply()` → `setUpduty(0xFF - bri)` 驱动硬件。
- **老系统 `g_config.brightness`（`config.h`）是死字段**：`config.c:18/103/211/300` 加载/保存它（NVS 键 `"brightness"`，"nasmon" 空间），但**无任何 UI/硬件代码读取它来设背光**。
- **结论**：老系统的亮度字段应删除，避免"改了老系统亮度却毫无效果"的隐性 bug。

### ② WiFi 凭证 — 双源并存

- 老系统 `config_save_wifi()` 写 `NVS_WIFI_SSID="wifi_ssid"` / `NVS_WIFI_PASS="wifi_pass"`（"nasmon" 空间）。
- 新系统 `g_cfg.last_ssid`（`app_cfg.h:94`）+ `app_cfg_wifi_connect_save()`，且注释明确"WiFi 凭证改由 `esp_wifi_config` 的 `default_networks` 注入"（见 `app_cfg.c` 相关段落）。
- **结论**：两套凭证源并存，存在谁权威的问题。应以 `esp_wifi_config`/`default_networks` 为权威，老 `config_save_wifi` 可废弃或改为回写 `default_networks`。

### ③ 自动变暗/旋转角 — 体系分裂

- 老系统 `AppConfig` 有 `rotation_angle`/`autodim`（`config.h` 的 `NVS_ROTATION_ANGLE`/`NVS_AUTODIM`）；新系统用 `g_cfg.dim_s`/`off_s`（`app_cfg.h:92-93`）。
- 背光/变暗逻辑实际走新系统（`ui_events.c` 用 `g_cfg` 计算调光）。老系统的 `autodim`/`rotation_angle` 需逐一确认是否仍被读取。

### ④ 风扇 / 磁盘 — 仅老系统

- `config_save_fan()` / `config_save_disk_config()` 只存在于老系统，分别发 `EVENT_FAN_CONFIG_CHANGED` / `EVENT_DISK_CONFIG_CHANGED`。
- `fan_control.c` 直接读 `g_config.fan`；`ui_events.c` 的 `EVENT_DISK_CONFIG_CHANGED` 处理重建屏。新系统 `app_cfg` 完全不碰这两项。
- **结论**：风扇/磁盘配置仍正确归属老系统，整合时不应误并入新系统（除非整体迁移）。

---

## 12.4 调用关系图

```
main.cpp
  ├─ config_load()            → 老系统 g_config（nasmon 命名空间）
  │     ├─ nas_event_loop     → 读 g_config.nas_type/ip/port/api_key
  │     ├─ fan_control        → 读 g_config.fan
  │     └─ FanTab/NasTab      → 读写 g_config + config_save_xxx()
  └─ app_cfg_init()           → 新系统 g_cfg（cfg 命名空间，增量脏字段）
        ├─ ui_clock           → 读 g_cfg.clock_*/bg_*/tz_idx/theme
        ├─ ui_events          → 读 g_cfg.brightness 驱动背光、dim/off 计时
        ├─ ui_settings        → app_cfg_set_*() + cfg_publish()
        └─ bg_fetcher         → 读 g_cfg.bg_mode/bg_url/bg_refresh_s

变更传播：
  老系统 用户改风扇/磁盘 → config_save_xxx() → EVENT_FAN/DISK_CONFIG_CHANGED（fan_control/ui_events 消费）
  新系统 用户改时钟/背景 → app_cfg_set_*()    → cfg_publish() → EVENT_CFG_CHANGED（字段级过滤）
```

---

## 12.5 整合建议

> 下列为**代码整合**方向。代码合并属高风险重构，需单独确认后实施；本文档先完成"认知整合"。

**P0（明确死字段，低风险）**
- 删除老系统 `AppConfig.brightness` 及其 NVS 读写（`config.c:18/103/211/300`、`config.h` 的 `NVS_BRIGHTNESS`）——背光权威源已是 `g_cfg.brightness`，老字段无任何消费者。

**P1（消除 WiFi 双源）**
- 以 `esp_wifi_config`/`default_networks` 为 WiFi 凭证唯一权威源；废弃老 `config_save_wifi()` 或改为回写 `default_networks`。
- 确认老系统 `rotation_angle`/`autodim` 是否仍被读，若为死字段一并清理。

**P2（统一配置体系，长期）— ✅ 已完成（2026-08-19）**
- 已将风扇、磁盘、NAS 连接、显示、时区、天气、自动轮播等全部配置迁入 `app_cfg` 体系，统一 NVS 命名空间 `"cfg"`，`CFG_VERSION` 升至 8（升级即重置旧 `"nasmon"` 配置，不迁移）。
- 老系统 `main/config/config.c` + `config.h` 已**彻底删除**，`main/CMakeLists.txt` 移除 `config.c` 源文件，`main/main.cpp` 移除 `config_load()` 调用；`g_config`/`config_save_*`/`config_load`/`config_get_*` 等全部引用已改为 `app_cfg_get/set_*`（固件 + sim 模拟器同步完成）。
- 原 `config.h` 中的硬件/协议常量宏（`FAN_PWM_GPIO` 等风扇管脚、`DEFAULT_HTTP_PORT` 等协议端口）迁移至 `fan_control.h` 与 `data_source.h`，避免随老系统删除丢失。
- 老 `config_is_sata_slot`/`config_is_m2_slot`/`config_get_total_disk_slots` 内联函数由 `app_cfg_is_sata_slot`/`app_cfg_is_m2_slot` 及 `app_cfg_get_sata_disk_count()+app_cfg_get_m2_disk_count()` 替代。
- 验证：`idf.py build` 与 sim `mingw32-make` 均通过。

---

## 12.6 现状小结

- `main/config` = 单一 `app_cfg` 系统（活跃、字段级 `EVENT_CFG_CHANGED` 事件、全量保存）。老 `[config.c/h]` 已删除，双 NVS 命名空间与 WiFi 双凭证源问题已在 P0/P1/P2 中消除。
- P0/P1/P2 全部完成，配置体系已统一。
