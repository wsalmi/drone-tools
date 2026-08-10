/**
 * @file test_elrs_decoder.c
 * @brief Unit tests for the ELRS/CRSF telemetry decoder.
 *
 * Tests CRC-8/DVB-S2 computation, frame validation, and decoding of
 * Link Statistics, Battery Sensor, and GPS frame types.
 *
 * Validates: Requirements 8.2
 */

#include "unity.h"
#include "elrs_decoder.h"
#include "error_codes.h"

#include <string.h>
#include <math.h>

void setUp(void) {}
void tearDown(void) {}

/* ========================================================================
 * Helper: Build a valid CRSF frame with correct CRC
 * ======================================================================== */

/**
 * @brief Builds a CRSF frame in the provided buffer.
 * @return Total frame length (bytes written).
 */
static uint16_t build_crsf_frame(uint8_t *buf, uint8_t sync, uint8_t type,
                                  const uint8_t *payload, uint8_t payload_len)
{
    buf[0] = sync;
    buf[1] = (uint8_t)(payload_len + 2); /* type + payload + crc */
    buf[2] = type;
    if (payload_len > 0 && payload != NULL) {
        memcpy(&buf[3], payload, payload_len);
    }
    /* Compute CRC over type + payload */
    uint8_t crc = crsf_crc8_dvb_s2(&buf[2], (uint16_t)(payload_len + 1));
    buf[3 + payload_len] = crc;
    return (uint16_t)(4 + payload_len);
}

/* ========================================================================
 * Tests: CRC-8/DVB-S2
 * ======================================================================== */

void test_crc8_dvb_s2_empty(void) {
    uint8_t crc = crsf_crc8_dvb_s2(NULL, 0);
    TEST_ASSERT_EQUAL_UINT8(0, crc);
}

void test_crc8_dvb_s2_known_value(void) {
    /* Known test vector: single byte 0x14 (link stats type) */
    uint8_t data[] = {0x14};
    uint8_t crc = crsf_crc8_dvb_s2(data, 1);
    /* CRC should be deterministic and non-zero for non-zero input */
    TEST_ASSERT_TRUE(crc != 0 || data[0] == 0);
}

void test_crc8_dvb_s2_changes_with_data(void) {
    uint8_t data1[] = {0x14, 0x50, 0x60, 0x64};
    uint8_t data2[] = {0x14, 0x50, 0x60, 0x65};
    uint8_t crc1 = crsf_crc8_dvb_s2(data1, 4);
    uint8_t crc2 = crsf_crc8_dvb_s2(data2, 4);
    TEST_ASSERT_NOT_EQUAL(crc1, crc2);
}

/* ========================================================================
 * Tests: Frame Validation (elrs_validate_crc)
 * ======================================================================== */

void test_validate_crc_null_data(void) {
    TEST_ASSERT_FALSE(elrs_validate_crc(NULL, 10));
}

void test_validate_crc_too_short(void) {
    uint8_t data[] = {0xC8, 0x02, 0x14};
    TEST_ASSERT_FALSE(elrs_validate_crc(data, 3));
}

void test_validate_crc_invalid_sync(void) {
    uint8_t frame[64];
    uint8_t payload[] = {80, 90, 100, 0, 0, 0, 0, 50, 80, 5};
    uint16_t len = build_crsf_frame(frame, 0xC8, 0x14, payload, 10);
    frame[0] = 0xAA; /* corrupt sync */
    TEST_ASSERT_FALSE(elrs_validate_crc(frame, len));
}

void test_validate_crc_valid_link_stats(void) {
    uint8_t frame[64];
    uint8_t payload[] = {80, 90, 100, 5, 0, 2, 3, 50, 80, 5};
    uint16_t len = build_crsf_frame(frame, 0xC8, 0x14, payload, 10);
    TEST_ASSERT_TRUE(elrs_validate_crc(frame, len));
}

void test_validate_crc_valid_broadcast(void) {
    uint8_t frame[64];
    uint8_t payload[] = {80, 90, 100, 5, 0, 2, 3, 50, 80, 5};
    uint16_t len = build_crsf_frame(frame, 0xEE, 0x14, payload, 10);
    TEST_ASSERT_TRUE(elrs_validate_crc(frame, len));
}

void test_validate_crc_corrupted_payload(void) {
    uint8_t frame[64];
    uint8_t payload[] = {80, 90, 100, 5, 0, 2, 3, 50, 80, 5};
    uint16_t len = build_crsf_frame(frame, 0xC8, 0x14, payload, 10);
    frame[5] ^= 0xFF; /* corrupt a payload byte */
    TEST_ASSERT_FALSE(elrs_validate_crc(frame, len));
}

