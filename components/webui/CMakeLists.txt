idf_component_register(
    SRCS "webui.c"
    INCLUDE_DIRS "." "../../main" "../../main/config" "../../main/data"
    REQUIRES esp_http_server fatfs sdcard_bsp recorder radio
             esp_wifi esp_netif lvgl mbedtls)
