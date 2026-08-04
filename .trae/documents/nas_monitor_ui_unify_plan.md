# NAS Monitor UI 统一设计计划

## 项目调研结论

### 屏幕规格
- **分辨率**: 640x172 像素（宽屏）
- **目标**: 所有页面完整显示在一屏内，禁止滚动

### Overview 页面设计模式（参考标准）
| 区域 | 高度 | 内容 |
|------|------|------|
| 状态栏 | 35px | 标题、时间、网络上下行、IP、刷新按钮 |
| 分隔线 | 2px | 灰色线 |
| 内容区 | 135px | CPU/温度仪表、内存/磁盘条、硬盘指示灯 |

### 当前各页面状态分析

| 页面 | 状态 | 问题 |
|------|------|------|
| **Overview** | ✅ 符合标准 | 640x172，无滚动 |
| **Storage** | ✅ 基本符合 | 640x172，无滚动，但布局可优化 |
| **Settings** | ❌ 有问题 | Tabview 和键盘高度超出，无固定尺寸 |
| **DiskDetail** | ❌ 有问题 | 内容区 138px，标签太多超出 |
| **SystemDetail** | ❌ 有问题 | 内容区 138px，行太多超出 |
| **FanTab** | ⚠️ 需检查 | 在 Settings Tabview 内，高度由 Tabview 控制 |

### 核心问题汇总
1. **Settings**: `lv_obj_create(NULL)` 创建的屏幕没有设置固定高度，导致子控件使用 `lv_pct(100)` 时高度不确定
2. **Detail 页面**: 内容区高度 138px，但包含太多行（每行 ~20px = 7行 = 140px），超出屏幕
3. **Storage**: 硬盘数量多时（如9个），两行布局可能超出
4. **键盘**: `lv_keyboard_set_height` 使用 `lv_pct(100)`，会撑满整个屏幕

---

## 修改文件清单

### 1. `main/ui/screens/ui_Screen_Settings.c`
- 设置屏幕固定尺寸 640x172
- 限制 Tabview 高度，留出标签按钮空间
- 限制键盘高度，避免超出屏幕

### 2. `main/ui/screens/ui_Screen_DiskDetail.c`
- 重新布局内容区：减少行数，合并信息
- 使用两列紧凑布局
- 优化字体大小和间距

### 3. `main/ui/screens/ui_Screen_SystemDetail.c`
- CPU模式：减少行数，合并信息
- MEM模式：减少行数，合并信息
- 优化字体大小和间距

### 4. `main/ui/screens/ui_Screen_Storage.c`
- 当硬盘数量多（>6）时，采用更紧凑的布局
- 减少每行高度，增加列数

### 5. `main/ui/screens/ui_Screen_Settings_FanTab.c`
- 调整控件位置，确保在 Tabview 内完整显示

---

## 详细设计

### 1. Settings 页面改造

**问题**: 屏幕没有固定尺寸，Tabview 和键盘超出

**改造方案**:
```c
// 屏幕创建时设置固定尺寸
ui_Screen_Settings = lv_obj_create(NULL);
lv_obj_set_size(ui_Screen_Settings, 640, 172);  // 新增
lv_obj_clear_flag(ui_Screen_Settings, LV_OBJ_FLAG_SCROLLABLE);

// Tabview 高度 = 屏幕高度 - 标签栏高度 - 关闭按钮
lv_obj_set_size(ui_Settings_Tabview_ConfigPanel, 640, 132);
lv_obj_align(ui_Settings_Tabview_ConfigPanel, LV_ALIGN_TOP_MID, 0, 40);

// 键盘高度限制
lv_obj_set_height(ui_Settings_Keyboard_Keyboard1, 130);  // 非 lv_pct(100)
```

### 2. DiskDetail 页面改造

**问题**: 内容区 138px 包含 11 个标签，超出屏幕

