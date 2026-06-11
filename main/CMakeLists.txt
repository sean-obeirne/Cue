idf_component_register(
    SRCS         "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        board
        display
        input
        battery
        storage
        audio
        bt_audio
        ui
        freertos
        log
)