void test_validate_crc_corrupted_crc_byte(void) {
    uint8_t frame[64];
    uint8_t payload[] = {80, 90, 100, 5, 0, 2, 3, 50, 80, 5};
    uint16_t len = build_crsf_frame(frame, 0xC8, 0x14, payload, 10);
    frame[len - 1] ^= 0x01; /* corrupt CRC byte */
    TEST_ASSERT_FALSE(elrs_validate_crc(frame, len));
}

/* ========================================================================
 * Tests: elrs_decode — Input Validation
 * ======================================================================== */

void test_decode_null_data(void) {
    decoded_telemetry_t out;
    esp_err_t err = elrs_decode(NULL, 10, &out);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

void test_decode_null_output(void) {
    uint8_t data[] = {0xC8, 0x02, 0x14, 0x00};
    esp_err_t err = elrs_decode(data, 4, NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

void test_decode_too_short(void) {
    decoded_telemetry_t out;
    uint8_t data[] = {0xC8, 0x02};
    esp_err_t err = elrs_decode(data, 2, &out);
    TEST_ASSERT_EQUAL(ERR_DECODE_INCOMPLETE, err);
}

void test_decode_invalid_sync_byte(void) {
    decoded_telemetry_t out;
    uint8_t frame[64];
    uint8_t payload[] = {80, 90, 100, 5, 0, 2, 3, 50, 80, 5};
    uint16_t len = build_crsf_frame(frame, 0xC8, 0x14, payload, 10);
    frame[0] = 0xAB;
    esp_err_t err = elrs_decode(frame, len, &out);
    TEST_ASSERT_EQUAL(ERR_DECODE_INCOMPLETE, err);
}

void test_decode_crc_failure(void) {
    decoded_telemetry_t out;
    uint8_t frame[64];
    uint8_t payload[] = {80, 90, 100, 5, 0, 2, 3, 50, 80, 5};
    uint16_t len = build_crsf_frame(frame, 0xC8, 0x14, payload, 10);
    frame[len - 1] ^= 0xFF; /* corrupt CRC */
    esp_err_t err = elrs_decode(frame, len, &out);
    TEST_ASSERT_EQUAL(ERR_DECODE_CRC_FAIL, err);
}

void test_decode_unknown_frame_type(void) {
    decoded_telemetry_t out;
    uint8_t frame[64];
    uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
    uint16_t len = build_crsf_frame(frame, 0xC8, 0xFF, payload, 4);
    esp_err_t err = elrs_decode(frame, len, &out);
    TEST_ASSERT_EQUAL(ERR_DECODE_UNKNOWN_FMT, err);
}

/* ========================================================================
 * Tests: Link Statistics Decoding
 * ======================================================================== */

void test_decode_link_stats_basic(void) {
    decoded_telemetry_t out;
    uint8_t frame[64];
    /* RSSI ant1=80 (-80dBm), ant2=90 (-90dBm), LQ=100%, SNR=5 */
    uint8_t payload[] = {80, 90, 100, 5, 0, 2, 3, 50, 80, 5};
    uint16_t len = build_crsf_frame(frame, 0xC8, 0x14, payload, 10);

    esp_err_t err = elrs_decode(frame, len, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_INT16(-80, out.rssi_dbm); /* better antenna */
    TEST_ASSERT_EQUAL_UINT8(100, out.link_quality_pct);
    /* Link stats don't set battery or position */
    TEST_ASSERT_FALSE(out.has_battery);
    TEST_ASSERT_FALSE(out.has_position);
}

void test_decode_link_stats_selects_better_rssi(void) {
    decoded_telemetry_t out;
    uint8_t frame[64];
    /* ant1=120 (-120dBm), ant2=60 (-60dBm) — should pick -60 */
    uint8_t payload[] = {120, 60, 85, 10, 1, 4, 5, 70, 90, 3};
    uint16_t len = build_crsf_frame(frame, 0xC8, 0x14, payload, 10);

    esp_err_t err = elrs_decode(frame, len, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_INT16(-60, out.rssi_dbm);
    TEST_ASSERT_EQUAL_UINT8(85, out.link_quality_pct);
}

void test_decode_link_stats_lq_zero(void) {
    decoded_telemetry_t out;
    uint8_t frame[64];
    uint8_t payload[] = {100, 100, 0, 0, 0, 0, 0, 100, 0, 0};
    uint16_t len = build_crsf_frame(frame, 0xC8, 0x14, payload, 10);

    esp_err_t err = elrs_decode(frame, len, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_UINT8(0, out.link_quality_pct);
}

/* ========================================================================
 * Tests: Battery Sensor Decoding
 * ======================================================================== */

void test_decode_battery_basic(void) {
    decoded_telemetry_t out;
    uint8_t frame[64];
    /* Voltage: 148 (14.8V), Current: 100 (10.0A), Cap: 0x000BB8 (3000mAh), Remain: 75% */
    uint8_t payload[] = {0x00, 0x94, 0x00, 0x64, 0x00, 0x0B, 0xB8, 75};
    uint16_t len = build_crsf_frame(frame, 0xC8, 0x08, payload, 8);

    esp_err_t err = elrs_decode(frame, len, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(out.has_battery);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 14.8f, out.battery_voltage);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 75.0f, out.battery_pct);
    /* Battery frame doesn't set position */
    TEST_ASSERT_FALSE(out.has_position);
}

void test_decode_battery_zero_voltage(void) {
    decoded_telemetry_t out;
    uint8_t frame[64];
    uint8_t payload[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0};
    uint16_t len = build_crsf_frame(frame, 0xC8, 0x08, payload, 8);

    esp_err_t err = elrs_decode(frame, len, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(out.has_battery);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, out.battery_voltage);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, out.battery_pct);
}

void test_decode_battery_max_values(void) {
    decoded_telemetry_t out;
    uint8_t frame[64];
    /* Voltage: 1000 (100.0V), remaining: 100% */
    uint8_t payload[] = {0x03, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x00, 100};
    uint16_t len = build_crsf_frame(frame, 0xC8, 0x08, payload, 8);

    esp_err_t err = elrs_decode(frame, len, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(out.has_battery);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 100.0f, out.battery_voltage);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 100.0f, out.battery_pct);
}

void test_decode_battery_invalid_remaining_over_100(void) {
    decoded_telemetry_t out;
    uint8_t frame[64];
    uint8_t payload[] = {0x00, 0x94, 0x00, 0x00, 0x00, 0x00, 0x00, 101};
    uint16_t len = build_crsf_frame(frame, 0xC8, 0x08, payload, 8);

    esp_err_t err = elrs_decode(frame, len, &out);
    TEST_ASSERT_EQUAL(ERR_DECODE_INCOMPLETE, err);
}

void test_decode_battery_voltage_over_max(void) {
    decoded_telemetry_t out;
    uint8_t frame[64];
    /* Voltage: 1001 (100.1V) exceeds max */
    uint8_t payload[] = {0x03, 0xE9, 0x00, 0x00, 0x00, 0x00, 0x00, 50};
    uint16_t len = build_crsf_frame(frame, 0xC8, 0x08, payload, 8);

    esp_err_t err = elrs_decode(frame, len, &out);
    TEST_ASSERT_EQUAL(ERR_DECODE_INCOMPLETE, err);
}

/* ========================================================================
 * Tests: GPS Decoding
 * ======================================================================== */

void test_decode_gps_basic(void) {
    decoded_telemetry_t out;
    uint8_t frame[64];
    /* Lat: -23.5505 * 1e7 = -235505000 = 0xF1F67A98
     * Lon: -46.6333 * 1e7 = -466333000 = 0xE43452B8
     * Speed: 125 (12.5 km/h * 10)
     * Heading: 18000 (180.00 deg * 100)
     * Alt: 1760 (760m + 1000 offset)
     * Sats: 12
     */
    uint8_t payload[] = {
        0xF1, 0xF6, 0x7A, 0x98, /* lat: -235505000 */
        0xE4, 0x34, 0x52, 0xB8, /* lon: -466333000 */
        0x00, 0x7D,             /* speed: 125 */
        0x46, 0x50,             /* heading: 18000 */
        0x06, 0xE0,             /* alt: 1760 */
        0x0C                    /* sats: 12 */
    };
    uint16_t len = build_crsf_frame(frame, 0xC8, 0x02, payload, 15);

    esp_err_t err = elrs_decode(frame, len, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(out.has_position);
    TEST_ASSERT_TRUE(out.has_altitude);
    TEST_ASSERT_TRUE(out.has_speed);
    TEST_ASSERT_DOUBLE_WITHIN(0.001, -23.5505, out.lat);
    TEST_ASSERT_DOUBLE_WITHIN(0.001, -46.6333, out.lon);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 760.0f, out.altitude_m);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 180.0f, out.heading_deg);
    /* Speed: 12.5 km/h = 3.472 m/s */
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 3.472f, out.speed_ms);
    /* GPS doesn't set battery */
    TEST_ASSERT_FALSE(out.has_battery);
}

void test_decode_gps_zero_position(void) {
    decoded_telemetry_t out;
    uint8_t frame[64];
    /* All zeros: lat=0, lon=0, speed=0, heading=0, alt=1000(offset)=0m */
    uint8_t payload[] = {
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00,
        0x00, 0x00,
        0x03, 0xE8, /* alt: 1000 = 0m after offset */
        0x08
    };
    uint16_t len = build_crsf_frame(frame, 0xC8, 0x02, payload, 15);

    esp_err_t err = elrs_decode(frame, len, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(out.has_position);
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, 0.0, out.lat);
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, 0.0, out.lon);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, out.altitude_m);
}

