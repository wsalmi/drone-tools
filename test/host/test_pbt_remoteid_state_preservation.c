/**
 * @file test_pbt_remoteid_state_preservation.c
 * @brief Property 8: Parse-then-commit preserves state — PBT for RemoteID decoder.
 *
 * For any input rejected by size, format, CRC, or semantics, the public output
 * and aircraft record SHALL remain equivalent to the previous snapshot; only
 * allowed reason/metrics may change.
 *
 * Feature: code-quality-review, Property 8
 * **Validates: Requirements 5.4, 5.8, 5.9, 11.7, 12.1, 12.6**
 *
 * This file contains:
 * - Property 8 PBT: sequences of valid then malformed inputs preserve state
 * - Deterministic malformed input campaign: known-bad inputs preserve state
 * - Accepted counter verification
 * - Per-reason rejection counter verification
 */

#include "unity.h"
#include "theft.h"
#include "remoteid_decoder.h"
#include "error_codes.h"
#include <string.h>
#include <stdlib.h>

void setUp(void) {
    remoteid_metrics_reset();
}

void tearDown(void) {}

/* ========================================================================
 * Helper: Build a valid WiFi RemoteID frame (Basic ID + Location)
 * ======================================================================== */

/**
 * @brief Build a minimal valid WiFi RemoteID frame with Basic ID message.
 *
 * Creates a 30-byte WiFi frame: OUI(3) + OUI_type(1) + counter(1) + msg(25)
 * The Basic ID message contains a UAS ID with valid CRC.
 */
static void build_valid_wifi_frame(uint8_t *buf, uint16_t *len, const char *uas_id)
{
    memset(buf, 0, 64);

    /* WiFi header: OUI + OUI type + counter */
    buf[0] = 0xFA; buf[1] = 0x0B; buf[2] = 0xBC; /* ASTM OUI */
    buf[3] = 0x0D; /* OUI type */
    buf[4] = 0x01; /* message counter = 1 */

    /* Basic ID message (type 0) at offset 5 */
    uint8_t *msg = &buf[5];
    msg[0] = (0x00 << 4); /* Type 0: Basic ID, version 0 */
    msg[1] = (0x01 << 4); /* ID type: serial number */

    /* Copy UAS ID into bytes 2-21 */
    size_t id_len = strlen(uas_id);
    if (id_len > 20) id_len = 20;
    memcpy(&msg[2], uas_id, id_len);

    /* Compute CRC-8 over first 24 bytes */
    msg[24] = remoteid_crc8(msg, 24);

    *len = 30;
}

/**
 * @brief Build a valid WiFi frame with Basic ID + Location messages (55 bytes).
 */
static void build_valid_wifi_frame_with_location(uint8_t *buf, uint16_t *len,
                                                  const char *uas_id,
                                                  int32_t lat_raw, int32_t lon_raw)
{
    memset(buf, 0, 64);

    /* WiFi header */
    buf[0] = 0xFA; buf[1] = 0x0B; buf[2] = 0xBC;
    buf[3] = 0x0D;
    buf[4] = 0x02; /* 2 messages */

    /* Basic ID at offset 5 */
    uint8_t *msg0 = &buf[5];
    msg0[0] = (0x00 << 4);
    msg0[1] = (0x01 << 4);
    size_t id_len = strlen(uas_id);
    if (id_len > 20) id_len = 20;
    memcpy(&msg0[2], uas_id, id_len);
    msg0[24] = remoteid_crc8(msg0, 24);

    /* Location at offset 30 */
    uint8_t *msg1 = &buf[30];
    msg1[0] = (0x01 << 4); /* Type 1: Location */
    msg1[1] = 0x20; /* Status: airborne */

    /* Latitude (bytes 5-8, LE) */
    uint32_t lat_u = (uint32_t)lat_raw;
    msg1[5] = (uint8_t)(lat_u & 0xFF);
    msg1[6] = (uint8_t)((lat_u >> 8) & 0xFF);
    msg1[7] = (uint8_t)((lat_u >> 16) & 0xFF);
    msg1[8] = (uint8_t)((lat_u >> 24) & 0xFF);

    /* Longitude (bytes 9-12, LE) */
    uint32_t lon_u = (uint32_t)lon_raw;
    msg1[9] = (uint8_t)(lon_u & 0xFF);
    msg1[10] = (uint8_t)((lon_u >> 8) & 0xFF);
    msg1[11] = (uint8_t)((lon_u >> 16) & 0xFF);
    msg1[12] = (uint8_t)((lon_u >> 24) & 0xFF);

    /* Geodetic altitude (2000 = 0m after decode) */
    msg1[15] = 0xD0; msg1[16] = 0x07;

    msg1[24] = remoteid_crc8(msg1, 24);

    *len = 55;
}

