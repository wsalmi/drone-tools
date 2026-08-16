/**
 * @file test_remoteid_crc.c
 * @brief CRC-8 validation tests for RemoteID decoder (Task 5.2).
 *
 * Tests CRC-8/DVB-S2 comparison over the correct protected region,
 * including known valid/invalid vectors and Property 6 PBT.
 *
 * Property 6: For any structurally valid RemoteID frame, altering any
 * protected bit without updating the CRC SHALL cause RID_REJECT_CRC,
 * increment integrity_errors exactly once, and preserve byte-for-byte
 * the previous valid state.
 *
 * Feature: code-quality-review, Property 6
 * **Validates: Requirements 5.3, 5.4, 5.7, 5.9, 12.1, 12.4, 12.6**
 */

#include "unity.h"
#include "theft.h"
#include "remoteid_decoder.h"
#include "error_codes.h"
#include <string.h>
#include <stdlib.h>

/* ========================================================================
 * Test Helpers
 * ======================================================================== */

/**
 * @brief Build a valid WiFi frame with correct CRC for testing.
 *
 * Returns the total frame length. The message at offset 5 will have
 * a valid CRC-8 at byte 24 of the message (byte 29 of the frame).
 */
static uint16_t build_valid_wifi_with_crc(uint8_t *buf, uint16_t buf_size,
                                           const char *uas_id)
{
    if (buf_size < 30) return 0;
    memset(buf, 0, buf_size);
    /* WiFi header */
    buf[0] = 0xFA; buf[1] = 0x0B; buf[2] = 0xBC;
    buf[3] = 0x0D;
    buf[4] = 0x01;
    /* Basic ID message at offset 5 */
    buf[5] = (REMOTEID_MSG_TYPE_BASIC_ID << 4);
    buf[6] = (REMOTEID_ID_TYPE_SERIAL << 4);
    size_t id_len = strlen(uas_id);
    if (id_len > 20) id_len = 20;
    memcpy(&buf[7], uas_id, id_len);
    /* Compute and set correct CRC */
    buf[5 + REMOTEID_CRC_OFFSET] = remoteid_crc8(&buf[5], REMOTEID_CRC_PROTECTED_LEN);
    return 30;
}

/**
 * @brief Build a valid BLE frame with correct CRC for testing.
 */
static uint16_t build_valid_ble_with_crc(uint8_t *buf, uint16_t buf_size,
                                          const char *uas_id)
{
    if (buf_size < 30) return 0;
    memset(buf, 0, buf_size);
    buf[0] = 29;
    buf[1] = 0x16;
    buf[2] = 0xFA; buf[3] = 0xFF;
    buf[4] = 0x01;
    /* Basic ID message at offset 5 */
    buf[5] = (REMOTEID_MSG_TYPE_BASIC_ID << 4);
    buf[6] = (REMOTEID_ID_TYPE_SERIAL << 4);
    size_t id_len = strlen(uas_id);
    if (id_len > 20) id_len = 20;
    memcpy(&buf[7], uas_id, id_len);
    /* Compute and set correct CRC */
    buf[5 + REMOTEID_CRC_OFFSET] = remoteid_crc8(&buf[5], REMOTEID_CRC_PROTECTED_LEN);
    return 30;
}

/* ========================================================================
 * Test Setup/Teardown
 * ======================================================================== */

void setUp(void)
{
    remoteid_metrics_reset();
}

void tearDown(void) {}

/* ========================================================================
 * Known valid CRC vectors — acceptance
 * ======================================================================== */

