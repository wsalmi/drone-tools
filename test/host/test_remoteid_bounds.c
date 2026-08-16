/**
 * @file test_remoteid_bounds.c
 * @brief Bounds safety tests for RemoteID decoder (Task 5.1).
 *
 * Tests safe bounds validation, parse-then-commit, and rejection reasons
 * for the RemoteID decoder. Covers empty buffers, truncation at each field
 * boundary, declared size mismatches, and oversized inputs.
 *
 * Demonstrates red on the reproduced defect (CQR-REMOTEID-003: no bounds
 * validation on declared size, offsets, or field access) and green under
 * ASan+UBSan after remediation.
 *
 * Feature: code-quality-review
 * Validates: Requirements 5.1, 5.2, 5.7, 5.8, 5.9, 12.1, 12.6
 */

#include "unity.h"
#include "remoteid_decoder.h"
#include "error_codes.h"
#include <string.h>

/* ========================================================================
 * Test Helpers
 * ======================================================================== */

/** Sentinel pattern to detect output corruption */
#define SENTINEL_BYTE 0xAA

/**
 * @brief Fill a remoteid_data_t with a known sentinel pattern.
 *        If the decoder writes partial results on rejection,
 *        we can detect it.
 */
static void fill_with_sentinel(remoteid_data_t *data)
{
    memset(data, SENTINEL_BYTE, sizeof(*data));
}

/**
 * @brief Verify that a remoteid_data_t still has the sentinel pattern
 *        (i.e., was NOT modified by a rejected decode).
 */
