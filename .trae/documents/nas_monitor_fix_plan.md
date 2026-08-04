# NAS Monitor 实施收尾修复计划

> 项目：ESP32-S3 NAS 监控屏幕
> 状态：实施步骤 1-9 已完成，步骤 10（编译验证）发现 2 个编译错误待修复
> 创建日期：2026-07-18
> 关联文档：`nas_monitor_full_plan.md`（原始规划，已批准并实施）

---

## 一、当前状态分析

### 1.1 已完成实施 ✅

| 步骤 | 文件 | 状态 |
|------|------|------|
| 1 | `main/config/config.h` — FAN GPIO 宏 | ✅ 已添加 |
| 1 | `main/config/config.c` — `config_save_fan()` 发布事件 | ✅ 已添加 |
| 1 | `main/data/fan_control.h` — API 与数据结构 | ✅ 完整 |
| 2 | `main/data/fan_control.c` — 风扇控制实现（LEDC+PCNT+任务） | ✅ 完整 |
| 3 | `main/ui/screens/ui_Screen_Settings_FanTab.c/.h` — FanTab | ⚠️ 已创建但用了 LVGL 9 API，需重写 |
| 4 | `main/ui/screens/ui_Screen_DiskDetail.c/.h` — 硬盘详情屏 | ✅ 已创建 |
| 5 | `main/ui/screens/ui_Screen_SystemDetail.c/.h` — 系统详情屏 | ✅ 已创建 |
| 6 | `main/ui/screens/ui_Screen_Settings.h/.c` — FanTab 集成 | ✅ 已集成 |
| 7 | `main/ui/screens/ui_Screen_Overview.c` — 点击导航改造 | ✅ 已绑定（HDD/CPU/MEM 点击事件已就绪，line 233/254/340） |
| 8 | `main/ui/ui.h` — 头文件包含 | ✅ 已添加 |
| 8 | `main/ui/ui_events.c` — 事件处理 + 3 个点击回调 | ✅ 已实现（line 436-474） |
| 9 | `main/utils/hw_init.c` — `fan_control_init()` 集成 | ✅ 已添加（line 107-109） |

### 1.2 待修复编译错误 ❌

**错误 1：函数未声明**

`ui_Screen_Overview.c:234` 和 `:255` 编译报错：
```
error: 'ui_event_Screen_Overview_cpu_clicked' undeclared
error: 'ui_event_Screen_Overview_mem_clicked' undeclared
```

**根因**：`ui_events.c` 已实现这两个回调（line 452-474），但 `ui_events.h` 中未声明。

**错误 2：FanTab.c 使用了 LVGL 9 独有 API**

项目实际版本：**LVGL 8.3.11**（`components/lvgl/lvgl.h:16-18` 确认）

FanTab.c 中错误使用的 LVGL 9 API（在 8.3.11 中不存在）：

| 错误 API（LVGL 9） | LVGL 8.3.11 替代 |
|--------------------|------------------|
| `LV_CANVAS_BUF_SIZE(w, h)` | `LV_CANVAS_BUF_SIZE_TRUE_COLOR(w, h)` |
| `LV_COLOR_FORMAT_RGB565` | `LV_IMG_CF_TRUE_COLOR` |
| `lv_canvas_init_layer()` / `lv_layer_t` / `lv_canvas_finish_layer()` | 直接调用 `lv_canvas_draw_rect/line/text` |
| `lv_point_precise_t` / `lv_value_precise_t` | `lv_point_t` / `lv_coord_t` |
| `lv_draw_line(&layer, &dsc, &p1, &p2)` | `lv_canvas_draw_line(canvas, points[], 2, &dsc)` |
| `lv_draw_rect(&layer, &dsc, &area)` | `lv_canvas_draw_rect(canvas, x, y, w, h, &dsc)` |
| `lv_draw_label(&layer, &dsc, &area, buf, NULL)` | `lv_canvas_draw_text(canvas, x, y, max_w, &dsc, buf)` |
| `LV_OBJ_TRANSFORM_POINT_INVERSE` 枚举 | `bool recursive=true, bool inv=true`（LVGL 8 原型） |

### 1.3 LVGL 8.3.11 实际可用 API（经源码验证）

