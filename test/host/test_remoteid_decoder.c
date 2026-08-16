/**
 * @file test_remoteid_decoder.c
 * @brief Unit tests for the RemoteID decoder (WiFi NAN/Beacon + BLE).
 *
 * Tests ASTM F3411 RemoteID decoding for WiFi and BLE frames,
 * including valid packets, invalid packets, and edge cases.
 *
 * Validates: Requirements 1.1, 1.2, 1.3, 1.4, 6.1
 */

#include "unity.h"
#include "remoteid_decoder.h"
#include "error_codes.h"
#include <string.h>
#include <math.h>

/* ========================================================================
 * Test Helpers
 * ======================================================================== */

/**
 * @brief Build a minimal valid WiFi RemoteID frame with Basic ID message.
 */
static void build_wifi_basic_id_frame(uint8_t *frame, uint16_t *len,
                                       const char *uas_id)
{
    uint16_t offset = 0;

    /* OUI: FA:0B:BC */
    frame[offset++] = 0xFA;
    frame[offset++] = 0x0B;
    frame[offset++] = 0xBC;
    /* OUI Type: 0x0D */
    frame[offset++] = 0x0D;
    /* Message counter: 1 */
    frame[offset++] = 0x01;

    /* Basic ID message (25 bytes) — zero-fill first */
    memset(&frame[offset], 0, REMOTEID_MSG_SIZE);
    frame[offset] = (REMOTEID_MSG_TYPE_BASIC_ID << 4); /* header */
    frame[offset + 1] = (REMOTEID_ID_TYPE_SERIAL << 4); /* ID type */

    /* UAS ID (20 bytes, padded with zeros) */
    size_t id_len = strlen(uas_id);
    if (id_len > 20) id_len = 20;
    memcpy(&frame[offset + 2], uas_id, id_len);
    /* Compute CRC-8 over protected region (first 24 bytes of message) */
    frame[offset + REMOTEID_CRC_OFFSET] = remoteid_crc8(&frame[offset], REMOTEID_CRC_PROTECTED_LEN);
    offset += REMOTEID_MSG_SIZE;

    *len = offset;
}
static void build_wifi_with_location(uint8_t *frame, uint16_t *len,
                                      const char *uas_id,
                                      double lat, double lon,
                                      float alt_m, float speed,
                                      float direction)
{
    uint16_t offset = 0;

    /* WiFi header */
    frame[offset++] = 0xFA;
    frame[offset++] = 0x0B;
    frame[offset++] = 0xBC;
    frame[offset++] = 0x0D;
    frame[offset++] = 0x02; /* 2 messages */

    /* Basic ID message (25 bytes) */
    memset(&frame[offset], 0, REMOTEID_MSG_SIZE);
    frame[offset] = (REMOTEID_MSG_TYPE_BASIC_ID << 4);
    frame[offset + 1] = (REMOTEID_ID_TYPE_SERIAL << 4);
    size_t id_len = strlen(uas_id);
    if (id_len > 20) id_len = 20;
    memcpy(&frame[offset + 2], uas_id, id_len);
    frame[offset + REMOTEID_CRC_OFFSET] = remoteid_crc8(&frame[offset], REMOTEID_CRC_PROTECTED_LEN);
    offset += REMOTEID_MSG_SIZE;

    /* Location message (25 bytes) */
    memset(&frame[offset], 0, REMOTEID_MSG_SIZE);
    frame[offset] = (REMOTEID_MSG_TYPE_LOCATION << 4);

    /* Status byte: airborne, compute speed mult + direction segment */
    uint8_t status_byte = (REMOTEID_STATUS_AIRBORNE << 4);
    float enc_speed = speed;
    if (enc_speed > 255.0f * 0.25f) {
        status_byte |= 0x01;
        enc_speed -= 255.0f * 0.25f;
    }
    float enc_dir = direction;
    if (enc_dir >= 180.0f) {
        status_byte |= 0x02;
        enc_dir -= 180.0f;
    }
    frame[offset + 1] = status_byte;
    frame[offset + 2] = (uint8_t)(enc_dir + 0.5f); /* direction */
    frame[offset + 3] = (uint8_t)(enc_speed / 0.25f + 0.5f); /* speed */

    /* Latitude (int32, little-endian) */
    int32_t lat_raw = (int32_t)(lat * 1e7);
    frame[offset + 5] = (uint8_t)(lat_raw & 0xFF);
    frame[offset + 6] = (uint8_t)((lat_raw >> 8) & 0xFF);
    frame[offset + 7] = (uint8_t)((lat_raw >> 16) & 0xFF);
    frame[offset + 8] = (uint8_t)((lat_raw >> 24) & 0xFF);

    /* Longitude (int32, little-endian) */
    int32_t lon_raw = (int32_t)(lon * 1e7);
    frame[offset + 9] = (uint8_t)(lon_raw & 0xFF);
    frame[offset + 10] = (uint8_t)((lon_raw >> 8) & 0xFF);
    frame[offset + 11] = (uint8_t)((lon_raw >> 16) & 0xFF);
    frame[offset + 12] = (uint8_t)((lon_raw >> 24) & 0xFF);

    /* Geodetic altitude (uint16, alt = raw * 0.5 - 1000) */
    uint16_t alt_raw = (uint16_t)((alt_m + 1000.0f) / 0.5f + 0.5f);
    frame[offset + 15] = (uint8_t)(alt_raw & 0xFF);
    frame[offset + 16] = (uint8_t)((alt_raw >> 8) & 0xFF);

    frame[offset + REMOTEID_CRC_OFFSET] = remoteid_crc8(&frame[offset], REMOTEID_CRC_PROTECTED_LEN);
    offset += REMOTEID_MSG_SIZE;
    *len = offset;
}

