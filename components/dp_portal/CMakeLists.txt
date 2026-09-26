idf_component_register(
    SRCS "dp_portal.c"
    INCLUDE_DIRS "include"
    EMBED_TXTFILES "power.html"
    REQUIRES esp_http_server json app_update nvs_flash dc_portal dc_wifi dc_evlog dp_relay
)
