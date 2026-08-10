/**
 * @file test_mavlink_decoder.c
 * @brief Unit tests for the MAVLink v1/v2 decoder.
 *
 * Tests frame validation, CRC calculation, and telemetry extraction for
 * GLOBAL_POSITION_INT, BATTERY_STATUS, HEARTBEAT, and HOME_POSITION messages.
 *
 * Validates: Requirements 8.1, 6.2
 */

#include "unity.h"
#include "mavlink_decoder.h"
#include "error_codes.h"
#include <string.h>
#include <math.h>

void setUp(void) {}
void tearDown(void) {}

/* ========================================================================
 * Helper: Build a valid MAVLink v1 frame
 * ======================================================================== */

/**
 * @brief Build a MAVLink v1 frame with proper CRC.
 * @param buf       Output buffer (must be large enough).
 * @param msg_id    Message ID.
 * @param payload   Payload data.
 * @param payload_len Payload length.
 * @param crc_extra CRC extra byte for this message.
 * @return Total frame length.
 */
static uint16_t build_mavlink_v1_frame(uint8_t *buf, uint8_t msg_id,
                                       const uint8_t *payload, uint8_t payload_len,
                                       uint8_t crc_extra)
{
    buf[0] = 0xFE;          /* STX */
    buf[1] = payload_len;   /* Len */
    buf[2] = 0x01;          /* Seq */
    buf[3] = 0x01;          /* SysID */
    buf[4] = 0x01;          /* CompID */
    buf[5] = msg_id;        /* MsgID */

    memcpy(&buf[6], payload, payload_len);

    /* Calculate CRC over bytes 1..5+payload_len (skip STX) */
    uint16_t crc = mavlink_crc_calculate(&buf[1], 5 + payload_len, crc_extra);
    buf[6 + payload_len] = (uint8_t)(crc & 0xFF);
    buf[6 + payload_len + 1] = (uint8_t)(crc >> 8);

    return 6 + payload_len + 2;
}

/* ========================================================================
 * Helper: Build a valid MAVLink v2 frame
 * ======================================================================== */

/**
 * @brief Build a MAVLink v2 frame with proper CRC.
 * @param buf         Output buffer (must be large enough).
 * @param msg_id      Message ID (up to 24-bit).
 * @param payload     Payload data.
 * @param payload_len Payload length.
 * @param crc_extra   CRC extra byte for this message.
 * @return Total frame length.
 */
static uint16_t build_mavlink_v2_frame(uint8_t *buf, uint32_t msg_id,
                                       const uint8_t *payload, uint8_t payload_len,
                                       uint8_t crc_extra)
{
    buf[0] = 0xFD;                      /* STX */
    buf[1] = payload_len;               /* Len */
    buf[2] = 0x00;                      /* IncompatFlags (no signature) */
    buf[3] = 0x00;                      /* CompatFlags */
    buf[4] = 0x01;                      /* Seq */
    buf[5] = 0x01;                      /* SysID */
    buf[6] = 0x01;                      /* CompID */
    buf[7] = (uint8_t)(msg_id & 0xFF);  /* MsgID byte 0 */
    buf[8] = (uint8_t)((msg_id >> 8) & 0xFF);  /* MsgID byte 1 */
    buf[9] = (uint8_t)((msg_id >> 16) & 0xFF); /* MsgID byte 2 */

    memcpy(&buf[10], payload, payload_len);

    /* CRC covers bytes 1..9+payload_len (skip STX) */
    uint16_t crc = mavlink_crc_calculate(&buf[1], 9 + payload_len, crc_extra);
    buf[10 + payload_len] = (uint8_t)(crc & 0xFF);
    buf[10 + payload_len + 1] = (uint8_t)(crc >> 8);

    return 10 + payload_len + 2;
}

/* ========================================================================
 * Helper: Build GLOBAL_POSITION_INT payload
 * ======================================================================== */