/**
 * @brief Build a BLE RemoteID advertisement with Basic ID message.
 */
static void build_ble_basic_id(uint8_t *adv, uint16_t *len,
                                const char *uas_id)
{
    uint16_t offset = 0;

    /* AD Length (type(1) + UUID(2) + counter(1) + message(25) = 29) */
    adv[offset++] = 29;
    /* AD Type: Service Data - 16-bit UUID */
    adv[offset++] = 0x16;
    /* UUID16 little-endian: 0xFFFA */
    adv[offset++] = 0xFA;
    adv[offset++] = 0xFF;
    /* Message counter */
    adv[offset++] = 0x01;

    /* Basic ID message (25 bytes) */
    memset(&adv[offset], 0, REMOTEID_MSG_SIZE);
    adv[offset] = (REMOTEID_MSG_TYPE_BASIC_ID << 4);
    adv[offset + 1] = (REMOTEID_ID_TYPE_SERIAL << 4);
    size_t id_len = strlen(uas_id);
    if (id_len > 20) id_len = 20;
    memcpy(&adv[offset + 2], uas_id, id_len);
    adv[offset + REMOTEID_CRC_OFFSET] = remoteid_crc8(&adv[offset], REMOTEID_CRC_PROTECTED_LEN);
    offset += REMOTEID_MSG_SIZE;

    *len = offset;
}

/**
 * @brief Build WiFi frame with Basic ID + System (operator location).
 */
