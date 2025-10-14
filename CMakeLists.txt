cmake_minimum_required(VERSION 3.16)

idf_component_register(
    SRCS
        "src/logging.cpp"
        "src/mdns_service.cpp"
        "src/metrics.cpp"
        "src/validation.cpp"
        "src/wifi_service.cpp"
    INCLUDE_DIRS
        "include"
    PRIV_INCLUDE_DIRS
        "src"
    REQUIRES
        esp_wifi
        mdns
    PRIV_REQUIRES
        esp_netif
        esp_event
        nvs_flash
        log
        esp_timer
        heap
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)

# Add version from idf_component.yml as compile definition
file(READ "${CMAKE_CURRENT_LIST_DIR}/idf_component.yml" IDF_COMPONENT_YML)
string(REGEX MATCH "version: *([0-9]+\\.[0-9]+\\.[0-9]+)" _ "${IDF_COMPONENT_YML}")
set(CORE_VERSION "${CMAKE_MATCH_1}")
target_compile_definitions(${COMPONENT_LIB} PUBLIC CORE_VERSION="${CORE_VERSION}")
message(STATUS "Set CORE_VERSION to: ${CORE_VERSION}")