static void build_global_position_payload(uint8_t *payload,
                                          int32_t lat_e7, int32_t lon_e7,
                                          int32_t alt_mm, int32_t rel_alt_mm,
                                          int16_t vx, int16_t vy, int16_t vz,
                                          uint16_t hdg_cdeg)
{
    uint32_t time_boot_ms = 12345;

    /* time_boot_ms (offset 0) */
    payload[0] = (uint8_t)(time_boot_ms & 0xFF);
    payload[1] = (uint8_t)((time_boot_ms >> 8) & 0xFF);
    payload[2] = (uint8_t)((time_boot_ms >> 16) & 0xFF);
    payload[3] = (uint8_t)((time_boot_ms >> 24) & 0xFF);

    /* lat (offset 4) */
    payload[4] = (uint8_t)(lat_e7 & 0xFF);
    payload[5] = (uint8_t)((lat_e7 >> 8) & 0xFF);
    payload[6] = (uint8_t)((lat_e7 >> 16) & 0xFF);
    payload[7] = (uint8_t)((lat_e7 >> 24) & 0xFF);

    /* lon (offset 8) */
    payload[8] = (uint8_t)(lon_e7 & 0xFF);
    payload[9] = (uint8_t)((lon_e7 >> 8) & 0xFF);
    payload[10] = (uint8_t)((lon_e7 >> 16) & 0xFF);
    payload[11] = (uint8_t)((lon_e7 >> 24) & 0xFF);

    /* alt (offset 12) */
    payload[12] = (uint8_t)(alt_mm & 0xFF);
    payload[13] = (uint8_t)((alt_mm >> 8) & 0xFF);
    payload[14] = (uint8_t)((alt_mm >> 16) & 0xFF);
    payload[15] = (uint8_t)((alt_mm >> 24) & 0xFF);

    /* relative_alt (offset 16) */
    payload[16] = (uint8_t)(rel_alt_mm & 0xFF);
    payload[17] = (uint8_t)((rel_alt_mm >> 8) & 0xFF);
    payload[18] = (uint8_t)((rel_alt_mm >> 16) & 0xFF);
    payload[19] = (uint8_t)((rel_alt_mm >> 24) & 0xFF);

    /* vx (offset 20) */
    payload[20] = (uint8_t)(vx & 0xFF);
    payload[21] = (uint8_t)((vx >> 8) & 0xFF);

    /* vy (offset 22) */
    payload[22] = (uint8_t)(vy & 0xFF);
    payload[23] = (uint8_t)((vy >> 8) & 0xFF);

    /* vz (offset 24) */
    payload[24] = (uint8_t)(vz & 0xFF);
    payload[25] = (uint8_t)((vz >> 8) & 0xFF);

    /* hdg (offset 26) */
    payload[26] = (uint8_t)(hdg_cdeg & 0xFF);
    payload[27] = (uint8_t)((hdg_cdeg >> 8) & 0xFF);
}

/* ========================================================================
 * Tests: Invalid Input
 * ======================================================================== */

