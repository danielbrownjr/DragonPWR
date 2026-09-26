# DragonPWR - open firmware for the BIGTREETECH Panda PWR (ESP32-C2 / ESP8684).
cmake_minimum_required(VERSION 3.16)

execute_process(
    COMMAND git describe --tags --always --dirty
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE DP_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT DP_VERSION)
    set(DP_VERSION "dev")
endif()
set(PROJECT_VER "${DP_VERSION}")

# Build variant: which printer clients are linked in. A client left out costs
# nothing in the image: DragonPWR's calls into it are compiled out, so the
# linker drops it and everything it pulls in (esp-mqtt and mbedTLS SSL for
# Bambu, the websocket client for Moonraker). It is still compiled - ESP-IDF
# builds every component the manifest pulls in - which costs build time only.
#
#   idf.py -B build-lite      -D DP_VARIANT=lite      build   # plug only
#   idf.py -B build-moonraker -D DP_VARIANT=moonraker build   # + Klipper
#   idf.py -B build-bambu     -D DP_VARIANT=bambu     build   # + Bambu (TLS)
#   idf.py                                            build   # full: both
#
# The printer itself is still picked at runtime in Device setup, from the
# clients this build carries.
set(DP_VARIANT "full" CACHE STRING "Printer clients to link: lite, moonraker, bambu or full")
set_property(CACHE DP_VARIANT PROPERTY STRINGS lite moonraker bambu full)
if(NOT DP_VARIANT MATCHES "^(lite|moonraker|bambu|full)$")
    message(FATAL_ERROR "DP_VARIANT must be lite, moonraker, bambu or full, not '${DP_VARIANT}'")
endif()
set(DP_WITH_MOONRAKER 0)
set(DP_WITH_BAMBU 0)
if(DP_VARIANT MATCHES "^(moonraker|full)$")
    set(DP_WITH_MOONRAKER 1)
endif()
if(DP_VARIANT MATCHES "^(bambu|full)$")
    set(DP_WITH_BAMBU 1)
endif()
message(STATUS "DragonPWR variant: ${DP_VARIANT}")

# One sdkconfig per build directory, generated from sdkconfig.defaults. An
# sdkconfig that already exists wins over the defaults, so one left in the
# source tree used to shadow later changes to sdkconfig.defaults.
set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# One value per call: idf_build_set_property takes a single value.
idf_build_set_property(COMPILE_DEFINITIONS "DP_WITH_MOONRAKER=${DP_WITH_MOONRAKER}" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "DP_WITH_BAMBU=${DP_WITH_BAMBU}" APPEND)
project(dragonpwr)
