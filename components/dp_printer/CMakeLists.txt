idf_component_register(
    SRCS "dp_printer.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash heap dc_source dc_moonraker dc_bambu dc_evlog
)
