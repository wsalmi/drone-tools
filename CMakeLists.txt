# Drone Telemetry Monitor - ESP-IDF Project
# Target: ESP32-S3 (M5 Stack Cardputer ADV)
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(drone-telemetry-monitor)

add_custom_command(
    TARGET gen_project_binary POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DPROJECT_NAME=${CMAKE_PROJECT_NAME}
                             -DBIN_FILE=${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
                             -DFIRMWARE_DIR=${CMAKE_SOURCE_DIR}/firmware
                             -P ${CMAKE_SOURCE_DIR}/tools/copy_firmware.cmake
    COMMENT "Copying versioned firmware binary to firmware/"
)
