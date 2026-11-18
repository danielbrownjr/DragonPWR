idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash app_update esp_driver_uart dp_board dp_relay dp_button dp_portal dc_wifi dc_evlog
)
