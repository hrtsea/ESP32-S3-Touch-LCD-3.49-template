# LVGL 代码设计变更摘要

基于 `.design` 设计页面（HTML）对 LVGL C 源码的反馈修改。

---

## 变更概览

| 文件 | 变更项 | 状态 |
|------|--------|------|
| `app_info.h` | STATUS_BAR_H: 24 → 35px | **待复制** |
| `ui_Screen_Overview.c` | 图标颜色 / HDD 布局 | **已是最新** |
| `ui_Screen_Storage.c` | 状态栏/分隔线/WiFi-BT/动态标题 | **待复制** |
| `ui_Screen_DiskDetail.c` | 状态栏/分隔线/内容区 | **待复制** |
| `ui_Screen_SystemDetail.c` | 状态栏/分隔线/内容区 | **待复制** |

---

## 详细变更

### 1. app_info.h
- `STATUS_BAR_H`: 24 → 35（与设计规范统一）

### 2. ui_Screen_Overview.c ✅ 已是最新
- `COLOR_ICON_DIM`: `lv_color_hex(0x666666)` ✓
- HDD 磁盘条: 无间距紧密排列 ✓
- 状态栏 35px / 分隔线 2px ✓

### 3. ui_Screen_Storage.c
- 状态栏高度: 30 → 35
- 分隔线: 高度 1px → 2px，y 偏移 30 → 35
- 容器 y 偏移: 32 → 37
- 添加 WiFi/BT 图标（`COLOR_ICON_DIM`）
- 标题改为动态（与 Overview 一致）
- 时间 x 偏移: 80 → 110
- IP x 偏移: -15 → -45

### 4. ui_Screen_DiskDetail.c
- 状态栏高度: 30 → 35
- 分隔线: 高度 1px → 2px，y 偏移 30 → 35
- 内容区 y 偏移: 32 → 37

### 5. ui_Screen_SystemDetail.c
- 状态栏高度: 30 → 35
- 分隔线: 高度 1px → 2px，y 偏移 30 → 35
- 内容区 y 偏移: 32 → 37（CPU 和 Memory 两个模式）

---

## 如何应用

修改后的文件在 `lvgl-changes/` 目录，需手动复制到原始位置：

| 修改文件 | 目标路径 |
|----------|----------|
| `lvgl-changes/app_info.h` | `main/config/app_info.h` |
| `lvgl-changes/ui_Screen_Storage.c` | `main/ui/screens/ui_Screen_Storage.c` |
| `lvgl-changes/ui_Screen_DiskDetail.c` | `main/ui/screens/ui_Screen_DiskDetail.c` |
| `lvgl-changes/ui_Screen_SystemDetail.c` | `main/ui/screens/ui_Screen_SystemDetail.c` |

> `ui_Screen_Overview.c` 原始文件已是最新，无需覆盖。

### 快速复制命令（PowerShell）

在项目根目录 `ESP32-S3-Touch-LCD-3.49-template` 下执行：

```powershell
Copy-Item -Path 'nas-monitor-lvgl\lvgl-changes\app_info.h' -Destination 'main\config\app_info.h' -Force
Copy-Item -Path 'nas-monitor-lvgl\lvgl-changes\ui_Screen_Storage.c' -Destination 'main\ui\screens\ui_Screen_Storage.c' -Force
Copy-Item -Path 'nas-monitor-lvgl\lvgl-changes\ui_Screen_DiskDetail.c' -Destination 'main\ui\screens\ui_Screen_DiskDetail.c' -Force
Copy-Item -Path 'nas-monitor-lvgl\lvgl-changes\ui_Screen_SystemDetail.c' -Destination 'main\ui\screens\ui_Screen_SystemDetail.c' -Force
```

复制后重新编译即可生效。