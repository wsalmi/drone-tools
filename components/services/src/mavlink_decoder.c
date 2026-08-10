/**
 * @file mavlink_decoder.c
 * @brief MAVLink v1/v2 frame decoder implementation.
 *
 * Implements MAVLink frame validation and telemetry extraction for
 * GLOBAL_POSITION_INT, BATTERY_STATUS, HEARTBEAT, and HOME_POSITION messages.
 *
 * MAVLink Frame Structures:
 * V1 (STX=0xFE): STX(1) + Len(1) + Seq(1) + SysID(1) + CompID(1) + MsgID(1) + Payload(N) + CRC(2)
 * V2 (STX=0xFD): STX(1) + Len(1) + IncompatFlags(1) + CompatFlags(1) + Seq(1) +
 *                 SysID(1) + CompID(1) + MsgID(3) + Payload(N) + CRC(2) + [Signature(13)]
 *
 * Validates: Requirements 8.1, 6.2
 */

#include "mavlink_decoder.h"
#include "error_codes.h"
#include <string.h>
#include <math.h>

/* ========================================================================
 * CRC Extra bytes per message ID (MAVLink standard seeds)
 * ======================================================================== */

/**
 * CRC_EXTRA values are message-specific seeds added to the CRC calculation
 * to ensure message format compatibility between sender and receiver.
 * These values come from the MAVLink message definitions (common.xml).
 */
#define CRC_EXTRA_HEARTBEAT             50
#define CRC_EXTRA_GLOBAL_POSITION_INT   104
#define CRC_EXTRA_BATTERY_STATUS        154
#define CRC_EXTRA_HOME_POSITION         104

/* ========================================================================
 * Internal helper: Get CRC extra byte for a message ID
 * ======================================================================== */

/**
 * @brief Get the CRC extra byte for a given message ID.
 * @param msg_id MAVLink message ID.
 * @param[out] crc_extra Pointer to store the CRC extra byte.
 * @return true if the message ID is known, false otherwise.
 */
static bool get_crc_extra(uint32_t msg_id, uint8_t *crc_extra)
{
    switch (msg_id) {
    case MAVLINK_MSG_ID_HEARTBEAT:
        *crc_extra = CRC_EXTRA_HEARTBEAT;
        return true;
    case MAVLINK_MSG_ID_GLOBAL_POSITION_INT:
        *crc_extra = CRC_EXTRA_GLOBAL_POSITION_INT;
        return true;
    case MAVLINK_MSG_ID_BATTERY_STATUS:
        *crc_extra = CRC_EXTRA_BATTERY_STATUS;
        return true;
    case MAVLINK_MSG_ID_HOME_POSITION:
        *crc_extra = CRC_EXTRA_HOME_POSITION;
        return true;
    default:
        return false;
    }
}

/* ========================================================================
 * MAVLink X.25 CRC (CRC-16/MCRF4XX)
 * ======================================================================== */

static inline void crc_accumulate(uint8_t byte, uint16_t *crc)
{
    uint8_t tmp;
    tmp = byte ^ (uint8_t)(*crc & 0xFF);
    tmp ^= (tmp << 4);
    *crc = (*crc >> 8) ^ ((uint16_t)tmp << 8) ^ ((uint16_t)tmp << 3) ^ ((uint16_t)tmp >> 4);
}

uint16_t mavlink_crc_calculate(const uint8_t *data, uint16_t len, uint8_t crc_extra)
{
    uint16_t crc = 0xFFFF;

    for (uint16_t i = 0; i < len; i++) {
        crc_accumulate(data[i], &crc);
    }

    /* Accumulate the CRC extra byte (message-specific seed) */
    crc_accumulate(crc_extra, &crc);

    return crc;
}

/* ========================================================================
 * Internal: Parse frame header and extract metadata
 * ======================================================================== */

typedef struct {
    uint8_t version;        /* 1 or 2 */
    uint8_t payload_len;
    uint8_t seq;
    uint8_t sys_id;
    uint8_t comp_id;
    uint32_t msg_id;
    uint16_t header_len;    /* Total header bytes (before payload) */
    const uint8_t *payload; /* Pointer to payload start */
    uint16_t frame_len;     /* Total expected frame length */
    bool has_signature;
} mavlink_frame_info_t;

