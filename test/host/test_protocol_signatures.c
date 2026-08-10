/**
 * @file test_protocol_signatures.c
 * @brief Unit tests for the protocol_signatures module.
 *
 * Tests CSV parsing, default table loading, header matching with masks,
 * frequency range checks, serialization round-trip, and error handling.
 *
 * Validates: Requirements 7.1, 7.2, 7.4, 7.5
 */

#include "unity.h"
#include "protocol_signatures.h"
#include "error_codes.h"

#include <string.h>
#include <stdio.h>

void setUp(void) {
    /* Reset to defaults before each test */
    signatures_load_defaults();
}

void tearDown(void) {}

/* ========================================================================
 * Tests: Default Table Loading
 * ======================================================================== */

void test_init_loads_defaults(void) {
    int result = signatures_init();
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_TRUE(signatures_get_count() > 0);
    TEST_ASSERT_EQUAL_UINT16(7, signatures_get_count());
}

void test_default_table_contains_expected_protocols(void) {
    signatures_load_defaults();
    uint16_t count;
    const protocol_signature_t *table = signatures_get_table(&count);
    TEST_ASSERT_NOT_NULL(table);
    TEST_ASSERT_EQUAL_UINT16(7, count);

    /* Verify ELRS 868 entry */
    TEST_ASSERT_EQUAL(PROTOCOL_ELRS, table[0].protocol);
    TEST_ASSERT_EQUAL_UINT8(0x00, table[0].header_pattern[0]);
    TEST_ASSERT_EQUAL_UINT8(0xFC, table[0].header_mask[0]);
    TEST_ASSERT_EQUAL_UINT8(1, table[0].header_len);
    TEST_ASSERT_EQUAL_UINT32(862000000, table[0].freq_min_hz);
    TEST_ASSERT_EQUAL_UINT32(928000000, table[0].freq_max_hz);
    TEST_ASSERT_EQUAL_STRING("LORA", table[0].modulation);

    /* Verify MAVLink v1 entry */
    TEST_ASSERT_EQUAL(PROTOCOL_MAVLINK, table[2].protocol);
    TEST_ASSERT_EQUAL_UINT8(0xFE, table[2].header_pattern[0]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, table[2].header_mask[0]);
    TEST_ASSERT_EQUAL_UINT32(0, table[2].freq_min_hz);
    TEST_ASSERT_EQUAL_UINT32(0, table[2].freq_max_hz);

    /* Verify DJI entry */
    TEST_ASSERT_EQUAL(PROTOCOL_DJI, table[4].protocol);
    TEST_ASSERT_EQUAL_UINT8(0x55, table[4].header_pattern[0]);
}

void test_get_table_null_count_returns_null(void) {
    const protocol_signature_t *table = signatures_get_table(NULL);
    TEST_ASSERT_NULL(table);
}

/* ========================================================================
 * Tests: CSV Parsing — Valid Input
 * ======================================================================== */

static const char *VALID_CSV =
    "protocol,header_hex,header_mask_hex,header_len,freq_min_mhz,freq_max_mhz,modulation\n"
    "ELRS,00,FC,1,862,928,LORA\n"
    "MAVLINK,FE,,1,0,0,ANY\n"
    "DJI,55,FF,1,2400,2500,OFDM\n";

void test_load_csv_valid_data(void) {
    int result = signatures_load_csv(VALID_CSV, strlen(VALID_CSV));
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_UINT16(3, signatures_get_count());

    uint16_t count;
    const protocol_signature_t *table = signatures_get_table(&count);

    /* ELRS */
    TEST_ASSERT_EQUAL(PROTOCOL_ELRS, table[0].protocol);
    TEST_ASSERT_EQUAL_UINT8(0x00, table[0].header_pattern[0]);
    TEST_ASSERT_EQUAL_UINT8(0xFC, table[0].header_mask[0]);
    TEST_ASSERT_EQUAL_UINT8(1, table[0].header_len);
    TEST_ASSERT_EQUAL_UINT32(862000000, table[0].freq_min_hz);
    TEST_ASSERT_EQUAL_UINT32(928000000, table[0].freq_max_hz);
    TEST_ASSERT_EQUAL_STRING("LORA", table[0].modulation);

    /* MAVLink — empty mask should default to 0xFF */
    TEST_ASSERT_EQUAL(PROTOCOL_MAVLINK, table[1].protocol);
    TEST_ASSERT_EQUAL_UINT8(0xFE, table[1].header_pattern[0]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, table[1].header_mask[0]);
    TEST_ASSERT_EQUAL_UINT32(0, table[1].freq_min_hz);
    TEST_ASSERT_EQUAL_UINT32(0, table[1].freq_max_hz);
    TEST_ASSERT_EQUAL_STRING("ANY", table[1].modulation);

    /* DJI */
    TEST_ASSERT_EQUAL(PROTOCOL_DJI, table[2].protocol);
    TEST_ASSERT_EQUAL_UINT8(0x55, table[2].header_pattern[0]);
    TEST_ASSERT_EQUAL_UINT32(2400000000UL, table[2].freq_min_hz);
    TEST_ASSERT_EQUAL_UINT32(2500000000UL, table[2].freq_max_hz);
}

