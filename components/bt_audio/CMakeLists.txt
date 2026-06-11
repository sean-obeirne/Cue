idf_component_register(
    SRCS         "bt_audio.c"
    INCLUDE_DIRS "include"
    REQUIRES     log
    # Future: PRIV_REQUIRES bt
)
