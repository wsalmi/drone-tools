/**
 * @file remoteid_decoder.h
 * @brief RemoteID decoder for WiFi NAN/Beacon and BLE Legacy Advertisement.
 *
 * Decodes ASTM F3411 RemoteID messages from raw WiFi and BLE frames,
 * extracting UAS ID, position, altitude, speed, direction, and
 * Operator Location when present.
 *
 * Message types (ASTM F3411, 25-byte messages):
 * - Type 0: Basic ID (UAS ID — serial number or registration)
 * - Type 1: Location/Vector (lat, lon, altitude, speed, direction, timestamp)
 * - Type 2: Authentication
 * - Type 3: Self-ID (operator description)
 * - Type 4: System (operator location, area count, area radius)
 * - Type 5: Operator ID
 *
 * Validates: Requirements 1.1, 1.2, 1.3, 1.4, 6.1
 */

#ifndef REMOTEID_DECODER_H
#define REMOTEID_DECODER_H

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

/** Maximum UAS ID length (20 chars + null terminator) */
#define REMOTEID_UAS_ID_MAX_LEN     21

/** ASTM F3411 single message size in bytes */
#define REMOTEID_MSG_SIZE           25

/** Minimum valid frame size for WiFi RemoteID (vendor-specific action frame overhead + 1 message) */
#define REMOTEID_WIFI_MIN_LEN       30

/** Minimum valid frame size for BLE RemoteID (AD header + 1 message) */
#define REMOTEID_BLE_MIN_LEN        29

/** Maximum number of messages in a message pack */
#define REMOTEID_MAX_MESSAGES       10

/* ASTM F3411 Message Types */
#define REMOTEID_MSG_TYPE_BASIC_ID      0
#define REMOTEID_MSG_TYPE_LOCATION      1
#define REMOTEID_MSG_TYPE_AUTH          2
#define REMOTEID_MSG_TYPE_SELF_ID       3
#define REMOTEID_MSG_TYPE_SYSTEM        4
#define REMOTEID_MSG_TYPE_OPERATOR_ID   5
#define REMOTEID_MSG_TYPE_MSG_PACK      0x0F
#define REMOTEID_MSG_TYPE_MAX           5

/* Location message status values */
#define REMOTEID_STATUS_UNDECLARED      0
#define REMOTEID_STATUS_GROUND          1
#define REMOTEID_STATUS_AIRBORNE        2
#define REMOTEID_STATUS_EMERGENCY       3
#define REMOTEID_STATUS_REMOTE_ID_FAIL  4

/* Basic ID type values */
#define REMOTEID_ID_TYPE_NONE           0
#define REMOTEID_ID_TYPE_SERIAL         1
#define REMOTEID_ID_TYPE_CAA_REG        2
#define REMOTEID_ID_TYPE_UTM_ASSIGNED   3
#define REMOTEID_ID_TYPE_SPECIFIC_SESSION 4

/* Coordinate conversion factor: int32 to degrees */
#define REMOTEID_LAT_LON_SCALE          1e-7

/* Invalid/unknown coordinate sentinel (ASTM F3411) */
#define REMOTEID_LAT_INVALID            0
#define REMOTEID_LON_INVALID            0

/* Altitude offset (ASTM F3411: altitude = raw * 0.5 - 1000) */
#define REMOTEID_ALT_OFFSET             (-1000.0f)
#define REMOTEID_ALT_SCALE              0.5f
#define REMOTEID_ALT_INVALID            0xFFFF

/* Speed scale (ASTM F3411: speed in 0.25 m/s increments) */
#define REMOTEID_SPEED_SCALE            0.25f
#define REMOTEID_SPEED_MULTIPLIER_FLAG  0x01

/* Direction scale (ASTM F3411: direction in degrees, 0-360) */
#define REMOTEID_DIRECTION_SCALE        1.0f

/* WiFi frame offsets */
#define REMOTEID_WIFI_OUI_OFFSET        0
#define REMOTEID_WIFI_OUI_TYPE_OFFSET   3
#define REMOTEID_WIFI_MSG_COUNTER_OFFSET 4
#define REMOTEID_WIFI_MSG_START_OFFSET  5

/* BLE AD type for RemoteID */
#define REMOTEID_BLE_AD_TYPE_SERVICE_DATA_16  0x16
#define REMOTEID_BLE_UUID16_ASTM             0xFFFA

/* ========================================================================
 * Data Types
 * ======================================================================== */

/**
 * @brief Decoded RemoteID data from WiFi or BLE frame.
 */
typedef struct {
    char uas_id[REMOTEID_UAS_ID_MAX_LEN];  /**< UAS ID from Basic ID message */
    double latitude;                        /**< Latitude in degrees from Location message */
    double longitude;                       /**< Longitude in degrees from Location message */
    float altitude_m;                       /**< Geodetic altitude in meters */
    float speed_ms;                         /**< Ground speed in m/s */
    float direction_deg;                    /**< Direction 0-360 degrees */
    double operator_lat;                    /**< Operator latitude from System message */
    double operator_lon;                    /**< Operator longitude from System message */
    bool has_position;                      /**< True if location data is valid */
    bool has_operator_location;             /**< True if operator location is present */
    bool has_speed;                         /**< True if speed data is valid */
} remoteid_data_t;

/* ========================================================================
 * API Functions
 * ======================================================================== */