/**
 * @brief Parse the MAVLink frame header.
 * @param[in]  data Raw frame data.
 * @param[in]  len  Length of data buffer.
 * @param[out] info Parsed frame information.
 * @return ESP_OK on success, error code otherwise.
 */
static esp_err_t parse_frame_header(const uint8_t *data, uint16_t len,
                                    mavlink_frame_info_t *info)
{
    if (len < 2) {
        return ERR_DECODE_INCOMPLETE;
    }

    uint8_t stx = data[0];
    info->payload_len = data[1];

    if (stx == MAVLINK_V1_STX) {
        info->version = 1;
        info->header_len = MAVLINK_V1_HEADER_LEN;

        uint16_t expected_len = MAVLINK_V1_HEADER_LEN + info->payload_len + MAVLINK_CRC_LEN;
        if (len < expected_len) {
            return ERR_DECODE_INCOMPLETE;
        }

        info->seq = data[2];
        info->sys_id = data[3];
        info->comp_id = data[4];
        info->msg_id = data[5];
        info->payload = &data[MAVLINK_V1_HEADER_LEN];
        info->frame_len = expected_len;
        info->has_signature = false;

    } else if (stx == MAVLINK_V2_STX) {
        info->version = 2;
        info->header_len = MAVLINK_V2_HEADER_LEN;

        /* Check incompat_flags for signature presence */
        if (len < 3) {
            return ERR_DECODE_INCOMPLETE;
        }
        uint8_t incompat_flags = data[2];
        info->has_signature = (incompat_flags & 0x01) != 0;

        uint16_t expected_len = MAVLINK_V2_HEADER_LEN + info->payload_len + MAVLINK_CRC_LEN;
        if (info->has_signature) {
            expected_len += MAVLINK_V2_SIGNATURE_LEN;
        }

        if (len < expected_len) {
            return ERR_DECODE_INCOMPLETE;
        }

        info->seq = data[4];
        info->sys_id = data[5];
        info->comp_id = data[6];
        /* MsgID is 3 bytes in v2, little-endian */
        info->msg_id = (uint32_t)data[7] |
                       ((uint32_t)data[8] << 8) |
                       ((uint32_t)data[9] << 16);
        info->payload = &data[MAVLINK_V2_HEADER_LEN];
        info->frame_len = expected_len;

    } else {
        return ERR_DECODE_UNKNOWN_FMT;
    }

    return ESP_OK;
}

/* ========================================================================
 * Internal: Validate CRC
 * ======================================================================== */

/**
 * @brief Validate the CRC of a MAVLink frame.
 * @param[in] data  Raw frame data.
 * @param[in] info  Parsed frame info.
 * @return true if CRC is valid, false otherwise.
 */
static bool validate_crc(const uint8_t *data, const mavlink_frame_info_t *info)
{
    uint8_t crc_extra;
    if (!get_crc_extra(info->msg_id, &crc_extra)) {
        /* Unknown message — cannot validate CRC without crc_extra */
        return false;
    }

    /* CRC covers bytes from index 1 (after STX) through end of payload */
    const uint8_t *crc_data = &data[1];
    uint16_t crc_data_len = (info->header_len - 1) + info->payload_len;

    uint16_t computed_crc = mavlink_crc_calculate(crc_data, crc_data_len, crc_extra);

    /* CRC is stored little-endian after the payload */
    uint16_t crc_offset = info->header_len + info->payload_len;
    uint16_t received_crc = (uint16_t)data[crc_offset] |
                            ((uint16_t)data[crc_offset + 1] << 8);

    return computed_crc == received_crc;
}

/* ========================================================================
 * Internal: Decode individual message types
 * ======================================================================== */

/**
 * @brief Read a little-endian int32 from a byte buffer.
 */
static inline int32_t read_le_int32(const uint8_t *buf)
{
    return (int32_t)((uint32_t)buf[0] |
                     ((uint32_t)buf[1] << 8) |
                     ((uint32_t)buf[2] << 16) |
                     ((uint32_t)buf[3] << 24));
}

