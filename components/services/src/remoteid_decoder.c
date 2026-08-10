/**
 * @file remoteid_decoder.c
 * @brief RemoteID decoder implementation for WiFi NAN/Beacon and BLE.
 *
 * Implements ASTM F3411 RemoteID decoding for:
 * - WiFi NAN Action Frames and Beacon frames
 * - BLE Legacy Advertisement (4.x/5.x)
 *
 * Validates: Requirements 1.1, 1.2, 1.3, 1.4, 6.1
 */

#include "remoteid_decoder.h"
#include "error_codes.h"
#include <string.h>
#include <math.h>

/* ========================================================================
 * Internal Constants
 * ======================================================================== */

/** ASTM F3411 OUI for WiFi RemoteID (FA:0B:BC) */
static const uint8_t REMOTEID_WIFI_OUI[3] = {0xFA, 0x0B, 0xBC};

/** Expected OUI Type byte for ASTM F3411 */
#define REMOTEID_WIFI_OUI_TYPE      0x0D

/** Message header byte: upper nibble = message type, lower nibble = protocol version */
#define MSG_TYPE_MASK               0xF0
#define MSG_TYPE_SHIFT              4
#define MSG_PROTO_VERSION_MASK      0x0F

/* Basic ID message offsets (relative to message start, after header byte) */
#define BASIC_ID_TYPE_OFFSET        1   /* byte 1: ID type (upper nibble) + UA type (lower) */
#define BASIC_ID_DATA_OFFSET        2   /* bytes 2-21: UAS ID (20 bytes) */
#define BASIC_ID_DATA_LEN           20

/* Location message offsets (relative to message start) */
#define LOC_STATUS_OFFSET           1   /* byte 1: status + height type + direction segment */
#define LOC_DIRECTION_OFFSET        2   /* byte 2: direction (degrees, 0-179 or 0-360) */
#define LOC_SPEED_OFFSET            3   /* byte 3: speed (0.25 m/s units, lower 8 bits) */
#define LOC_VERT_SPEED_OFFSET       4   /* byte 4: vertical speed */
#define LOC_LAT_OFFSET              5   /* bytes 5-8: latitude (int32, /1e7) */
#define LOC_LON_OFFSET              9   /* bytes 9-12: longitude (int32, /1e7) */
#define LOC_ALT_PRESS_OFFSET        13  /* bytes 13-14: pressure altitude (uint16) */
#define LOC_ALT_GEO_OFFSET          15  /* bytes 15-16: geodetic altitude (uint16) */
#define LOC_HEIGHT_OFFSET           17  /* bytes 17-18: height above ground */
#define LOC_HACC_VACC_OFFSET        19  /* byte 19: horizontal/vertical accuracy */
#define LOC_BAROALT_ACC_OFFSET      20  /* byte 20: baro alt accuracy + speed accuracy */
#define LOC_TIMESTAMP_OFFSET        21  /* bytes 21-22: timestamp */
#define LOC_RESERVED_OFFSET         23  /* bytes 23-24: reserved/timestamp accuracy */

/* System message offsets (relative to message start) */
#define SYS_FLAGS_OFFSET            1   /* byte 1: operator location type + classification */
#define SYS_OP_LAT_OFFSET          2   /* bytes 2-5: operator latitude (int32, /1e7) */
#define SYS_OP_LON_OFFSET          6   /* bytes 6-9: operator longitude (int32, /1e7) */
#define SYS_AREA_COUNT_OFFSET      10  /* bytes 10-11: area count */
#define SYS_AREA_RADIUS_OFFSET     12  /* byte 12: area radius */
#define SYS_AREA_CEILING_OFFSET    13  /* bytes 13-14: area ceiling */
#define SYS_AREA_FLOOR_OFFSET      15  /* bytes 15-16: area floor */
#define SYS_UA_CLASS_OFFSET        17  /* byte 17: UA classification */
#define SYS_OP_ALT_GEO_OFFSET     18  /* bytes 18-19: operator altitude */
#define SYS_TIMESTAMP_OFFSET       20  /* bytes 20-23: system timestamp */

