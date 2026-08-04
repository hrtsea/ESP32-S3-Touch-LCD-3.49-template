/* sim/stubs/esp_lcd_panel_io.h - LCD 面板 IO stub（仅 disp_driver.c 使用，已 override） */
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_lcd_panel_io_t *esp_lcd_panel_io_handle_t;

typedef struct {
    void *cb;
    void *cb_ctx;
    int pclk_hz;
    size_t trans_queue_depth;
    int lcd_cmd_bits;
    int lcd_param_bits;
    int cs_gpio_num;
    void *spi_config;
} esp_lcd_panel_io_spi_config_t;

#ifdef __cplusplus
}
#endif