static void build_wifi_with_operator(uint8_t *frame, uint16_t *len,
                                      const char *uas_id,
                                      double op_lat, double op_lon)
{
    uint16_t offset = 0;

    /* WiFi header */
    frame[offset++] = 0xFA;
    frame[offset++] = 0x0B;
    frame[offset++] = 0xBC;
    frame[offset++] = 0x0D;
    frame[offset++] = 0x02; /* 2 messages */

    /* Basic ID message (25 bytes) */
    memset(&frame[offset], 0, REMOTEID_MSG_SIZE);
    frame[offset] = (REMOTEID_MSG_TYPE_BASIC_ID << 4);
    frame[offset + 1] = (REMOTEID_ID_TYPE_SERIAL << 4);
    size_t id_len = strlen(uas_id);
    if (id_len > 20) id_len = 20;
    memcpy(&frame[offset + 2], uas_id, id_len);
    frame[offset + REMOTEID_CRC_OFFSET] = remoteid_crc8(&frame[offset], REMOTEID_CRC_PROTECTED_LEN);
    offset += REMOTEID_MSG_SIZE;

    /* System message (25 bytes) */
    memset(&frame[offset], 0, REMOTEID_MSG_SIZE);
    frame[offset] = (REMOTEID_MSG_TYPE_SYSTEM << 4);

    /* Operator latitude (bytes 2-5): int32, little-endian */
    int32_t op_lat_raw = (int32_t)(op_lat * 1e7);
    frame[offset + 2] = (uint8_t)(op_lat_raw & 0xFF);
    frame[offset + 3] = (uint8_t)((op_lat_raw >> 8) & 0xFF);
    frame[offset + 4] = (uint8_t)((op_lat_raw >> 16) & 0xFF);
    frame[offset + 5] = (uint8_t)((op_lat_raw >> 24) & 0xFF);

    /* Operator longitude (bytes 6-9): int32, little-endian */
    int32_t op_lon_raw = (int32_t)(op_lon * 1e7);
    frame[offset + 6] = (uint8_t)(op_lon_raw & 0xFF);
    frame[offset + 7] = (uint8_t)((op_lon_raw >> 8) & 0xFF);
    frame[offset + 8] = (uint8_t)((op_lon_raw >> 16) & 0xFF);
    frame[offset + 9] = (uint8_t)((op_lon_raw >> 24) & 0xFF);

    frame[offset + REMOTEID_CRC_OFFSET] = remoteid_crc8(&frame[offset], REMOTEID_CRC_PROTECTED_LEN);
    offset += REMOTEID_MSG_SIZE;
    *len = offset;
}

/* ========================================================================
 * Unit Tests
 * ======================================================================== */

void setUp(void) {}
void tearDown(void) {}

/* --- NULL pointer handling --- */

void test_wifi_null_frame_returns_invalid_arg(void)
{
    remoteid_data_t data;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      remoteid_decode_wifi(NULL, 50, &data));
}

void test_wifi_null_output_returns_invalid_arg(void)
{
    uint8_t frame[50] = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      remoteid_decode_wifi(frame, 50, NULL));
}

void test_ble_null_adv_returns_invalid_arg(void)
{
    remoteid_data_t data;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      remoteid_decode_ble(NULL, 50, &data));
}

void test_ble_null_output_returns_invalid_arg(void)
{
    uint8_t adv[50] = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      remoteid_decode_ble(adv, 50, NULL));
}

/* --- Size validation --- */

void test_wifi_too_short_returns_invalid_size(void)
{
    uint8_t frame[10] = {0xFA, 0x0B, 0xBC, 0x0D, 0x01};
    remoteid_data_t data;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE,
                      remoteid_decode_wifi(frame, 10, &data));
}

void test_ble_too_short_returns_invalid_size(void)
{
    uint8_t adv[10] = {29, 0x16, 0xFA, 0xFF, 0x01};
    remoteid_data_t data;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE,
                      remoteid_decode_ble(adv, 10, &data));
}

/* --- Invalid OUI/UUID --- */

void test_wifi_wrong_oui_returns_unknown_fmt(void)
{
    uint8_t frame[55];
    uint16_t len;
    build_wifi_basic_id_frame(frame, &len, "TEST-DRONE-001");
    /* Corrupt OUI */
    frame[0] = 0x00;
    remoteid_data_t data;
    TEST_ASSERT_EQUAL(ERR_DECODE_UNKNOWN_FMT,
                      remoteid_decode_wifi(frame, len, &data));
}

void test_wifi_wrong_oui_type_returns_unknown_fmt(void)
{
    uint8_t frame[55];
    uint16_t len;
    build_wifi_basic_id_frame(frame, &len, "TEST-DRONE-001");
    /* Wrong OUI Type */
    frame[3] = 0x0E;
    remoteid_data_t data;
    TEST_ASSERT_EQUAL(ERR_DECODE_UNKNOWN_FMT,
                      remoteid_decode_wifi(frame, len, &data));
}