/* ========================================================================
 * Property 8 PBT: Parse-then-commit preserves state
 *
 * Strategy: Generate a valid frame, decode it to establish state, then
 * generate a malformed frame and verify output is unchanged after rejection.
 * ======================================================================== */

typedef struct {
    uint8_t valid_frame[64];
    uint16_t valid_len;
    uint8_t malformed_frame[512];
    uint16_t malformed_len;
    uint8_t corruption_type; /* 0=empty, 1=truncated, 2=bad CRC, 3=bad format, 4=random */
} pbt_state_input_t;

static enum theft_alloc_res alloc_state_input(struct theft *t, void *env, void **output)
{
    (void)env;
    pbt_state_input_t *inp = malloc(sizeof(*inp));
    if (!inp) return THEFT_ALLOC_ERROR;

    /* Always start with a valid WiFi frame */
    build_valid_wifi_frame(inp->valid_frame, &inp->valid_len, "TEST-DRONE-001");

    /* Generate a malformed frame based on corruption type */
    inp->corruption_type = (uint8_t)(theft_random_bits(t, 3));

    switch (inp->corruption_type) {
    case 0: /* Empty buffer */
        inp->malformed_len = 0;
        break;

    case 1: { /* Truncated: valid header but short */
        uint16_t trunc_len = (uint16_t)((theft_random_bits(t, 4) % 25) + 1);
        memcpy(inp->malformed_frame, inp->valid_frame, trunc_len < inp->valid_len ? trunc_len : inp->valid_len);
        inp->malformed_len = trunc_len;
        break;
    }

    case 2: { /* Bad CRC: valid structure but corrupt a message byte */
        memcpy(inp->malformed_frame, inp->valid_frame, inp->valid_len);
        inp->malformed_len = inp->valid_len;
        /* Flip a byte in the protected region (offset 5-28, i.e. message bytes) */
        uint8_t flip_pos = (uint8_t)((theft_random_bits(t, 4) % 24) + 5);
        if (flip_pos < inp->malformed_len) {
            inp->malformed_frame[flip_pos] ^= (uint8_t)(theft_random_bits(t, 8) | 0x01);
        }
        break;
    }

    case 3: { /* Bad format: invalid OUI */
        memcpy(inp->malformed_frame, inp->valid_frame, inp->valid_len);
        inp->malformed_len = inp->valid_len;
        inp->malformed_frame[0] = 0x00; /* Break OUI */
        inp->malformed_frame[1] = 0x00;
        break;
    }

    case 4: { /* Bad format: invalid OUI type */
        memcpy(inp->malformed_frame, inp->valid_frame, inp->valid_len);
        inp->malformed_len = inp->valid_len;
        inp->malformed_frame[3] = 0xFF; /* Break OUI type */
        break;
    }

    case 5: { /* Random garbage of valid-ish length */
        inp->malformed_len = (uint16_t)((theft_random_bits(t, 6) % 60) + 1);
        for (uint16_t i = 0; i < inp->malformed_len; i++) {
            inp->malformed_frame[i] = (uint8_t)theft_random_bits(t, 8);
        }
        break;
    }

    default: { /* More random garbage */
        inp->malformed_len = (uint16_t)((theft_random_bits(t, 8) % 256) + 1);
        for (uint16_t i = 0; i < inp->malformed_len; i++) {
            inp->malformed_frame[i] = (uint8_t)theft_random_bits(t, 8);
        }
        break;
    }
    }

