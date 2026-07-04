string(TIMESTAMP BUILD_EPOCH_UTC "%s" UTC)

idf_component_register(
    SRCS
        "main.cpp"
        "ui/ui_main.c"
        "ui/ui_clock.c"
        "ui/ui_quotes.c"
        "ui/ui_settings.c"
        "ui/ui_radio.c"
        "ui/ui_recorder.c"
        "drivers/disp_driver.c"
        "network/wifi_manager.c"
        "network/sntp_manager.c"
        "config/app_cfg.c"
        "utils/i18n.c"
        "utils/theme.c"
        "utils/cli.c"
        "utils/hw_init.c"
        "utils/system_monitor.c"
        "utils/bg_fetcher.c"
        "fonts/font_jbmono_24.c"
        "fonts/font_jbmono_48.c"
        "fonts/font_jbmono_64.c"
        "fonts/font_jbmono_96.c"
        "fonts/font_cjk_14.c"
    INCLUDE_DIRS
        "."
        "ui"
        "drivers"
        "network"
        "config"
        "utils"
        "fonts"
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE BUILD_EPOCH_UTC=${BUILD_EPOCH_UTC})