void test_load_csv_without_header_row(void) {
    /* CSV without header line — data lines should still parse */
    const char *csv =
        "CROSSFIRE,C8,FF,1,862,928,LORA\n"
        "FRSKY,7E,FF,1,2400,2500,FHSS\n";
    int result = signatures_load_csv(csv, strlen(csv));
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_UINT16(2, signatures_get_count());
}

void test_load_csv_skips_invalid_lines(void) {
    /* Mix of valid and invalid lines */
    const char *csv =
        "protocol,header_hex,header_mask_hex,header_len,freq_min_mhz,freq_max_mhz,modulation\n"
        "ELRS,00,FC,1,862,928,LORA\n"
        "INVALID_PROTOCOL,ZZ,FF,1,100,200,FOO\n"  /* ZZ is invalid hex */
        "MAVLINK,FD,,1,0,0,ANY\n";
    int result = signatures_load_csv(csv, strlen(csv));
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_UINT16(2, signatures_get_count());
}

/* ========================================================================
 * Tests: CSV Parsing — Error Cases
 * ======================================================================== */

void test_load_csv_null_data_returns_error_with_defaults(void) {
    int result = signatures_load_csv(NULL, 0);
    TEST_ASSERT_EQUAL_INT(ERR_CONFIG_PARSE_FAIL, result);
    /* Should fall back to defaults */
    TEST_ASSERT_EQUAL_UINT16(7, signatures_get_count());
}

void test_load_csv_empty_string_returns_error_with_defaults(void) {
    int result = signatures_load_csv("", 0);
    TEST_ASSERT_EQUAL_INT(ERR_CONFIG_PARSE_FAIL, result);
    TEST_ASSERT_EQUAL_UINT16(7, signatures_get_count());
}

void test_load_csv_only_header_returns_error_with_defaults(void) {
    const char *csv = "protocol,header_hex,header_mask_hex,header_len,freq_min_mhz,freq_max_mhz,modulation\n";
    int result = signatures_load_csv(csv, strlen(csv));
    TEST_ASSERT_EQUAL_INT(ERR_CONFIG_PARSE_FAIL, result);
    TEST_ASSERT_EQUAL_UINT16(7, signatures_get_count());
}

void test_load_csv_all_lines_malformed_returns_error_with_defaults(void) {
    const char *csv =
        "this is not csv at all\n"
        "neither is this\n"
        "garbage,data,more\n";
    int result = signatures_load_csv(csv, strlen(csv));
    TEST_ASSERT_EQUAL_INT(ERR_CONFIG_PARSE_FAIL, result);
    TEST_ASSERT_EQUAL_UINT16(7, signatures_get_count());
}

/* ========================================================================
 * Tests: Header + Frequency Matching
 * ======================================================================== */

void test_find_match_mavlink_v1_any_freq(void) {
    signatures_load_defaults();

    uint8_t header[] = {0xFE, 0x09, 0x00};
    const protocol_signature_t *match = signatures_find_match(header, 3, 915000000);
    TEST_ASSERT_NOT_NULL(match);
    TEST_ASSERT_EQUAL(PROTOCOL_MAVLINK, match->protocol);
}

void test_find_match_mavlink_v2_any_freq(void) {
    signatures_load_defaults();

    uint8_t header[] = {0xFD, 0x09, 0x00};
    const protocol_signature_t *match = signatures_find_match(header, 3, 0);
    TEST_ASSERT_NOT_NULL(match);
    TEST_ASSERT_EQUAL(PROTOCOL_MAVLINK, match->protocol);
}