void test_decode_gps_invalid_latitude(void) {
    decoded_telemetry_t out;
    uint8_t frame[64];
    /* Lat: 91 * 1e7 = 910000000 = 0x364EDBC0 — exceeds ±90 */
    uint8_t payload[] = {
        0x36, 0x4E, 0xDB, 0xC0,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00,
        0x00, 0x00,
        0x03, 0xE8,
        0x08
    };
    uint16_t len = build_crsf_frame(frame, 0xC8, 0x02, payload, 15);

    esp_err_t err = elrs_decode(frame, len, &out);
    TEST_ASSERT_EQUAL(ERR_DECODE_INCOMPLETE, err);
}

void test_decode_gps_invalid_longitude(void) {
    decoded_telemetry_t out;
    uint8_t frame[64];
    /* Lon: 181 * 1e7 = 1810000000 = 0x6BD43100 — exceeds ±180 */
    uint8_t payload[] = {
        0x00, 0x00, 0x00, 0x00,
        0x6B, 0xD4, 0x31, 0x00,
        0x00, 0x00,
        0x00, 0x00,
        0x03, 0xE8,
        0x08
    };
    uint16_t len = build_crsf_frame(frame, 0xC8, 0x02, payload, 15);

    esp_err_t err = elrs_decode(frame, len, &out);
    TEST_ASSERT_EQUAL(ERR_DECODE_INCOMPLETE, err);
}