- ✅ `lv_canvas_set_buffer(canvas, buf, w, h, lv_img_cf_t cf)` — `lv_canvas.h:66`
- ✅ `lv_canvas_fill_bg(canvas, color, opa)` — `lv_canvas.h:185`
- ✅ `lv_canvas_draw_rect(canvas, x, y, w, h, &dsc)` — `lv_canvas.h:196`
- ✅ `lv_canvas_draw_text(canvas, x, y, max_w, &dsc, txt)` — `lv_canvas.h:208`
- ✅ `lv_canvas_draw_line(canvas, points[], point_cnt, &dsc)` — `lv_canvas.h:229`
- ✅ `LV_CANVAS_BUF_SIZE_TRUE_COLOR(w, h)` — `lv_canvas.h:258`
- ✅ `lv_draw_line_dsc_t` / `lv_draw_line_dsc_init(&dsc)` — `lv_draw_line.h`
- ✅ `lv_draw_rect_dsc_t` / `lv_draw_rect_dsc_init(&dsc)` — `lv_draw_rect.h`（字段：radius、bg_color、border_width）
- ✅ `lv_draw_label_dsc_t` / `lv_draw_label_dsc_init(&dsc)` — `lv_draw_label.h`（字段：color、font、align）
- ✅ `lv_obj_transform_point(obj, &p, bool recursive, bool inv)` — `lv_obj_pos.h:358`

### 1.4 不阻塞编译的 warning（暂不处理）

- `driver/pcnt.h` deprecated — 仅 warning，建议后续迁移到 `driver/pulse_cnt.h`

---

## 二、修复方案

### 2.1 修复 1：`main/ui/ui_events.h` 添加声明

在 line 27（`ui_event_Screen_Overview_hdd_clicked` 声明之后）插入：

```c
void ui_event_Screen_Overview_hdd_clicked(lv_event_t* e);
void ui_event_Screen_Overview_cpu_clicked(lv_event_t* e);   /* 新增 */
void ui_event_Screen_Overview_mem_clicked(lv_event_t* e);    /* 新增 */
void ui_event_Screen_Overview_gesture(lv_event_t* e);
```

### 2.2 修复 2：重写 `ui_Screen_Settings_FanTab.c` 的 LVGL 9 API

**修改文件**：`e:\zotlabnas-split\ESP32-S3-Touch-LCD-3.49-template\main\ui\screens\ui_Screen_Settings_FanTab.c`

#### 修改点 A：画布缓冲区宏（line 37）

```diff
- static uint8_t s_canvas_buf[LV_CANVAS_BUF_SIZE(CANVAS_W, CANVAS_H)];
+ static uint8_t s_canvas_buf[LV_CANVAS_BUF_SIZE_TRUE_COLOR(CANVAS_W, CANVAS_H)];
```

#### 修改点 B：坐标换算函数返回类型（line 58-86）

将 `lv_point_precise_t` 改为 `lv_point_t`，`lv_value_precise_t` 改为 `lv_coord_t`：

```c
static lv_point_t temp_pwm_to_pixel(int16_t temp, uint8_t pwm)
{
    lv_point_t p;
    int16_t t = temp;
    if (t < TEMP_MIN) t = TEMP_MIN;
    if (t > TEMP_MAX) t = TEMP_MAX;
    int pwm_i = pwm;
    if (pwm_i < PWM_MIN) pwm_i = PWM_MIN;
    if (pwm_i > PWM_MAX) pwm_i = PWM_MAX;
    p.x = (lv_coord_t)(CANVAS_MARGIN_L + (int32_t)(t - TEMP_MIN) * PLOT_W / (TEMP_MAX - TEMP_MIN));
    p.y = (lv_coord_t)(PLOT_H - (int32_t)(pwm_i - PWM_MIN) * PLOT_H / (PWM_MAX - PWM_MIN));
    return p;
}
```

#### 修改点 C：重写 `draw_canvas()` 函数（line 91-167）

完整重写为 LVGL 8 API：