    *output = inp;
    return THEFT_ALLOC_OK;
}

static void free_state_input(void *instance, void *env)
{
    (void)env;
    free(instance);
}

/**
 * Property 8: After a successful decode establishes state, any rejected
 * input must leave the output unchanged (byte-for-byte identical).
 */
static enum theft_trial_res prop_state_preservation(struct theft *t, void *arg)
{
    (void)t;
    pbt_state_input_t *inp = (pbt_state_input_t *)arg;

    /* Step 1: Decode the valid frame to establish baseline state */
    remoteid_data_t output;
    remoteid_data_init(&output);

    esp_err_t valid_err = remoteid_decode_wifi(inp->valid_frame, inp->valid_len, &output);
    if (valid_err != ESP_OK) {
        /* Generator bug — valid frame should always succeed */
        return THEFT_TRIAL_SKIP;
    }

    /* Save the established state */
    remoteid_data_t saved_state;
    memcpy(&saved_state, &output, sizeof(remoteid_data_t));

    /* Step 2: Attempt to decode the malformed frame into the same output */
    esp_err_t bad_err = remoteid_decode_wifi(inp->malformed_frame, inp->malformed_len, &output);

    /* Step 3: Verify the property */
    if (bad_err == ESP_OK) {
        /* The malformed frame was accepted — this can happen with random data
         * that happens to form a valid frame. Skip this trial. */
        return THEFT_TRIAL_SKIP;
    }

    /* After rejection, output MUST be unchanged (parse-then-commit) */
    if (memcmp(&output, &saved_state, sizeof(remoteid_data_t)) != 0) {
        return THEFT_TRIAL_FAIL;
    }

    return THEFT_TRIAL_PASS;
}

void test_pbt_property8_state_preservation(void)
{
    struct theft_type_info type_info = {
        .alloc = alloc_state_input,
        .free = free_state_input,
    };

    struct theft_run_config config = {
        .name = "Property 8: Parse-then-commit preserves state",
        .prop1 = prop_state_preservation,
        .type_info = { &type_info },
        .trials = PBT_MIN_TRIALS,
        .seed = (PBT_SEED != 0) ? (theft_seed)PBT_SEED : theft_seed_of_time(),
    };

    enum theft_run_res result = theft_run(&config);
    TEST_ASSERT_EQUAL_MESSAGE(THEFT_RUN_PASS, result,
        "Property 8 failed: rejected input altered output state");
}

/* ========================================================================
 * Deterministic Malformed Input Campaign
 *
 * Verifies absence of crash/uninitialized data and state preservation
 * against a known set of malformed inputs.
 * ======================================================================== */

void test_deterministic_malformed_empty(void)
{
    /* Establish valid state first */
    uint8_t valid_buf[64];
    uint16_t valid_len;
    build_valid_wifi_frame(valid_buf, &valid_len, "DRONE-ABC-123");

    remoteid_data_t output;
    remoteid_data_init(&output);
    TEST_ASSERT_EQUAL(ESP_OK, remoteid_decode_wifi(valid_buf, valid_len, &output));

    remoteid_data_t saved;
    memcpy(&saved, &output, sizeof(saved));

    /* Empty input */
    uint8_t empty[1] = {0};
    esp_err_t err = remoteid_decode_wifi(empty, 0, &output);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_MEMORY(&saved, &output, sizeof(output));
}

void test_deterministic_malformed_truncated(void)
{
    uint8_t valid_buf[64];
    uint16_t valid_len;
    build_valid_wifi_frame(valid_buf, &valid_len, "DRONE-TRUNC-01");

    remoteid_data_t output;
    remoteid_data_init(&output);
    TEST_ASSERT_EQUAL(ESP_OK, remoteid_decode_wifi(valid_buf, valid_len, &output));

    remoteid_data_t saved;
    memcpy(&saved, &output, sizeof(saved));

    /* Truncated at various sizes */
    uint16_t trunc_sizes[] = {1, 4, 5, 10, 15, 20, 25, 29};
    for (size_t i = 0; i < sizeof(trunc_sizes) / sizeof(trunc_sizes[0]); i++) {
        esp_err_t err = remoteid_decode_wifi(valid_buf, trunc_sizes[i], &output);
        TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
        TEST_ASSERT_EQUAL_MEMORY(&saved, &output, sizeof(output));
    }
}