**改造方案**（两列紧凑布局，总高度 ≤ 138px）:
```
行1 (20px): Name + Model
行2 (20px): Device + Mount
行3 (20px): Type + Slot + Online
行4 (20px): Health + Usage Bar (带百分比)
行5 (18px): Temp + Read + Write
```

- 字体改为 12px（montserrat_12）
- 行间距从 20px 减少到 18px
- 合并相关信息到同一行

### 3. SystemDetail 页面改造

**CPU模式改造**（4行布局，总高度 ≤ 138px）:
```
行1 (20px): CPU Total % + Load Average
行2 (36px): 4个核心条（垂直排列）
行3 (18px): Temp CPU + Temp SYS + Uptime
行4 (18px): Hostname + Model
```

**MEM模式改造**（4行布局，总高度 ≤ 138px）:
```
行1 (22px): RAM 条 + Free/Cached
行2 (22px): Swap 条（如存在）
行3 (22px): Disk 条
行4 (18px): Hostname
```

### 4. Storage 页面改造

**问题**: 9个硬盘两行布局时高度超出

**改造方案**:
- ≤4个硬盘：单列
- 5-6个硬盘：两列
- 7-9个硬盘：三列（更紧凑）

```c
uint8_t cols = (total_disks <= 4) ? 1 : (total_disks <= 6) ? 2 : 3;
uint8_t rows = (total_disks + cols - 1) / cols;
int y_offset = row * 24;  // 减少行高
```

### 5. FanTab 改造

**问题**: 控件位置可能超出 Tabview 区域

**改造方案**:
- 调整 `create_top_bar`、`create_canvas_area`、`create_params_row` 的位置
- 确保总高度 ≤ Tabview 高度（132px）

---

## 实施步骤

### 步骤1: 修改 Settings 页面（基础）
- 设置屏幕尺寸 640x172
- 限制 Tabview 高度和位置
- 限制键盘高度

### 步骤2: 修改 DiskDetail 页面
- 重新布局内容为紧凑两列
- 减少行数和间距

### 步骤3: 修改 SystemDetail 页面
- CPU模式和MEM模式都重新布局为紧凑布局
- 减少行数和间距

### 步骤4: 修改 Storage 页面
- 支持三列布局
- 减少行高

### 步骤5: 修改 FanTab
- 调整控件位置适应新的 Tabview 高度

### 步骤6: 编译验证
- 编译项目
- 检查是否有错误

---

## 风险与注意事项

### 风险1: 字体大小调整导致文字截断
- **缓解**: 使用 `lv_label_set_long_mode(lv_label, LV_LABEL_LONG_DOT)` 自动省略号
- **验证**: 运行时检查所有标签是否完整显示

### 风险2: 控件重叠
- **缓解**: 使用固定像素定位而非百分比
- **验证**: 运行时检查各页面布局

### 风险3: 键盘弹出时遮挡内容
- **缓解**: 键盘只在需要时显示（已实现），且高度限制为 130px
- **验证**: 测试输入框交互

### 风险4: 硬盘数量变化导致布局异常
- **缓解**: 动态计算列数和行高
- **验证**: 测试不同硬盘数量配置

---

## 验证步骤

1. **编译验证**: `idf.py build` 无错误
2. **Settings 页面**: 所有标签页完整显示，键盘不超出
3. **DiskDetail 页面**: 所有硬盘信息完整显示在一屏
4. **SystemDetail 页面**: CPU 和 MEM 模式都完整显示在一屏
5. **Storage 页面**: 1-9个硬盘都完整显示在一屏
6. **FanTab**: 风扇曲线编辑完整显示在一屏

---

## 依赖关系

- **无外部依赖**: 所有修改都是 LVGL UI 布局调整
- **模块间依赖**: Settings 页面修改会影响 FanTab，需同步调整

---

## 完成标准

- 所有页面设置 `lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE)`
- 所有页面高度 ≤ 172px，宽度 ≤ 640px
- 编译成功，无错误
- 运行时所有页面完整显示，无滚动条