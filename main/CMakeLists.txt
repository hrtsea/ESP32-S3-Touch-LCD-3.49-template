string(TIMESTAMP BUILD_EPOCH_UTC "%s" UTC)

idf_component_register(
    SRCS
        "main.cpp"
        "ui/ui.c"
        "ui/ui_helpers.c"
        "ui/ui_events.c"
        "ui/screens/ui_Screen_Settings_WifiTab.c"
        "ui/screens/ui_Screen_Settings_NasTab.c"
        "ui/screens/ui_Screen_Settings_ScreenTab.c"
        "ui/screens/ui_Screen_Settings_StationTab.c"
        "ui/screens/ui_Screen_Settings_MusicTab.c"
        "ui/screens/ui_Screen_Settings_RegionTab.c"
        "ui/screens/ui_Screen_Settings_GuideTab.c"
        "ui/screens/ui_Screen_Settings_FanTab.c"
        "ui/screens/ui_Screen_Boot.c"
        "ui/screens/ui_Screen_WifiConfig.c"
        "ui/screens/ui_Screen_Overview.c"
        "ui/screens/ui_Screen_Settings.c"
        "ui/screens/ui_Screen_Storage.c"
        "ui/screens/ui_Screen_DiskDetail.c"
        "ui/screens/ui_Screen_SystemDetail.c"
        "ui/fonts/ui_font_NotoSanThai16.c"
        "ui/images/ui_img_images_button_png.c"
        "ui/images/ui_img_images_globe_png.c"
        "ui/images/ui_img_images_timezone_png.c"
        "drivers/disp_driver.c"
        "network/sntp_manager.c"
        "config/app_cfg.c"
        "utils/event_bus.c"
        "utils/i18n.c"
        "utils/theme.c"
        "utils/cli.c"
        "utils/hw_init.c"
        "utils/bg_fetcher.c"
        "utils/http_timer.c"
        "utils/wifi_adapter.c"
        "utils/wifi_bridge.c"
        "config/config.c"
        "data/data_source.c"
        "data/fan_control.c"
        "data/nas_event_loop.c"
        "data/nas_data_json.c"
        "data/client/mock_client.c"
        "data/client/api_client.c"
        "data/client/synology_client.c"
        "data/client/netdata_client.c"
        "data/client/truenas_client.c"
        "data/client/qnap_client.c"
        "data/client/serial_client.c"
        "data/client/snmp_client.c"
        "ui/fonts/font_jbmono_24.c"
        "ui/fonts/font_jbmono_48.c"
        "ui/fonts/font_jbmono_64.c"
        "ui/fonts/font_jbmono_96.c"
        "ui/fonts/font_cjk_14.c"
    INCLUDE_DIRS
        "."
        "ui"
        "drivers"
        "network"
        "config"
        "utils"
        "data"
    REQUIRES
        json
        esp_wifi_config
        esp_bus
        lvgl
        esp_lcd
        esp_io_expander_tca9554
        esp_audio_simple_player
        esp_codec_dev
        esp_http_server
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        lwip
        console
        i2c_bsp
        i2c_equipment
        sdcard_bsp
        button_bsp
        adc_bsp
        lcd_bl_pwm_bsp
        audio_min
        recorder
        radio
        webui

)

target_compile_definitions(${COMPONENT_LIB} PRIVATE BUILD_EPOCH_UTC=${BUILD_EPOCH_UTC})
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=stringop-truncation)

# Force linker to include all symbols from radio and recorder archives
# (webui references them but link order doesn't resolve the dependency)
idf_build_set_property(LINK_OPTIONS "-Wl,--whole-archive" APPEND)
idf_build_set_property(LINK_OPTIONS "esp-idf/radio/libradio.a" APPEND)
idf_build_set_property(LINK_OPTIONS "esp-idf/recorder/librecorder.a" APPEND)
idf_build_set_property(LINK_OPTIONS "-Wl,--no-whole-archive" APPEND)





