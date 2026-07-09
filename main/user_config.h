#ifndef USER_CONFIG_H
#define USER_CONFIG_H

//spi & i2c handle
#define LCD_HOST SPI3_HOST

// touch I2C port
#define Touch_SCL_NUM (GPIO_NUM_18)
#define Touch_SDA_NUM (GPIO_NUM_17)

// touch esp
#define ESP_SCL_NUM (GPIO_NUM_48)
#define ESP_SDA_NUM (GPIO_NUM_47)

//  DISP
#define EXAMPLE_PIN_NUM_LCD_CS     (GPIO_NUM_9) 
#define EXAMPLE_PIN_NUM_LCD_PCLK   (GPIO_NUM_10)
#define EXAMPLE_PIN_NUM_LCD_DATA0  (GPIO_NUM_11)
#define EXAMPLE_PIN_NUM_LCD_DATA1  (GPIO_NUM_12)
#define EXAMPLE_PIN_NUM_LCD_DATA2  (GPIO_NUM_13)
#define EXAMPLE_PIN_NUM_LCD_DATA3  (GPIO_NUM_14)
#define EXAMPLE_PIN_NUM_LCD_RST    (GPIO_NUM_21)
#define EXAMPLE_PIN_NUM_BK_LIGHT   (GPIO_NUM_8) 

/* Physical panel: 172 wide x 640 tall, portrait. */
#define EXAMPLE_LCD_H_RES 172
#define EXAMPLE_LCD_V_RES 640
/* Logical LVGL canvas: 640 wide x 172 tall (landscape).
   flush_cb rotates 90deg CW into the panel's portrait address window. */
#define UI_CANVAS_W 640
#define UI_CANVAS_H 172
#define LVGL_FLUSH_STRIP_ROWS 128
#define LVGL_DMA_BUFF_LEN (EXAMPLE_LCD_H_RES * LVGL_FLUSH_STRIP_ROWS * 2)
#define LVGL_SPIRAM_BUFF_LEN (UI_CANVAS_W * UI_CANVAS_H * 2)


#define EXAMPLE_PIN_NUM_TOUCH_ADDR        0x3b
#define EXAMPLE_PIN_NUM_TOUCH_RST         (-1)
#define EXAMPLE_PIN_NUM_TOUCH_INT         (-1)


#define EXAMPLE_LVGL_TICK_PERIOD_MS    2
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 16
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 2



/*ADDR*/
#define EXAMPLE_RTC_ADDR 0x51

#define EXAMPLE_IMU_ADDR 0x6b

/* World sunlight map recompute interval (ms). */
#define SUNMAP_RECOMPUTE_MS (5 * 60 * 1000)


/*
 * Provisioning / SoftAP & web UI credentials.
 *
 * These are DEVELOPMENT defaults kept out of main.cpp so the source tree
 * carries no inline plaintext secrets. For production builds, override via
 * sdkconfig (CONFIG_DEFAULT_AP_SSID / CONFIG_DEFAULT_AP_PASSWORD / ...) or
 * inject from NVS / a local secrets header excluded from VCS.
 */
#ifndef DEFAULT_AP_SSID
#define DEFAULT_AP_SSID      "NAS-Monitor"
#endif
#ifndef DEFAULT_AP_PASSWORD
#define DEFAULT_AP_PASSWORD  "12345678"   /* DEV ONLY: must be overridden in production */
#endif
#ifndef WEBUI_AUTH_USER
#define WEBUI_AUTH_USER      "admin"
#endif
#ifndef WEBUI_AUTH_PASSWORD
#define WEBUI_AUTH_PASSWORD  "admin"      /* DEV ONLY: must be overridden in production */
#endif


#endif