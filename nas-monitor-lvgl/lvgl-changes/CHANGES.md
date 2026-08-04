# LVGL 代码设计变更摘要

基于 `.design` 设计页面（画布 `page-overview.html`）对 LVGL C 源码的反馈修改。

---

## 变更概览

| 文件 | 变更项 | 状态 |
|------|--------|------|
| `app_info.h` | STATUS_BAR_H: 24 → 35px | **已同步** |
| `ui_Screen_Overview.c` | 全局字号 14px → 12px、警告色修正、padding 补齐、DOM ID 对齐 | **已同步** |
| `ui_Screen_Storage.c` | 状态栏元素位置统一为画布坐标 | **已同步** |
| `ui_Screen_DiskDetail.c` | WiFi/BT 图标位置统一 | **已同步** |
| `ui_Screen_SystemDetail.c` | WiFi/BT 图标位置统一 | **已同步** |
| `ui_Screen_SDCopy.h` | **新建** — SD 卡复制页面 | **已同步** |
| `ui_Screen_SDCopy.c` | **新建** — SD 卡复制页面实现 | **已同步** |
| `ui.h` | 新增 SDCopy 头文件引用 | **已同步** |
| `ui_events.h` | 新增 SDCopy 手势处理声明 | **已同步** |
| `ui_events.c` | 新增 SDCopy 时间/网络/IP/WiFi/HDD 更新 + 手势处理 | **已同步** |
| `app_info.h` | TOTAL_PAGES: 7→8 | **已同步** |
| `page-overview.html` | DOM ID 与 LVGL 变量名对齐 | **已同步** |
| `spec-overview.md` | 布局结构/坐标表中的 DOM ID 更新 | **已同步** |

---

## 详细变更

### 1. app_info.h
- `STATUS_BAR_H`: 24 → 35（与设计规范统一）
- `APP_VERSION` 保持 `"1.2.0"`

### 2. 状态栏位置统一（以画布 `page-overview.html` 为准）

所有页面状态栏元素统一使用 `LV_ALIGN_LEFT_MID` 绝对定位，与画布坐标一致：

| 元素 | 画布 left | LVGL 对齐 |
|------|----------|-----------|
| 标题 | 5px | LV_ALIGN_LEFT_MID, 5 |
| 时间 | 110px | LV_ALIGN_LEFT_MID, 110 |
| 上传速率 | 250px | LV_ALIGN_LEFT_MID, 250 |
| 下载速率 | 330px | LV_ALIGN_LEFT_MID, 330 |
| IP 地址 | 445px | LV_ALIGN_LEFT_MID, 445 |
| WiFi 图标 | 594px | LV_ALIGN_LEFT_MID, 594 |
| 蓝牙图标 | 614px | LV_ALIGN_LEFT_MID, 614 |

### 3. ui_Screen_Overview.c — 2026-08-04 第二轮修复

基于运行截图与画布逐项对比，修复以下差异：

| 问题 | 画布值 | 修复前 | 修复后 |
|------|--------|--------|--------|
| 全局字号 | 12px | 14px (lv_font_montserrat_14) | 12px (lv_font_montserrat_12) |
| 警告色 COLOR_WARNING | #FF8C00 | #FFA500 | #FF8C00 |
| 仪表面板 padding | 0 12px | 无 | lv_obj_set_style_pad_hor(cpu_container, 12) |
| 指标栏 padding | 0 12px | pad_all: 0 | lv_obj_set_style_pad_hor(md_container, 12) |
| 字体声明 | lv_font_montserrat_12 | 缺失 | 添加 LV_FONT_DECLARE |

WiFi/BT 图标样式差异（SVG vs LVGL 内置符号字体）为 LVGL 硬件限制，暂不修复。

### 3b. ui_Screen_Overview.c — 2026-08-05 M.2 空槽位样式 + HDD padding

基于画布与 LVGL 代码逐项对比，修复以下差异：

