/**
 * @file mavlink_decoder.h
 * @brief MAVLink v1/v2 frame decoder for drone telemetry extraction.
 *
 * Decodes MAVLink frames (v1 STX=0xFE, v2 STX=0xFD) and extracts telemetry
 * data from supported message types:
 * - GLOBAL_POSITION_INT (msg_id=33): lat, lon, alt, speed, heading
 * - BATTERY_STATUS (msg_id=147): voltage, remaining percentage
 * - HEARTBEAT (msg_id=0): flight mode
 * - HOME_POSITION (msg_id=242): home lat, lon, alt
 *
 * Unit conversions applied:
 * - lat/lon: int32 / 1e7 → degrees
 * - altitude: mm → meters
 * - speed: cm/s → m/s
 * - heading: cdeg → degrees
 *
 * Validates: Requirements 8.1, 6.2
 */

#ifndef MAVLINK_DECODER_H
#define MAVLINK_DECODER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "aircraft_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Constants
 * ======================================================================== */

/** MAVLink v1 start-of-frame byte */
#define MAVLINK_V1_STX          0xFE

/** MAVLink v2 start-of-frame byte */
#define MAVLINK_V2_STX          0xFD

/** MAVLink v1 header size (STX + Len + Seq + SysID + CompID + MsgID) */
#define MAVLINK_V1_HEADER_LEN   6

/** MAVLink v2 header size (STX + Len + IncompatFlags + CompatFlags + Seq + SysID + CompID + MsgID(3)) */
#define MAVLINK_V2_HEADER_LEN   10

/** CRC size in bytes */
#define MAVLINK_CRC_LEN         2

/** MAVLink v2 signature size */
#define MAVLINK_V2_SIGNATURE_LEN 13

/** Maximum payload length */
#define MAVLINK_MAX_PAYLOAD_LEN 255

/* MAVLink Message IDs */
#define MAVLINK_MSG_ID_HEARTBEAT            0
#define MAVLINK_MSG_ID_GLOBAL_POSITION_INT  33
#define MAVLINK_MSG_ID_BATTERY_STATUS       147
#define MAVLINK_MSG_ID_HOME_POSITION        242

/* ========================================================================
 * API Functions
 * ======================================================================== */

/**
 * @brief Decode a MAVLink frame and extract telemetry data.
 *
 * Validates the frame structure and CRC, then extracts relevant telemetry
 * fields into the output struct. Sets has_* flags for fields that were
 * successfully extracted.
 *
 * @param[in]  data  Pointer to raw MAVLink frame data.
 * @param[in]  len   Length of the data buffer in bytes.
 * @param[out] out   Pointer to decoded telemetry struct to populate.
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_ARG if data or out is NULL,
 *         ERR_DECODE_INCOMPLETE if frame is truncated,
 *         ERR_DECODE_CRC_FAIL if CRC validation fails,
 *         ERR_DECODE_UNKNOWN_FMT if message type is not supported.
 */
esp_err_t mavlink_decode(const uint8_t *data, uint16_t len, decoded_telemetry_t *out);

/**
 * @brief Check if data contains a valid MAVLink frame structure.
 *
 * Performs basic structural validation: checks STX byte, verifies the
 * buffer is long enough for the declared payload, and validates CRC.
 *
 * @param[in] data  Pointer to raw data to validate.
 * @param[in] len   Length of the data buffer in bytes.
 * @return true if the data contains a structurally valid MAVLink frame,
 *         false otherwise.
 */
bool mavlink_is_valid_frame(const uint8_t *data, uint16_t len);

/**
 * @brief Calculate MAVLink X.25 CRC for a buffer.
 *
 * Computes the CRC-16/MCRF4XX (X.25) checksum used by MAVLink.
 * This is exposed for testing purposes.
 *
 * @param[in] data   Pointer to data to checksum.
 * @param[in] len    Length of data in bytes.
 * @param[in] crc_extra  Message-specific CRC extra byte (seed).
 * @return Computed 16-bit CRC value.
 */
uint16_t mavlink_crc_calculate(const uint8_t *data, uint16_t len, uint8_t crc_extra);

#ifdef __cplusplus
}
#endif

#endif /* MAVLINK_DECODER_H */