void test_find_match_elrs_with_mask(void) {
    signatures_load_defaults();

    /* ELRS mask is 0xFC — lower 2 bits are ignored */
    /* 0x00 & 0xFC = 0x00, 0x03 & 0xFC = 0x00, so 0x03 should match */
    uint8_t header[] = {0x03};
    const protocol_signature_t *match = signatures_find_match(header, 1, 915000000);
    TEST_ASSERT_NOT_NULL(match);
    TEST_ASSERT_EQUAL(PROTOCOL_ELRS, match->protocol);
}

void test_find_match_elrs_mask_rejects_nonmatch(void) {
    signatures_load_defaults();

    /* 0x04 & 0xFC = 0x04, pattern 0x00 & 0xFC = 0x00 → no match for ELRS */
    /* But 0x04 doesn't match other 868 MHz signatures either */
    uint8_t header[] = {0x04};
    /* Use a frequency that only ELRS 868 would match */
    const protocol_signature_t *match = signatures_find_match(header, 1, 870000000);
    TEST_ASSERT_NULL(match);
}

void test_find_match_dji_correct_frequency(void) {
    signatures_load_defaults();

    uint8_t header[] = {0x55};
    /* DJI requires 2.4 GHz band */
    const protocol_signature_t *match = signatures_find_match(header, 1, 2450000000UL);
    TEST_ASSERT_NOT_NULL(match);
    TEST_ASSERT_EQUAL(PROTOCOL_DJI, match->protocol);
}

void test_find_match_dji_wrong_frequency_no_match(void) {
    signatures_load_defaults();

    uint8_t header[] = {0x55};
    /* 868 MHz — DJI only matches at 2.4 GHz, no other sig matches 0x55 at 868 */
    const protocol_signature_t *match = signatures_find_match(header, 1, 868000000);
    TEST_ASSERT_NULL(match);
}

void test_find_match_crossfire(void) {
    signatures_load_defaults();

    uint8_t header[] = {0xC8};
    const protocol_signature_t *match = signatures_find_match(header, 1, 868000000);
    TEST_ASSERT_NOT_NULL(match);
    TEST_ASSERT_EQUAL(PROTOCOL_CROSSFIRE, match->protocol);
}

void test_find_match_frsky(void) {
    signatures_load_defaults();

    uint8_t header[] = {0x7E};
    const protocol_signature_t *match = signatures_find_match(header, 1, 2450000000UL);
    TEST_ASSERT_NOT_NULL(match);
    TEST_ASSERT_EQUAL(PROTOCOL_FRSKY, match->protocol);
}

void test_find_match_no_match_returns_null(void) {
    signatures_load_defaults();

    uint8_t header[] = {0xAB, 0xCD};
    const protocol_signature_t *match = signatures_find_match(header, 2, 100000000);
    TEST_ASSERT_NULL(match);
}

void test_find_match_null_header_returns_null(void) {
    const protocol_signature_t *match = signatures_find_match(NULL, 1, 915000000);
    TEST_ASSERT_NULL(match);
}

void test_find_match_zero_len_returns_null(void) {
    uint8_t header[] = {0xFE};
    const protocol_signature_t *match = signatures_find_match(header, 0, 915000000);
    TEST_ASSERT_NULL(match);
}

/* ========================================================================
 * Tests: CSV Serialization
 * ======================================================================== */

void test_serialize_csv_basic(void) {
    signatures_load_defaults();

    char buf[2048];
    size_t written = 0;
    int result = signatures_serialize_csv(buf, sizeof(buf), &written);

    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_TRUE(written > 0);

    /* Should start with header row */
    TEST_ASSERT_TRUE(strstr(buf, "protocol,header_hex,header_mask_hex,header_len,freq_min_mhz,freq_max_mhz,modulation\n") != NULL);

    /* Should contain known entries */
    TEST_ASSERT_TRUE(strstr(buf, "ELRS") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "MAVLINK") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "DJI") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "CROSSFIRE") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "FRSKY") != NULL);
}

void test_serialize_csv_null_buf_returns_error(void) {
    size_t written = 0;
    int result = signatures_serialize_csv(NULL, 100, &written);
    TEST_ASSERT_NOT_EQUAL(0, result);
}