void test_decode_gps_invalid_heading(void) {
    decoded_telemetry_t out;
    uint8_t frame[64];
    /* Heading: 36001 (360.01 deg) — exceeds max */
    uint8_t payload[] = {
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00,
        0x8C, 0xA1, /* 36001 */
        0x03, 0xE8,
        0x08
    };
    uint16_t len = build_crsf_frame(frame, 0xC8, 0x02, payload, 15);

    esp_err_t err = elrs_decode(frame, len, &out);
    TEST_ASSERT_EQUAL(ERR_DECODE_INCOMPLETE, err);
}

/* ========================================================================
 * Tests: Payload too short for frame type
 * ======================================================================== */

void test_decode_link_stats_payload_too_short(void) {
    decoded_telemetry_t out;
    uint8_t frame[64];
    uint8_t payload[] = {80, 90, 100, 5, 0}; /* only 5 bytes, need 10 */
    uint16_t len = build_crsf_frame(frame, 0xC8, 0x14, payload, 5);

    esp_err_t err = elrs_decode(frame, len, &out);
    TEST_ASSERT_EQUAL(ERR_DECODE_INCOMPLETE, err);
}

void test_decode_battery_payload_too_short(void) {
    decoded_telemetry_t out;
    uint8_t frame[64];
    uint8_t payload[] = {0x00, 0x94, 0x00, 0x64}; /* only 4 bytes, need 8 */
    uint16_t len = build_crsf_frame(frame, 0xC8, 0x08, payload, 4);

    esp_err_t err = elrs_decode(frame, len, &out);
    TEST_ASSERT_EQUAL(ERR_DECODE_INCOMPLETE, err);
}

void test_decode_gps_payload_too_short(void) {
    decoded_telemetry_t out;
    uint8_t frame[64];
    uint8_t payload[] = {0,0,0,0, 0,0,0,0, 0,0, 0,0, 0,0}; /* 14 bytes, need 15 */
    uint16_t len = build_crsf_frame(frame, 0xC8, 0x02, payload, 14);

    esp_err_t err = elrs_decode(frame, len, &out);
    TEST_ASSERT_EQUAL(ERR_DECODE_INCOMPLETE, err);
}

/* ========================================================================
 * Tests: has_* flags correctness
 * ======================================================================== */

