cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/..")

# Pass example selection flags to C/C++ compiler
if(EXAMPLE_LOGGING)
    add_definitions(-DEXAMPLE_LOGGING)
endif()

if(EXAMPLE_WIFI)
    add_definitions(-DEXAMPLE_WIFI)
endif()

if(EXAMPLE_SMARTCONFIG)
    add_definitions(-DEXAMPLE_SMARTCONFIG)
endif()

if(EXAMPLE_TASKS)
    add_definitions(-DEXAMPLE_TASKS)
endif()

if(EXAMPLE_METRICS)
    add_definitions(-DEXAMPLE_METRICS)
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp_core_example)
