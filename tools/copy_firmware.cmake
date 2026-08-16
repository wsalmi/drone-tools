# copy_firmware.cmake
# Copies the generated binary into firmware/ directory with naming: <nome>_<ano>_<mês>_<dia>_<hora><minuto>.bin

if(NOT DEFINED PROJECT_NAME OR NOT DEFINED BIN_FILE OR NOT DEFINED FIRMWARE_DIR)
    message(FATAL_ERROR "Missing required variables: PROJECT_NAME, BIN_FILE, FIRMWARE_DIR")
endif()

string(TIMESTAMP BUILD_TIMESTAMP "%Y_%m_%d_%H%M")
set(DEST_FILE "${FIRMWARE_DIR}/${PROJECT_NAME}_${BUILD_TIMESTAMP}.bin")

file(MAKE_DIRECTORY "${FIRMWARE_DIR}")
file(COPY_FILE "${BIN_FILE}" "${DEST_FILE}")
message(STATUS "Firmware copied: ${DEST_FILE}")
