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

# Export compile_commands.json for static analysis (Requirement 11.1)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable warnings
add_compile_options(-Wall -Wextra -Wpedantic)

# =============================================================================
# Sanitizer support (additive — when SANITIZER is not set, builds work as before)
# Usage:
#   cmake -S test/host -B build/host_asan -DSANITIZER=address
#   cmake -S test/host -B build/host_tsan -DSANITIZER=thread
# ASan+UBSan and TSan are mutually exclusive and require separate build dirs.
# =============================================================================
set(SANITIZER "" CACHE STRING "Sanitizer mode: address (ASan+UBSan) or thread (TSan)")

if(SANITIZER STREQUAL "address")
    message(STATUS "[Sanitizer] Enabling AddressSanitizer + UndefinedBehaviorSanitizer")
    set(SANITIZER_FLAGS "-fsanitize=address,undefined -fno-omit-frame-pointer -fno-optimize-sibling-calls")
    set(SANITIZER_LINK_FLAGS "-fsanitize=address,undefined")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-optimize-sibling-calls)
    add_link_options(-fsanitize=address,undefined)

    # Configure ASan/UBSan runtime options for CTest
    # Note: abort_on_error=0 makes ASan _exit() instead of abort(), which allows
    # CTest WILL_FAIL to work correctly with sanitizer probes (signals bypass WILL_FAIL).
    # Leak detection is only supported on Linux; macOS/AppleClang does not support it.
    if(APPLE)
        set(ASAN_ENV "ASAN_OPTIONS=detect_leaks=0:halt_on_error=1:abort_on_error=0:print_stats=1:check_initialization_order=1")
    else()
        set(ASAN_ENV "ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:abort_on_error=0:print_stats=1:check_initialization_order=1")
    endif()
    set(UBSAN_ENV "UBSAN_OPTIONS=halt_on_error=1:abort_on_error=0:print_stacktrace=1:suppressions=${CMAKE_CURRENT_SOURCE_DIR}/ubsan_suppressions.txt")

elseif(SANITIZER STREQUAL "thread")
    # TSan may have limited support on macOS/AppleClang. Warn but still configure.
    include(CheckCCompilerFlag)
    check_c_compiler_flag("-fsanitize=thread" COMPILER_SUPPORTS_TSAN)
    if(NOT COMPILER_SUPPORTS_TSAN)
        message(WARNING "[Sanitizer] ThreadSanitizer is not supported by the current compiler. "
                        "TSan build will be configured but may fail to compile. "
                        "Use GCC on Linux for full TSan support in CI.")
    endif()

    message(STATUS "[Sanitizer] Enabling ThreadSanitizer")
    set(SANITIZER_FLAGS "-fsanitize=thread")
    set(SANITIZER_LINK_FLAGS "-fsanitize=thread")
    add_compile_options(-fsanitize=thread)
    add_link_options(-fsanitize=thread)

    set(TSAN_ENV "TSAN_OPTIONS=halt_on_error=1:abort_on_error=0:second_deadlock_stack=1")

elseif(NOT SANITIZER STREQUAL "")
    message(FATAL_ERROR "[Sanitizer] Unknown SANITIZER value '${SANITIZER}'. Use 'address' or 'thread'.")
endif()

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
    ${MOCKS_DIR}/esp_system_mock.c
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
    ${MOCKS_DIR}/hal_keyboard_mock.c
)
target_include_directories(hal_mocks PUBLIC
    ${MOCKS_DIR}/include
    ${COMPONENTS_DIR}/common/include
    ${COMPONENTS_DIR}/hw_hal/include
    ${COMPONENTS_DIR}/ui/include
)
target_link_libraries(hal_mocks PUBLIC esp_idf_mocks)

# =============================================================================
# PBT Configuration
# =============================================================================
# Minimum iterations for property-based tests
add_compile_definitions(PBT_MIN_TRIALS=100)

# Reproducible seed for PBT — override with -DPBT_SEED=<value> on the command line
if(NOT DEFINED PBT_SEED)
    set(PBT_SEED 0 CACHE STRING "Seed for property-based tests (0 = time-based)")
endif()
add_compile_definitions(PBT_SEED=${PBT_SEED})

