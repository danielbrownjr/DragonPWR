idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash app_update dp_board dp_relay dp_button dp_portal dp_printer dc_wifi dc_evlog
)