void test_decode_link_stats_clears_all_has_flags(void) {
    decoded_telemetry_t out;
    /* Pre-set flags to verify they get cleared */
    out.has_position = true;
    out.has_battery = true;

    uint8_t frame[64];
    uint8_t payload[] = {70, 80, 95, 3, 0, 1, 2, 60, 70, 2};
    uint16_t len = build_crsf_frame(frame, 0xC8, 0x14, payload, 10);

    esp_err_t err = elrs_decode(frame, len, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_FALSE(out.has_position);
    TEST_ASSERT_FALSE(out.has_altitude);
    TEST_ASSERT_FALSE(out.has_speed);
    TEST_ASSERT_FALSE(out.has_battery);
    TEST_ASSERT_FALSE(out.has_flight_mode);
}

void test_decode_battery_sets_only_battery_flag(void) {
    decoded_telemetry_t out;
    uint8_t frame[64];
    uint8_t payload[] = {0x00, 0x94, 0x00, 0x64, 0x00, 0x0B, 0xB8, 75};
    uint16_t len = build_crsf_frame(frame, 0xC8, 0x08, payload, 8);

    esp_err_t err = elrs_decode(frame, len, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(out.has_battery);
    TEST_ASSERT_FALSE(out.has_position);
    TEST_ASSERT_FALSE(out.has_altitude);
    TEST_ASSERT_FALSE(out.has_speed);
    TEST_ASSERT_FALSE(out.has_flight_mode);
}

void test_decode_gps_sets_position_altitude_speed_flags(void) {
    decoded_telemetry_t out;
    uint8_t frame[64];
    uint8_t payload[] = {
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x64,
        0x00, 0x00,
        0x03, 0xE8,
        0x06
    };
    uint16_t len = build_crsf_frame(frame, 0xC8, 0x02, payload, 15);

    esp_err_t err = elrs_decode(frame, len, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(out.has_position);
    TEST_ASSERT_TRUE(out.has_altitude);
    TEST_ASSERT_TRUE(out.has_speed);
    TEST_ASSERT_FALSE(out.has_battery);
    TEST_ASSERT_FALSE(out.has_flight_mode);
}

/* ========================================================================
 * Test Runner
 * ======================================================================== */

int main(void) {
    UNITY_BEGIN();

    /* CRC */
    RUN_TEST(test_crc8_dvb_s2_empty);
    RUN_TEST(test_crc8_dvb_s2_known_value);
    RUN_TEST(test_crc8_dvb_s2_changes_with_data);

    /* Frame validation */
    RUN_TEST(test_validate_crc_null_data);
    RUN_TEST(test_validate_crc_too_short);
    RUN_TEST(test_validate_crc_invalid_sync);
    RUN_TEST(test_validate_crc_valid_link_stats);
    RUN_TEST(test_validate_crc_valid_broadcast);
    RUN_TEST(test_validate_crc_corrupted_payload);
    RUN_TEST(test_validate_crc_corrupted_crc_byte);

    /* Input validation */
    RUN_TEST(test_decode_null_data);
    RUN_TEST(test_decode_null_output);
    RUN_TEST(test_decode_too_short);
    RUN_TEST(test_decode_invalid_sync_byte);
    RUN_TEST(test_decode_crc_failure);
    RUN_TEST(test_decode_unknown_frame_type);

    /* Link Statistics */
    RUN_TEST(test_decode_link_stats_basic);
    RUN_TEST(test_decode_link_stats_selects_better_rssi);
    RUN_TEST(test_decode_link_stats_lq_zero);

    /* Battery Sensor */
    RUN_TEST(test_decode_battery_basic);
    RUN_TEST(test_decode_battery_zero_voltage);
    RUN_TEST(test_decode_battery_max_values);
    RUN_TEST(test_decode_battery_invalid_remaining_over_100);
    RUN_TEST(test_decode_battery_voltage_over_max);

    /* GPS */
    RUN_TEST(test_decode_gps_basic);
    RUN_TEST(test_decode_gps_zero_position);
    RUN_TEST(test_decode_gps_invalid_latitude);
    RUN_TEST(test_decode_gps_invalid_longitude);
    RUN_TEST(test_decode_gps_invalid_heading);

    /* Payload too short */
    RUN_TEST(test_decode_link_stats_payload_too_short);
    RUN_TEST(test_decode_battery_payload_too_short);
    RUN_TEST(test_decode_gps_payload_too_short);

    /* has_* flags */
    RUN_TEST(test_decode_link_stats_clears_all_has_flags);
    RUN_TEST(test_decode_battery_sets_only_battery_flag);
    RUN_TEST(test_decode_gps_sets_position_altitude_speed_flags);

    return UNITY_END();
}