void test_ble_wrong_ad_type_returns_unknown_fmt(void)
{
    uint8_t adv[55];
    uint16_t len;
    build_ble_basic_id(adv, &len, "TEST-DRONE-001");
    /* Wrong AD type */
    adv[1] = 0x20;
    remoteid_data_t data;
    TEST_ASSERT_EQUAL(ERR_DECODE_UNKNOWN_FMT,
                      remoteid_decode_ble(adv, len, &data));
}

void test_ble_wrong_uuid_returns_unknown_fmt(void)
{
    uint8_t adv[55];
    uint16_t len;
    build_ble_basic_id(adv, &len, "TEST-DRONE-001");
    /* Wrong UUID */
    adv[2] = 0x00;
    adv[3] = 0x00;
    remoteid_data_t data;
    TEST_ASSERT_EQUAL(ERR_DECODE_UNKNOWN_FMT,
                      remoteid_decode_ble(adv, len, &data));
}

/* --- Valid Basic ID decoding --- */

void test_wifi_decode_basic_id(void)
{
    uint8_t frame[55];
    uint16_t len;
    build_wifi_basic_id_frame(frame, &len, "BRA-UAS-001");

    remoteid_data_t data;
    esp_err_t err = remoteid_decode_wifi(frame, len, &data);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING("BRA-UAS-001", data.uas_id);
    TEST_ASSERT_FALSE(data.has_position);
    TEST_ASSERT_FALSE(data.has_operator_location);
    TEST_ASSERT_FALSE(data.has_speed);
}

void test_ble_decode_basic_id(void)
{
    uint8_t adv[55];
    uint16_t len;
    build_ble_basic_id(adv, &len, "DRONE-SERIAL-XYZ");

    remoteid_data_t data;
    esp_err_t err = remoteid_decode_ble(adv, len, &data);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING("DRONE-SERIAL-XYZ", data.uas_id);
    TEST_ASSERT_FALSE(data.has_position);
    TEST_ASSERT_FALSE(data.has_operator_location);
}

/* --- Location message decoding --- */

void test_wifi_decode_location(void)
{
    uint8_t frame[128];
    uint16_t len;
    double lat = -23.5505;
    double lon = -46.6333;
    float alt = 780.0f;
    float speed = 12.5f;
    float dir = 45.0f;

    build_wifi_with_location(frame, &len, "BRA-UAS-002",
                             lat, lon, alt, speed, dir);

    remoteid_data_t data;
    esp_err_t err = remoteid_decode_wifi(frame, len, &data);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING("BRA-UAS-002", data.uas_id);
    TEST_ASSERT_TRUE(data.has_position);

    /* Verify coordinates with tolerance for int32/1e7 quantization */
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, lat, data.latitude);
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, lon, data.longitude);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, alt, data.altitude_m);
    TEST_ASSERT_TRUE(data.has_speed);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, speed, data.speed_ms);
}

/* --- Operator location decoding --- */

void test_wifi_decode_operator_location(void)
{
    uint8_t frame[128];
    uint16_t len;
    double op_lat = -23.5600;
    double op_lon = -46.6400;

    build_wifi_with_operator(frame, &len, "BRA-UAS-003", op_lat, op_lon);

    remoteid_data_t data;
    esp_err_t err = remoteid_decode_wifi(frame, len, &data);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING("BRA-UAS-003", data.uas_id);
    TEST_ASSERT_TRUE(data.has_operator_location);
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, op_lat, data.operator_lat);
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, op_lon, data.operator_lon);
}

/* --- Empty UAS ID rejection --- */

void test_wifi_empty_uas_id_returns_incomplete(void)
{
    uint8_t frame[55];
    uint16_t len;
    build_wifi_basic_id_frame(frame, &len, "");

    remoteid_data_t data;
    esp_err_t err = remoteid_decode_wifi(frame, len, &data);
    TEST_ASSERT_EQUAL(ERR_DECODE_INCOMPLETE, err);
}

/* --- Invalid message type rejection --- */

