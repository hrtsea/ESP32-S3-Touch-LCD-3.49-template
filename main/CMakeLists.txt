string(TIMESTAMP BUILD_EPOCH_UTC "%s" UTC)

idf_component_register(
    SRCS
        "main.cpp"
        "ui/ui.c"
        "ui/ui_helpers.c"
        "ui/ui_events.c"
        "ui/ui_hello.c"
        "ui/ui_clock.c"
        "ui/ui_quotes.c"
        "ui/ui_settings.c"
        
        
        "drivers/disp_driver.c"
        "network/wifi_manager.c"
        "network/sntp_manager.c"
        "config/app_cfg.c"
        "utils/event_bus.c"
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
    EMBED_FILES "ui/canon.pcm"
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE BUILD_EPOCH_UTC=${BUILD_EPOCH_UTC})