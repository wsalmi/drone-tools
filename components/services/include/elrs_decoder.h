/**
 * @file elrs_decoder.h
 * @brief ELRS/CRSF telemetry decoder.
 *
 * Decodes CRSF (Crossfire Serial Protocol) frames used by ExpressLRS for
 * telemetry uplink. Extracts link statistics (RSSI, LQ, SNR), battery
 * information (voltage, current, capacity, remaining%), and GPS data
 * (lat, lon, groundspeed, heading, altitude, satellites).
 *
 * CRSF frame format:
 *   [Sync/Address] [Length] [Type] [Payload...] [CRC8]
 *
 * - Sync byte: 0xC8 (device address) or 0xEE (broadcast)
 * - Length: payload_size + type_byte + crc_byte
 * - Type: frame type identifier
 * - Payload: variable-length data
 * - CRC8: CRC-8/DVB-S2 (poly 0xD5) over type + payload bytes
 *
 * Validates: Requirements 8.2
 */

#ifndef ELRS_DECODER_H
#define ELRS_DECODER_H

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

/** CRSF sync bytes (valid frame start markers) */
#define CRSF_SYNC_BYTE          0xC8
#define CRSF_BROADCAST_BYTE     0xEE

/** CRSF frame types of interest for telemetry */
#define CRSF_FRAMETYPE_GPS              0x02
#define CRSF_FRAMETYPE_BATTERY_SENSOR   0x08
#define CRSF_FRAMETYPE_LINK_STATISTICS  0x14

/** Minimum frame size: sync(1) + len(1) + type(1) + crc(1) = 4 bytes */
#define CRSF_FRAME_MIN_SIZE     4

/** Maximum frame size: sync(1) + len(1) + payload(62) + type(1) + crc(1) = 64 */
#define CRSF_FRAME_MAX_SIZE     64

/** Maximum payload length (CRSF spec: length field max = 62, minus type and CRC) */
#define CRSF_MAX_PAYLOAD_LEN    60

/* ========================================================================
 * CRSF payload sizes for known frame types
 * ======================================================================== */

/** Link statistics payload: 10 bytes */
#define CRSF_LINK_STATS_PAYLOAD_LEN     10

/** Battery sensor payload: 8 bytes */
#define CRSF_BATTERY_PAYLOAD_LEN        8

/** GPS payload: 15 bytes */
#define CRSF_GPS_PAYLOAD_LEN            15

/* ========================================================================
 * Validation ranges
 * ======================================================================== */

/** RSSI valid range: -130 dBm to 0 dBm */
#define CRSF_RSSI_MIN_DBM       (-130)
#define CRSF_RSSI_MAX_DBM       (0)

/** Link quality: 0–100 percent */
#define CRSF_LQ_MIN_PCT         0
#define CRSF_LQ_MAX_PCT         100

/** Battery voltage: 0.0 to 100.0 V (in 0.1V units in protocol) */
#define CRSF_BATTERY_VOLTAGE_MAX_DV     1000

/** Battery remaining: 0–100 percent */
#define CRSF_BATTERY_PCT_MIN    0
#define CRSF_BATTERY_PCT_MAX    100

/** GPS latitude: -90.0 to +90.0 degrees (stored as deg * 1e7) */
#define CRSF_GPS_LAT_MIN       (-900000000)
#define CRSF_GPS_LAT_MAX       (900000000)

/** GPS longitude: -180.0 to +180.0 degrees (stored as deg * 1e7) */
#define CRSF_GPS_LON_MIN       (-1800000000)
#define CRSF_GPS_LON_MAX       (1800000000)

/** GPS altitude: offset by 1000m in protocol (0 = -1000m), max ~65535 - 1000 = 64535m */
#define CRSF_GPS_ALT_OFFSET     1000

/** GPS heading: 0–36000 (in 0.01 degree units) */
#define CRSF_GPS_HEADING_MAX    36000

/* ========================================================================
 * API Functions
 * ======================================================================== */

/**
 * @brief Decode a CRSF telemetry frame and populate decoded telemetry struct.
 *
 * Validates frame structure (sync byte, length, CRC), then decodes the
 * payload based on frame type. Supported frame types:
 * - 0x14 (Link Statistics): extracts RSSI, LQ
 * - 0x08 (Battery Sensor): extracts voltage, remaining%
 * - 0x02 (GPS): extracts lat, lon, altitude, speed, heading
 *
 * Fields that are not present in the decoded frame are marked with
 * has_* = false in the output struct. Fields from previously decoded
 * frames are NOT preserved — caller should merge if accumulation is desired.
 *
 * @param[in]  data  Pointer to raw CRSF frame data (starting at sync byte).
 * @param[in]  len   Length of data buffer in bytes.
 * @param[out] out   Pointer to decoded_telemetry_t to populate.
 *
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_ARG if data or out is NULL,
 *         ERR_DECODE_INCOMPLETE if frame is too short or truncated,
 *         ERR_DECODE_CRC_FAIL if CRC validation fails,
 *         ERR_DECODE_UNKNOWN_FMT if frame type is not supported.
 */
esp_err_t elrs_decode(const uint8_t *data, uint16_t len, decoded_telemetry_t *out);

/**
 * @brief Validate CRC-8/DVB-S2 of a CRSF frame.
 *
 * Computes CRC-8 with polynomial 0xD5 over the type byte + payload bytes
 * and compares against the CRC byte at the end of the frame.
 *
 * @param[in] data  Pointer to raw CRSF frame data (starting at sync byte).
 * @param[in] len   Length of data buffer in bytes.
 *
 * @return true if CRC is valid, false otherwise (including if frame is
 *         too short to contain a valid CRC).
 */
bool elrs_validate_crc(const uint8_t *data, uint16_t len);

/**
 * @brief Compute CRC-8/DVB-S2 over a byte buffer.
 *
 * Uses polynomial 0xD5 (x^8 + x^7 + x^6 + x^4 + x^2 + 1).
 * This is exposed for testing purposes.
 *
 * @param[in] data  Pointer to data buffer.
 * @param[in] len   Number of bytes to process.
 *
 * @return Computed CRC-8 value.
 */
uint8_t crsf_crc8_dvb_s2(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* ELRS_DECODER_H */