void test_wifi_invalid_msg_type_no_basic_id_returns_error(void)
{
    uint8_t frame[55];
    uint16_t offset = 0;

    /* WiFi header */
    frame[offset++] = 0xFA;
    frame[offset++] = 0x0B;
    frame[offset++] = 0xBC;
    frame[offset++] = 0x0D;
    frame[offset++] = 0x01;

    /* Message with invalid type (0x0F = msg pack, but 0x0E is invalid) */
    memset(&frame[offset], 0, REMOTEID_MSG_SIZE);
    frame[offset] = (0x0E << 4); /* Invalid type 14 */
    frame[offset + REMOTEID_CRC_OFFSET] = remoteid_crc8(&frame[offset], REMOTEID_CRC_PROTECTED_LEN);
    offset += REMOTEID_MSG_SIZE;

    remoteid_data_t data;
    esp_err_t err = remoteid_decode_wifi(frame, offset, &data);
    TEST_ASSERT_EQUAL(ERR_DECODE_UNKNOWN_FMT, err);
}

/* --- Encode/decode round-trip --- */

void test_encode_decode_roundtrip_basic(void)
{
    remoteid_data_t original;
    remoteid_data_init(&original);
    strncpy(original.uas_id, "ROUNDTRIP-001", REMOTEID_UAS_ID_MAX_LEN - 1);
    original.has_position = false;
    original.has_operator_location = false;

    uint8_t buf[128];
    uint16_t encoded_len;
    esp_err_t err = remoteid_encode(&original, buf, sizeof(buf), &encoded_len);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(REMOTEID_MSG_SIZE, encoded_len);

    /* Wrap in WiFi frame for decoding */
    uint8_t frame[160];
    frame[0] = 0xFA; frame[1] = 0x0B; frame[2] = 0xBC;
    frame[3] = 0x0D;
    frame[4] = 0x01;
    memcpy(&frame[5], buf, encoded_len);

    remoteid_data_t decoded;
    err = remoteid_decode_wifi(frame, 5 + encoded_len, &decoded);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING("ROUNDTRIP-001", decoded.uas_id);
}

void test_encode_decode_roundtrip_with_position(void)
{
    remoteid_data_t original;
    remoteid_data_init(&original);
    strncpy(original.uas_id, "RT-POS-002", REMOTEID_UAS_ID_MAX_LEN - 1);
    original.has_position = true;
    original.latitude = 48.8566;
    original.longitude = 2.3522;
    original.altitude_m = 150.0f;
    original.speed_ms = 5.0f;
    original.has_speed = true;
    original.direction_deg = 90.0f;
    original.has_operator_location = false;

    uint8_t buf[128];
    uint16_t encoded_len;
    esp_err_t err = remoteid_encode(&original, buf, sizeof(buf), &encoded_len);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(2 * REMOTEID_MSG_SIZE, encoded_len);

    /* Wrap in WiFi frame */
    uint8_t frame[160];
    frame[0] = 0xFA; frame[1] = 0x0B; frame[2] = 0xBC;
    frame[3] = 0x0D;
    frame[4] = 0x02;
    memcpy(&frame[5], buf, encoded_len);

    remoteid_data_t decoded;
    err = remoteid_decode_wifi(frame, 5 + encoded_len, &decoded);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING("RT-POS-002", decoded.uas_id);
    TEST_ASSERT_TRUE(decoded.has_position);
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, original.latitude, decoded.latitude);
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, original.longitude, decoded.longitude);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, original.altitude_m, decoded.altitude_m);
}

void test_encode_decode_roundtrip_with_operator(void)
{
    remoteid_data_t original;
    remoteid_data_init(&original);
    strncpy(original.uas_id, "RT-OP-003", REMOTEID_UAS_ID_MAX_LEN - 1);
    original.has_position = false;
    original.has_operator_location = true;
    original.operator_lat = -33.8688;
    original.operator_lon = 151.2093;

    uint8_t buf[128];
    uint16_t encoded_len;
    esp_err_t err = remoteid_encode(&original, buf, sizeof(buf), &encoded_len);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(2 * REMOTEID_MSG_SIZE, encoded_len);

    /* Wrap in WiFi frame */
    uint8_t frame[160];
    frame[0] = 0xFA; frame[1] = 0x0B; frame[2] = 0xBC;
    frame[3] = 0x0D;
    frame[4] = 0x02;
    memcpy(&frame[5], buf, encoded_len);

    remoteid_data_t decoded;
    err = remoteid_decode_wifi(frame, 5 + encoded_len, &decoded);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING("RT-OP-003", decoded.uas_id);
    TEST_ASSERT_TRUE(decoded.has_operator_location);
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, original.operator_lat, decoded.operator_lat);
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, original.operator_lon, decoded.operator_lon);
}