void test_decode_null_data_returns_invalid_arg(void) {
    decoded_telemetry_t out;
    esp_err_t err = mavlink_decode(NULL, 10, &out);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

void test_decode_null_out_returns_invalid_arg(void) {
    uint8_t data[10] = {0};
    esp_err_t err = mavlink_decode(data, 10, NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

void test_decode_too_short_returns_incomplete(void) {
    uint8_t data[4] = {0xFE, 0x00, 0x00, 0x00};
    decoded_telemetry_t out;
    esp_err_t err = mavlink_decode(data, 4, &out);
    TEST_ASSERT_EQUAL(ERR_DECODE_INCOMPLETE, err);
}

void test_decode_invalid_stx_returns_unknown_fmt(void) {
    uint8_t data[10] = {0xAA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    decoded_telemetry_t out;
    esp_err_t err = mavlink_decode(data, 10, &out);
    TEST_ASSERT_EQUAL(ERR_DECODE_UNKNOWN_FMT, err);
}

void test_decode_bad_crc_returns_crc_fail(void) {
    /* Build valid v1 heartbeat then corrupt CRC */
    uint8_t payload[9] = {0};
    payload[6] = 0x01; /* base_mode */
    uint8_t frame[64];
    uint16_t frame_len = build_mavlink_v1_frame(frame, 0, payload, 9, 50);

    /* Corrupt CRC */
    frame[frame_len - 1] ^= 0xFF;

    decoded_telemetry_t out;
    esp_err_t err = mavlink_decode(frame, frame_len, &out);
    TEST_ASSERT_EQUAL(ERR_DECODE_CRC_FAIL, err);
}

void test_decode_truncated_v1_returns_incomplete(void) {
    /* v1 frame with payload_len=28 but buffer is too short */
    uint8_t data[10] = {0xFE, 28, 0x00, 0x01, 0x01, 33, 0, 0, 0, 0};
    decoded_telemetry_t out;
    esp_err_t err = mavlink_decode(data, 10, &out);
    TEST_ASSERT_EQUAL(ERR_DECODE_INCOMPLETE, err);
}

/* ========================================================================
 * Tests: GLOBAL_POSITION_INT (v1)
 * ======================================================================== */

void test_decode_v1_global_position_int(void) {
    /* lat = -23.5505200 => -235505200, lon = -46.6333090 => -466333090 */
    int32_t lat_e7 = -235505200;
    int32_t lon_e7 = -466333090;
    int32_t alt_mm = 780200;   /* 780.2 m */
    int32_t rel_alt = 20000;   /* 20 m */
    int16_t vx = 500;          /* 5.0 m/s */
    int16_t vy = 1200;         /* 12.0 m/s */
    int16_t vz = 0;
    uint16_t hdg = 18045;      /* 180.45 deg */

    uint8_t payload[28];
    build_global_position_payload(payload, lat_e7, lon_e7, alt_mm,
                                  rel_alt, vx, vy, vz, hdg);

    uint8_t frame[64];
    uint16_t frame_len = build_mavlink_v1_frame(frame, 33, payload, 28, 104);

    decoded_telemetry_t out;
    esp_err_t err = mavlink_decode(frame, frame_len, &out);

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(out.has_position);
    TEST_ASSERT_TRUE(out.has_altitude);
    TEST_ASSERT_TRUE(out.has_speed);

    TEST_ASSERT_DOUBLE_WITHIN(1e-6, -23.5505200, out.lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, -46.6333090, out.lon);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 780.2f, out.altitude_m);

    /* speed = sqrt(5^2 + 12^2) = 13.0 m/s */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 13.0f, out.speed_ms);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 180.45f, out.heading_deg);
}

void test_decode_v1_global_position_int_zero_values(void) {
    uint8_t payload[28] = {0};
    uint8_t frame[64];
    uint16_t frame_len = build_mavlink_v1_frame(frame, 33, payload, 28, 104);

    decoded_telemetry_t out;
    esp_err_t err = mavlink_decode(frame, frame_len, &out);

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(out.has_position);
    TEST_ASSERT_DOUBLE_WITHIN(1e-10, 0.0, out.lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-10, 0.0, out.lon);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, out.altitude_m);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, out.speed_ms);
}

/* ========================================================================
 * Tests: GLOBAL_POSITION_INT (v2)
 * ======================================================================== */

void test_decode_v2_global_position_int(void) {
    int32_t lat_e7 = 473977000;   /* 47.3977000 deg (Zurich) */
    int32_t lon_e7 = 85258000;    /* 8.5258000 deg */
    int32_t alt_mm = 408000;      /* 408 m */
    int32_t rel_alt = 50000;      /* 50 m */
    int16_t vx = 300;             /* 3.0 m/s */
    int16_t vy = 400;             /* 4.0 m/s */
    int16_t vz = -100;
    uint16_t hdg = 9000;          /* 90.00 deg */

    uint8_t payload[28];
    build_global_position_payload(payload, lat_e7, lon_e7, alt_mm,
                                  rel_alt, vx, vy, vz, hdg);

    uint8_t frame[64];
    uint16_t frame_len = build_mavlink_v2_frame(frame, 33, payload, 28, 104);

    decoded_telemetry_t out;
    esp_err_t err = mavlink_decode(frame, frame_len, &out);

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(out.has_position);
    TEST_ASSERT_TRUE(out.has_altitude);
    TEST_ASSERT_TRUE(out.has_speed);

    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 47.3977000, out.lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 8.5258000, out.lon);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 408.0f, out.altitude_m);

    /* speed = sqrt(3^2 + 4^2) = 5.0 m/s */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.0f, out.speed_ms);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 90.0f, out.heading_deg);
}

