# =============================================================================
# Host Test Build (x86/x64) - Independent of ESP-IDF
# =============================================================================
# This CMake project compiles and runs tests on the host machine.
# It uses Unity for unit tests and Theft for property-based tests (PBT).
# ESP-IDF dependencies are replaced with mocks in test/mocks/.
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(drone-telemetry-monitor-tests C)

# Allow older CMake policy versions for dependencies (e.g., cJSON)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
if(POLICY CMP0077)
    cmake_policy(SET CMP0077 NEW)
endif()
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Build type defaults to Debug for test builds
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Enable warnings
add_compile_options(-Wall -Wextra -Wpedantic)

# =============================================================================
# Project paths
# =============================================================================
set(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(COMPONENTS_DIR "${PROJECT_ROOT}/components")
set(MOCKS_DIR "${PROJECT_ROOT}/test/mocks")
set(GENERATORS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generators")

# =============================================================================
# External dependencies via FetchContent (URL-based for reliability)
# =============================================================================
include(FetchContent)

# Unity - C Test Framework (ThrowTheSwitch)
FetchContent_Declare(
    unity
    URL https://github.com/ThrowTheSwitch/Unity/archive/refs/tags/v2.6.0.tar.gz
)
FetchContent_MakeAvailable(unity)

# Enable double precision support in Unity (must be after MakeAvailable)
target_compile_definitions(unity PUBLIC UNITY_INCLUDE_DOUBLE)

# cJSON - Lightweight JSON parser (used by config_store)
FetchContent_Declare(
    cjson
    URL https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.18.tar.gz
)
set(ENABLE_CJSON_TEST OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cjson)

# Theft - Property-Based Testing for C
FetchContent_Declare(
    theft
    URL https://github.com/silentbicycle/theft/archive/refs/tags/v0.4.5.tar.gz
)
FetchContent_GetProperties(theft)
if(NOT theft_POPULATED)
    FetchContent_Populate(theft)
    # Theft doesn't have a CMakeLists.txt, so we build it manually
    add_library(theft STATIC
        ${theft_SOURCE_DIR}/src/theft.c
        ${theft_SOURCE_DIR}/src/theft_bloom.c
        ${theft_SOURCE_DIR}/src/theft_hash.c
        ${theft_SOURCE_DIR}/src/theft_random.c
        ${theft_SOURCE_DIR}/src/theft_rng.c
        ${theft_SOURCE_DIR}/src/theft_run.c
        ${theft_SOURCE_DIR}/src/theft_shrink.c
        ${theft_SOURCE_DIR}/src/theft_autoshrink.c
        ${theft_SOURCE_DIR}/src/theft_call.c
        ${theft_SOURCE_DIR}/src/theft_aux.c
        ${theft_SOURCE_DIR}/src/theft_aux_builtin.c
        ${theft_SOURCE_DIR}/src/theft_trial.c
    )
    target_include_directories(theft PUBLIC
        ${theft_SOURCE_DIR}/inc
    )
    target_include_directories(theft PRIVATE
        ${theft_SOURCE_DIR}/src
    )
endif()

# =============================================================================
# Mock libraries (ESP-IDF stubs)
# =============================================================================
add_library(esp_idf_mocks STATIC
    ${MOCKS_DIR}/esp_err_mock.c
    ${MOCKS_DIR}/esp_log_mock.c
    ${MOCKS_DIR}/esp_timer_mock.c
    ${MOCKS_DIR}/freertos_mock.c
    ${MOCKS_DIR}/freertos_queue_mock.c
    ${MOCKS_DIR}/freertos_event_groups_mock.c
)
target_include_directories(esp_idf_mocks PUBLIC
    ${MOCKS_DIR}/include
)

# =============================================================================
# HAL Mock library
# =============================================================================
add_library(hal_mocks STATIC
    ${MOCKS_DIR}/hal_lora_mock.c
    ${MOCKS_DIR}/hal_nrf24_mock.c
    ${MOCKS_DIR}/hal_sdr_mock.c
    ${MOCKS_DIR}/hal_gps_mock.c
    ${MOCKS_DIR}/hal_wifi_scanner_mock.c
    ${MOCKS_DIR}/hal_ble_scanner_mock.c
    ${MOCKS_DIR}/hal_buzzer_mock.c
)
target_include_directories(hal_mocks PUBLIC
    ${MOCKS_DIR}/include
    ${COMPONENTS_DIR}/common/include
    ${COMPONENTS_DIR}/hal/include
)
target_link_libraries(hal_mocks PUBLIC esp_idf_mocks)

# =============================================================================
# PBT Configuration
# =============================================================================
# Minimum iterations for property-based tests
add_compile_definitions(PBT_MIN_TRIALS=100)

# =============================================================================
# Include paths for component sources
# =============================================================================
set(COMPONENT_INCLUDE_DIRS
    ${COMPONENTS_DIR}/common/include
    ${COMPONENTS_DIR}/domain/include
    ${COMPONENTS_DIR}/hal/include
    ${COMPONENTS_DIR}/services/include
    ${COMPONENTS_DIR}/ui/include
    ${MOCKS_DIR}/include
    ${GENERATORS_DIR}
)

# =============================================================================
# Test helper library (generators + test utilities)
# =============================================================================
add_library(test_helpers STATIC
    ${GENERATORS_DIR}/gen_common.c
)
target_include_directories(test_helpers PUBLIC
    ${GENERATORS_DIR}
    ${COMPONENTS_DIR}/common/include
    ${MOCKS_DIR}/include
)
target_link_libraries(test_helpers PUBLIC theft esp_idf_mocks)

# =============================================================================
# Function to add a unit test executable
# =============================================================================
function(add_unit_test TEST_NAME)
    set(options "")
    set(oneValueArgs "")
    set(multiValueArgs SOURCES LIBRARIES)
    cmake_parse_arguments(UT "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    add_executable(${TEST_NAME} ${UT_SOURCES})
    target_include_directories(${TEST_NAME} PRIVATE ${COMPONENT_INCLUDE_DIRS})
    target_link_libraries(${TEST_NAME} PRIVATE
        unity
        esp_idf_mocks
        hal_mocks
        ${UT_LIBRARIES}
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

# =============================================================================
# Function to add a property-based test executable
# =============================================================================
function(add_pbt_test TEST_NAME)
    set(options "")
    set(oneValueArgs "")
    set(multiValueArgs SOURCES LIBRARIES)
    cmake_parse_arguments(PBT "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    add_executable(${TEST_NAME} ${PBT_SOURCES})
    target_include_directories(${TEST_NAME} PRIVATE ${COMPONENT_INCLUDE_DIRS})
    target_link_libraries(${TEST_NAME} PRIVATE
        theft
        unity
        esp_idf_mocks
        hal_mocks
        test_helpers
        ${PBT_LIBRARIES}
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

# =============================================================================
# Enable CTest
# =============================================================================
enable_testing()

# =============================================================================
# Smoke test to verify framework integration
# =============================================================================
add_unit_test(test_framework_smoke
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_framework_smoke.c
)

add_pbt_test(test_pbt_smoke
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_pbt_smoke.c
)

# =============================================================================
# Config Store - Domain component library (for testing)
# =============================================================================
if(EXISTS "${COMPONENTS_DIR}/domain/src/config_store.c")
    add_library(config_store STATIC
        ${COMPONENTS_DIR}/domain/src/config_store.c
    )
    target_include_directories(config_store PUBLIC
        ${COMPONENTS_DIR}/domain/include
        ${COMPONENTS_DIR}/common/include
        ${MOCKS_DIR}/include
    )
    target_link_libraries(config_store PUBLIC cjson esp_idf_mocks)
    target_include_directories(config_store PUBLIC ${cjson_SOURCE_DIR})

    # Config Store Unit Tests
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_config_store.c")
        add_unit_test(test_config_store
            SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_config_store.c
            LIBRARIES config_store cjson
        )
    endif()
endif()

# =============================================================================
# Aircraft Registry - Domain component library (for testing)
# =============================================================================
if(EXISTS "${COMPONENTS_DIR}/domain/src/aircraft_registry.c")
    add_library(aircraft_registry STATIC
        ${COMPONENTS_DIR}/domain/src/aircraft_registry.c
    )
    target_include_directories(aircraft_registry PUBLIC
        ${COMPONENTS_DIR}/domain/include
        ${COMPONENTS_DIR}/common/include
        ${COMPONENTS_DIR}/hal/include
        ${MOCKS_DIR}/include
    )
    target_link_libraries(aircraft_registry PUBLIC esp_idf_mocks)

    # Aircraft Registry Unit Tests
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_aircraft_registry.c")
        add_unit_test(test_aircraft_registry
            SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_aircraft_registry.c
            LIBRARIES aircraft_registry
        )
    endif()
endif()

# =============================================================================
# HAL GPS - NMEA Parser library (for testing, built with mock flag)
# =============================================================================
if(EXISTS "${COMPONENTS_DIR}/hal/src/hal_gps.c")
    add_library(hal_gps_parser STATIC
        ${COMPONENTS_DIR}/hal/src/hal_gps.c
    )
    target_include_directories(hal_gps_parser PUBLIC
        ${COMPONENTS_DIR}/hal/include
        ${COMPONENTS_DIR}/common/include
        ${MOCKS_DIR}/include
    )
    target_compile_definitions(hal_gps_parser PRIVATE CONFIG_HAL_GPS_MOCK=1)
    target_link_libraries(hal_gps_parser PUBLIC esp_idf_mocks)

    # GPS Unit Tests
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_hal_gps.c")
        add_unit_test(test_hal_gps
            SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_hal_gps.c
            LIBRARIES hal_gps_parser
        )
    endif()
endif()

# =============================================================================
# Protocol Signatures - Domain component library (for testing)
# =============================================================================
if(EXISTS "${COMPONENTS_DIR}/domain/src/protocol_signatures.c")
    add_library(protocol_signatures STATIC
        ${COMPONENTS_DIR}/domain/src/protocol_signatures.c
    )
    target_include_directories(protocol_signatures PUBLIC
        ${COMPONENTS_DIR}/domain/include
        ${COMPONENTS_DIR}/common/include
        ${MOCKS_DIR}/include
    )
    target_link_libraries(protocol_signatures PUBLIC esp_idf_mocks)

    # Protocol Signatures Unit Tests
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_protocol_signatures.c")
        add_unit_test(test_protocol_signatures
            SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_protocol_signatures.c
            LIBRARIES protocol_signatures
        )
    endif()
endif()


# =============================================================================
# MAVLink Decoder - Service component library (for testing)
# =============================================================================
if(EXISTS "${COMPONENTS_DIR}/services/src/mavlink_decoder.c")
    add_library(mavlink_decoder STATIC
        ${COMPONENTS_DIR}/services/src/mavlink_decoder.c
    )
    target_include_directories(mavlink_decoder PUBLIC
        ${COMPONENTS_DIR}/services/include
        ${COMPONENTS_DIR}/domain/include
        ${COMPONENTS_DIR}/common/include
        ${COMPONENTS_DIR}/hal/include
        ${MOCKS_DIR}/include
    )
    target_link_libraries(mavlink_decoder PUBLIC esp_idf_mocks m)

    # MAVLink Decoder Unit Tests
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_mavlink_decoder.c")
        add_unit_test(test_mavlink_decoder
            SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_mavlink_decoder.c
            LIBRARIES mavlink_decoder
        )
        target_link_libraries(test_mavlink_decoder PRIVATE m)
    endif()
endif()

# =============================================================================
# Hardware Manager - HAL component library (for testing)
# =============================================================================
if(EXISTS "${COMPONENTS_DIR}/hal/src/hw_manager.c")
    add_library(hw_manager STATIC
        ${COMPONENTS_DIR}/hal/src/hw_manager.c
    )
    target_include_directories(hw_manager PUBLIC
        ${COMPONENTS_DIR}/hal/include
        ${COMPONENTS_DIR}/common/include
        ${MOCKS_DIR}/include
    )
    target_link_libraries(hw_manager PUBLIC esp_idf_mocks hal_mocks)

    # Hardware Manager Unit Tests
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_hw_manager.c")
        add_unit_test(test_hw_manager
            SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_hw_manager.c
            LIBRARIES hw_manager
        )
    endif()
endif()


# =============================================================================
# RemoteID Decoder - Services component library (for testing)
# =============================================================================
if(EXISTS "${COMPONENTS_DIR}/services/src/remoteid_decoder.c")
    add_library(remoteid_decoder STATIC
        ${COMPONENTS_DIR}/services/src/remoteid_decoder.c
    )
    target_include_directories(remoteid_decoder PUBLIC
        ${COMPONENTS_DIR}/services/include
        ${COMPONENTS_DIR}/domain/include
        ${COMPONENTS_DIR}/hal/include
        ${COMPONENTS_DIR}/common/include
        ${MOCKS_DIR}/include
    )
    target_link_libraries(remoteid_decoder PUBLIC esp_idf_mocks)

    # RemoteID Decoder Unit Tests
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_remoteid_decoder.c")
        add_unit_test(test_remoteid_decoder
            SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_remoteid_decoder.c
            LIBRARIES remoteid_decoder
        )
    endif()
endif()

# =============================================================================
# Geolocation Service - Services component library (for testing)
# =============================================================================
if(EXISTS "${COMPONENTS_DIR}/services/src/geolocation_service.c")
    add_library(geolocation_service STATIC
        ${COMPONENTS_DIR}/services/src/geolocation_service.c
    )
    target_include_directories(geolocation_service PUBLIC
        ${COMPONENTS_DIR}/services/include
        ${COMPONENTS_DIR}/common/include
        ${COMPONENTS_DIR}/hal/include
        ${MOCKS_DIR}/include
    )
    target_link_libraries(geolocation_service PUBLIC esp_idf_mocks hal_mocks m)

    # Geolocation Service Unit Tests
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_geolocation_service.c")
        add_unit_test(test_geolocation_service
            SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_geolocation_service.c
            LIBRARIES geolocation_service
        )
        target_link_libraries(test_geolocation_service PRIVATE m)
    endif()
endif()

# =============================================================================
# ELRS Decoder - Services component library (for testing)
# =============================================================================
if(EXISTS "${COMPONENTS_DIR}/services/src/elrs_decoder.c")
    add_library(elrs_decoder STATIC
        ${COMPONENTS_DIR}/services/src/elrs_decoder.c
    )
    target_include_directories(elrs_decoder PUBLIC
        ${COMPONENTS_DIR}/services/include
        ${COMPONENTS_DIR}/domain/include
        ${COMPONENTS_DIR}/hal/include
        ${COMPONENTS_DIR}/common/include
        ${MOCKS_DIR}/include
    )
    target_link_libraries(elrs_decoder PUBLIC esp_idf_mocks)

    # ELRS Decoder Unit Tests
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_elrs_decoder.c")
        add_unit_test(test_elrs_decoder
            SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_elrs_decoder.c
            LIBRARIES elrs_decoder
        )
    endif()
endif()

# =============================================================================
# Protocol Classifier - Services component library (for testing)
# =============================================================================
if(EXISTS "${COMPONENTS_DIR}/services/src/protocol_classifier.c")
    add_library(protocol_classifier STATIC
        ${COMPONENTS_DIR}/services/src/protocol_classifier.c
    )
    target_include_directories(protocol_classifier PUBLIC
        ${COMPONENTS_DIR}/services/include
        ${COMPONENTS_DIR}/domain/include
        ${COMPONENTS_DIR}/common/include
        ${COMPONENTS_DIR}/hal/include
        ${MOCKS_DIR}/include
    )
    target_link_libraries(protocol_classifier PUBLIC esp_idf_mocks protocol_signatures)

    # Protocol Classifier Unit Tests
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_protocol_classifier.c")
        add_unit_test(test_protocol_classifier
            SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_protocol_classifier.c
            LIBRARIES protocol_classifier protocol_signatures
        )
    endif()
endif()

# =============================================================================
# Telemetry Decoder - Services component library (for testing)
# =============================================================================
if(EXISTS "${COMPONENTS_DIR}/services/src/telemetry_decoder.c")
    add_library(telemetry_decoder STATIC
        ${COMPONENTS_DIR}/services/src/telemetry_decoder.c
    )
    target_include_directories(telemetry_decoder PUBLIC
        ${COMPONENTS_DIR}/services/include
        ${COMPONENTS_DIR}/domain/include
        ${COMPONENTS_DIR}/common/include
        ${COMPONENTS_DIR}/hal/include
        ${MOCKS_DIR}/include
    )
    target_link_libraries(telemetry_decoder PUBLIC
        esp_idf_mocks
        protocol_classifier
        protocol_signatures
        remoteid_decoder
        mavlink_decoder
        elrs_decoder
        aircraft_registry
    )

    # Telemetry Decoder Unit Tests
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_telemetry_decoder.c")
        add_unit_test(test_telemetry_decoder
            SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_telemetry_decoder.c
            LIBRARIES telemetry_decoder
        )
        target_link_libraries(test_telemetry_decoder PRIVATE m)
    endif()
endif()

# =============================================================================
# Detection Service - Services component library (for testing)
# =============================================================================
if(EXISTS "${COMPONENTS_DIR}/services/src/detection_service.c")
    add_library(detection_service STATIC
        ${COMPONENTS_DIR}/services/src/detection_service.c
    )
    target_include_directories(detection_service PUBLIC
        ${COMPONENTS_DIR}/services/include
        ${COMPONENTS_DIR}/domain/include
        ${COMPONENTS_DIR}/common/include
        ${COMPONENTS_DIR}/hal/include
        ${MOCKS_DIR}/include
    )
    target_link_libraries(detection_service PUBLIC esp_idf_mocks hal_mocks hw_manager)

    # Detection Service Unit Tests
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_detection_service.c")
        add_unit_test(test_detection_service
            SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_detection_service.c
            LIBRARIES detection_service hw_manager
        )
    endif()
endif()

# =============================================================================
# Spectrum Analyzer - Services component library (for testing)
# =============================================================================
if(EXISTS "${COMPONENTS_DIR}/services/src/spectrum_analyzer.c")
    add_library(spectrum_analyzer STATIC
        ${COMPONENTS_DIR}/services/src/spectrum_analyzer.c
    )
    target_include_directories(spectrum_analyzer PUBLIC
        ${COMPONENTS_DIR}/services/include
        ${COMPONENTS_DIR}/domain/include
        ${COMPONENTS_DIR}/common/include
        ${COMPONENTS_DIR}/hal/include
        ${MOCKS_DIR}/include
    )
    target_link_libraries(spectrum_analyzer PUBLIC esp_idf_mocks hal_mocks m)

    # Spectrum Analyzer Unit Tests
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_spectrum_analyzer.c")
        add_unit_test(test_spectrum_analyzer
            SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_spectrum_analyzer.c
            LIBRARIES spectrum_analyzer
        )
        target_link_libraries(test_spectrum_analyzer PRIVATE m)
    endif()
endif()

# =============================================================================
# Pilot Locator - Services component library (for testing)
# =============================================================================
if(EXISTS "${COMPONENTS_DIR}/services/src/pilot_locator.c")
    add_library(pilot_locator STATIC
        ${COMPONENTS_DIR}/services/src/pilot_locator.c
    )
    target_include_directories(pilot_locator PUBLIC
        ${COMPONENTS_DIR}/services/include
        ${COMPONENTS_DIR}/domain/include
        ${COMPONENTS_DIR}/common/include
        ${COMPONENTS_DIR}/hal/include
        ${MOCKS_DIR}/include
    )
    target_link_libraries(pilot_locator PUBLIC esp_idf_mocks hal_mocks m)

    # Pilot Locator Unit Tests
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_pilot_locator.c")
        add_unit_test(test_pilot_locator
            SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_pilot_locator.c
            LIBRARIES pilot_locator geolocation_service
        )
        target_link_libraries(test_pilot_locator PRIVATE m)
    endif()
endif()


# =============================================================================
# Alert Engine - Services component library (for testing)
# =============================================================================
if(EXISTS "${COMPONENTS_DIR}/services/src/alert_engine.c")
    add_library(alert_engine STATIC
        ${COMPONENTS_DIR}/services/src/alert_engine.c
    )
    target_include_directories(alert_engine PUBLIC
        ${COMPONENTS_DIR}/services/include
        ${COMPONENTS_DIR}/domain/include
        ${COMPONENTS_DIR}/common/include
        ${COMPONENTS_DIR}/hal/include
        ${MOCKS_DIR}/include
    )
    target_link_libraries(alert_engine PUBLIC esp_idf_mocks hal_mocks geolocation_service)

    # Alert Engine Unit Tests
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_alert_engine.c")
        add_unit_test(test_alert_engine
            SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_alert_engine.c
            LIBRARIES alert_engine geolocation_service
        )
    endif()
endif()

# =============================================================================
# HAL Display Mock library (for UI testing)
# =============================================================================
add_library(hal_display_mock STATIC
    ${MOCKS_DIR}/hal_display_mock.c
)
target_include_directories(hal_display_mock PUBLIC
    ${MOCKS_DIR}/include
    ${COMPONENTS_DIR}/common/include
    ${COMPONENTS_DIR}/hal/include
)
target_link_libraries(hal_display_mock PUBLIC esp_idf_mocks)

# =============================================================================
# UI Manager - UI component library (for testing)
# =============================================================================
if(EXISTS "${COMPONENTS_DIR}/ui/src/ui_manager.c")
    add_library(ui_manager STATIC
        ${COMPONENTS_DIR}/ui/src/ui_manager.c
    )
    target_include_directories(ui_manager PUBLIC
        ${COMPONENTS_DIR}/ui/include
        ${COMPONENTS_DIR}/common/include
        ${COMPONENTS_DIR}/hal/include
        ${MOCKS_DIR}/include
    )
    target_link_libraries(ui_manager PUBLIC esp_idf_mocks hal_display_mock)

    # UI Manager Unit Tests
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_ui_manager.c")
        add_unit_test(test_ui_manager
            SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_manager.c
            LIBRARIES ui_manager hal_display_mock
        )
    endif()
endif()

# =============================================================================
# Screen Scanner - UI component library (for testing)
# =============================================================================
if(EXISTS "${COMPONENTS_DIR}/ui/src/screen_scanner.c")
    add_library(screen_scanner STATIC
        ${COMPONENTS_DIR}/ui/src/screen_scanner.c
    )
    target_include_directories(screen_scanner PUBLIC
        ${COMPONENTS_DIR}/ui/include
        ${COMPONENTS_DIR}/domain/include
        ${COMPONENTS_DIR}/common/include
        ${COMPONENTS_DIR}/hal/include
        ${MOCKS_DIR}/include
    )
    target_link_libraries(screen_scanner PUBLIC esp_idf_mocks hal_display_mock ui_manager)

    # Screen Scanner Unit Tests
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_screen_scanner.c")
        add_unit_test(test_screen_scanner
            SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_screen_scanner.c
            LIBRARIES screen_scanner ui_manager hal_display_mock aircraft_registry
        )
    endif()
endif()

# =============================================================================
# Screen Map - UI component library (for testing)
# =============================================================================
if(EXISTS "${COMPONENTS_DIR}/ui/src/screen_map.c")
    add_library(screen_map STATIC
        ${COMPONENTS_DIR}/ui/src/screen_map.c
    )
    target_include_directories(screen_map PUBLIC
        ${COMPONENTS_DIR}/ui/include
        ${COMPONENTS_DIR}/services/include
        ${COMPONENTS_DIR}/domain/include
        ${COMPONENTS_DIR}/common/include
        ${COMPONENTS_DIR}/hal/include
        ${MOCKS_DIR}/include
    )
    target_link_libraries(screen_map PUBLIC esp_idf_mocks hal_display_mock ui_manager geolocation_service m)

    # Screen Map Unit Tests
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_screen_map.c")
        add_unit_test(test_screen_map
            SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_screen_map.c
            LIBRARIES screen_map ui_manager hal_display_mock geolocation_service aircraft_registry
        )
        target_link_libraries(test_screen_map PRIVATE m)
    endif()
endif()

# =============================================================================
# HAL SD Mock library (for data logger testing)
# =============================================================================
add_library(hal_sd_mock STATIC
    ${MOCKS_DIR}/hal_sd_mock.c
)
target_include_directories(hal_sd_mock PUBLIC
    ${MOCKS_DIR}/include
    ${COMPONENTS_DIR}/common/include
    ${COMPONENTS_DIR}/hal/include
)
target_link_libraries(hal_sd_mock PUBLIC esp_idf_mocks)

# =============================================================================
# Data Logger - Services component library (for testing)
# =============================================================================
if(EXISTS "${COMPONENTS_DIR}/services/src/data_logger.c")
    add_library(data_logger STATIC
        ${COMPONENTS_DIR}/services/src/data_logger.c
    )
    target_include_directories(data_logger PUBLIC
        ${COMPONENTS_DIR}/services/include
        ${COMPONENTS_DIR}/domain/include
        ${COMPONENTS_DIR}/common/include
        ${COMPONENTS_DIR}/hal/include
        ${MOCKS_DIR}/include
    )
    target_link_libraries(data_logger PUBLIC esp_idf_mocks hal_sd_mock)

    # Data Logger Unit Tests
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_data_logger.c")
        add_unit_test(test_data_logger
            SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_data_logger.c
            LIBRARIES data_logger hal_sd_mock
        )
    endif()
endif()

# =============================================================================
# Data Pipeline - Main module library (for testing)
# =============================================================================
if(EXISTS "${PROJECT_ROOT}/main/data_pipeline.c")
    add_library(data_pipeline STATIC
        ${PROJECT_ROOT}/main/data_pipeline.c
    )
    target_include_directories(data_pipeline PUBLIC
        ${PROJECT_ROOT}/main
        ${COMPONENTS_DIR}/services/include
        ${COMPONENTS_DIR}/domain/include
        ${COMPONENTS_DIR}/common/include
        ${COMPONENTS_DIR}/hal/include
        ${MOCKS_DIR}/include
    )
    target_link_libraries(data_pipeline PUBLIC esp_idf_mocks)

    # Data Pipeline Unit Tests
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_data_pipeline.c")
        add_unit_test(test_data_pipeline
            SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_data_pipeline.c
            LIBRARIES data_pipeline
        )
    endif()
endif()