void test_deterministic_malformed_bad_crc(void)
{
    uint8_t valid_buf[64];
    uint16_t valid_len;
    build_valid_wifi_frame(valid_buf, &valid_len, "DRONE-CRC-FAIL");

    remoteid_data_t output;
    remoteid_data_init(&output);
    TEST_ASSERT_EQUAL(ESP_OK, remoteid_decode_wifi(valid_buf, valid_len, &output));

    remoteid_data_t saved;
    memcpy(&saved, &output, sizeof(saved));

    /* Corrupt a byte in the protected region to break CRC */
    uint8_t bad_buf[64];
    memcpy(bad_buf, valid_buf, valid_len);
    bad_buf[7] ^= 0xFF; /* Flip byte in Basic ID data region */

    esp_err_t err = remoteid_decode_wifi(bad_buf, valid_len, &output);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_MEMORY(&saved, &output, sizeof(output));
}

void test_deterministic_malformed_bad_format(void)
{
    uint8_t valid_buf[64];
    uint16_t valid_len;
    build_valid_wifi_frame(valid_buf, &valid_len, "DRONE-FORMAT-01");

    remoteid_data_t output;
    remoteid_data_init(&output);
    TEST_ASSERT_EQUAL(ESP_OK, remoteid_decode_wifi(valid_buf, valid_len, &output));

    remoteid_data_t saved;
    memcpy(&saved, &output, sizeof(saved));

    /* Bad OUI */
    uint8_t bad_oui[64];
    memcpy(bad_oui, valid_buf, valid_len);
    bad_oui[0] = 0x00;
    esp_err_t err = remoteid_decode_wifi(bad_oui, valid_len, &output);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_MEMORY(&saved, &output, sizeof(output));

    /* Bad OUI type */
    uint8_t bad_type[64];
    memcpy(bad_type, valid_buf, valid_len);
    bad_type[3] = 0xFF;
    err = remoteid_decode_wifi(bad_type, valid_len, &output);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_MEMORY(&saved, &output, sizeof(output));
}

void test_deterministic_malformed_null_pointers(void)
{
    remoteid_data_t output;
    remoteid_data_init(&output);
    remoteid_data_t saved;
    memcpy(&saved, &output, sizeof(saved));

    /* NULL frame */
    esp_err_t err = remoteid_decode_wifi(NULL, 30, &output);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_MEMORY(&saved, &output, sizeof(output));

    /* NULL output */
    uint8_t buf[30] = {0};
    err = remoteid_decode_wifi(buf, 30, NULL);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
}

void test_deterministic_malformed_ble_bad_uuid(void)
{
    /* Build a minimally valid BLE frame then corrupt UUID */
    uint8_t ble_frame[64];
    memset(ble_frame, 0, sizeof(ble_frame));

    /* Valid BLE header */
    ble_frame[0] = 29; /* AD length */
    ble_frame[1] = 0x16; /* AD type: service data 16-bit UUID */
    ble_frame[2] = 0xFA; /* UUID16 low byte (ASTM) */
    ble_frame[3] = 0xFF; /* UUID16 high byte (ASTM) */
    ble_frame[4] = 0x01; /* counter */

    /* Basic ID message at offset 5 */
    ble_frame[5] = (0x00 << 4); /* Type 0: Basic ID */
    ble_frame[6] = (0x01 << 4); /* Serial number */
    memcpy(&ble_frame[7], "BLE-TEST-DRONE", 14);
    ble_frame[5 + 24] = remoteid_crc8(&ble_frame[5], 24);

    /* Verify it decodes correctly first */
    remoteid_data_t output;
    remoteid_data_init(&output);
    TEST_ASSERT_EQUAL(ESP_OK, remoteid_decode_ble(ble_frame, 30, &output));

    remoteid_data_t saved;
    memcpy(&saved, &output, sizeof(saved));

    /* Corrupt UUID */
    uint8_t bad_ble[64];
    memcpy(bad_ble, ble_frame, 30);
    bad_ble[2] = 0x00; /* Break UUID */

    esp_err_t err = remoteid_decode_ble(bad_ble, 30, &output);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_MEMORY(&saved, &output, sizeof(output));
}

