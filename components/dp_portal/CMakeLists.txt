if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    set(DP_JSON_COMPONENT espressif__cjson)
else()
    set(DP_JSON_COMPONENT json)
endif()
idf_component_register(
    SRCS "dp_portal.c"
    INCLUDE_DIRS "include"
    EMBED_TXTFILES "power.html"
    REQUIRES esp_http_server ${DP_JSON_COMPONENT} app_update nvs_flash dc_portal dc_wifi dc_evlog dp_relay
)
