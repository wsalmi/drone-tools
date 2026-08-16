/**
 * @file error_codes.h
 * @brief Custom error codes for the Drone Telemetry Monitor firmware.
 *
 * Error codes are organized by subsystem with a dedicated base offset:
 * - 0x1000: HAL (Hardware Abstraction Layer)
 * - 0x2000: Decode (Protocol decoding)
 * - 0x3000: Configuration
 *
 * These codes extend the ESP-IDF esp_err_t space and should not
 * overlap with standard ESP error codes (0x0000–0x0FFF).
 */

#ifndef ERROR_CODES_H
#define ERROR_CODES_H

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * HAL Error Codes (0x1000 – 0x1FFF)
 * ======================================================================== */

/** @brief Base error code for HAL subsystem */
#define ERR_HAL_BASE            0x1000

/* --- LoRa SX1262 errors (0x1001 – 0x100F) --- */

/** @brief LoRa module did not respond within timeout */
#define ERR_HAL_LORA_TIMEOUT    (ERR_HAL_BASE + 0x01)

/** @brief SPI communication failure with LoRa module */
#define ERR_HAL_LORA_SPI_FAIL   (ERR_HAL_BASE + 0x02)

/** @brief LoRa module not found or not connected */
#define ERR_HAL_LORA_NO_DEVICE  (ERR_HAL_BASE + 0x03)

/* --- NRF24L01+ errors (0x1010 – 0x101F) --- */

/** @brief NRF24 module did not respond within timeout */
#define ERR_HAL_NRF_TIMEOUT     (ERR_HAL_BASE + 0x10)

/** @brief SPI communication failure with NRF24 module */
#define ERR_HAL_NRF_SPI_FAIL    (ERR_HAL_BASE + 0x11)

/* --- RTL-SDR errors (0x1020 – 0x102F) --- */

/** @brief USB communication failure with SDR module */
#define ERR_HAL_SDR_USB_FAIL    (ERR_HAL_BASE + 0x20)

/** @brief SDR device not found on USB bus */
#define ERR_HAL_SDR_NO_DEVICE   (ERR_HAL_BASE + 0x21)

/* --- GPS ATGM336H errors (0x1030 – 0x103F) --- */

/** @brief GPS module has no satellite fix */
#define ERR_HAL_GPS_NO_FIX      (ERR_HAL_BASE + 0x30)

/** @brief GPS fix is degraded (insufficient satellites or high HDOP) */
#define ERR_HAL_GPS_DEGRADED    (ERR_HAL_BASE + 0x31)

/* --- SD Card errors (0x1040 – 0x104F) --- */

/** @brief SD card is full, no space for new data */
#define ERR_HAL_SD_FULL         (ERR_HAL_BASE + 0x40)

/** @brief SD card is not inserted or not detected */
#define ERR_HAL_SD_ABSENT       (ERR_HAL_BASE + 0x41)

/* ========================================================================
 * Decode Error Codes (0x2000 – 0x2FFF)
 * ======================================================================== */

/** @brief Base error code for decode subsystem */
#define ERR_DECODE_BASE         0x2000

/** @brief Packet CRC validation failed */
#define ERR_DECODE_CRC_FAIL     (ERR_DECODE_BASE + 0x01)

/** @brief Packet is incomplete (truncated or missing fields) */
#define ERR_DECODE_INCOMPLETE   (ERR_DECODE_BASE + 0x02)

/** @brief Unknown or unsupported packet format */
#define ERR_DECODE_UNKNOWN_FMT  (ERR_DECODE_BASE + 0x03)

/* ========================================================================
 * Configuration Error Codes (0x3000 – 0x3FFF)
 * ======================================================================== */

/** @brief Base error code for configuration subsystem */
#define ERR_CONFIG_BASE         0x3000

/** @brief Failed to parse configuration file (invalid JSON/CSV) */
#define ERR_CONFIG_PARSE_FAIL   (ERR_CONFIG_BASE + 0x01)

/** @brief Configuration file not found on SD card */
#define ERR_CONFIG_FILE_ABSENT  (ERR_CONFIG_BASE + 0x02)

#ifdef __cplusplus
}
#endif

#endif /* ERROR_CODES_H */
