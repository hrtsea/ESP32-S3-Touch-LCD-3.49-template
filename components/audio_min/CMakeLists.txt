idf_component_register(
    SRCS "audio_min.c"
    INCLUDE_DIRS "."
    REQUIRES radio
    PRIV_REQUIRES esp_codec_dev
)