void test_serialize_csv_zero_size_returns_error(void) {
    char buf[10];
    size_t written = 0;
    int result = signatures_serialize_csv(buf, 0, &written);
    TEST_ASSERT_NOT_EQUAL(0, result);
}

void test_serialize_csv_small_buffer_returns_error(void) {
    char buf[10];
    size_t written = 0;
    int result = signatures_serialize_csv(buf, sizeof(buf), &written);
    /* Buffer too small for header + data */
    TEST_ASSERT_NOT_EQUAL(0, result);
}

/* ========================================================================
 * Tests: Round-trip (serialize then parse back)
 * ======================================================================== */

void test_csv_round_trip(void) {
    signatures_load_defaults();
    uint16_t original_count = signatures_get_count();

    /* Serialize */
    char buf[4096];
    size_t written = 0;
    int result = signatures_serialize_csv(buf, sizeof(buf), &written);
    TEST_ASSERT_EQUAL_INT(0, result);

    /* Parse back */
    result = signatures_load_csv(buf, written);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_UINT16(original_count, signatures_get_count());

    /* Verify entries match */
    uint16_t count;
    const protocol_signature_t *table = signatures_get_table(&count);

    /* Check first ELRS entry preserved */
    TEST_ASSERT_EQUAL(PROTOCOL_ELRS, table[0].protocol);
    TEST_ASSERT_EQUAL_UINT8(0x00, table[0].header_pattern[0]);
    TEST_ASSERT_EQUAL_UINT8(0xFC, table[0].header_mask[0]);
    TEST_ASSERT_EQUAL_UINT8(1, table[0].header_len);
    TEST_ASSERT_EQUAL_UINT32(862000000, table[0].freq_min_hz);
    TEST_ASSERT_EQUAL_UINT32(928000000, table[0].freq_max_hz);
    TEST_ASSERT_EQUAL_STRING("LORA", table[0].modulation);

    /* Check MAVLink v1 (with empty mask → 0xFF) */
    TEST_ASSERT_EQUAL(PROTOCOL_MAVLINK, table[2].protocol);
    TEST_ASSERT_EQUAL_UINT8(0xFE, table[2].header_pattern[0]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, table[2].header_mask[0]);
}

/* ========================================================================
 * Test Runner
 * ======================================================================== */

int main(void) {
    UNITY_BEGIN();

    /* Default table */
    RUN_TEST(test_init_loads_defaults);
    RUN_TEST(test_default_table_contains_expected_protocols);
    RUN_TEST(test_get_table_null_count_returns_null);

    /* CSV parsing — valid */
    RUN_TEST(test_load_csv_valid_data);
    RUN_TEST(test_load_csv_without_header_row);
    RUN_TEST(test_load_csv_skips_invalid_lines);

    /* CSV parsing — errors */
    RUN_TEST(test_load_csv_null_data_returns_error_with_defaults);
    RUN_TEST(test_load_csv_empty_string_returns_error_with_defaults);
    RUN_TEST(test_load_csv_only_header_returns_error_with_defaults);
    RUN_TEST(test_load_csv_all_lines_malformed_returns_error_with_defaults);

    /* Header + frequency matching */
    RUN_TEST(test_find_match_mavlink_v1_any_freq);
    RUN_TEST(test_find_match_mavlink_v2_any_freq);
    RUN_TEST(test_find_match_elrs_with_mask);
    RUN_TEST(test_find_match_elrs_mask_rejects_nonmatch);
    RUN_TEST(test_find_match_dji_correct_frequency);
    RUN_TEST(test_find_match_dji_wrong_frequency_no_match);
    RUN_TEST(test_find_match_crossfire);
    RUN_TEST(test_find_match_frsky);
    RUN_TEST(test_find_match_no_match_returns_null);
    RUN_TEST(test_find_match_null_header_returns_null);
    RUN_TEST(test_find_match_zero_len_returns_null);

    /* CSV serialization */
    RUN_TEST(test_serialize_csv_basic);
    RUN_TEST(test_serialize_csv_null_buf_returns_error);
    RUN_TEST(test_serialize_csv_zero_size_returns_error);
    RUN_TEST(test_serialize_csv_small_buffer_returns_error);

    /* Round-trip */
    RUN_TEST(test_csv_round_trip);

    return UNITY_END();
}
