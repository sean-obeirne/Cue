idf_component_register(
    SRCS         "audio.c"
    INCLUDE_DIRS "include"
    REQUIRES     log
    # Future: PRIV_REQUIRES esp_driver_i2s
)