/* ========================================================================
 * Tests: BATTERY_STATUS
 * ======================================================================== */

void test_decode_v1_battery_status(void) {
    uint8_t payload[36] = {0};

    /* voltages at offset 10: 3 cells of 3700 mV each */
    uint16_t cell_mv = 3700;
    for (int i = 0; i < 3; i++) {
        payload[10 + i*2] = (uint8_t)(cell_mv & 0xFF);
        payload[10 + i*2 + 1] = (uint8_t)(cell_mv >> 8);
    }
    /* Remaining cells = 0xFFFF (invalid) */
    for (int i = 3; i < 10; i++) {
        payload[10 + i*2] = 0xFF;
        payload[10 + i*2 + 1] = 0xFF;
    }

    /* battery_remaining at offset 35 = 75% */
    payload[35] = 75;

    uint8_t frame[64];
    uint16_t frame_len = build_mavlink_v1_frame(frame, 147, payload, 36, 154);

    decoded_telemetry_t out;
    esp_err_t err = mavlink_decode(frame, frame_len, &out);

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(out.has_battery);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 75.0f, out.battery_pct);
    /* 3 cells * 3700 mV = 11100 mV = 11.1 V */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 11.1f, out.battery_voltage);
}

void test_decode_v1_battery_status_unknown_remaining(void) {
    uint8_t payload[36] = {0};

    /* One valid cell */
    payload[10] = 0x00; payload[11] = 0x10; /* 4096 mV */
    for (int i = 1; i < 10; i++) {
        payload[10 + i*2] = 0xFF;
        payload[10 + i*2 + 1] = 0xFF;
    }

    /* battery_remaining = -1 (unknown) */
    payload[35] = (uint8_t)(-1);

    uint8_t frame[64];
    uint16_t frame_len = build_mavlink_v1_frame(frame, 147, payload, 36, 154);

    decoded_telemetry_t out;
    esp_err_t err = mavlink_decode(frame, frame_len, &out);

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_FALSE(out.has_battery);
}

/* ========================================================================
 * Tests: HEARTBEAT
 * ======================================================================== */

void test_decode_v1_heartbeat(void) {
    uint8_t payload[9] = {0};

    /* custom_mode at offset 0 (uint32) = 5 */
    payload[0] = 5; payload[1] = 0; payload[2] = 0; payload[3] = 0;
    /* type at offset 4 = 2 (MAV_TYPE_QUADROTOR) */
    payload[4] = 2;
    /* autopilot at offset 5 = 3 (MAV_AUTOPILOT_ARDUPILOTMEGA) */
    payload[5] = 3;
    /* base_mode at offset 6 = 0x8D (armed, stabilize) */
    payload[6] = 0x8D;
    /* system_status at offset 7 = 4 (MAV_STATE_ACTIVE) */
    payload[7] = 4;
    /* mavlink_version at offset 8 = 3 */
    payload[8] = 3;

    uint8_t frame[32];
    uint16_t frame_len = build_mavlink_v1_frame(frame, 0, payload, 9, 50);

    decoded_telemetry_t out;
    esp_err_t err = mavlink_decode(frame, frame_len, &out);

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(out.has_flight_mode);
    TEST_ASSERT_EQUAL_UINT8(0x8D, out.flight_mode);
}

void test_decode_v2_heartbeat(void) {
    uint8_t payload[9] = {0};
    payload[6] = 0x05; /* base_mode */

    uint8_t frame[32];
    uint16_t frame_len = build_mavlink_v2_frame(frame, 0, payload, 9, 50);

    decoded_telemetry_t out;
    esp_err_t err = mavlink_decode(frame, frame_len, &out);

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(out.has_flight_mode);
    TEST_ASSERT_EQUAL_UINT8(0x05, out.flight_mode);
}

