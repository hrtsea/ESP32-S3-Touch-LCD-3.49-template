idf_component_register(
    SRCS "usb_hid_bsp.c"
    INCLUDE_DIRS "."
    REQUIRES esp_timer usb
)