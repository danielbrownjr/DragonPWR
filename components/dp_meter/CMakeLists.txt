idf_component_register(SRCS "dp_meter.c" "dp_meter_protocol.c"
    INCLUDE_DIRS "include" REQUIRES dp_board esp_driver_uart esp_timer)