# =============================================================================
# Include paths for component sources
# =============================================================================
set(COMPONENT_INCLUDE_DIRS
    ${COMPONENTS_DIR}/common/include
    ${COMPONENTS_DIR}/domain/include
    ${COMPONENTS_DIR}/hw_hal/include
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

    # Propagate sanitizer runtime environment to CTest
    if(SANITIZER STREQUAL "address")
        set_tests_properties(${TEST_NAME} PROPERTIES
            ENVIRONMENT "${ASAN_ENV};${UBSAN_ENV}"
        )
    elseif(SANITIZER STREQUAL "thread")
        set_tests_properties(${TEST_NAME} PROPERTIES
            ENVIRONMENT "${TSAN_ENV}"
        )
    endif()
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

    # Propagate sanitizer runtime environment to CTest
    if(SANITIZER STREQUAL "address")
        set_tests_properties(${TEST_NAME} PROPERTIES
            ENVIRONMENT "${ASAN_ENV};${UBSAN_ENV}"
        )
    elseif(SANITIZER STREQUAL "thread")
        set_tests_properties(${TEST_NAME} PROPERTIES
            ENVIRONMENT "${TSAN_ENV}"
        )
    endif()
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
        ${COMPONENTS_DIR}/hw_hal/include
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
if(EXISTS "${COMPONENTS_DIR}/hw_hal/src/hal_gps.c")
    add_library(hal_gps_parser STATIC
        ${COMPONENTS_DIR}/hw_hal/src/hal_gps.c
    )
    target_include_directories(hal_gps_parser PUBLIC
        ${COMPONENTS_DIR}/hw_hal/include
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
        ${COMPONENTS_DIR}/hw_hal/include
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
if(EXISTS "${COMPONENTS_DIR}/hw_hal/src/hw_manager.c")
    add_library(hw_manager STATIC
        ${COMPONENTS_DIR}/hw_hal/src/hw_manager.c
    )
    target_include_directories(hw_manager PUBLIC
        ${COMPONENTS_DIR}/hw_hal/include
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
        ${COMPONENTS_DIR}/hw_hal/include
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

    # RemoteID Bounds Safety Unit Tests (Task 5.1 — CQR-REMOTEID-003)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_remoteid_bounds.c")
        add_unit_test(test_remoteid_bounds
            SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_remoteid_bounds.c
            LIBRARIES remoteid_decoder
        )
    endif()

    # RemoteID Bounds PBT — Property 5: Parser bounds safety
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_pbt_remoteid_bounds.c")
        add_pbt_test(test_pbt_remoteid_bounds
            SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_pbt_remoteid_bounds.c
            LIBRARIES remoteid_decoder
        )
    endif()

    # RemoteID CRC Unit Tests + Property 6 PBT (Task 5.2 — CQR-REMOTEID-001)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_remoteid_crc.c")
        add_pbt_test(test_remoteid_crc
            SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_remoteid_crc.c
            LIBRARIES remoteid_decoder
        )
    endif()

    # RemoteID Coordinates — Property 7: Joint coordinate validity (Task 5.3)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_remoteid_coordinates.c")
        add_pbt_test(test_remoteid_coordinates
            SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_remoteid_coordinates.c
            LIBRARIES remoteid_decoder
        )
    endif()

    # RemoteID State Preservation — Property 8: Parse-then-commit preserves state (Task 5.4)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_pbt_remoteid_state_preservation.c")
        add_pbt_test(test_pbt_remoteid_state_preservation
            SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_pbt_remoteid_state_preservation.c
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
        ${COMPONENTS_DIR}/hw_hal/include
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
        ${COMPONENTS_DIR}/hw_hal/include
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
        ${COMPONENTS_DIR}/hw_hal/include
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
        ${COMPONENTS_DIR}/hw_hal/include
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
        ${COMPONENTS_DIR}/hw_hal/include
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
        ${COMPONENTS_DIR}/hw_hal/include
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
        ${COMPONENTS_DIR}/hw_hal/include
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
        ${COMPONENTS_DIR}/hw_hal/include
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
    ${COMPONENTS_DIR}/hw_hal/include
)
target_link_libraries(hal_display_mock PUBLIC esp_idf_mocks)

if(EXISTS "${COMPONENTS_DIR}/ui/src/ui_manager.c")
    add_library(ui_manager STATIC
        ${COMPONENTS_DIR}/ui/src/ui_manager.c
        ${COMPONENTS_DIR}/ui/src/screen_splash.c
        ${COMPONENTS_DIR}/ui/src/screen_menu.c
        ${COMPONENTS_DIR}/ui/src/screen_hud.c
        ${COMPONENTS_DIR}/ui/src/screen_modes.c
        ${COMPONENTS_DIR}/ui/src/screen_spectrum.c
        ${COMPONENTS_DIR}/ui/src/screen_settings.c
        ${COMPONENTS_DIR}/ui/src/screen_log.c
        ${COMPONENTS_DIR}/ui/src/screen_scanner.c
        ${COMPONENTS_DIR}/ui/src/screen_map.c
    )
    target_include_directories(ui_manager PUBLIC
        ${COMPONENTS_DIR}/ui/include
        ${COMPONENTS_DIR}/domain/include
        ${COMPONENTS_DIR}/services/include
        ${COMPONENTS_DIR}/common/include
        ${COMPONENTS_DIR}/hw_hal/include
        ${MOCKS_DIR}/include
    )
    target_link_libraries(ui_manager PUBLIC
        esp_idf_mocks
        hal_display_mock
        hal_sd_mock
        aircraft_registry
        geolocation_service
        alert_engine
        spectrum_analyzer
        config_store
        data_logger
        m
    )

    # UI Manager Unit Tests
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_ui_manager.c")
        add_unit_test(test_ui_manager
            SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_manager.c
            LIBRARIES ui_manager hal_display_mock
        )
        target_link_libraries(test_ui_manager PRIVATE m)
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
        ${COMPONENTS_DIR}/hw_hal/include
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
        ${COMPONENTS_DIR}/hw_hal/include
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
# Screen HUD - UI component library (for testing)
# =============================================================================
if(EXISTS "${COMPONENTS_DIR}/ui/src/screen_hud.c")
    add_library(screen_hud STATIC
        ${COMPONENTS_DIR}/ui/src/screen_hud.c
    )
    target_include_directories(screen_hud PUBLIC
        ${COMPONENTS_DIR}/ui/include
        ${COMPONENTS_DIR}/services/include
        ${COMPONENTS_DIR}/domain/include
        ${COMPONENTS_DIR}/common/include
        ${COMPONENTS_DIR}/hw_hal/include
        ${MOCKS_DIR}/include
    )
    target_link_libraries(screen_hud PUBLIC esp_idf_mocks hal_display_mock ui_manager geolocation_service aircraft_registry m)

    # Screen HUD Unit Tests
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_screen_hud.c")
        add_unit_test(test_screen_hud
            SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_screen_hud.c
            LIBRARIES screen_hud ui_manager hal_display_mock geolocation_service aircraft_registry
        )
        target_link_libraries(test_screen_hud PRIVATE m)
    endif()
endif()

# =============================================================================
# Screen Modes - UI component library (for testing)
# =============================================================================
if(EXISTS "${COMPONENTS_DIR}/ui/src/screen_modes.c")
    add_library(screen_modes STATIC
        ${COMPONENTS_DIR}/ui/src/screen_modes.c
    )
    target_include_directories(screen_modes PUBLIC
        ${COMPONENTS_DIR}/ui/include
        ${COMPONENTS_DIR}/services/include
        ${COMPONENTS_DIR}/domain/include
        ${COMPONENTS_DIR}/common/include
        ${COMPONENTS_DIR}/hw_hal/include
        ${MOCKS_DIR}/include
    )
    target_link_libraries(screen_modes PUBLIC esp_idf_mocks hal_display_mock ui_manager alert_engine)

    # Screen Modes Unit Tests
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_screen_modes.c")
        add_unit_test(test_screen_modes
            SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_screen_modes.c
            LIBRARIES screen_modes ui_manager hal_display_mock alert_engine
        )
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
    ${COMPONENTS_DIR}/hw_hal/include
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
        ${COMPONENTS_DIR}/hw_hal/include
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
        ${COMPONENTS_DIR}/hw_hal/include
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

# =============================================================================
# CTest / JUnit Integration Notes
# =============================================================================
# CTest discovers all test executables registered via add_test() automatically.
# JUnit XML output: ctest --test-dir build/host --output-junit results.xml
# Reproducible PBT: cmake -DPBT_SEED=12345 ../test/host
# =============================================================================

# =============================================================================
# Infrastructure validation test (CQR-CI-INFRA)
# Validates CTest discovery, doubles isolation, and PBT configuration.
# =============================================================================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_ctest_infrastructure.c")
    add_unit_test(test_ctest_infrastructure
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_ctest_infrastructure.c
    )
    target_compile_definitions(test_ctest_infrastructure PRIVATE
        PROJECT_ROOT_PATH="${PROJECT_ROOT}"
        CTEST_TESTFILE_PATH="${CMAKE_CURRENT_BINARY_DIR}/CTestTestfile.cmake"
    )
endif()

# =============================================================================
# Sanitizer Probe Fixtures (CQR-CI-SANITIZER-PROBES)
# =============================================================================
# These tests INTENTIONALLY trigger memory errors to validate that the sanitizer
# gate catches them. They are excluded from the default CTest run (no label
# filter) and included only when running with label "sanitizer_probe".
#
# Usage:
#   ctest --test-dir build/host_asan -L sanitizer_probe
#
# Each probe MUST fail under ASan/UBSan/TSan. If a probe passes, the gate is
# misconfigured or the sanitizer is not active.
# =============================================================================
if(SANITIZER STREQUAL "address" OR SANITIZER STREQUAL "thread")
    set(SANITIZER_PROBES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sanitizer_probes")

    if(SANITIZER STREQUAL "address")
        # Heap buffer over-read probe
        if(EXISTS "${SANITIZER_PROBES_DIR}/probe_heap_overread.c")
            add_executable(probe_heap_overread
                ${SANITIZER_PROBES_DIR}/probe_heap_overread.c
            )
            add_test(NAME probe_heap_overread COMMAND probe_heap_overread)
            set_tests_properties(probe_heap_overread PROPERTIES
                LABELS "sanitizer_probe"
                WILL_FAIL TRUE
                ENVIRONMENT "${ASAN_ENV};${UBSAN_ENV}"
            )
        endif()

        # Use-after-free probe
        if(EXISTS "${SANITIZER_PROBES_DIR}/probe_use_after_free.c")
            add_executable(probe_use_after_free
                ${SANITIZER_PROBES_DIR}/probe_use_after_free.c
            )
            add_test(NAME probe_use_after_free COMMAND probe_use_after_free)
            set_tests_properties(probe_use_after_free PROPERTIES
                LABELS "sanitizer_probe"
                WILL_FAIL TRUE
                ENVIRONMENT "${ASAN_ENV};${UBSAN_ENV}"
            )
        endif()

        # Double-free probe
        if(EXISTS "${SANITIZER_PROBES_DIR}/probe_double_free.c")
            add_executable(probe_double_free
                ${SANITIZER_PROBES_DIR}/probe_double_free.c
            )
            add_test(NAME probe_double_free COMMAND probe_double_free)
            set_tests_properties(probe_double_free PROPERTIES
                LABELS "sanitizer_probe"
                WILL_FAIL TRUE
                ENVIRONMENT "${ASAN_ENV};${UBSAN_ENV}"
            )
        endif()
    endif()

    if(SANITIZER STREQUAL "thread")
        # Data race probe
        if(EXISTS "${SANITIZER_PROBES_DIR}/probe_data_race.c")
            add_executable(probe_data_race
                ${SANITIZER_PROBES_DIR}/probe_data_race.c
            )
            target_link_libraries(probe_data_race PRIVATE pthread)
            add_test(NAME probe_data_race COMMAND probe_data_race)
            set_tests_properties(probe_data_race PROPERTIES
                LABELS "sanitizer_probe"
                WILL_FAIL TRUE
                ENVIRONMENT "${TSAN_ENV}"
            )
        endif()
    endif()
endif()