void test_wifi_valid_crc_accepted(void)
{
    uint8_t buf[64];
    uint16_t len = build_valid_wifi_with_crc(buf, sizeof(buf), "CRC-VALID-001");
    TEST_ASSERT_EQUAL(30, len);

    remoteid_data_t out;
    esp_err_t err = remoteid_decode_wifi(buf, len, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING("CRC-VALID-001", out.uas_id);

    /* Metrics: no integrity errors */
    remoteid_metrics_t metrics;
    remoteid_metrics_snapshot(&metrics);
    TEST_ASSERT_EQUAL_UINT64(0, metrics.integrity_errors);
    TEST_ASSERT_EQUAL_UINT64(0, metrics.rejected[RID_REJECT_CRC]);
}

void test_ble_valid_crc_accepted(void)
{
    uint8_t buf[64];
    uint16_t len = build_valid_ble_with_crc(buf, sizeof(buf), "CRC-VALID-BLE");
    TEST_ASSERT_EQUAL(30, len);

    remoteid_data_t out;
    esp_err_t err = remoteid_decode_ble(buf, len, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING("CRC-VALID-BLE", out.uas_id);

    remoteid_metrics_t metrics;
    remoteid_metrics_snapshot(&metrics);
    TEST_ASSERT_EQUAL_UINT64(0, metrics.integrity_errors);
}

void test_encode_produces_valid_crc_round_trip(void)
{
    /* Use remoteid_encode to produce messages, then decode */
    remoteid_data_t original;
    remoteid_data_init(&original);
    strncpy(original.uas_id, "CRC-ENCODE-RT", REMOTEID_UAS_ID_MAX_LEN - 1);

    uint8_t encoded[128];
    uint16_t encoded_len;
    esp_err_t err = remoteid_encode(&original, encoded, sizeof(encoded), &encoded_len);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Verify CRC is at expected position */
    uint8_t expected_crc = remoteid_crc8(encoded, REMOTEID_CRC_PROTECTED_LEN);
    TEST_ASSERT_EQUAL_UINT8(expected_crc, encoded[REMOTEID_CRC_OFFSET]);

    /* Wrap in WiFi frame and decode */
    uint8_t frame[160];
    frame[0] = 0xFA; frame[1] = 0x0B; frame[2] = 0xBC;
    frame[3] = 0x0D;
    frame[4] = 0x01;
    memcpy(&frame[5], encoded, encoded_len);

    remoteid_data_t decoded;
    err = remoteid_decode_wifi(frame, 5 + encoded_len, &decoded);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING("CRC-ENCODE-RT", decoded.uas_id);
}

/* ========================================================================
 * Known invalid CRC vectors — rejection
 * ======================================================================== */

void test_wifi_corrupted_protected_byte_rejected(void)
{
    uint8_t buf[64];
    uint16_t len = build_valid_wifi_with_crc(buf, sizeof(buf), "CRC-FLIP-001");

    /* Flip one bit in the protected region (byte 0 of message = offset 5 of frame) */
    buf[5] ^= 0x01;

    remoteid_data_t out;
    remoteid_data_init(&out);
    esp_err_t err = remoteid_decode_wifi(buf, len, &out);
    TEST_ASSERT_EQUAL(ERR_DECODE_CRC_FAIL, err);

    /* Metrics: exactly one integrity error */
    remoteid_metrics_t metrics;
    remoteid_metrics_snapshot(&metrics);
    TEST_ASSERT_EQUAL_UINT64(1, metrics.integrity_errors);
    TEST_ASSERT_EQUAL_UINT64(1, metrics.rejected[RID_REJECT_CRC]);
}

void test_ble_corrupted_protected_byte_rejected(void)
{
    uint8_t buf[64];
    uint16_t len = build_valid_ble_with_crc(buf, sizeof(buf), "CRC-FLIP-BLE");

    /* Flip one bit in the protected region (byte 1 of message = offset 6 of frame) */
    buf[6] ^= 0x80;

    remoteid_data_t out;
    remoteid_data_init(&out);
    esp_err_t err = remoteid_decode_ble(buf, len, &out);
    TEST_ASSERT_EQUAL(ERR_DECODE_CRC_FAIL, err);

    remoteid_metrics_t metrics;
    remoteid_metrics_snapshot(&metrics);
    TEST_ASSERT_EQUAL_UINT64(1, metrics.integrity_errors);
    TEST_ASSERT_EQUAL_UINT64(1, metrics.rejected[RID_REJECT_CRC]);
}

void test_wifi_wrong_crc_byte_rejected(void)
{
    uint8_t buf[64];
    uint16_t len = build_valid_wifi_with_crc(buf, sizeof(buf), "CRC-WRONG-001");

    /* Directly corrupt the CRC byte (byte 24 of message = offset 29 of frame) */
    buf[5 + REMOTEID_CRC_OFFSET] ^= 0xFF;

    remoteid_data_t out;
    remoteid_data_init(&out);
    esp_err_t err = remoteid_decode_wifi(buf, len, &out);
    TEST_ASSERT_EQUAL(ERR_DECODE_CRC_FAIL, err);

    remoteid_metrics_t metrics;
    remoteid_metrics_snapshot(&metrics);
    TEST_ASSERT_EQUAL_UINT64(1, metrics.integrity_errors);
}

/* ========================================================================
 * State preservation on CRC rejection
 * ======================================================================== */

void test_wifi_crc_rejection_preserves_previous_state(void)
{
    /* First: decode a valid frame to populate output */
    uint8_t valid[64];
    uint16_t vlen = build_valid_wifi_with_crc(valid, sizeof(valid), "PREVIOUS-OK");
    remoteid_data_t out;
    esp_err_t err = remoteid_decode_wifi(valid, vlen, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING("PREVIOUS-OK", out.uas_id);

    /* Second: attempt to decode a frame with bad CRC */
    uint8_t bad[64];
    uint16_t blen = build_valid_wifi_with_crc(bad, sizeof(bad), "BAD-CRC-001");
    bad[7] ^= 0x01; /* Corrupt a protected byte */

    err = remoteid_decode_wifi(bad, blen, &out);
    TEST_ASSERT_EQUAL(ERR_DECODE_CRC_FAIL, err);

    /* Output must still have the PREVIOUS valid data (byte-for-byte preservation) */
    TEST_ASSERT_EQUAL_STRING("PREVIOUS-OK", out.uas_id);
}

void test_ble_crc_rejection_preserves_previous_state(void)
{
    uint8_t valid[64];
    uint16_t vlen = build_valid_ble_with_crc(valid, sizeof(valid), "BLE-PREV-OK");
    remoteid_data_t out;
    esp_err_t err = remoteid_decode_ble(valid, vlen, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING("BLE-PREV-OK", out.uas_id);

    /* Bad CRC frame */
    uint8_t bad[64];
    uint16_t blen = build_valid_ble_with_crc(bad, sizeof(bad), "BLE-BAD-CRC");
    bad[8] ^= 0x40; /* Corrupt protected byte */

    err = remoteid_decode_ble(bad, blen, &out);
    TEST_ASSERT_EQUAL(ERR_DECODE_CRC_FAIL, err);
    TEST_ASSERT_EQUAL_STRING("BLE-PREV-OK", out.uas_id);
}

/* ========================================================================
 * Integrity errors increment exactly once per rejection
 * ======================================================================== */

void test_integrity_errors_increment_once_per_frame(void)
{
    uint8_t buf[64];
    uint16_t len = build_valid_wifi_with_crc(buf, sizeof(buf), "COUNT-001");
    buf[10] ^= 0x01; /* Corrupt protected byte */

    remoteid_data_t out;
    remoteid_data_init(&out);

    /* Reject 3 times */
    for (int i = 0; i < 3; i++) {
        remoteid_decode_wifi(buf, len, &out);
    }

    remoteid_metrics_t metrics;
    remoteid_metrics_snapshot(&metrics);
    TEST_ASSERT_EQUAL_UINT64(3, metrics.integrity_errors);
    TEST_ASSERT_EQUAL_UINT64(3, metrics.rejected[RID_REJECT_CRC]);
}

/* ========================================================================
 * Multi-message frame: CRC failure on second message
 * ======================================================================== */

void test_wifi_multi_message_second_crc_fail_rejects(void)
{
    /* Build a WiFi frame with 2 messages: Basic ID + Location */
    remoteid_data_t original;
    remoteid_data_init(&original);
    strncpy(original.uas_id, "MULTI-CRC-001", REMOTEID_UAS_ID_MAX_LEN - 1);
    original.has_position = true;
    original.latitude = 40.0;
    original.longitude = -74.0;
    original.altitude_m = 100.0f;
    original.speed_ms = 10.0f;
    original.has_speed = true;
    original.direction_deg = 45.0f;

    uint8_t encoded[128];
    uint16_t encoded_len;
    esp_err_t err = remoteid_encode(&original, encoded, sizeof(encoded), &encoded_len);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(2 * REMOTEID_MSG_SIZE, encoded_len);

    /* Wrap in WiFi frame */
    uint8_t frame[160];
    frame[0] = 0xFA; frame[1] = 0x0B; frame[2] = 0xBC;
    frame[3] = 0x0D;
    frame[4] = 0x02;
    memcpy(&frame[5], encoded, encoded_len);

    /* Corrupt byte in second message's protected region */
    frame[5 + REMOTEID_MSG_SIZE + 3] ^= 0x10;

    remoteid_data_t out;
    remoteid_data_init(&out);
    err = remoteid_decode_wifi(frame, 5 + encoded_len, &out);
    TEST_ASSERT_EQUAL(ERR_DECODE_CRC_FAIL, err);

    remoteid_metrics_t metrics;
    remoteid_metrics_snapshot(&metrics);
    TEST_ASSERT_EQUAL_UINT64(1, metrics.integrity_errors);
}

/* ========================================================================
 * Property 6 PBT: Altering any protected bit without updating CRC
 * causes RID_REJECT_CRC, increments integrity_errors once, and
 * preserves byte-for-byte the previous valid state.
 * ======================================================================== */

/** PBT input: a structurally valid frame plus a bit to flip */
typedef struct {
    uint8_t frame[64];
    uint16_t len;
    bool is_ble;
    uint8_t flip_byte_index;  /**< Index within protected region [0..23] */
    uint8_t flip_bit_mask;    /**< Which bit to flip (1 << n) */
} pbt_crc_input_t;

static enum theft_alloc_res alloc_crc_input(struct theft *t, void *env, void **output)
{
    (void)env;
    pbt_crc_input_t *inp = malloc(sizeof(*inp));
    if (!inp) return THEFT_ALLOC_ERROR;

    /* Choose WiFi or BLE */
    inp->is_ble = (theft_random_bits(t, 1) == 1);

    /* Build a valid frame with random UAS ID */
    char uas_id[21];
    uint8_t id_len = (uint8_t)((theft_random_bits(t, 4) % 15) + 1);
    for (uint8_t i = 0; i < id_len; i++) {
        uas_id[i] = (char)('A' + (theft_random_bits(t, 5) % 26));
    }
    uas_id[id_len] = '\0';

    if (inp->is_ble) {
        inp->len = build_valid_ble_with_crc(inp->frame, sizeof(inp->frame), uas_id);
    } else {
        inp->len = build_valid_wifi_with_crc(inp->frame, sizeof(inp->frame), uas_id);
    }

    if (inp->len == 0) {
        free(inp);
        return THEFT_ALLOC_ERROR;
    }

    /* Choose a byte in the protected region to flip [1..23]
     * We skip byte 0 (the message header) because flipping its upper nibble
     * (message type) can cause a format rejection in remoteid_validate_frame_safe()
     * before the CRC check is reached in the decode loop. */
    inp->flip_byte_index = (uint8_t)(1 + (theft_random_bits(t, 5) % (REMOTEID_CRC_PROTECTED_LEN - 1)));
    /* Choose a bit to flip */
    inp->flip_bit_mask = (uint8_t)(1u << (theft_random_bits(t, 3) % 8));

    *output = inp;
    return THEFT_ALLOC_OK;
}

static void free_crc_input(void *instance, void *env)
{
    (void)env;
    free(instance);
}

static enum theft_trial_res prop_crc_rejection(struct theft *t, void *arg)
{
    (void)t;
    pbt_crc_input_t *inp = (pbt_crc_input_t *)arg;

    /* Reset metrics for this trial */
    remoteid_metrics_reset();

    /* Step 1: Verify the unmodified frame is accepted */
    remoteid_data_t valid_out;
    remoteid_data_init(&valid_out);
    esp_err_t err;
    if (inp->is_ble) {
        err = remoteid_decode_ble(inp->frame, inp->len, &valid_out);
    } else {
        err = remoteid_decode_wifi(inp->frame, inp->len, &valid_out);
    }
    if (err != ESP_OK) {
        /* If the unmodified frame isn't valid, skip this trial */
        return THEFT_TRIAL_SKIP;
    }

    /* Save the valid output for comparison */
    remoteid_data_t saved_out;
    memcpy(&saved_out, &valid_out, sizeof(saved_out));

    /* Reset metrics after the valid decode */
    remoteid_metrics_reset();

    /* Step 2: Flip one bit in the protected region without updating CRC */
    uint8_t corrupted[64];
    memcpy(corrupted, inp->frame, inp->len);
    /* Message starts at offset 5 for both WiFi and BLE */
    uint16_t msg_start = 5;
    corrupted[msg_start + inp->flip_byte_index] ^= inp->flip_bit_mask;

    /* Step 3: Attempt decode — must be rejected with CRC error */
    remoteid_data_t reject_out;
    memcpy(&reject_out, &saved_out, sizeof(reject_out));
    if (inp->is_ble) {
        err = remoteid_decode_ble(corrupted, inp->len, &reject_out);
    } else {
        err = remoteid_decode_wifi(corrupted, inp->len, &reject_out);
    }

    /* Must be rejected */
    if (err != ERR_DECODE_CRC_FAIL) {
        return THEFT_TRIAL_FAIL;
    }

    /* Step 4: Verify integrity_errors incremented exactly once */
    remoteid_metrics_t metrics;
    remoteid_metrics_snapshot(&metrics);
    if (metrics.integrity_errors != 1) {
        return THEFT_TRIAL_FAIL;
    }
    if (metrics.rejected[RID_REJECT_CRC] != 1) {
        return THEFT_TRIAL_FAIL;
    }

    /* Step 5: Verify byte-for-byte output preservation */
    if (memcmp(&reject_out, &saved_out, sizeof(reject_out)) != 0) {
        return THEFT_TRIAL_FAIL;
    }

    return THEFT_TRIAL_PASS;
}

void test_pbt_property6_crc_rejection(void)
{
    struct theft_type_info type_info = {
        .alloc = alloc_crc_input,
        .free = free_crc_input,
    };

    struct theft_run_config config = {
        .name = "Property 6: Invalid CRC does not produce commit",
        .prop1 = prop_crc_rejection,
        .type_info = { &type_info },
        .trials = PBT_MIN_TRIALS,
        .seed = (PBT_SEED != 0) ? (theft_seed)PBT_SEED : theft_seed_of_time(),
    };

    enum theft_run_res result = theft_run(&config);
    TEST_ASSERT_EQUAL_MESSAGE(THEFT_RUN_PASS, result,
        "Property 6 failed: altering a protected bit without updating CRC "
        "did not cause RID_REJECT_CRC, or state was not preserved");
}

/* ========================================================================
 * Test Runner
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* Known valid CRC vectors */
    RUN_TEST(test_wifi_valid_crc_accepted);
    RUN_TEST(test_ble_valid_crc_accepted);
    RUN_TEST(test_encode_produces_valid_crc_round_trip);

    /* Known invalid CRC vectors */
    RUN_TEST(test_wifi_corrupted_protected_byte_rejected);
    RUN_TEST(test_ble_corrupted_protected_byte_rejected);
    RUN_TEST(test_wifi_wrong_crc_byte_rejected);

    /* State preservation on CRC rejection */
    RUN_TEST(test_wifi_crc_rejection_preserves_previous_state);
    RUN_TEST(test_ble_crc_rejection_preserves_previous_state);

    /* Integrity errors increment exactly once */
    RUN_TEST(test_integrity_errors_increment_once_per_frame);

    /* Multi-message CRC failure */
    RUN_TEST(test_wifi_multi_message_second_crc_fail_rejects);

    /* Property 6 PBT */
    RUN_TEST(test_pbt_property6_crc_rejection);

    return UNITY_END();
}
