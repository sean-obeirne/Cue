idf_component_register(
    SRCS        "display.c" "ssd1309.c" "glyphs.c"
    INCLUDE_DIRS "include"
    REQUIRES     driver esp_driver_i2c board log
)
