idf_component_register(
    SRCS         "debounce.c" "buttons.c" "encoder.c"
    INCLUDE_DIRS "include"
    REQUIRES     driver esp_driver_gpio board log
)