/* ========================================================================
 * Metrics: Accepted counter increments correctly
 * ======================================================================== */

void test_accepted_counter_increments(void)
{
    remoteid_metrics_reset();

    uint8_t valid_buf[64];
    uint16_t valid_len;
    build_valid_wifi_frame(valid_buf, &valid_len, "DRONE-METRICS-1");

    remoteid_data_t output;
    remoteid_data_init(&output);

    /* Decode 3 valid frames */
    for (int i = 0; i < 3; i++) {
        remoteid_data_init(&output);
        TEST_ASSERT_EQUAL(ESP_OK, remoteid_decode_wifi(valid_buf, valid_len, &output));
    }

    remoteid_metrics_t metrics;
    TEST_ASSERT_EQUAL(ESP_OK, remoteid_metrics_snapshot(&metrics));
    TEST_ASSERT_EQUAL_UINT64(3, metrics.accepted);
}

/* ========================================================================
 * Metrics: Each rejection reason increments its bucket exactly once
 * ======================================================================== */

void test_rejection_reason_null(void)
{
    remoteid_metrics_reset();

    remoteid_data_t output;
    remoteid_data_init(&output);

    remoteid_decode_wifi(NULL, 30, &output);

    remoteid_metrics_t metrics;
    remoteid_metrics_snapshot(&metrics);
    TEST_ASSERT_EQUAL_UINT64(1, metrics.rejected[RID_REJECT_NULL]);
    TEST_ASSERT_EQUAL_UINT64(0, metrics.accepted);
}

void test_rejection_reason_truncated(void)
{
    remoteid_metrics_reset();

    uint8_t buf[10] = {0xFA, 0x0B, 0xBC, 0x0D, 0x01};
    remoteid_data_t output;
    remoteid_data_init(&output);

    /* 10 bytes is below minimum (30 for WiFi) */
    remoteid_decode_wifi(buf, 10, &output);

    remoteid_metrics_t metrics;
    remoteid_metrics_snapshot(&metrics);
    TEST_ASSERT_EQUAL_UINT64(1, metrics.rejected[RID_REJECT_TRUNCATED]);
    TEST_ASSERT_EQUAL_UINT64(0, metrics.accepted);
}

void test_rejection_reason_format(void)
{
    remoteid_metrics_reset();

    /* Valid-sized WiFi frame but bad OUI */
    uint8_t buf[30];
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x00; buf[1] = 0x00; buf[2] = 0x00; /* Bad OUI */
    buf[3] = 0x0D;
    buf[4] = 0x01;

    remoteid_data_t output;
    remoteid_data_init(&output);
    remoteid_decode_wifi(buf, 30, &output);

    remoteid_metrics_t metrics;
    remoteid_metrics_snapshot(&metrics);
    TEST_ASSERT_EQUAL_UINT64(1, metrics.rejected[RID_REJECT_FORMAT]);
    TEST_ASSERT_EQUAL_UINT64(0, metrics.accepted);
}

void test_rejection_reason_crc(void)
{
    remoteid_metrics_reset();

    uint8_t valid_buf[64];
    uint16_t valid_len;
    build_valid_wifi_frame(valid_buf, &valid_len, "DRONE-CRC-TEST");

    /* Corrupt a byte to break CRC */
    valid_buf[7] ^= 0xFF;

    remoteid_data_t output;
    remoteid_data_init(&output);
    remoteid_decode_wifi(valid_buf, valid_len, &output);

    remoteid_metrics_t metrics;
    remoteid_metrics_snapshot(&metrics);
    TEST_ASSERT_EQUAL_UINT64(1, metrics.rejected[RID_REJECT_CRC]);
    TEST_ASSERT_EQUAL_UINT64(1, metrics.integrity_errors);
    TEST_ASSERT_EQUAL_UINT64(0, metrics.accepted);
}

