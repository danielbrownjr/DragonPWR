idf_component_register(
    SRCS "dp_relay.c"
    INCLUDE_DIRS "include"
    REQUIRES driver nvs_flash dp_board
)
