idf_component_register(
    SRCS "dp_portal.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server json app_update dc_portal dc_evlog dp_relay
)
