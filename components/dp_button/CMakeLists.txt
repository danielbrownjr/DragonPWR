if(IDF_TARGET STREQUAL "esp32c3")
    set(DP_BUTTON_SOURCE "dp_button_v2_inactive.c")
else()
    set(DP_BUTTON_SOURCE "dp_button.c")
endif()
idf_component_register(
    SRCS "${DP_BUTTON_SOURCE}"
    INCLUDE_DIRS "include"
    REQUIRES dp_board dp_relay esp_driver_gpio
)