/**
 * @brief Read a little-endian uint32 from a byte buffer.
 */
static inline uint32_t read_le_uint32(const uint8_t *buf)
{
    return (uint32_t)buf[0] |
           ((uint32_t)buf[1] << 8) |
           ((uint32_t)buf[2] << 16) |
           ((uint32_t)buf[3] << 24);
}

/**
 * @brief Read a little-endian int16 from a byte buffer.
 */
static inline int16_t read_le_int16(const uint8_t *buf)
{
    return (int16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
}

/**
 * @brief Read a little-endian uint16 from a byte buffer.
 */
static inline uint16_t read_le_uint16(const uint8_t *buf)
{
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

/**
 * @brief Decode GLOBAL_POSITION_INT (msg_id=33).
 *
 * Payload layout (28 bytes):
 *   time_boot_ms  (uint32, offset 0)
 *   lat           (int32,  offset 4)  — degE7
 *   lon           (int32,  offset 8)  — degE7
 *   alt           (int32,  offset 12) — mm (MSL)
 *   relative_alt  (int32,  offset 16) — mm (AGL)
 *   vx            (int16,  offset 20) — cm/s
 *   vy            (int16,  offset 22) — cm/s
 *   vz            (int16,  offset 24) — cm/s
 *   hdg           (uint16, offset 26) — cdeg (0..35999)
 */
static void decode_global_position_int(const uint8_t *payload, uint8_t payload_len,
                                       decoded_telemetry_t *out)
{
    if (payload_len < 28) {
        return;
    }

    int32_t lat_e7 = read_le_int32(&payload[4]);
    int32_t lon_e7 = read_le_int32(&payload[8]);
    int32_t alt_mm = read_le_int32(&payload[12]);
    int16_t vx = read_le_int16(&payload[20]);
    int16_t vy = read_le_int16(&payload[22]);
    uint16_t hdg_cdeg = read_le_uint16(&payload[26]);

    out->lat = (double)lat_e7 / 1e7;
    out->lon = (double)lon_e7 / 1e7;
    out->has_position = true;

    out->altitude_m = (float)alt_mm / 1000.0f;
    out->has_altitude = true;

    /* Speed: sqrt(vx² + vy²) in cm/s → m/s */
    float vx_ms = (float)vx / 100.0f;
    float vy_ms = (float)vy / 100.0f;
    out->speed_ms = sqrtf(vx_ms * vx_ms + vy_ms * vy_ms);
    out->has_speed = true;

    /* Heading: centidegrees → degrees */
    out->heading_deg = (float)hdg_cdeg / 100.0f;
}

/**
 * @brief Decode BATTERY_STATUS (msg_id=147).
 *
 * Payload layout (relevant fields):
 *   current_consumed (int32,   offset 0)  — mAh
 *   energy_consumed  (int32,   offset 4)  — hJ
 *   temperature      (int16,   offset 8)  — cdegC
 *   voltages[10]     (uint16,  offset 10) — mV (10 cells, UINT16_MAX=invalid)
 *   current_battery  (int16,   offset 30) — cA (10*mA)
 *   id               (uint8,   offset 32)
 *   battery_function (uint8,   offset 33)
 *   type             (uint8,   offset 34)
 *   battery_remaining(int8,    offset 35) — % (-1 if unknown)
 */
static void decode_battery_status(const uint8_t *payload, uint8_t payload_len,
                                  decoded_telemetry_t *out)
{
    if (payload_len < 36) {
        return;
    }

    /* Sum valid cell voltages to get total voltage */
    float total_voltage_mv = 0.0f;
    int valid_cells = 0;
    for (int i = 0; i < 10; i++) {
        uint16_t cell_mv = read_le_uint16(&payload[10 + i * 2]);
        if (cell_mv != 0xFFFF && cell_mv != 0) {
            total_voltage_mv += (float)cell_mv;
            valid_cells++;
        }
    }

    if (valid_cells > 0) {
        out->battery_voltage = total_voltage_mv / 1000.0f; /* mV → V */
    }

    /* Battery remaining percentage */
    int8_t remaining = (int8_t)payload[35];
    if (remaining >= 0 && remaining <= 100) {
        out->battery_pct = (float)remaining;
        out->has_battery = true;
    }
}

/**
 * @brief Decode HEARTBEAT (msg_id=0).
 *
 * Payload layout (9 bytes):
 *   custom_mode  (uint32, offset 0)
 *   type         (uint8,  offset 4)  — MAV_TYPE
 *   autopilot    (uint8,  offset 5)  — MAV_AUTOPILOT
 *   base_mode    (uint8,  offset 6)  — MAV_MODE_FLAG
 *   system_status(uint8,  offset 7)  — MAV_STATE
 *   mavlink_version(uint8,offset 8)
 */
static void decode_heartbeat(const uint8_t *payload, uint8_t payload_len,
                             decoded_telemetry_t *out)
{
    if (payload_len < 9) {
        return;
    }

    uint32_t custom_mode = read_le_uint32(&payload[0]);
    uint8_t base_mode = payload[6];

    /* Encode flight mode as a combination of base_mode flags + custom_mode */
    /* Use base_mode directly as the flight_mode byte for simplicity */
    (void)custom_mode;
    out->flight_mode = base_mode;
    out->has_flight_mode = true;
}

/**
 * @brief Decode HOME_POSITION (msg_id=242).
 *
 * Payload layout (relevant fields):
 *   latitude   (int32, offset 0)  — degE7
 *   longitude  (int32, offset 4)  — degE7
 *   altitude   (int32, offset 8)  — mm (MSL)
 *   ... (other fields: x, y, z, q[4], approach...)
 *
 * HOME_POSITION provides the home/launch point. We decode it as a position
 * update (useful for pilot locator).
 */
static void decode_home_position(const uint8_t *payload, uint8_t payload_len,
                                 decoded_telemetry_t *out)
{
    if (payload_len < 12) {
        return;
    }

    int32_t lat_e7 = read_le_int32(&payload[0]);
    int32_t lon_e7 = read_le_int32(&payload[4]);
    int32_t alt_mm = read_le_int32(&payload[8]);

    out->lat = (double)lat_e7 / 1e7;
    out->lon = (double)lon_e7 / 1e7;
    out->has_position = true;

    out->altitude_m = (float)alt_mm / 1000.0f;
    out->has_altitude = true;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

esp_err_t mavlink_decode(const uint8_t *data, uint16_t len, decoded_telemetry_t *out)
{
    if (data == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (len < MAVLINK_V1_HEADER_LEN + MAVLINK_CRC_LEN) {
        return ERR_DECODE_INCOMPLETE;
    }

    /* Initialize output struct */
    memset(out, 0, sizeof(decoded_telemetry_t));

    /* Parse frame header */
    mavlink_frame_info_t info;
    esp_err_t err = parse_frame_header(data, len, &info);
    if (err != ESP_OK) {
        return err;
    }

    /* Validate CRC */
    if (!validate_crc(data, &info)) {
        return ERR_DECODE_CRC_FAIL;
    }

    /* Decode message payload based on message ID */
    switch (info.msg_id) {
    case MAVLINK_MSG_ID_GLOBAL_POSITION_INT:
        decode_global_position_int(info.payload, info.payload_len, out);
        break;
    case MAVLINK_MSG_ID_BATTERY_STATUS:
        decode_battery_status(info.payload, info.payload_len, out);
        break;
    case MAVLINK_MSG_ID_HEARTBEAT:
        decode_heartbeat(info.payload, info.payload_len, out);
        break;
    case MAVLINK_MSG_ID_HOME_POSITION:
        decode_home_position(info.payload, info.payload_len, out);
        break;
    default:
        return ERR_DECODE_UNKNOWN_FMT;
    }

    return ESP_OK;
}

bool mavlink_is_valid_frame(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len < MAVLINK_V1_HEADER_LEN + MAVLINK_CRC_LEN) {
        return false;
    }

    mavlink_frame_info_t info;
    esp_err_t err = parse_frame_header(data, len, &info);
    if (err != ESP_OK) {
        return false;
    }

    return validate_crc(data, &info);
}
