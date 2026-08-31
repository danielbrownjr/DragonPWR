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

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(dragonpwr)
