/**
 * @file elrs_decoder.c
 * @brief ELRS/CRSF telemetry decoder implementation.
 *
 * Implements CRSF frame validation and decoding for telemetry data
 * transmitted by ExpressLRS systems. Supports Link Statistics (0x14),
 * Battery Sensor (0x08), and GPS (0x02) frame types.
 *
 * Validates: Requirements 8.2
 */

#include "elrs_decoder.h"
#include "error_codes.h"

#include <string.h>

/* ========================================================================
 * Internal Helpers
 * ======================================================================== */

/**
 * @brief Check if a byte is a valid CRSF sync/address byte.
 */
static inline bool is_valid_sync_byte(uint8_t byte)
{
    return (byte == CRSF_SYNC_BYTE) || (byte == CRSF_BROADCAST_BYTE);
}

/**
 * @brief Initialize decoded_telemetry_t to a clean state with all has_* = false.
 */
static void telemetry_clear(decoded_telemetry_t *out)
{
    memset(out, 0, sizeof(decoded_telemetry_t));
    out->has_position = false;
    out->has_altitude = false;
    out->has_speed = false;
    out->has_battery = false;
    out->has_flight_mode = false;
    out->rssi_dbm = 0;
    out->link_quality_pct = 0;
}

/**
 * @brief Read a big-endian int32 from a byte buffer.
 */
static int32_t read_be_int32(const uint8_t *buf)
{
    return (int32_t)(((uint32_t)buf[0] << 24) |
                     ((uint32_t)buf[1] << 16) |
                     ((uint32_t)buf[2] << 8) |
                     ((uint32_t)buf[3]));
}

/**
 * @brief Read a big-endian uint16 from a byte buffer.
 */
static uint16_t read_be_uint16(const uint8_t *buf)
{
    return (uint16_t)(((uint16_t)buf[0] << 8) | (uint16_t)buf[1]);
}

/* ========================================================================
 * CRC-8/DVB-S2
 * ======================================================================== */

uint8_t crsf_crc8_dvb_s2(const uint8_t *data, uint16_t len)
{
    uint8_t crc = 0;

    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (uint8_t)((crc << 1) ^ 0xD5);
            } else {
                crc = (uint8_t)(crc << 1);
            }
        }
    }

    return crc;
}

/* ========================================================================
 * CRC Validation
 * ======================================================================== */

bool elrs_validate_crc(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len < CRSF_FRAME_MIN_SIZE) {
        return false;
    }

    /* Validate sync byte */
    if (!is_valid_sync_byte(data[0])) {
        return false;
    }

    /* Frame length field: number of bytes after the length field itself
     * (includes type + payload + crc) */
    uint8_t frame_len = data[1];

    /* frame_len must be at least 2 (type + crc, no payload) */
    if (frame_len < 2) {
        return false;
    }

    /* Total frame size = sync(1) + len_field(1) + frame_len */
    uint16_t total_size = (uint16_t)(2 + frame_len);
    if (total_size > len) {
        return false; /* Buffer doesn't contain the full frame */
    }

    /* CRC is computed over type + payload (everything after length field
     * except the CRC byte itself) */
    const uint8_t *crc_data = &data[2]; /* starts at type byte */
    uint16_t crc_len = (uint16_t)(frame_len - 1); /* exclude the CRC byte itself */

    uint8_t computed_crc = crsf_crc8_dvb_s2(crc_data, crc_len);
    uint8_t received_crc = data[total_size - 1];

    return (computed_crc == received_crc);
}

/* ========================================================================
 * Frame Decoders
 * ======================================================================== */

/**
 * @brief Decode Link Statistics frame (type 0x14).
 *
 * Payload layout (10 bytes):
 *   [0]  Uplink RSSI Ant. 1 (dBm, negated: value = -dBm)
 *   [1]  Uplink RSSI Ant. 2 (dBm, negated)
 *   [2]  Uplink Link Quality (0–100%)
 *   [3]  Uplink SNR (dB, signed)
 *   [4]  Active antenna (0 or 1)
 *   [5]  RF Mode
 *   [6]  Uplink TX Power (index)
 *   [7]  Downlink RSSI (dBm, negated)
 *   [8]  Downlink Link Quality (0–100%)
 *   [9]  Downlink SNR (dB, signed)
 */
static esp_err_t decode_link_statistics(const uint8_t *payload, uint8_t payload_len,
                                         decoded_telemetry_t *out)
{
    if (payload_len < CRSF_LINK_STATS_PAYLOAD_LEN) {
        return ERR_DECODE_INCOMPLETE;
    }

    /* Uplink RSSI (take the better of two antennas) — stored as positive
     * value representing magnitude, actual RSSI is negative */
    int16_t rssi_ant1 = -(int16_t)payload[0];
    int16_t rssi_ant2 = -(int16_t)payload[1];

    /* Use the stronger signal (less negative = better) */
    int16_t rssi = (rssi_ant1 > rssi_ant2) ? rssi_ant1 : rssi_ant2;

    /* Validate RSSI range */
    if (rssi < CRSF_RSSI_MIN_DBM || rssi > CRSF_RSSI_MAX_DBM) {
        return ERR_DECODE_INCOMPLETE;
    }

    /* Link Quality */
    uint8_t lq = payload[2];
    if (lq > CRSF_LQ_MAX_PCT) {
        return ERR_DECODE_INCOMPLETE;
    }

    out->rssi_dbm = rssi;
    out->link_quality_pct = lq;

    return ESP_OK;
}

