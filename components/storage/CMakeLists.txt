idf_component_register(
    SRCS         "storage.c"
    INCLUDE_DIRS "include"
    REQUIRES     board log esp_driver_spi
    # Future: PRIV_REQUIRES fatfs sdmmc esp_driver_sdspi
)