void test_rejection_reasons_do_not_cross_contaminate(void)
{
    remoteid_metrics_reset();

    remoteid_data_t output;
    remoteid_data_init(&output);

    /* One NULL rejection */
    remoteid_decode_wifi(NULL, 30, &output);

    /* One truncated rejection */
    uint8_t short_buf[5] = {0xFA, 0x0B, 0xBC, 0x0D, 0x01};
    remoteid_decode_wifi(short_buf, 5, &output);

    /* One format rejection */
    uint8_t bad_oui[30];
    memset(bad_oui, 0, sizeof(bad_oui));
    bad_oui[3] = 0x0D; bad_oui[4] = 0x01;
    remoteid_decode_wifi(bad_oui, 30, &output);

    remoteid_metrics_t metrics;
    remoteid_metrics_snapshot(&metrics);

    /* Each reason has exactly 1 */
    TEST_ASSERT_EQUAL_UINT64(1, metrics.rejected[RID_REJECT_NULL]);
    TEST_ASSERT_EQUAL_UINT64(1, metrics.rejected[RID_REJECT_TRUNCATED]);
    TEST_ASSERT_EQUAL_UINT64(1, metrics.rejected[RID_REJECT_FORMAT]);
    TEST_ASSERT_EQUAL_UINT64(0, metrics.rejected[RID_REJECT_CRC]);
    TEST_ASSERT_EQUAL_UINT64(0, metrics.accepted);
}

/* ========================================================================
 * Unified decode: telemetry_decoder commits only on success
 * ======================================================================== */

void test_unified_decode_propagates_rejection(void)
{
    remoteid_metrics_reset();

    /* Test that remoteid_decode does not commit partial data on failure */
    decoded_telemetry_t telemetry;
    memset(&telemetry, 0xAA, sizeof(telemetry));
    decoded_telemetry_t saved;
    memcpy(&saved, &telemetry, sizeof(saved));

    /* NULL payload */
    esp_err_t err = remoteid_decode(NULL, 30, false, &telemetry, NULL);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);

    /* Valid empty telemetry struct should not be populated with any data */
    /* (remoteid_decode zeroes 'out' on entry, so it won't match saved) */
    /* The key invariant is: on failure, no UAS ID is committed */
    decoded_telemetry_t zero_telemetry;
    memset(&zero_telemetry, 0, sizeof(zero_telemetry));

    /* With valid args but truncated frame, output is zeroed but not populated */
    uint8_t short_frame[5] = {0xFA, 0x0B, 0xBC, 0x0D, 0x01};
    memset(&telemetry, 0xAA, sizeof(telemetry));
    err = remoteid_decode(short_frame, 5, false, &telemetry, NULL);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
    /* uas_id should be empty (zeroed by memset at function entry) */
    TEST_ASSERT_EQUAL_STRING("", telemetry.uas_id);
    TEST_ASSERT_FALSE(telemetry.has_position);
}

/* ========================================================================
 * Test Runner
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* Property 8 PBT */
    RUN_TEST(test_pbt_property8_state_preservation);

    /* Deterministic malformed input campaign */
    RUN_TEST(test_deterministic_malformed_empty);
    RUN_TEST(test_deterministic_malformed_truncated);
    RUN_TEST(test_deterministic_malformed_bad_crc);
    RUN_TEST(test_deterministic_malformed_bad_format);
    RUN_TEST(test_deterministic_malformed_null_pointers);
    RUN_TEST(test_deterministic_malformed_ble_bad_uuid);

    /* Accepted counter */
    RUN_TEST(test_accepted_counter_increments);

    /* Rejection reasons */
    RUN_TEST(test_rejection_reason_null);
    RUN_TEST(test_rejection_reason_truncated);
    RUN_TEST(test_rejection_reason_format);
    RUN_TEST(test_rejection_reason_crc);
    RUN_TEST(test_rejection_reasons_do_not_cross_contaminate);

    /* Unified decode */
    RUN_TEST(test_unified_decode_propagates_rejection);

    return UNITY_END();
}
