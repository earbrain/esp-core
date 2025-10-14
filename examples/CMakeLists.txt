cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/..")

# Select which example to build (default: wifi_ap)
if(NOT EXAMPLE)
    set(EXAMPLE "wifi_ap")
endif()

# Validate EXAMPLE value
set(VALID_EXAMPLES wifi_ap wifi_sta wifi_scan mdns smartconfig logging metrics tasks)
if(NOT EXAMPLE IN_LIST VALID_EXAMPLES)
    message(FATAL_ERROR "Invalid EXAMPLE value: ${EXAMPLE}. Valid values are: ${VALID_EXAMPLES}")
endif()

message(STATUS "Building example: ${EXAMPLE}")

# Set the source file path based on EXAMPLE
set(EXAMPLE_SOURCE "${CMAKE_CURRENT_LIST_DIR}/${EXAMPLE}/main.cpp")

# Check if the example source exists
if(NOT EXISTS "${EXAMPLE_SOURCE}")
    message(FATAL_ERROR "Example source not found: ${EXAMPLE_SOURCE}")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp_core_example)
