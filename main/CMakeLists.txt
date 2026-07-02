idf_component_register(SRCS "main.cpp" "cli.c" "i18n.c" "font_cjk_14.c"
                          "font_jbmono_24.c" "font_jbmono_48.c"
                          "font_jbmono_64.c" "font_jbmono_96.c"
                    INCLUDE_DIRS "."
                    REQUIRES audio_min radio recorder webui esp_timer esp_lcd driver
                         i2c_bsp i2c_equipment lcd_bl_pwm_bsp adc_bsp
                         sdcard_bsp button_bsp lvgl console
                         nvs_flash esp_wifi esp_event esp_netif
                         esp_http_client mbedtls
                             espressif__esp_lcd_axs15231b
                             espressif__esp_io_expander_tca9554)

string(TIMESTAMP BUILD_EPOCH_UTC "%s" UTC)
target_compile_definitions(${COMPONENT_LIB} PRIVATE BUILD_EPOCH_UTC=${BUILD_EPOCH_UTC})
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=stringop-truncation)