/* --- Encode validation --- */

void test_encode_null_returns_invalid_arg(void)
{
    uint8_t buf[100];
    uint16_t len;
    remoteid_data_t data;
    remoteid_data_init(&data);

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      remoteid_encode(NULL, buf, 100, &len));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      remoteid_encode(&data, NULL, 100, &len));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      remoteid_encode(&data, buf, 100, NULL));
}

void test_encode_buffer_too_small(void)
{
    remoteid_data_t data;
    remoteid_data_init(&data);
    strncpy(data.uas_id, "TEST", REMOTEID_UAS_ID_MAX_LEN - 1);

    uint8_t buf[10]; /* Too small for 25-byte message */
    uint16_t len;
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM,
                      remoteid_encode(&data, buf, 10, &len));
}

/* --- remoteid_data_init --- */

void test_data_init_clears_all_fields(void)
{
    remoteid_data_t data;
    /* Fill with garbage */
    memset(&data, 0xFF, sizeof(data));

    remoteid_data_init(&data);
    TEST_ASSERT_EQUAL_STRING("", data.uas_id);
    TEST_ASSERT_DOUBLE_WITHIN(0.0, 0.0, data.latitude);
    TEST_ASSERT_DOUBLE_WITHIN(0.0, 0.0, data.longitude);
    TEST_ASSERT_FLOAT_WITHIN(0.0f, 0.0f, data.altitude_m);
    TEST_ASSERT_FALSE(data.has_position);
    TEST_ASSERT_FALSE(data.has_operator_location);
    TEST_ASSERT_FALSE(data.has_speed);
}

/* --- Max length UAS ID --- */

void test_wifi_decode_max_length_uas_id(void)
{
    uint8_t frame[55];
    uint16_t len;
    /* 20-character UAS ID (maximum) */
    build_wifi_basic_id_frame(frame, &len, "12345678901234567890");

    remoteid_data_t data;
    esp_err_t err = remoteid_decode_wifi(frame, len, &data);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING("12345678901234567890", data.uas_id);
}

/* --- CRC-8 validation --- */

void test_crc8_zero_length_returns_zero(void)
{
    uint8_t data[] = {0};
    uint8_t crc = remoteid_crc8(data, 0);
    TEST_ASSERT_EQUAL_UINT8(0x00, crc);
}

void test_crc8_known_pattern(void)
{
    /* A known pattern should produce a consistent CRC value */
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    uint8_t crc1 = remoteid_crc8(data, 5);
    uint8_t crc2 = remoteid_crc8(data, 5);
    TEST_ASSERT_EQUAL_UINT8(crc1, crc2); /* Deterministic */
    TEST_ASSERT_NOT_EQUAL(0, crc1); /* Non-trivial */
}

void test_crc8_different_data_different_crc(void)
{
    uint8_t data1[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    uint8_t data2[] = {0x01, 0x02, 0x03, 0x04, 0x06}; /* Last byte different */
    uint8_t crc1 = remoteid_crc8(data1, 5);
    uint8_t crc2 = remoteid_crc8(data2, 5);
    TEST_ASSERT_NOT_EQUAL(crc1, crc2);
}

/* --- Frame validation --- */

void test_validate_wifi_frame_valid(void)
{
    uint8_t frame[55];
    uint16_t len;
    build_wifi_basic_id_frame(frame, &len, "VALID-DRONE-001");

    esp_err_t err = remoteid_validate_frame(frame, len, false);
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

void test_validate_ble_frame_valid(void)
{
    uint8_t adv[55];
    uint16_t len;
    build_ble_basic_id(adv, &len, "VALID-BLE-001");

    esp_err_t err = remoteid_validate_frame(adv, len, true);
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

void test_validate_null_returns_invalid_arg(void)
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, remoteid_validate_frame(NULL, 50, false));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, remoteid_validate_frame(NULL, 50, true));
}

