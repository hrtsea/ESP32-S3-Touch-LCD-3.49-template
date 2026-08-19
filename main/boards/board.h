/**
 * @file board.h
 * @brief 板描述结构体 + 查询 API（board 抽象层）
 *
 * 集中描述一块开发板：屏幕接口、物理/逻辑分辨率、旋转能力、GPIO、
 * 触摸、背光、LVGL 时序、SD 存储与外围能力位图。
 * 值全部来自板级配置（迁移自 main/user_config.h 与 BSP 硬编码），
 * 上层（app / disp_driver / UI）只通过 board_get() 读取，不写死。
 *
 * 详见 docs/15-多开发板适配重构计划.md。
 */
#ifndef BOARDS_BOARD_H
#define BOARDS_BOARD_H

#include <stdint.h>
#include <stdbool.h>
#include "res_class.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 屏幕接口类型（决定 flush / DMA / 旋转策略，见计划 4.2） */
typedef enum {
    LCD_IF_QSPI,
    LCD_IF_SPI,
    LCD_IF_RGB,
} lcd_if_t;

/** 旋转模式：面板支持四档 90° 旋转，还是仅 180° 翻转 */
typedef enum {
    ROT_STEP_QUARTER,   /* 四档旋转（竖屏 QSPI，CPU 旋转换算） */
    ROT_STEP_FLIP,      /* 仅翻转（横屏 RGB / 带 MADCTL 面板） */
} rotation_step_mode_t;

typedef struct {
    int gpio;            /* 背光 PWM GPIO */
    int pwm_channel;     /* LEDC 通道（-1 = 由 BSP 决定） */
    int max_brightness;  /* PWM 最大值（当前 255） */
    uint8_t min_input;   /* 最小可见输入值（>= 此值映射为 1% 亮度） */
} backlight_desc_t;

typedef struct {
    int cs, pclk, data0, data1, data2, data3, rst;
    int dc;              /* SPI 接口使用，QSPI 为 -1 */
} panel_pins_t;

typedef struct {
    int i2c_port;        /* 复用 i2c_bsp 总线端口 */
    uint8_t addr;        /* 触摸 IC I2C 地址（CSTxxx: 0x3b） */
    int rst_gpio, int_gpio;
    int h_res, v_res;    /* 物理触摸坐标上限（= 面板物理分辨率） */
} touch_desc_t;

typedef struct {
    int tick_period_ms;      /* LVGL tick 周期（原 EXAMPLE_LVGL_TICK_PERIOD_MS） */
    int task_max_delay_ms;   /* LVGL 任务最大延迟 */
    int task_min_delay_ms;   /* LVGL 任务最小延迟 */
} lvgl_timing_t;

typedef struct {
    bool sdmmc;            /* true=SDMMC 主机，false=SDSPI */
    int clk, cmd, d0;      /* SDMMC 引脚（3.49": 41/39/40） */
    int width;             /* 数据线宽 1/4 */
    uint32_t max_freq_khz; /* 时钟频率（SDMMC_FREQ_DEFAULT=20000） */
    int mount_max_files;   /* FATFS max_files */
    bool internal_pullup;  /* S3 GPIO matrix 需内上拉，否则挂载 EACCES */
    bool auto_format;      /* format_if_mount_failed */
} sd_desc_t;

typedef struct {
    bool tca9554;          /* IO 扩展器电源控制 */
    bool rtc, imu, adc, audio, fan, sd, button, rgb;
} periph_caps_t;

typedef struct {
    const char *name;          /* "esp32s3_touch_lcd_3_49" */

    lcd_if_t lcd_if;
    int panel_h_res, panel_v_res;  /* 物理面板分辨率（写入面板驱动） */
    int canvas_w, canvas_h;        /* 逻辑 LVGL 画布分辨率（横屏语义） */
    int default_rotation;          /* 0/90/180/270 */
    rotation_step_mode_t rotation_step_mode;
    uint8_t display_flush_bands;   /* 分条 flush 行数（原 LVGL_FLUSH_STRIP_ROWS） */
    res_class_t res_class;         /* 分辨率档位（画布宽×高，见 res_class.h） */

    backlight_desc_t backlight;
    panel_pins_t panel_pins;
    touch_desc_t touch;
    lvgl_timing_t lvgl_timing;
    sd_desc_t sd;
    periph_caps_t periph;
} board_desc_t;

/** 返回当前编译板的描述（静态只读，可在全局初始化期安全调用） */
const board_desc_t *board_get(void);

/** 板级初始化进度回调（每完成一步调用一次，line 以换行结尾，可为 NULL） */
typedef void (*board_init_status_cb_t)(const char *line);

/**
 * 注册板级初始化进度回调（UI 启动画面用；未注册则仅输出日志）。
 * 需在 board_init() 之前调用。
 */
void board_set_status_cb(board_init_status_cb_t cb);

/**
 * 板级硬件初始化（随板内聚，替代原 hw_init() 的板级部分）：
 * I2C / TCA9554 / 背光 / 显示+LVGL / RTC / IMU / ADC / 音频 / 风扇 / SD / 按键，
 * 按 board_get()->periph.* 能力位图裁剪执行。
 * 需在 app_cfg_load() 之后调用（背光亮度/音频音量读取 g_cfg）。
 */
void board_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARDS_BOARD_H */