static bool has_sentinel(const remoteid_data_t *data)
{
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0; i < sizeof(*data); i++) {
        if (bytes[i] != SENTINEL_BYTE) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Build a minimal valid WiFi frame (header + Basic ID with valid UAS ID).
 */
static uint16_t build_valid_wifi_frame(uint8_t *buf, uint16_t buf_size)
{
    /* WiFi: OUI(3) + OUI_type(1) + counter(1) + message(25) = 30 */
    if (buf_size < 30) return 0;
    memset(buf, 0, buf_size);
    buf[0] = 0xFA; buf[1] = 0x0B; buf[2] = 0xBC; /* OUI */
    buf[3] = 0x0D; /* OUI Type */
    buf[4] = 0x01; /* counter */
    /* Basic ID message at offset 5 */
    buf[5] = (REMOTEID_MSG_TYPE_BASIC_ID << 4); /* header */
    buf[6] = (1 << 4); /* ID type: serial */
    memcpy(&buf[7], "BOUNDS-TEST-001", 15); /* UAS ID */
    /* Compute CRC-8 over protected region (first 24 bytes of message) */
    buf[5 + REMOTEID_CRC_OFFSET] = remoteid_crc8(&buf[5], REMOTEID_CRC_PROTECTED_LEN);
    return 30;
}

/**
 * @brief Build a minimal valid BLE frame (header + Basic ID with valid UAS ID).
 */
static uint16_t build_valid_ble_frame(uint8_t *buf, uint16_t buf_size)
{
    /* BLE: AD_len(1) + AD_type(1) + UUID(2) + counter(1) + message(25) = 30 */
    if (buf_size < 30) return 0;
    memset(buf, 0, buf_size);
    buf[0] = 29; /* AD length: type(1) + UUID(2) + counter(1) + msg(25) */
    buf[1] = 0x16; /* AD type: Service Data */
    buf[2] = 0xFA; buf[3] = 0xFF; /* UUID16 = 0xFFFA LE */
    buf[4] = 0x01; /* counter */
    /* Basic ID message at offset 5 */
    buf[5] = (REMOTEID_MSG_TYPE_BASIC_ID << 4);
    buf[6] = (1 << 4); /* ID type: serial */
    memcpy(&buf[7], "BLE-BOUNDS-001", 14);
    /* Compute CRC-8 over protected region (first 24 bytes of message) */
    buf[5 + REMOTEID_CRC_OFFSET] = remoteid_crc8(&buf[5], REMOTEID_CRC_PROTECTED_LEN);
    return 30;
}

/* ========================================================================
 * Test Setup/Teardown
 * ======================================================================== */

void setUp(void) {}
void tearDown(void) {}

/* ========================================================================
 * Empty buffer (0 bytes)
 * ======================================================================== */

void test_wifi_empty_buffer_rejects_truncated(void)
{
    uint8_t buf[1] = {0};
    remoteid_data_t out;
    fill_with_sentinel(&out);

    esp_err_t err = remoteid_decode_wifi(buf, 0, &out);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
    /* Output must not have been modified (parse-then-commit) */
    TEST_ASSERT_TRUE(has_sentinel(&out));
}

void test_ble_empty_buffer_rejects_truncated(void)
{
    uint8_t buf[1] = {0};
    remoteid_data_t out;
    fill_with_sentinel(&out);

    esp_err_t err = remoteid_decode_ble(buf, 0, &out);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(has_sentinel(&out));
}

void test_validate_safe_empty_wifi(void)
{
    uint8_t buf[1] = {0};
    remoteid_validation_result_t result;

    esp_err_t err = remoteid_validate_frame_safe(buf, 0, false, &result);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
    TEST_ASSERT_FALSE(result.valid);
    TEST_ASSERT_EQUAL(RID_REJECT_TRUNCATED, result.reason);
}

void test_validate_safe_empty_ble(void)
{
    uint8_t buf[1] = {0};
    remoteid_validation_result_t result;

    esp_err_t err = remoteid_validate_frame_safe(buf, 0, true, &result);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
    TEST_ASSERT_FALSE(result.valid);
    TEST_ASSERT_EQUAL(RID_REJECT_TRUNCATED, result.reason);
}

/* ========================================================================
 * NULL pointer rejection
 * ======================================================================== */

void test_validate_safe_null_frame(void)
{
    remoteid_validation_result_t result;
    esp_err_t err = remoteid_validate_frame_safe(NULL, 50, false, &result);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
    TEST_ASSERT_FALSE(result.valid);
    TEST_ASSERT_EQUAL(RID_REJECT_NULL, result.reason);
}

void test_validate_safe_null_result(void)
{
    uint8_t buf[50] = {0};
    esp_err_t err = remoteid_validate_frame_safe(buf, 50, false, NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

/* ========================================================================
 * Minimum valid frame
 * ======================================================================== */

void test_wifi_minimum_valid_frame(void)
{
    uint8_t buf[64];
    uint16_t len = build_valid_wifi_frame(buf, sizeof(buf));
    TEST_ASSERT_EQUAL(30, len);

    remoteid_data_t out;
    esp_err_t err = remoteid_decode_wifi(buf, len, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING("BOUNDS-TEST-001", out.uas_id);
}

void test_ble_minimum_valid_frame(void)
{
    uint8_t buf[64];
    uint16_t len = build_valid_ble_frame(buf, sizeof(buf));
    TEST_ASSERT_EQUAL(30, len);

    remoteid_data_t out;
    esp_err_t err = remoteid_decode_ble(buf, len, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING("BLE-BOUNDS-001", out.uas_id);
}

/* ========================================================================
 * Truncation at each field boundary (WiFi)
 * ======================================================================== */

void test_wifi_truncated_at_oui(void)
{
    /* Only 2 bytes of OUI — too short */
    uint8_t buf[2] = {0xFA, 0x0B};
    remoteid_data_t out;
    fill_with_sentinel(&out);

    esp_err_t err = remoteid_decode_wifi(buf, 2, &out);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(has_sentinel(&out));
}

void test_wifi_truncated_at_oui_type(void)
{
    /* OUI(3) + OUI_type partial = 4 bytes — too short */
    uint8_t buf[4] = {0xFA, 0x0B, 0xBC, 0x0D};
    remoteid_data_t out;
    fill_with_sentinel(&out);

    esp_err_t err = remoteid_decode_wifi(buf, 4, &out);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(has_sentinel(&out));
}

void test_wifi_truncated_at_counter(void)
{
    /* OUI(3) + OUI_type(1) + counter(1) = 5 bytes — no message data */
    uint8_t buf[5] = {0xFA, 0x0B, 0xBC, 0x0D, 0x01};
    remoteid_data_t out;
    fill_with_sentinel(&out);

    esp_err_t err = remoteid_decode_wifi(buf, 5, &out);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(has_sentinel(&out));
}

void test_wifi_truncated_mid_message(void)
{
    /* Header + partial message: 5 header + 12 bytes of message = 17 */
    uint8_t buf[64];
    build_valid_wifi_frame(buf, sizeof(buf));
    remoteid_data_t out;
    fill_with_sentinel(&out);

    /* Only provide 17 bytes (message truncated) */
    esp_err_t err = remoteid_decode_wifi(buf, 17, &out);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(has_sentinel(&out));
}

void test_wifi_truncated_one_byte_short(void)
{
    /* Frame is 29 bytes = minimum(30) - 1 */
    uint8_t buf[64];
    build_valid_wifi_frame(buf, sizeof(buf));
    remoteid_data_t out;
    fill_with_sentinel(&out);

    esp_err_t err = remoteid_decode_wifi(buf, 29, &out);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(has_sentinel(&out));
}

/* ========================================================================
 * Truncation at each field boundary (BLE)
 * ======================================================================== */

void test_ble_truncated_at_ad_type(void)
{
    uint8_t buf[2] = {29, 0x16};
    remoteid_data_t out;
    fill_with_sentinel(&out);

    esp_err_t err = remoteid_decode_ble(buf, 2, &out);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(has_sentinel(&out));
}

void test_ble_truncated_at_uuid(void)
{
    uint8_t buf[4] = {29, 0x16, 0xFA, 0xFF};
    remoteid_data_t out;
    fill_with_sentinel(&out);

    esp_err_t err = remoteid_decode_ble(buf, 4, &out);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(has_sentinel(&out));
}

void test_ble_truncated_one_byte_short(void)
{
    uint8_t buf[64];
    build_valid_ble_frame(buf, sizeof(buf));
    remoteid_data_t out;
    fill_with_sentinel(&out);

    /* 29 bytes = minimum(29 for BLE_MIN_LEN) - but wait, REMOTEID_BLE_MIN_LEN is 29.
     * So 28 is one short. */
    esp_err_t err = remoteid_decode_ble(buf, 28, &out);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(has_sentinel(&out));
}

/* ========================================================================
 * Declared size smaller than actual (BLE AD length)
 * ======================================================================== */

void test_ble_declared_length_smaller_than_minimum(void)
{
    uint8_t buf[64];
    build_valid_ble_frame(buf, sizeof(buf));
    /* Set AD length to 10 (much less than required 29) */
    buf[0] = 10;
    remoteid_data_t out;
    fill_with_sentinel(&out);

    esp_err_t err = remoteid_decode_ble(buf, 30, &out);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(has_sentinel(&out));
}

/* ========================================================================
 * Declared size larger than buffer (BLE AD length > available)
 * ======================================================================== */

void test_ble_declared_length_larger_than_buffer(void)
{
    uint8_t buf[30];
    build_valid_ble_frame(buf, sizeof(buf));
    /* Set AD length to 200, but only 30 bytes available */
    buf[0] = 200;
    remoteid_data_t out;
    fill_with_sentinel(&out);

    esp_err_t err = remoteid_decode_ble(buf, 30, &out);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(has_sentinel(&out));
}

/* ========================================================================
 * Oversized buffer (> max messages)
 * ======================================================================== */

void test_wifi_oversized_many_messages(void)
{
    /* Build a frame with 11 messages (exceeds REMOTEID_MAX_MESSAGES=10) */
    uint16_t frame_size = 5 + (11 * REMOTEID_MSG_SIZE); /* 5 + 275 = 280 */
    uint8_t buf[300];
    memset(buf, 0, sizeof(buf));
    buf[0] = 0xFA; buf[1] = 0x0B; buf[2] = 0xBC;
    buf[3] = 0x0D;
    buf[4] = 11; /* counter says 11 */
    /* First message is a valid Basic ID */
    buf[5] = (REMOTEID_MSG_TYPE_BASIC_ID << 4);
    buf[6] = (1 << 4);
    memcpy(&buf[7], "OVERSIZED-001", 13);
    buf[5 + REMOTEID_CRC_OFFSET] = remoteid_crc8(&buf[5], REMOTEID_CRC_PROTECTED_LEN);
    /* Fill remaining messages with Location type (non-Basic-ID) to avoid
     * overriding the UAS ID with an empty one */
    for (int i = 1; i < 11; i++) {
        uint16_t offset = 5 + (uint16_t)(i * REMOTEID_MSG_SIZE);
        buf[offset] = (REMOTEID_MSG_TYPE_LOCATION << 4);
        buf[offset + REMOTEID_CRC_OFFSET] = remoteid_crc8(&buf[offset], REMOTEID_CRC_PROTECTED_LEN);
    }

    remoteid_data_t out;
    esp_err_t err = remoteid_decode_wifi(buf, frame_size, &out);
    /* Should succeed — the decoder caps at MAX_MESSAGES */
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING("OVERSIZED-001", out.uas_id);
}

/* ========================================================================
 * Parse-then-commit: rejected input preserves previous output
 * ======================================================================== */

void test_parse_then_commit_preserves_previous_on_rejection(void)
{
    /* First: decode a valid frame to populate 'out' */
    uint8_t valid[64];
    uint16_t vlen = build_valid_wifi_frame(valid, sizeof(valid));
    remoteid_data_t out;
    esp_err_t err = remoteid_decode_wifi(valid, vlen, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING("BOUNDS-TEST-001", out.uas_id);

    /* Second: attempt to decode an invalid (truncated) frame */
    uint8_t invalid[5] = {0xFA, 0x0B, 0xBC, 0x0D, 0x01};
    err = remoteid_decode_wifi(invalid, 5, &out);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);

    /* The output should still contain the PREVIOUS valid result —
     * i.e., the decoder did NOT overwrite it before failing. */
    TEST_ASSERT_EQUAL_STRING("BOUNDS-TEST-001", out.uas_id);
}

void test_ble_parse_then_commit_preserves_previous(void)
{
    /* First: decode valid BLE frame */
    uint8_t valid[64];
    uint16_t vlen = build_valid_ble_frame(valid, sizeof(valid));
    remoteid_data_t out;
    esp_err_t err = remoteid_decode_ble(valid, vlen, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING("BLE-BOUNDS-001", out.uas_id);

    /* Second: attempt invalid */
    uint8_t invalid[10] = {29, 0x16, 0xFA, 0xFF, 0x01};
    err = remoteid_decode_ble(invalid, 10, &out);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);

    /* Previous state preserved */
    TEST_ASSERT_EQUAL_STRING("BLE-BOUNDS-001", out.uas_id);
}

/* ========================================================================
 * Rejection reason observability (remoteid_validate_frame_safe)
 * ======================================================================== */

void test_validate_safe_wifi_valid_frame(void)
{
    uint8_t buf[64];
    uint16_t len = build_valid_wifi_frame(buf, sizeof(buf));
    remoteid_validation_result_t result;

    esp_err_t err = remoteid_validate_frame_safe(buf, len, false, &result);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(result.valid);
    TEST_ASSERT_EQUAL(RID_REJECT_NONE, result.reason);
    TEST_ASSERT_EQUAL(1, result.message_count);
    TEST_ASSERT_EQUAL(25, result.available_payload);
}

void test_validate_safe_ble_valid_frame(void)
{
    uint8_t buf[64];
    uint16_t len = build_valid_ble_frame(buf, sizeof(buf));
    remoteid_validation_result_t result;

    esp_err_t err = remoteid_validate_frame_safe(buf, len, true, &result);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(result.valid);
    TEST_ASSERT_EQUAL(RID_REJECT_NONE, result.reason);
    TEST_ASSERT_EQUAL(1, result.message_count);
}

void test_validate_safe_wifi_format_rejection(void)
{
    uint8_t buf[64];
    build_valid_wifi_frame(buf, sizeof(buf));
    /* Corrupt OUI */
    buf[0] = 0x00;
    remoteid_validation_result_t result;

    esp_err_t err = remoteid_validate_frame_safe(buf, 30, false, &result);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
    TEST_ASSERT_FALSE(result.valid);
    TEST_ASSERT_EQUAL(RID_REJECT_FORMAT, result.reason);
}

void test_validate_safe_ble_format_rejection(void)
{
    uint8_t buf[64];
    build_valid_ble_frame(buf, sizeof(buf));
    /* Wrong AD type */
    buf[1] = 0x20;
    remoteid_validation_result_t result;

    esp_err_t err = remoteid_validate_frame_safe(buf, 30, true, &result);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
    TEST_ASSERT_FALSE(result.valid);
    TEST_ASSERT_EQUAL(RID_REJECT_FORMAT, result.reason);
}

void test_validate_safe_ble_declared_length_rejection(void)
{
    uint8_t buf[64];
    build_valid_ble_frame(buf, sizeof(buf));
    /* AD length says 200 but only 30 available */
    buf[0] = 200;
    remoteid_validation_result_t result;

    esp_err_t err = remoteid_validate_frame_safe(buf, 30, true, &result);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
    TEST_ASSERT_FALSE(result.valid);
    TEST_ASSERT_EQUAL(RID_REJECT_DECLARED_LENGTH, result.reason);
}

/* ========================================================================
 * Regression: original defect reproduced (CQR-REMOTEID-003)
 *
 * The original decoder trusted caller's minimum frame check without
 * per-field validation. This test ensures that a frame with valid header
 * but insufficient bytes for message content is now properly rejected.
 * ======================================================================== */

void test_regression_cqr_remoteid_003_wifi_header_only(void)
{
    /* A frame with valid WiFi header (5 bytes) + only 10 bytes of message
     * data (not enough for a complete 25-byte message).
     * OLD BEHAVIOR: would read past buffer bounds
     * NEW BEHAVIOR: rejected with RID_REJECT_TRUNCATED or RID_REJECT_BOUNDS */
    uint8_t buf[15];
    memset(buf, 0, sizeof(buf));
    buf[0] = 0xFA; buf[1] = 0x0B; buf[2] = 0xBC;
    buf[3] = 0x0D;
    buf[4] = 0x01; /* claims 1 message */
    /* Only 10 bytes of "message" — insufficient */

    remoteid_data_t out;
    fill_with_sentinel(&out);

    esp_err_t err = remoteid_decode_wifi(buf, 15, &out);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
    /* Output must not be corrupted */
    TEST_ASSERT_TRUE(has_sentinel(&out));
}

void test_regression_cqr_remoteid_003_ble_truncated_message(void)
{
    /* BLE frame with valid header but message truncated at 10 bytes */
    uint8_t buf[15];
    memset(buf, 0, sizeof(buf));
    buf[0] = 29; /* claims 29 bytes of content */
    buf[1] = 0x16;
    buf[2] = 0xFA; buf[3] = 0xFF;
    buf[4] = 0x01;
    /* Only 10 bytes total — message area is 5 bytes (insufficient) */

    remoteid_data_t out;
    fill_with_sentinel(&out);

    esp_err_t err = remoteid_decode_ble(buf, 15, &out);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(has_sentinel(&out));
}

/* ========================================================================
 * Test Runner
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* Empty buffer */
    RUN_TEST(test_wifi_empty_buffer_rejects_truncated);
    RUN_TEST(test_ble_empty_buffer_rejects_truncated);
    RUN_TEST(test_validate_safe_empty_wifi);
    RUN_TEST(test_validate_safe_empty_ble);

    /* NULL pointer */
    RUN_TEST(test_validate_safe_null_frame);
    RUN_TEST(test_validate_safe_null_result);

    /* Minimum valid frame */
    RUN_TEST(test_wifi_minimum_valid_frame);
    RUN_TEST(test_ble_minimum_valid_frame);

    /* Truncation at field boundaries - WiFi */
    RUN_TEST(test_wifi_truncated_at_oui);
    RUN_TEST(test_wifi_truncated_at_oui_type);
    RUN_TEST(test_wifi_truncated_at_counter);
    RUN_TEST(test_wifi_truncated_mid_message);
    RUN_TEST(test_wifi_truncated_one_byte_short);

    /* Truncation at field boundaries - BLE */
    RUN_TEST(test_ble_truncated_at_ad_type);
    RUN_TEST(test_ble_truncated_at_uuid);
    RUN_TEST(test_ble_truncated_one_byte_short);

    /* Declared size mismatches */
    RUN_TEST(test_ble_declared_length_smaller_than_minimum);
    RUN_TEST(test_ble_declared_length_larger_than_buffer);

    /* Oversized */
    RUN_TEST(test_wifi_oversized_many_messages);

    /* Parse-then-commit */
    RUN_TEST(test_parse_then_commit_preserves_previous_on_rejection);
    RUN_TEST(test_ble_parse_then_commit_preserves_previous);

    /* Rejection reason observability */
    RUN_TEST(test_validate_safe_wifi_valid_frame);
    RUN_TEST(test_validate_safe_ble_valid_frame);
    RUN_TEST(test_validate_safe_wifi_format_rejection);
    RUN_TEST(test_validate_safe_ble_format_rejection);
    RUN_TEST(test_validate_safe_ble_declared_length_rejection);

    /* Regression: CQR-REMOTEID-003 */
    RUN_TEST(test_regression_cqr_remoteid_003_wifi_header_only);
    RUN_TEST(test_regression_cqr_remoteid_003_ble_truncated_message);

    return UNITY_END();
}
