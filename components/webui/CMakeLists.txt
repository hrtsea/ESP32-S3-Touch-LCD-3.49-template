idf_component_register(
    SRCS "webui.c"
    INCLUDE_DIRS "." "../../main" "../../main/config" "../../main/data" "../../main/drivers"
    REQUIRES esp_http_server fatfs sdcard_bsp recorder radio
             esp_wifi esp_netif lvgl mbedtls esp_wifi_config)