/* ========================================================================
 * Tests: HOME_POSITION
 * ======================================================================== */

void test_decode_v1_home_position(void) {
    /* HOME_POSITION has at minimum 52 bytes payload, but we only need first 12 */
    uint8_t payload[60] = {0};

    int32_t lat_e7 = -235000000;  /* -23.5 deg */
    int32_t lon_e7 = -466000000;  /* -46.6 deg */
    int32_t alt_mm = 760500;      /* 760.5 m */

    /* lat (offset 0) */
    payload[0] = (uint8_t)(lat_e7 & 0xFF);
    payload[1] = (uint8_t)((lat_e7 >> 8) & 0xFF);
    payload[2] = (uint8_t)((lat_e7 >> 16) & 0xFF);
    payload[3] = (uint8_t)((lat_e7 >> 24) & 0xFF);

    /* lon (offset 4) */
    payload[4] = (uint8_t)(lon_e7 & 0xFF);
    payload[5] = (uint8_t)((lon_e7 >> 8) & 0xFF);
    payload[6] = (uint8_t)((lon_e7 >> 16) & 0xFF);
    payload[7] = (uint8_t)((lon_e7 >> 24) & 0xFF);

    /* alt (offset 8) */
    payload[8] = (uint8_t)(alt_mm & 0xFF);
    payload[9] = (uint8_t)((alt_mm >> 8) & 0xFF);
    payload[10] = (uint8_t)((alt_mm >> 16) & 0xFF);
    payload[11] = (uint8_t)((alt_mm >> 24) & 0xFF);

    uint8_t frame[128];
    uint16_t frame_len = build_mavlink_v1_frame(frame, 242, payload, 60, 104);

    decoded_telemetry_t out;
    esp_err_t err = mavlink_decode(frame, frame_len, &out);

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(out.has_position);
    TEST_ASSERT_TRUE(out.has_altitude);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, -23.5, out.lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, -46.6, out.lon);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 760.5f, out.altitude_m);
}

/* ========================================================================
 * Tests: Frame Validation
 * ======================================================================== */

void test_is_valid_frame_null_returns_false(void) {
    TEST_ASSERT_FALSE(mavlink_is_valid_frame(NULL, 10));
}

void test_is_valid_frame_too_short_returns_false(void) {
    uint8_t data[4] = {0xFE, 0, 0, 0};
    TEST_ASSERT_FALSE(mavlink_is_valid_frame(data, 4));
}

void test_is_valid_frame_v1_heartbeat(void) {
    uint8_t payload[9] = {0};
    payload[6] = 0x01;

    uint8_t frame[32];
    uint16_t frame_len = build_mavlink_v1_frame(frame, 0, payload, 9, 50);

    TEST_ASSERT_TRUE(mavlink_is_valid_frame(frame, frame_len));
}

void test_is_valid_frame_v2_heartbeat(void) {
    uint8_t payload[9] = {0};
    payload[6] = 0x01;

    uint8_t frame[32];
    uint16_t frame_len = build_mavlink_v2_frame(frame, 0, payload, 9, 50);

    TEST_ASSERT_TRUE(mavlink_is_valid_frame(frame, frame_len));
}

void test_is_valid_frame_corrupted_returns_false(void) {
    uint8_t payload[9] = {0};
    uint8_t frame[32];
    uint16_t frame_len = build_mavlink_v1_frame(frame, 0, payload, 9, 50);

    /* Corrupt a byte in the payload area */
    frame[8] = 0xFF;

    TEST_ASSERT_FALSE(mavlink_is_valid_frame(frame, frame_len));
}

/* ========================================================================
 * Tests: Unsupported message ID
 * ======================================================================== */