/**
 * @brief Decode Battery Sensor frame (type 0x08).
 *
 * Payload layout (8 bytes, big-endian):
 *   [0-1]  Voltage (in 0.1V units, uint16)
 *   [2-3]  Current (in 0.1A units, uint16)
 *   [4-6]  Capacity used (in mAh, uint24)
 *   [7]    Remaining battery (0–100%)
 */
static esp_err_t decode_battery_sensor(const uint8_t *payload, uint8_t payload_len,
                                        decoded_telemetry_t *out)
{
    if (payload_len < CRSF_BATTERY_PAYLOAD_LEN) {
        return ERR_DECODE_INCOMPLETE;
    }

    /* Voltage in 0.1V units */
    uint16_t voltage_dv = read_be_uint16(&payload[0]);

    /* Validate voltage range */
    if (voltage_dv > CRSF_BATTERY_VOLTAGE_MAX_DV) {
        return ERR_DECODE_INCOMPLETE;
    }

    /* Remaining percentage */
    uint8_t remaining = payload[7];
    if (remaining > CRSF_BATTERY_PCT_MAX) {
        return ERR_DECODE_INCOMPLETE;
    }

    /* Convert voltage from 0.1V to volts */
    out->battery_voltage = (float)voltage_dv / 10.0f;
    out->battery_pct = (float)remaining;
    out->has_battery = true;

    return ESP_OK;
}

/**
 * @brief Decode GPS frame (type 0x02).
 *
 * Payload layout (15 bytes, big-endian):
 *   [0-3]   Latitude (degrees * 1e7, int32)
 *   [4-7]   Longitude (degrees * 1e7, int32)
 *   [8-9]   Groundspeed (km/h * 10, uint16)
 *   [10-11]  Heading (degrees * 100, uint16)
 *   [12-13]  Altitude (meters + 1000m offset, uint16)
 *   [14]     Satellites in use (uint8)
 */
static esp_err_t decode_gps(const uint8_t *payload, uint8_t payload_len,
                             decoded_telemetry_t *out)
{
    if (payload_len < CRSF_GPS_PAYLOAD_LEN) {
        return ERR_DECODE_INCOMPLETE;
    }

    /* Latitude (deg * 1e7) */
    int32_t lat_raw = read_be_int32(&payload[0]);
    if (lat_raw < CRSF_GPS_LAT_MIN || lat_raw > CRSF_GPS_LAT_MAX) {
        return ERR_DECODE_INCOMPLETE;
    }

    /* Longitude (deg * 1e7) */
    int32_t lon_raw = read_be_int32(&payload[4]);
    if (lon_raw < CRSF_GPS_LON_MIN || lon_raw > CRSF_GPS_LON_MAX) {
        return ERR_DECODE_INCOMPLETE;
    }

    /* Groundspeed (km/h * 10) → convert to m/s */
    uint16_t speed_raw = read_be_uint16(&payload[8]);

    /* Heading (degrees * 100) */
    uint16_t heading_raw = read_be_uint16(&payload[10]);
    if (heading_raw > CRSF_GPS_HEADING_MAX) {
        return ERR_DECODE_INCOMPLETE;
    }

    /* Altitude (meters + 1000m offset) */
    uint16_t alt_raw = read_be_uint16(&payload[12]);

    /* Convert and store */
    out->lat = (double)lat_raw / 1e7;
    out->lon = (double)lon_raw / 1e7;
    out->speed_ms = (float)speed_raw / 10.0f / 3.6f; /* km/h*10 → km/h → m/s */
    out->heading_deg = (float)heading_raw / 100.0f;
    out->altitude_m = (float)alt_raw - (float)CRSF_GPS_ALT_OFFSET;

    out->has_position = true;
    out->has_altitude = true;
    out->has_speed = true;

    return ESP_OK;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

esp_err_t elrs_decode(const uint8_t *data, uint16_t len, decoded_telemetry_t *out)
{
    /* Parameter validation */
    if (data == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Clear output struct */
    telemetry_clear(out);

    /* Minimum frame size check */
    if (len < CRSF_FRAME_MIN_SIZE) {
        return ERR_DECODE_INCOMPLETE;
    }

    /* Validate sync byte */
    if (!is_valid_sync_byte(data[0])) {
        return ERR_DECODE_INCOMPLETE;
    }

    /* Frame length field */
    uint8_t frame_len = data[1];

    /* frame_len must be at least 2 (type + crc) */
    if (frame_len < 2) {
        return ERR_DECODE_INCOMPLETE;
    }

    /* Total frame size = sync(1) + len_field(1) + frame_len */
    uint16_t total_size = (uint16_t)(2 + frame_len);
    if (total_size > len) {
        return ERR_DECODE_INCOMPLETE;
    }

    /* Validate CRC */
    if (!elrs_validate_crc(data, len)) {
        return ERR_DECODE_CRC_FAIL;
    }

    /* Extract frame type and payload */
    uint8_t frame_type = data[2];
    const uint8_t *payload = &data[3];
    uint8_t payload_len = (uint8_t)(frame_len - 2); /* exclude type and CRC */

    /* Decode based on frame type */
    switch (frame_type) {
        case CRSF_FRAMETYPE_LINK_STATISTICS:
            return decode_link_statistics(payload, payload_len, out);

        case CRSF_FRAMETYPE_BATTERY_SENSOR:
            return decode_battery_sensor(payload, payload_len, out);

        case CRSF_FRAMETYPE_GPS:
            return decode_gps(payload, payload_len, out);

        default:
            return ERR_DECODE_UNKNOWN_FMT;
    }
}
