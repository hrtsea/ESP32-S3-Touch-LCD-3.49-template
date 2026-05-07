idf_component_register(SRCS "main.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES audio_min esp_timer esp_lcd driver
                             i2c_bsp i2c_equipment lcd_bl_pwm_bsp adc_bsp
                             sdcard_bsp button_bsp lvgl
                             espressif__esp_lcd_axs15231b
                             espressif__esp_io_expander_tca9554)
