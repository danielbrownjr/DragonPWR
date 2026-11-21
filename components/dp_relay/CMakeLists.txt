if(IDF_TARGET STREQUAL "esp32c3")
    set(DP_RELAY_SOURCE "dp_relay_v2_inactive.c")
else()
    set(DP_RELAY_SOURCE "dp_relay.c")
endif()
idf_component_register(
    SRCS "${DP_RELAY_SOURCE}"
    INCLUDE_DIRS "include"
    REQUIRES dp_board esp_driver_gpio nvs_flash
)