void test_validate_wifi_too_short(void)
{
    uint8_t frame[10] = {0xFA, 0x0B, 0xBC, 0x0D, 0x01};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, remoteid_validate_frame(frame, 10, false));
}

void test_validate_ble_too_short(void)
{
    uint8_t adv[10] = {29, 0x16, 0xFA, 0xFF, 0x01};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, remoteid_validate_frame(adv, 10, true));
}

void test_validate_wifi_wrong_oui(void)
{
    uint8_t frame[55];
    uint16_t len;
    build_wifi_basic_id_frame(frame, &len, "TEST-001");
    frame[0] = 0x00; /* corrupt OUI */
    TEST_ASSERT_EQUAL(ERR_DECODE_UNKNOWN_FMT, remoteid_validate_frame(frame, len, false));
}

void test_validate_wifi_invalid_msg_type(void)
{
    uint8_t frame[55];
    uint16_t len;
    build_wifi_basic_id_frame(frame, &len, "TEST-001");
    /* Set invalid message type in first message (byte at offset 5) */
    frame[5] = (0x0E << 4); /* Type 14 is invalid */
    TEST_ASSERT_EQUAL(ERR_DECODE_UNKNOWN_FMT, remoteid_validate_frame(frame, len, false));
}

/* --- Unified remoteid_decode --- */

void test_unified_decode_wifi_basic_id(void)
{
    uint8_t frame[55];
    uint16_t len;
    build_wifi_basic_id_frame(frame, &len, "UNIFIED-WIFI-001");

    decoded_telemetry_t telemetry;
    remoteid_data_t rid_data;
    esp_err_t err = remoteid_decode(frame, len, false, &telemetry, &rid_data);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING("UNIFIED-WIFI-001", telemetry.uas_id);
    TEST_ASSERT_EQUAL_STRING("UNIFIED-WIFI-001", rid_data.uas_id);
    TEST_ASSERT_FALSE(telemetry.has_position);
    TEST_ASSERT_FALSE(telemetry.has_speed);
    TEST_ASSERT_FALSE(telemetry.has_battery);
}

void test_unified_decode_ble_basic_id(void)
{
    uint8_t adv[55];
    uint16_t len;
    build_ble_basic_id(adv, &len, "UNIFIED-BLE-001");

    decoded_telemetry_t telemetry;
    remoteid_data_t rid_data;
    esp_err_t err = remoteid_decode(adv, len, true, &telemetry, &rid_data);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING("UNIFIED-BLE-001", telemetry.uas_id);
    TEST_ASSERT_EQUAL_STRING("UNIFIED-BLE-001", rid_data.uas_id);
}

void test_unified_decode_with_position(void)
{
    uint8_t frame[128];
    uint16_t len;
    double lat = 48.8566;
    double lon = 2.3522;
    float alt = 200.0f;
    float speed = 15.0f;
    float dir = 120.0f;

    build_wifi_with_location(frame, &len, "UNIFIED-POS-001", lat, lon, alt, speed, dir);

    decoded_telemetry_t telemetry;
    remoteid_data_t rid_data;
    esp_err_t err = remoteid_decode(frame, len, false, &telemetry, &rid_data);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING("UNIFIED-POS-001", telemetry.uas_id);
    TEST_ASSERT_TRUE(telemetry.has_position);
    TEST_ASSERT_TRUE(telemetry.has_altitude);
    TEST_ASSERT_TRUE(telemetry.has_speed);
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, lat, telemetry.lat);
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, lon, telemetry.lon);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, alt, telemetry.altitude_m);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, speed, telemetry.speed_ms);
}

void test_unified_decode_null_rid_out_ok(void)
{
    uint8_t frame[55];
    uint16_t len;
    build_wifi_basic_id_frame(frame, &len, "NORID-001");

    decoded_telemetry_t telemetry;
    /* Pass NULL for rid_out — should still work */
    esp_err_t err = remoteid_decode(frame, len, false, &telemetry, NULL);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING("NORID-001", telemetry.uas_id);
}