/**
 * @brief Decode a RemoteID message from a WiFi NAN or Beacon frame.
 *
 * Parses the raw WiFi frame data according to ASTM F3411, extracting
 * UAS ID, position, altitude, speed, direction, and operator location.
 *
 * The WiFi frame is expected to contain:
 * - 3-byte OUI (FA:0B:BC for ASTM F3411)
 * - 1-byte OUI Type (0x0D)
 * - 1-byte message counter
 * - One or more 25-byte ASTM F3411 messages
 *
 * @param[in]  frame  Raw WiFi frame data (after MAC header, starting at payload).
 * @param[in]  len    Length of the frame data in bytes.
 * @param[out] out    Pointer to decoded data output struct.
 *
 * @return ESP_OK on success (at least Basic ID decoded),
 *         ESP_ERR_INVALID_ARG if frame or out is NULL,
 *         ESP_ERR_INVALID_SIZE if len is below minimum or message count invalid,
 *         ERR_DECODE_CRC_FAIL if CRC validation fails,
 *         ERR_DECODE_INCOMPLETE if required fields (Basic ID) are missing,
 *         ERR_DECODE_UNKNOWN_FMT if message type is out of bounds.
 */
esp_err_t remoteid_decode_wifi(const uint8_t *frame, uint16_t len, remoteid_data_t *out);

/**
 * @brief Decode a RemoteID message from a BLE Legacy Advertisement.
 *
 * Parses BLE advertising data according to ASTM F3411, extracting
 * UAS ID, position, altitude, speed, direction, and operator location.
 *
 * The BLE advertisement data is expected to contain:
 * - 1-byte AD length
 * - 1-byte AD type (0x16 = Service Data - 16-bit UUID)
 * - 2-byte UUID (0xFFFA for ASTM F3411, little-endian)
 * - 1-byte message counter
 * - One 25-byte ASTM F3411 message (single message per advertisement)
 *
 * @param[in]  adv_data  Raw BLE advertisement data.
 * @param[in]  len       Length of the advertisement data in bytes.
 * @param[out] out       Pointer to decoded data output struct.
 *
 * @return ESP_OK on success (at least Basic ID decoded),
 *         ESP_ERR_INVALID_ARG if adv_data or out is NULL,
 *         ESP_ERR_INVALID_SIZE if len is below minimum or inconsistent,
 *         ERR_DECODE_CRC_FAIL if CRC validation fails,
 *         ERR_DECODE_INCOMPLETE if required fields (Basic ID) are missing,
 *         ERR_DECODE_UNKNOWN_FMT if message type is out of bounds.
 */
esp_err_t remoteid_decode_ble(const uint8_t *adv_data, uint16_t len, remoteid_data_t *out);

/**
 * @brief Initialize the remoteid_data_t struct to default/invalid values.
 *
 * Sets all fields to zero/false and clears the UAS ID string.
 *
 * @param[out] data  Pointer to struct to initialize.
 */
void remoteid_data_init(remoteid_data_t *data);

/**
 * @brief Unified RemoteID decoder following the telemetry_decode_fn pattern.
 *
 * Decodes raw RemoteID data from either WiFi or BLE source (determined by
 * the source field in raw detection) and populates the decoded_telemetry_t
 * output struct for integration with the aircraft registry pipeline.
 *
 * This function dispatches to remoteid_decode_wifi() or remoteid_decode_ble()
 * based on the detection source, then maps remoteid_data_t fields to
 * decoded_telemetry_t.
 *
 * @param[in]  raw_payload  Raw frame/advertisement data.
 * @param[in]  payload_len  Length of raw data in bytes.
 * @param[in]  is_ble       true if BLE source, false if WiFi source.
 * @param[out] out          Pointer to decoded telemetry output struct.
 * @param[out] rid_out      Optional pointer to full RemoteID data (NULL to skip).
 *
 * @return ESP_OK on success, or appropriate error code on failure.
 */
esp_err_t remoteid_decode(const uint8_t *raw_payload, uint16_t payload_len,
                          bool is_ble, decoded_telemetry_t *out,
                          remoteid_data_t *rid_out);

/**
 * @brief Compute CRC-8 for ASTM F3411 message validation.
 *
 * Computes a CRC-8 over the message bytes for validation purposes.
 * Used internally to verify packet integrity when CRC byte is present.
 *
 * @param[in] data  Pointer to message data.
 * @param[in] len   Length of data in bytes.
 * @return Computed CRC-8 value.
 */
uint8_t remoteid_crc8(const uint8_t *data, uint16_t len);

/**
 * @brief Validate a RemoteID frame structure without fully decoding.
 *
 * Performs structural validation including size checks, OUI/UUID verification,
 * and CRC validation (when available). Useful for quickly filtering invalid
 * frames before attempting full decode.
 *
 * @param[in] frame    Raw frame data.
 * @param[in] len      Length of frame data.
 * @param[in] is_ble   true if BLE source, false if WiFi source.
 * @return ESP_OK if frame passes validation, error code otherwise.
 */
esp_err_t remoteid_validate_frame(const uint8_t *frame, uint16_t len, bool is_ble);

/**
 * @brief Encode RemoteID data back into ASTM F3411 message bytes (for testing).
 *
 * Creates a minimal message set (Basic ID + Location + optionally System)
 * from decoded data. Used for round-trip property testing.
 *
 * @param[in]  data       Decoded RemoteID data to encode.
 * @param[out] out_buf    Buffer to write encoded messages into.
 * @param[in]  buf_size   Size of the output buffer.
 * @param[out] out_len    Number of bytes written.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if pointers are NULL,
 *         ESP_ERR_NO_MEM if buffer is too small.
 */
esp_err_t remoteid_encode(const remoteid_data_t *data, uint8_t *out_buf,
                          uint16_t buf_size, uint16_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* REMOTEID_DECODER_H */