| 问题 | 画布值 | 修复前 | 修复后 |
|------|--------|--------|--------|
| HDD 槽位 padding | 0 4px | pad_all: 0 | lv_obj_set_style_pad_hor(btn_hdd, 4) |
| M.2 空槽位背景 | transparent | COLOR_INACTIVE（实心） | COLOR_BG（透明） + 1px 边框 |
| M.2 空槽位边框 | 1px dashed #666 | 无 | 1px solid COLOR_ICON_DIM（LVGL 8.3 不支持虚线，用实线近似） |

**实现细节**：
- 新增 `hdd_buttons[MAX_DISKS]` 数组存储槽位按钮引用
- `overview_screen_update_hdd_led` 在离线时同步更新按钮背景为透明 + 边框，在线时恢复实心背景
- 文本颜色和 LED 颜色在离线时自动变为 `COLOR_ICON_DIM`（#666666），与画布 `--nas-ink-3` 一致

### 4. DOM ID 同步（画布 ↔ LVGL）

#### Overview
| 画布旧 ID | 画布新 ID | LVGL 变量名 |
|-----------|----------|------------|
| `meter-panel` | `cpu-container` | `cpu_container` |
| `metrics-bar` | `md-container` | `md_container` |
| `disk-strip` | `hdd-container` | `hdd_container` |

#### Storage + DiskDetail（2026-08-05）
| 画布旧 ID | 画布新 ID | LVGL 变量名 |
|-----------|----------|------------|
| `storage-grid` | `storage-container` | `s_screen.container` |
| `smart-section` | `smart-container` | `s_screen.smart_container` |

SystemDetail 和 Boot 画布内容区无显式 `data-dom-id`，无需同步。

### 5. ui_Screen_Storage.c
- 上传/下载/ IP 从 CENTER/RIGHT_MID 改为 LEFT_MID 绝对定位
- WiFi/BT 图标从 RIGHT_MID 改为 LEFT_MID 绝对定位

### 6. ui_Screen_DiskDetail.c
- WiFi/BT 图标从 RIGHT_MID 改为 LEFT_MID 绝对定位

### 7. ui_Screen_SystemDetail.c
- WiFi/BT 图标从 RIGHT_MID 改为 LEFT_MID 绝对定位

### 8. 新建 SD Copy 页面 (2026-08-05)

基于画布 `page-sd-copy.html` 新建 LVGL SD 卡复制进度页面。

**页面结构**：
| 区域 | 说明 |
|------|------|
| 状态栏 | 与 Overview 完全一致（标题、时间、上传/下载速率、IP、WiFi、BT） |
| 复制进度区 | 左侧：状态标签 "复制中"、百分比大号字体、速度信息；右侧：进度条（496×22px） |
| 底部 HDD 槽位 | 与 Overview 完全一致（按钮 + LED + 名称） |

**新增文件**：
- `ui_Screen_SDCopy.h` — 声明 init/destroy/update 函数
- `ui_Screen_SDCopy.c` — 完整实现，复用 Overview 的状态栏和 HDD 槽位代码

**修改文件**：
- `ui.h` — 新增 `#include "screens/ui_Screen_SDCopy.h"`
- `ui_events.h` — 新增 `ui_event_Screen_SDCopy_gesture` 声明
- `ui_events.c` — 新增 SDCopy 时间/网络/IP/WiFi/HDD 数据更新调用 + 手势处理（右滑→Storage）
- `app_info.h` — `TOTAL_PAGES` 7→8

**更新函数**：
- `sdcopy_screen_update_time` — 更新时间
- `sdcopy_screen_update_network` — 更新上传/下载速率
- `sdcopy_screen_update_ip` — 更新 IP 地址
- `sdcopy_screen_update_wifi` — 更新 WiFi 图标颜色
- `sdcopy_screen_update_hdd_led` — 更新 HDD LED 颜色和在线状态
- `sdcopy_screen_update_hdd_name` — 更新 HDD 名称
- `sdcopy_screen_update_progress` — 更新复制进度百分比和速度信息

**进度条精度**：使用 0.01% 精度（`lv_bar_set_range(0, 10000)`），支持 `87.87%` 这样的两位小数显示。

---

## 如何应用

所有修改已直接应用到源文件，无需手动复制。lvgl-changes 目录保留供参考。