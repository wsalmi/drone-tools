# Drone Telemetry Monitor - ESP-IDF Project
# Target: ESP32-S3 (M5 Stack Cardputer ADV)
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(drone-telemetry-monitor)