```c
static void draw_canvas(void)
{
    lv_canvas_fill_bg(s_canvas, COLOR_PANEL, LV_OPA_COVER);

    /* 网格线描述符 */
    lv_draw_line_dsc_t grid_dsc;
    lv_draw_line_dsc_init(&grid_dsc);
    grid_dsc.color = COLOR_GRID;
    grid_dsc.width = 1;

    /* 水平网格 (PWM 0/25/50/75/100) */
    for (int pwm = 0; pwm <= 100; pwm += 25) {
        lv_point_t pts[2];
        pts[0] = temp_pwm_to_pixel(TEMP_MIN, pwm);
        pts[1] = temp_pwm_to_pixel(TEMP_MAX, pwm);
        lv_canvas_draw_line(s_canvas, pts, 2, &grid_dsc);
    }
    /* 垂直网格 (Temp 0/30/60/90) */
    for (int t = 0; t <= 90; t += 30) {
        lv_point_t pts[2];
        pts[0] = temp_pwm_to_pixel(t, PWM_MIN);
        pts[1] = temp_pwm_to_pixel(t, PWM_MAX);
        lv_canvas_draw_line(s_canvas, pts, 2, &grid_dsc);
    }

    /* 标尺文字 */
    lv_draw_label_dsc_t lbl_dsc;
    lv_draw_label_dsc_init(&lbl_dsc);
    lbl_dsc.color = COLOR_TEXT_DIM;
    lbl_dsc.font = &lv_font_montserrat_12;
    lbl_dsc.align = LV_TEXT_ALIGN_RIGHT;

    char buf[8];
    for (int pwm = 0; pwm <= 100; pwm += 50) {
        lv_point_t p = temp_pwm_to_pixel(TEMP_MIN, pwm);
        snprintf(buf, sizeof(buf), "%d", pwm);
        lv_canvas_draw_text(s_canvas, 0, p.y - 6, CANVAS_MARGIN_L - 2, &lbl_dsc, buf);
    }

    lbl_dsc.align = LV_TEXT_ALIGN_CENTER;
    for (int t = 30; t <= 90; t += 30) {
        lv_point_t p = temp_pwm_to_pixel(t, PWM_MIN);
        snprintf(buf, sizeof(buf), "%d", t);
        lv_canvas_draw_text(s_canvas, p.x - 12, CANVAS_H - CANVAS_MARGIN_B + 2, 24, &lbl_dsc, buf);
    }

    /* 曲线折线 */
    lv_draw_line_dsc_t curve_dsc;
    lv_draw_line_dsc_init(&curve_dsc);
    curve_dsc.color = COLOR_CURVE;
    curve_dsc.width = 2;
    for (int i = 0; i < FAN_CURVE_POINTS - 1; i++) {
        lv_point_t pts[2];
        pts[0] = temp_pwm_to_pixel(s_editing_cfg.curve[i].temp, s_editing_cfg.curve[i].pwm_pct);
        pts[1] = temp_pwm_to_pixel(s_editing_cfg.curve[i+1].temp, s_editing_cfg.curve[i+1].pwm_pct);
        lv_canvas_draw_line(s_canvas, pts, 2, &curve_dsc);
    }

    /* 5 个控制点（实心圆） */
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.radius = LV_RADIUS_CIRCLE;
    rect_dsc.border_width = 0;
    for (int i = 0; i < FAN_CURVE_POINTS; i++) {
        lv_point_t p = temp_pwm_to_pixel(s_editing_cfg.curve[i].temp, s_editing_cfg.curve[i].pwm_pct);
        rect_dsc.bg_color = (i == s_dragging_point_idx) ? COLOR_POINT_SEL : COLOR_POINT;
        lv_canvas_draw_rect(s_canvas, p.x - 4, p.y - 4, 8, 8, &rect_dsc);
    }
}
```

#### 修改点 D：重写 `canvas_pressed_cb()` 坐标转换（line 194-246）

将 LVGL 9 的 `lv_obj_transform_point(obj, &canvas_p, LV_OBJ_TRANSFORM_POINT_INVERSE)` 替换为 LVGL 8 的方式：用 `lv_obj_get_coords` 获取画布屏幕坐标，手动减偏移。

```c
static void canvas_pressed_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = lv_event_get_target(e);
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;

    /* 获取屏幕坐标点 */
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    /* 转换为画布内坐标：减去画布在屏幕中的左上角坐标 */
    lv_area_t obj_coords;
    lv_obj_get_coords(obj, &obj_coords);
    lv_coord_t cx = p.x - obj_coords.x1;
    lv_coord_t cy = p.y - obj_coords.y1;

    if (code == LV_EVENT_PRESSED) {
        s_dragging_point_idx = -1;
        for (int i = 0; i < FAN_CURVE_POINTS; i++) {
            lv_point_t pt = temp_pwm_to_pixel(s_editing_cfg.curve[i].temp, s_editing_cfg.curve[i].pwm_pct);
            int32_t dx = cx - pt.x;
            int32_t dy = cy - pt.y;
            if (dx * dx + dy * dy <= POINT_HIT_RADIUS * POINT_HIT_RADIUS) {
                s_dragging_point_idx = (int8_t)i;
                break;
            }
        }
    } else if (code == LV_EVENT_PRESSING || code == LV_EVENT_PRESS_LOST) {
        if (s_dragging_point_idx < 0) return;
        int i = s_dragging_point_idx;

        /* 限幅：温度在 [前一点+1, 后一点-1] */
        int16_t min_t = (i == 0) ? TEMP_MIN : s_editing_cfg.curve[i-1].temp + 1;
        int16_t max_t = (i == FAN_CURVE_POINTS - 1) ? TEMP_MAX : s_editing_cfg.curve[i+1].temp - 1;

        int16_t new_temp = pixel_to_temp(cx);
        int new_pwm = pixel_to_pwm(cy);
        if (new_temp < min_t) new_temp = min_t;
        if (new_temp > max_t) new_temp = max_t;
        if (new_pwm < 0) new_pwm = 0;
        if (new_pwm > 100) new_pwm = 100;

        if (s_editing_cfg.curve[i].temp != new_temp || s_editing_cfg.curve[i].pwm_pct != new_pwm) {
            s_editing_cfg.curve[i].temp = new_temp;
            s_editing_cfg.curve[i].pwm_pct = (uint8_t)new_pwm;
            draw_canvas();
            update_point_labels();
            update_dirty_state();
        }
    } else if (code == LV_EVENT_RELEASED) {
        s_dragging_point_idx = -1;
        draw_canvas();
    }
}
```