void test_unified_decode_null_payload_returns_error(void)
{
    decoded_telemetry_t telemetry;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, remoteid_decode(NULL, 50, false, &telemetry, NULL));
}

void test_unified_decode_null_out_returns_error(void)
{
    uint8_t frame[55] = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, remoteid_decode(frame, 55, false, NULL, NULL));
}

void test_unified_decode_invalid_frame_rejected(void)
{
    /* Frame too short */
    uint8_t frame[10] = {0xFA, 0x0B, 0xBC, 0x0D, 0x01, 0, 0, 0, 0, 0};
    decoded_telemetry_t telemetry;
    esp_err_t err = remoteid_decode(frame, 10, false, &telemetry, NULL);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
}

/* ========================================================================
 * Test Runner
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* NULL pointer tests */
    RUN_TEST(test_wifi_null_frame_returns_invalid_arg);
    RUN_TEST(test_wifi_null_output_returns_invalid_arg);
    RUN_TEST(test_ble_null_adv_returns_invalid_arg);
    RUN_TEST(test_ble_null_output_returns_invalid_arg);

    /* Size validation */
    RUN_TEST(test_wifi_too_short_returns_invalid_size);
    RUN_TEST(test_ble_too_short_returns_invalid_size);

    /* Invalid OUI/UUID */
    RUN_TEST(test_wifi_wrong_oui_returns_unknown_fmt);
    RUN_TEST(test_wifi_wrong_oui_type_returns_unknown_fmt);
    RUN_TEST(test_ble_wrong_ad_type_returns_unknown_fmt);
    RUN_TEST(test_ble_wrong_uuid_returns_unknown_fmt);

    /* Valid Basic ID decoding */
    RUN_TEST(test_wifi_decode_basic_id);
    RUN_TEST(test_ble_decode_basic_id);

    /* Location message decoding */
    RUN_TEST(test_wifi_decode_location);

    /* Operator location decoding */
    RUN_TEST(test_wifi_decode_operator_location);

    /* Empty UAS ID rejection */
    RUN_TEST(test_wifi_empty_uas_id_returns_incomplete);

    /* Invalid message type */
    RUN_TEST(test_wifi_invalid_msg_type_no_basic_id_returns_error);

    /* Encode/decode round-trip */
    RUN_TEST(test_encode_decode_roundtrip_basic);
    RUN_TEST(test_encode_decode_roundtrip_with_position);
    RUN_TEST(test_encode_decode_roundtrip_with_operator);

    /* Encode validation */
    RUN_TEST(test_encode_null_returns_invalid_arg);
    RUN_TEST(test_encode_buffer_too_small);

    /* Data init */
    RUN_TEST(test_data_init_clears_all_fields);

    /* Max length UAS ID */
    RUN_TEST(test_wifi_decode_max_length_uas_id);

    /* CRC-8 */
    RUN_TEST(test_crc8_zero_length_returns_zero);
    RUN_TEST(test_crc8_known_pattern);
    RUN_TEST(test_crc8_different_data_different_crc);

    /* Frame validation */
    RUN_TEST(test_validate_wifi_frame_valid);
    RUN_TEST(test_validate_ble_frame_valid);
    RUN_TEST(test_validate_null_returns_invalid_arg);
    RUN_TEST(test_validate_wifi_too_short);
    RUN_TEST(test_validate_ble_too_short);
    RUN_TEST(test_validate_wifi_wrong_oui);
    RUN_TEST(test_validate_wifi_invalid_msg_type);

    /* Unified remoteid_decode */
    RUN_TEST(test_unified_decode_wifi_basic_id);
    RUN_TEST(test_unified_decode_ble_basic_id);
    RUN_TEST(test_unified_decode_with_position);
    RUN_TEST(test_unified_decode_null_rid_out_ok);
    RUN_TEST(test_unified_decode_null_payload_returns_error);
    RUN_TEST(test_unified_decode_null_out_returns_error);
    RUN_TEST(test_unified_decode_invalid_frame_rejected);

    return UNITY_END();
}
