/* sim/stubs/esp_lcd_panel_ops.h - LCD 面板操作 stub */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_lcd_panel_t *esp_lcd_panel_handle_t;

typedef enum {
    ESP_LCD_COLOR_PIXEL_FORMAT_RGB565 = 0,
    ESP_LCD_COLOR_PIXEL_FORMAT_RGB888,
    ESP_LCD_COLOR_PIXEL_FORMAT_INVALID,
} esp_lcd_color_pixel_format_t;

#ifdef __cplusplus
}
#endif