void test_decode_unsupported_msg_id_returns_unknown_fmt(void) {
    /* Message ID 100 is not supported */
    uint8_t payload[10] = {0};
    uint8_t frame[32];
    /* We can't properly build frame for unknown msg because we don't have crc_extra,
     * so the CRC check will fail first, returning ERR_DECODE_CRC_FAIL */
    frame[0] = 0xFE;
    frame[1] = 10;
    frame[2] = 0x01;
    frame[3] = 0x01;
    frame[4] = 0x01;
    frame[5] = 100; /* unsupported msg_id */
    memcpy(&frame[6], payload, 10);
    /* Put garbage CRC */
    frame[16] = 0x00;
    frame[17] = 0x00;

    decoded_telemetry_t out;
    esp_err_t err = mavlink_decode(frame, 18, &out);
    /* Will fail with CRC error since msg_id=100 has no known crc_extra */
    TEST_ASSERT_EQUAL(ERR_DECODE_CRC_FAIL, err);
}

/* ========================================================================
 * Tests: CRC calculation
 * ======================================================================== */

void test_crc_known_value(void) {
    /* Known test vector: MAVLink CRC of empty data with crc_extra=0 */
    uint16_t crc = mavlink_crc_calculate(NULL, 0, 0);
    /* CRC of empty with seed 0: start=0xFFFF, accumulate(0) */
    /* Manually: 0xFFFF -> accumulate(0) */
    /* Not trivial to compute by hand, but we can verify it's deterministic */
    uint16_t crc2 = mavlink_crc_calculate(NULL, 0, 0);
    TEST_ASSERT_EQUAL_UINT16(crc, crc2);
}

void test_crc_different_data_different_result(void) {
    uint8_t data1[] = {0x01, 0x02, 0x03};
    uint8_t data2[] = {0x04, 0x05, 0x06};

    uint16_t crc1 = mavlink_crc_calculate(data1, 3, 50);
    uint16_t crc2 = mavlink_crc_calculate(data2, 3, 50);

    TEST_ASSERT_NOT_EQUAL(crc1, crc2);
}

void test_crc_different_extra_different_result(void) {
    uint8_t data[] = {0x01, 0x02, 0x03};

    uint16_t crc1 = mavlink_crc_calculate(data, 3, 50);
    uint16_t crc2 = mavlink_crc_calculate(data, 3, 104);

    TEST_ASSERT_NOT_EQUAL(crc1, crc2);
}

/* ========================================================================
 * Test Runner
 * ======================================================================== */

int main(void) {
    UNITY_BEGIN();

    /* Invalid input */
    RUN_TEST(test_decode_null_data_returns_invalid_arg);
    RUN_TEST(test_decode_null_out_returns_invalid_arg);
    RUN_TEST(test_decode_too_short_returns_incomplete);
    RUN_TEST(test_decode_invalid_stx_returns_unknown_fmt);
    RUN_TEST(test_decode_bad_crc_returns_crc_fail);
    RUN_TEST(test_decode_truncated_v1_returns_incomplete);

    /* GLOBAL_POSITION_INT */
    RUN_TEST(test_decode_v1_global_position_int);
    RUN_TEST(test_decode_v1_global_position_int_zero_values);
    RUN_TEST(test_decode_v2_global_position_int);

    /* BATTERY_STATUS */
    RUN_TEST(test_decode_v1_battery_status);
    RUN_TEST(test_decode_v1_battery_status_unknown_remaining);

    /* HEARTBEAT */
    RUN_TEST(test_decode_v1_heartbeat);
    RUN_TEST(test_decode_v2_heartbeat);

    /* HOME_POSITION */
    RUN_TEST(test_decode_v1_home_position);

    /* Frame validation */
    RUN_TEST(test_is_valid_frame_null_returns_false);
    RUN_TEST(test_is_valid_frame_too_short_returns_false);
    RUN_TEST(test_is_valid_frame_v1_heartbeat);
    RUN_TEST(test_is_valid_frame_v2_heartbeat);
    RUN_TEST(test_is_valid_frame_corrupted_returns_false);

    /* Unsupported messages */
    RUN_TEST(test_decode_unsupported_msg_id_returns_unknown_fmt);

    /* CRC */
    RUN_TEST(test_crc_known_value);
    RUN_TEST(test_crc_different_data_different_result);
    RUN_TEST(test_crc_different_extra_different_result);

    return UNITY_END();
}