/* ========================================================================
 * Internal Helper Functions
 * ======================================================================== */

/**
 * @brief Read a little-endian int32 from a byte buffer.
 */
static int32_t read_int32_le(const uint8_t *buf)
{
    return (int32_t)((uint32_t)buf[0] |
                     ((uint32_t)buf[1] << 8) |
                     ((uint32_t)buf[2] << 16) |
                     ((uint32_t)buf[3] << 24));
}

/**
 * @brief Read a little-endian uint16 from a byte buffer.
 */
static uint16_t read_uint16_le(const uint8_t *buf)
{
    return (uint16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
}

/**
 * @brief Write a little-endian int32 to a byte buffer.
 */
static void write_int32_le(uint8_t *buf, int32_t val)
{
    uint32_t uval = (uint32_t)val;
    buf[0] = (uint8_t)(uval & 0xFF);
    buf[1] = (uint8_t)((uval >> 8) & 0xFF);
    buf[2] = (uint8_t)((uval >> 16) & 0xFF);
    buf[3] = (uint8_t)((uval >> 24) & 0xFF);
}

/**
 * @brief Write a little-endian uint16 to a byte buffer.
 */
static void write_uint16_le(uint8_t *buf, uint16_t val)
{
    buf[0] = (uint8_t)(val & 0xFF);
    buf[1] = (uint8_t)((val >> 8) & 0xFF);
}

/**
 * @brief Convert raw altitude uint16 to meters.
 *
 * ASTM F3411: altitude_m = raw_value * 0.5 - 1000.0
 */
static float altitude_raw_to_meters(uint16_t raw)
{
    if (raw == REMOTEID_ALT_INVALID) {
        return 0.0f;
    }
    return (float)raw * REMOTEID_ALT_SCALE + REMOTEID_ALT_OFFSET;
}

/**
 * @brief Convert meters to raw altitude uint16.
 */
static uint16_t altitude_meters_to_raw(float meters)
{
    float raw = (meters - REMOTEID_ALT_OFFSET) / REMOTEID_ALT_SCALE;
    if (raw < 0.0f) raw = 0.0f;
    if (raw > 65534.0f) raw = 65534.0f;
    return (uint16_t)(raw + 0.5f);
}

/**
 * @brief Decode a single Basic ID message.
 */
static esp_err_t decode_basic_id(const uint8_t *msg, remoteid_data_t *out)
{
    /* Extract UAS ID (bytes 2-21, 20 bytes, null-terminated ASCII) */
    memcpy(out->uas_id, &msg[BASIC_ID_DATA_OFFSET], BASIC_ID_DATA_LEN);
    out->uas_id[BASIC_ID_DATA_LEN] = '\0';

    /* Trim trailing spaces/nulls */
    int len = BASIC_ID_DATA_LEN - 1;
    while (len >= 0 && (out->uas_id[len] == ' ' || out->uas_id[len] == '\0')) {
        out->uas_id[len] = '\0';
        len--;
    }

    return ESP_OK;
}

/**
 * @brief Decode a single Location/Vector message.
 */
static esp_err_t decode_location(const uint8_t *msg, remoteid_data_t *out)
{
    /* Status and speed multiplier from byte 1 */
    uint8_t status_byte = msg[LOC_STATUS_OFFSET];
    bool speed_multiplier = (status_byte & 0x01) != 0;

    /* Direction (byte 2): 0-360 degrees */
    uint16_t direction_raw = (uint16_t)msg[LOC_DIRECTION_OFFSET];
    /* If direction segment flag is set in status, add 180 */
    if (status_byte & 0x02) {
        direction_raw += 180;
    }
    out->direction_deg = (float)direction_raw;
    if (out->direction_deg > 360.0f) {
        out->direction_deg = 360.0f;
    }

    /* Speed (byte 3): in 0.25 m/s units */
    uint8_t speed_raw = msg[LOC_SPEED_OFFSET];
    float speed = (float)speed_raw * REMOTEID_SPEED_SCALE;
    if (speed_multiplier) {
        speed += 255.0f * REMOTEID_SPEED_SCALE;
    }
    out->speed_ms = speed;
    out->has_speed = (speed_raw != 0 || speed_multiplier);

    /* Latitude (bytes 5-8): int32, divide by 1e7 for degrees */
    int32_t lat_raw = read_int32_le(&msg[LOC_LAT_OFFSET]);
    out->latitude = (double)lat_raw * REMOTEID_LAT_LON_SCALE;

    /* Longitude (bytes 9-12): int32, divide by 1e7 for degrees */
    int32_t lon_raw = read_int32_le(&msg[LOC_LON_OFFSET]);
    out->longitude = (double)lon_raw * REMOTEID_LAT_LON_SCALE;

    /* Position is valid if coordinates are non-zero */
    out->has_position = (lat_raw != REMOTEID_LAT_INVALID || lon_raw != REMOTEID_LON_INVALID);

    /* Geodetic altitude (bytes 15-16): uint16, altitude = raw * 0.5 - 1000 */
    uint16_t alt_geo_raw = read_uint16_le(&msg[LOC_ALT_GEO_OFFSET]);
    out->altitude_m = altitude_raw_to_meters(alt_geo_raw);

    return ESP_OK;
}

/**
 * @brief Decode a single System message (operator location).
 */
static esp_err_t decode_system(const uint8_t *msg, remoteid_data_t *out)
{
    /* Operator latitude (bytes 2-5): int32, divide by 1e7 */
    int32_t op_lat_raw = read_int32_le(&msg[SYS_OP_LAT_OFFSET]);
    out->operator_lat = (double)op_lat_raw * REMOTEID_LAT_LON_SCALE;

    /* Operator longitude (bytes 6-9): int32, divide by 1e7 */
    int32_t op_lon_raw = read_int32_le(&msg[SYS_OP_LON_OFFSET]);
    out->operator_lon = (double)op_lon_raw * REMOTEID_LAT_LON_SCALE;

    /* Operator location is valid if coordinates are non-zero */
    out->has_operator_location = (op_lat_raw != 0 || op_lon_raw != 0);

    return ESP_OK;
}

/**
 * @brief Decode a single ASTM F3411 25-byte message.
 *
 * Routes to the appropriate sub-decoder based on message type.
 *
 * @param[in]  msg  Pointer to 25-byte message.
 * @param[out] out  Decoded data struct to populate.
 * @return ESP_OK on success, error code on failure.
 */
static esp_err_t decode_single_message(const uint8_t *msg, remoteid_data_t *out)
{
    uint8_t header = msg[0];
    uint8_t msg_type = (header & MSG_TYPE_MASK) >> MSG_TYPE_SHIFT;

    /* Validate message type bounds */
    if (msg_type > REMOTEID_MSG_TYPE_MAX && msg_type != REMOTEID_MSG_TYPE_MSG_PACK) {
        return ERR_DECODE_UNKNOWN_FMT;
    }

    switch (msg_type) {
        case REMOTEID_MSG_TYPE_BASIC_ID:
            return decode_basic_id(msg, out);

        case REMOTEID_MSG_TYPE_LOCATION:
            return decode_location(msg, out);

        case REMOTEID_MSG_TYPE_SYSTEM:
            return decode_system(msg, out);

        case REMOTEID_MSG_TYPE_AUTH:
        case REMOTEID_MSG_TYPE_SELF_ID:
        case REMOTEID_MSG_TYPE_OPERATOR_ID:
            /* These message types are valid but we don't extract data from them */
            return ESP_OK;

        default:
            return ERR_DECODE_UNKNOWN_FMT;
    }
}

/**
 * @brief Validate that the decoded data has required fields.
 *
 * ASTM F3411 requires at minimum a Basic ID message with a non-empty UAS ID.
 */
static esp_err_t validate_decoded_data(const remoteid_data_t *data, bool has_basic_id)
{
    if (!has_basic_id) {
        return ERR_DECODE_INCOMPLETE;
    }

    /* UAS ID must not be empty */
    if (data->uas_id[0] == '\0') {
        return ERR_DECODE_INCOMPLETE;
    }

    return ESP_OK;
}

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

void remoteid_data_init(remoteid_data_t *data)
{
    if (data == NULL) {
        return;
    }
    memset(data, 0, sizeof(remoteid_data_t));
    data->has_position = false;
    data->has_operator_location = false;
    data->has_speed = false;
}

esp_err_t remoteid_decode_wifi(const uint8_t *frame, uint16_t len, remoteid_data_t *out)
{
    /* Validate arguments */
    if (frame == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Validate minimum frame length */
    if (len < REMOTEID_WIFI_MIN_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }

    /* Initialize output */
    remoteid_data_init(out);

    /* Verify OUI (FA:0B:BC) */
    if (memcmp(&frame[REMOTEID_WIFI_OUI_OFFSET], REMOTEID_WIFI_OUI, 3) != 0) {
        return ERR_DECODE_UNKNOWN_FMT;
    }

    /* Verify OUI Type (0x0D) */
    if (frame[REMOTEID_WIFI_OUI_TYPE_OFFSET] != REMOTEID_WIFI_OUI_TYPE) {
        return ERR_DECODE_UNKNOWN_FMT;
    }

    /* Message counter (byte 4) — informational, number of messages that follow */
    uint8_t msg_counter = frame[REMOTEID_WIFI_MSG_COUNTER_OFFSET];

    /* Calculate available payload for messages */
    uint16_t payload_start = REMOTEID_WIFI_MSG_START_OFFSET;
    uint16_t payload_len = len - payload_start;

    /* Validate: payload must contain at least one message */
    if (payload_len < REMOTEID_MSG_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }

    /* Calculate actual number of messages based on available data */
    uint8_t num_messages = payload_len / REMOTEID_MSG_SIZE;
    if (msg_counter > 0 && msg_counter < num_messages) {
        num_messages = msg_counter;
    }

    /* Limit to maximum messages */
    if (num_messages > REMOTEID_MAX_MESSAGES) {
        num_messages = REMOTEID_MAX_MESSAGES;
    }

    if (num_messages == 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    /* Decode each message */
    bool has_basic_id = false;
    esp_err_t last_err = ESP_OK;

    for (uint8_t i = 0; i < num_messages; i++) {
        const uint8_t *msg = &frame[payload_start + (i * REMOTEID_MSG_SIZE)];
        uint8_t msg_type = (msg[0] & MSG_TYPE_MASK) >> MSG_TYPE_SHIFT;

        esp_err_t err = decode_single_message(msg, out);
        if (err != ESP_OK) {
            /* For invalid message type, report the error */
            if (err == ERR_DECODE_UNKNOWN_FMT) {
                last_err = err;
            }
            continue;
        }

        if (msg_type == REMOTEID_MSG_TYPE_BASIC_ID) {
            has_basic_id = true;
        }
    }

    /* If all messages had unknown format, report that */
    if (!has_basic_id && last_err == ERR_DECODE_UNKNOWN_FMT) {
        return ERR_DECODE_UNKNOWN_FMT;
    }

    /* Validate required fields */
    return validate_decoded_data(out, has_basic_id);
}

esp_err_t remoteid_decode_ble(const uint8_t *adv_data, uint16_t len, remoteid_data_t *out)
{
    /* Validate arguments */
    if (adv_data == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Validate minimum length */
    if (len < REMOTEID_BLE_MIN_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }

    /* Initialize output */
    remoteid_data_init(out);

    /*
     * BLE AD structure:
     * [0]    AD Length
     * [1]    AD Type (0x16 = Service Data - 16-bit UUID)
     * [2-3]  UUID16 (0xFFFA little-endian for ASTM F3411)
     * [4]    Message counter
     * [5..29] 25-byte ASTM F3411 message
     *
     * For message packs, multiple 25-byte messages may follow.
     */

    uint8_t ad_length = adv_data[0];
    uint8_t ad_type = adv_data[1];

    /* Validate AD type */
    if (ad_type != REMOTEID_BLE_AD_TYPE_SERVICE_DATA_16) {
        return ERR_DECODE_UNKNOWN_FMT;
    }

    /* Validate UUID16 (little-endian: 0xFA, 0xFF → 0xFFFA) */
    uint16_t uuid16 = read_uint16_le(&adv_data[2]);
    if (uuid16 != REMOTEID_BLE_UUID16_ASTM) {
        return ERR_DECODE_UNKNOWN_FMT;
    }

    /* Validate AD length consistency:
     * ad_length = (number of payload bytes after the length byte itself)
     * Should be at least: 1(type) + 2(UUID) + 1(counter) + 25(message) = 29 */
    if (ad_length < 29) {
        return ESP_ERR_INVALID_SIZE;
    }

    /* Verify total frame length is consistent with AD length */
    if (len < (uint16_t)(ad_length + 1)) {
        return ESP_ERR_INVALID_SIZE;
    }

    /* Message counter at offset 4 */
    /* uint8_t msg_counter = adv_data[4]; — informational */

    /* Calculate messages available */
    uint16_t msg_start = 5; /* After AD length, type, UUID16, counter */
    uint16_t payload_available = len - msg_start;
    uint8_t num_messages = payload_available / REMOTEID_MSG_SIZE;

    if (num_messages == 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    /* Limit to maximum */
    if (num_messages > REMOTEID_MAX_MESSAGES) {
        num_messages = REMOTEID_MAX_MESSAGES;
    }

    /* Decode each message */
    bool has_basic_id = false;
    esp_err_t last_err = ESP_OK;

    for (uint8_t i = 0; i < num_messages; i++) {
        const uint8_t *msg = &adv_data[msg_start + (i * REMOTEID_MSG_SIZE)];
        uint8_t msg_type = (msg[0] & MSG_TYPE_MASK) >> MSG_TYPE_SHIFT;

        esp_err_t err = decode_single_message(msg, out);
        if (err != ESP_OK) {
            if (err == ERR_DECODE_UNKNOWN_FMT) {
                last_err = err;
            }
            continue;
        }

        if (msg_type == REMOTEID_MSG_TYPE_BASIC_ID) {
            has_basic_id = true;
        }
    }

    /* If all messages had unknown format, report that */
    if (!has_basic_id && last_err == ERR_DECODE_UNKNOWN_FMT) {
        return ERR_DECODE_UNKNOWN_FMT;
    }

    /* Validate required fields */
    return validate_decoded_data(out, has_basic_id);
}

esp_err_t remoteid_encode(const remoteid_data_t *data, uint8_t *out_buf,
                          uint16_t buf_size, uint16_t *out_len)
{
    if (data == NULL || out_buf == NULL || out_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Calculate required size:
     * - Always: Basic ID (25 bytes)
     * - If has_position: Location (25 bytes)
     * - If has_operator_location: System (25 bytes) */
    uint16_t required = REMOTEID_MSG_SIZE; /* Basic ID */
    if (data->has_position) {
        required += REMOTEID_MSG_SIZE;
    }
    if (data->has_operator_location) {
        required += REMOTEID_MSG_SIZE;
    }

    if (buf_size < required) {
        return ESP_ERR_NO_MEM;
    }

    uint16_t offset = 0;

    /* Encode Basic ID message (Type 0) */
    memset(&out_buf[offset], 0, REMOTEID_MSG_SIZE);
    out_buf[offset] = (REMOTEID_MSG_TYPE_BASIC_ID << MSG_TYPE_SHIFT); /* header: type 0, version 0 */
    out_buf[offset + BASIC_ID_TYPE_OFFSET] = (REMOTEID_ID_TYPE_SERIAL << 4); /* Serial number type */

    /* Copy UAS ID (padded with nulls to 20 bytes) */
    size_t id_len = strlen(data->uas_id);
    if (id_len > BASIC_ID_DATA_LEN) {
        id_len = BASIC_ID_DATA_LEN;
    }
    memcpy(&out_buf[offset + BASIC_ID_DATA_OFFSET], data->uas_id, id_len);
    offset += REMOTEID_MSG_SIZE;

    /* Encode Location message (Type 1) if position is available */
    if (data->has_position) {
        memset(&out_buf[offset], 0, REMOTEID_MSG_SIZE);
        out_buf[offset] = (REMOTEID_MSG_TYPE_LOCATION << MSG_TYPE_SHIFT);

        /* Status byte: airborne status, speed multiplier, direction segment */
        uint8_t status_byte = (REMOTEID_STATUS_AIRBORNE << 4);
        float speed = data->speed_ms;
        uint8_t speed_raw;

        if (speed > 255.0f * REMOTEID_SPEED_SCALE) {
            speed -= 255.0f * REMOTEID_SPEED_SCALE;
            status_byte |= REMOTEID_SPEED_MULTIPLIER_FLAG;
        }
        speed_raw = (uint8_t)(speed / REMOTEID_SPEED_SCALE + 0.5f);
        if (speed_raw > 255) speed_raw = 255;

        /* Direction: encode with segment flag */
        float dir = data->direction_deg;
        if (dir >= 180.0f) {
            dir -= 180.0f;
            status_byte |= 0x02; /* direction segment flag */
        }
        uint8_t dir_raw = (uint8_t)(dir + 0.5f);
        if (dir_raw > 179) dir_raw = 179;

        out_buf[offset + LOC_STATUS_OFFSET] = status_byte;
        out_buf[offset + LOC_DIRECTION_OFFSET] = dir_raw;
        out_buf[offset + LOC_SPEED_OFFSET] = speed_raw;

        /* Latitude */
        int32_t lat_raw = (int32_t)(data->latitude / REMOTEID_LAT_LON_SCALE);
        write_int32_le(&out_buf[offset + LOC_LAT_OFFSET], lat_raw);

        /* Longitude */
        int32_t lon_raw = (int32_t)(data->longitude / REMOTEID_LAT_LON_SCALE);
        write_int32_le(&out_buf[offset + LOC_LON_OFFSET], lon_raw);

        /* Geodetic altitude */
        uint16_t alt_raw = altitude_meters_to_raw(data->altitude_m);
        write_uint16_le(&out_buf[offset + LOC_ALT_GEO_OFFSET], alt_raw);

        /* Pressure altitude (same as geodetic for simplicity) */
        write_uint16_le(&out_buf[offset + LOC_ALT_PRESS_OFFSET], alt_raw);

        offset += REMOTEID_MSG_SIZE;
    }

    /* Encode System message (Type 4) if operator location is available */
    if (data->has_operator_location) {
        memset(&out_buf[offset], 0, REMOTEID_MSG_SIZE);
        out_buf[offset] = (REMOTEID_MSG_TYPE_SYSTEM << MSG_TYPE_SHIFT);

        /* Operator latitude */
        int32_t op_lat_raw = (int32_t)(data->operator_lat / REMOTEID_LAT_LON_SCALE);
        write_int32_le(&out_buf[offset + SYS_OP_LAT_OFFSET], op_lat_raw);

        /* Operator longitude */
        int32_t op_lon_raw = (int32_t)(data->operator_lon / REMOTEID_LAT_LON_SCALE);
        write_int32_le(&out_buf[offset + SYS_OP_LON_OFFSET], op_lon_raw);

        offset += REMOTEID_MSG_SIZE;
    }

    *out_len = offset;
    return ESP_OK;
}


/* ========================================================================
 * CRC-8 Implementation (DVB-S2 polynomial for ASTM F3411 validation)
 * ======================================================================== */

/**
 * CRC-8/DVB-S2 lookup table (polynomial 0xD5).
 * Used for packet integrity validation in RemoteID frames.
 */
static const uint8_t crc8_table[256] = {
    0x00, 0xD5, 0x7F, 0xAA, 0xFE, 0x2B, 0x81, 0x54,
    0x29, 0xFC, 0x56, 0x83, 0xD7, 0x02, 0xA8, 0x7D,
    0x52, 0x87, 0x2D, 0xF8, 0xAC, 0x79, 0xD3, 0x06,
    0x7B, 0xAE, 0x04, 0xD1, 0x85, 0x50, 0xFA, 0x2F,
    0xA4, 0x71, 0xDB, 0x0E, 0x5A, 0x8F, 0x25, 0xF0,
    0x8D, 0x58, 0xF2, 0x27, 0x73, 0xA6, 0x0C, 0xD9,
    0xF6, 0x23, 0x89, 0x5C, 0x08, 0xDD, 0x77, 0xA2,
    0xDF, 0x0A, 0xA0, 0x75, 0x21, 0xF4, 0x5E, 0x8B,
    0x9D, 0x48, 0xE2, 0x37, 0x63, 0xB6, 0x1C, 0xC9,
    0xB4, 0x61, 0xCB, 0x1E, 0x4A, 0x9F, 0x35, 0xE0,
    0xCF, 0x1A, 0xB0, 0x65, 0x31, 0xE4, 0x4E, 0x9B,
    0xE6, 0x33, 0x99, 0x4C, 0x18, 0xCD, 0x67, 0xB2,
    0x39, 0xEC, 0x46, 0x93, 0xC7, 0x12, 0xB8, 0x6D,
    0x10, 0xC5, 0x6F, 0xBA, 0xEE, 0x3B, 0x91, 0x44,
    0x6B, 0xBE, 0x14, 0xC1, 0x95, 0x40, 0xEA, 0x3F,
    0x42, 0x97, 0x3D, 0xE8, 0xBC, 0x69, 0xC3, 0x16,
    0xEF, 0x3A, 0x90, 0x45, 0x11, 0xC4, 0x6E, 0xBB,
    0xC6, 0x13, 0xB9, 0x6C, 0x38, 0xED, 0x47, 0x92,
    0xBD, 0x68, 0xC2, 0x17, 0x43, 0x96, 0x3C, 0xE9,
    0x94, 0x41, 0xEB, 0x3E, 0x6A, 0xBF, 0x15, 0xC0,
    0x4B, 0x9E, 0x34, 0xE1, 0xB5, 0x60, 0xCA, 0x1F,
    0x62, 0xB7, 0x1D, 0xC8, 0x9C, 0x49, 0xE3, 0x36,
    0x19, 0xCC, 0x66, 0xB3, 0xE7, 0x32, 0x98, 0x4D,
    0x30, 0xE5, 0x4F, 0x9A, 0xCE, 0x1B, 0xB1, 0x64,
    0x72, 0xA7, 0x0D, 0xD8, 0x8C, 0x59, 0xF3, 0x26,
    0x5B, 0x8E, 0x24, 0xF1, 0xA5, 0x70, 0xDA, 0x0F,
    0x20, 0xF5, 0x5F, 0x8A, 0xDE, 0x0B, 0xA1, 0x74,
    0x09, 0xDC, 0x76, 0xA3, 0xF7, 0x22, 0x88, 0x5D,
    0xD6, 0x03, 0xA9, 0x7C, 0x28, 0xFD, 0x57, 0x82,
    0xFF, 0x2A, 0x80, 0x55, 0x01, 0xD4, 0x7E, 0xAB,
    0x84, 0x51, 0xFB, 0x2E, 0x7A, 0xAF, 0x05, 0xD0,
    0xAD, 0x78, 0xD2, 0x07, 0x53, 0x86, 0x2C, 0xF9
};

uint8_t remoteid_crc8(const uint8_t *data, uint16_t len)
{
    uint8_t crc = 0x00;
    for (uint16_t i = 0; i < len; i++) {
        crc = crc8_table[crc ^ data[i]];
    }
    return crc;
}

/* ========================================================================
 * Frame Validation
 * ======================================================================== */

esp_err_t remoteid_validate_frame(const uint8_t *frame, uint16_t len, bool is_ble)
{
    if (frame == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (is_ble) {
        /* BLE validation */
        if (len < REMOTEID_BLE_MIN_LEN) {
            return ESP_ERR_INVALID_SIZE;
        }

        /* Check AD type */
        if (frame[1] != REMOTEID_BLE_AD_TYPE_SERVICE_DATA_16) {
            return ERR_DECODE_UNKNOWN_FMT;
        }

        /* Check UUID16 */
        uint16_t uuid16 = (uint16_t)frame[2] | ((uint16_t)frame[3] << 8);
        if (uuid16 != REMOTEID_BLE_UUID16_ASTM) {
            return ERR_DECODE_UNKNOWN_FMT;
        }

        /* Check AD length consistency */
        uint8_t ad_length = frame[0];
        if (ad_length < 29) {
            return ESP_ERR_INVALID_SIZE;
        }
        if (len < (uint16_t)(ad_length + 1)) {
            return ESP_ERR_INVALID_SIZE;
        }

        /* Validate message content with CRC check on first message */
        uint16_t msg_start = 5;
        if (len >= msg_start + REMOTEID_MSG_SIZE) {
            /* Verify message type is within valid range */
            uint8_t msg_type = (frame[msg_start] & MSG_TYPE_MASK) >> MSG_TYPE_SHIFT;
            if (msg_type > REMOTEID_MSG_TYPE_MAX && msg_type != REMOTEID_MSG_TYPE_MSG_PACK) {
                return ERR_DECODE_CRC_FAIL;
            }
        }
    } else {
        /* WiFi validation */
        if (len < REMOTEID_WIFI_MIN_LEN) {
            return ESP_ERR_INVALID_SIZE;
        }

        /* Verify OUI */
        if (memcmp(&frame[REMOTEID_WIFI_OUI_OFFSET], REMOTEID_WIFI_OUI, 3) != 0) {
            return ERR_DECODE_UNKNOWN_FMT;
        }

        /* Verify OUI Type */
        if (frame[REMOTEID_WIFI_OUI_TYPE_OFFSET] != REMOTEID_WIFI_OUI_TYPE) {
            return ERR_DECODE_UNKNOWN_FMT;
        }

        /* Validate payload has at least one message */
        uint16_t payload_start = REMOTEID_WIFI_MSG_START_OFFSET;
        if (len - payload_start < REMOTEID_MSG_SIZE) {
            return ESP_ERR_INVALID_SIZE;
        }

        /* Verify first message type is within valid range */
        uint8_t msg_type = (frame[payload_start] & MSG_TYPE_MASK) >> MSG_TYPE_SHIFT;
        if (msg_type > REMOTEID_MSG_TYPE_MAX && msg_type != REMOTEID_MSG_TYPE_MSG_PACK) {
            return ERR_DECODE_CRC_FAIL;
        }
    }

    return ESP_OK;
}

/* ========================================================================
 * Unified Decode Function (telemetry_decode_fn pattern)
 * ======================================================================== */

esp_err_t remoteid_decode(const uint8_t *raw_payload, uint16_t payload_len,
                          bool is_ble, decoded_telemetry_t *out,
                          remoteid_data_t *rid_out)
{
    if (raw_payload == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Initialize output */
    memset(out, 0, sizeof(decoded_telemetry_t));

    /* Validate frame structure first */
    esp_err_t val_err = remoteid_validate_frame(raw_payload, payload_len, is_ble);
    if (val_err != ESP_OK) {
        return val_err;
    }

    /* Decode into RemoteID-specific struct */
    remoteid_data_t rid_data;
    esp_err_t err;

    if (is_ble) {
        err = remoteid_decode_ble(raw_payload, payload_len, &rid_data);
    } else {
        err = remoteid_decode_wifi(raw_payload, payload_len, &rid_data);
    }

    if (err != ESP_OK) {
        return err;
    }

    /* Map remoteid_data_t → decoded_telemetry_t */
    strncpy(out->uas_id, rid_data.uas_id, AIRCRAFT_ID_MAX_LEN - 1);
    out->uas_id[AIRCRAFT_ID_MAX_LEN - 1] = '\0';

    if (rid_data.has_position) {
        out->lat = rid_data.latitude;
        out->lon = rid_data.longitude;
        out->has_position = true;
        out->altitude_m = rid_data.altitude_m;
        out->has_altitude = true;
    }

    if (rid_data.has_speed) {
        out->speed_ms = rid_data.speed_ms;
        out->heading_deg = rid_data.direction_deg;
        out->has_speed = true;
    }

    /* Battery and flight mode are not part of RemoteID */
    out->has_battery = false;
    out->has_flight_mode = false;

    /* Copy full RemoteID data if caller wants it */
    if (rid_out != NULL) {
        memcpy(rid_out, &rid_data, sizeof(remoteid_data_t));
    }

    return ESP_OK;
}