#### 修改点 E：画布缓冲区创建（line 394）

```diff
- lv_canvas_set_buffer(s_canvas, s_canvas_buf, CANVAS_W, CANVAS_H, LV_COLOR_FORMAT_RGB565);
+ lv_canvas_set_buffer(s_canvas, s_canvas_buf, CANVAS_W, CANVAS_H, LV_IMG_CF_TRUE_COLOR);
```

---

## 三、实施步骤

| 步骤 | 操作 | 文件 |
|------|------|------|
| 1 | 添加 `ui_event_Screen_Overview_cpu_clicked` 和 `ui_event_Screen_Overview_mem_clicked` 声明 | `main/ui/ui_events.h` |
| 2 | 替换 `LV_CANVAS_BUF_SIZE` → `LV_CANVAS_BUF_SIZE_TRUE_COLOR` | `main/ui/screens/ui_Screen_Settings_FanTab.c:37` |
| 3 | 替换 `LV_COLOR_FORMAT_RGB565` → `LV_IMG_CF_TRUE_COLOR` | `main/ui/screens/ui_Screen_Settings_FanTab.c:394` |
| 4 | 重写 `temp_pwm_to_pixel()` — `lv_point_precise_t`→`lv_point_t`，`lv_value_precise_t`→`lv_coord_t` | `main/ui/screens/ui_Screen_Settings_FanTab.c:58-70` |
| 5 | 重写 `draw_canvas()` — 移除 `lv_canvas_init_layer/finish_layer`，改用 `lv_canvas_draw_line/rect/text` | `main/ui/screens/ui_Screen_Settings_FanTab.c:91-167` |
| 6 | 重写 `canvas_pressed_cb()` — 用 `lv_obj_get_coords` 替代 `lv_obj_transform_point` | `main/ui/screens/ui_Screen_Settings_FanTab.c:194-246` |
| 7 | 编译验证：`& "C:\esp\v5.5.4\esp-idf\export.ps1"; idf.py build 2>&1 | Tee-Object -FilePath "build_log.txt"` | — |

---

## 四、验证标准

### 4.1 编译通过标准

- 0 error
- 允许 warning（pcnt.h deprecated 等）

### 4.2 关键编译检查点

- `ui_Screen_Overview.c` 不再报 `ui_event_Screen_Overview_cpu_clicked` undeclared
- `ui_Screen_Settings_FanTab.c` 不再报 `lv_canvas_init_layer`、`LV_CANVAS_BUF_SIZE`、`LV_COLOR_FORMAT_RGB565`、`lv_point_precise_t`、`lv_value_precise_t`、`LV_OBJ_TRANSFORM_POINT_INVERSE` 未定义

### 4.3 烧录功能验证（编译通过后）

按 `nas_monitor_full_plan.md` 第 7.2 节验证清单执行：
- [ ] FanTab 显示 5 点曲线，可拖拽
- [ ] HDD 点击进入 DiskDetail
- [ ] CPU meter 点击进入 SystemDetail (CPU 模式)
- [ ] 内存条点击进入 SystemDetail (MEM 模式)

---

## 五、风险与缓解

| 风险 | 缓解 |
|------|------|
| `lv_obj_get_coords` 返回的屏幕坐标可能与滚动 tabview 有偏移 | 若点击命中不准，改用 `lv_obj_transform_point(obj, &p, true, true)` |
| `lv_canvas_draw_text` 的 `max_w` 参数影响换行 | 标尺文字短，给足够宽度（如 24px）即可 |
| Canvas 缓冲区静态分配占用 RAM | 280×84×2 = 47KB，可接受（PSRAM 充足） |

---

## 六、不在本次修复范围

- `driver/pcnt.h` → `driver/pulse_cnt.h` 迁移（仅 warning，后续处理）
- FanTab UI 视觉优化（曲线平滑、预设方案等）
- 详情屏返回时数据缓存
