/**
 * @file test_config_store.c
 * @brief Unit tests for the config_store module.
 *
 * Tests JSON parsing, validation, default fallback behavior, and
 * per-field resilience to invalid/missing data.
 *
 * Validates: Requirements 7.4, 7.5, 12.2
 */

#include "unity.h"
#include "config_store.h"
#include "error_codes.h"

#include <string.h>
#include <math.h>

void setUp(void) {}
void tearDown(void) {}

/* ========================================================================
 * Helper: Valid full config JSON
 * ======================================================================== */

static const char *VALID_CONFIG_JSON =
    "{"
    "  \"alert\": {"
    "    \"sound_enabled\": true,"
    "    \"proximity_threshold_m\": 500,"
    "    \"proximity_repeat_interval_s\": 10,"
    "    \"out_of_range_timeout_s\": 30"
    "  },"
    "  \"spectrum\": {"
    "    \"default_center_freq_mhz\": 915,"
    "    \"default_bandwidth_khz\": 500,"
    "    \"default_gain_db\": 20.0,"
    "    \"detection_threshold_dbm\": -60"
    "  },"
    "  \"logging\": {"
    "    \"max_file_size_mb\": 10,"
    "    \"buffer_size_records\": 100"
    "  },"
    "  \"scan\": {"
    "    \"remoteid_cycle_ms\": 3000,"
    "    \"nrf24_dwell_time_ms\": 100,"
    "    \"lora_dwell_time_ms\": 50,"
    "    \"module_poll_interval_ms\": 500"
    "  },"
    "  \"gps\": {"
    "    \"min_satellites\": 4,"
    "    \"max_hdop\": 5.0,"
    "    \"fix_timeout_s\": 60,"
    "    \"degraded_timeout_s\": 5"
    "  }"
    "}";

/* ========================================================================
 * Tests: Default Configuration
 * ======================================================================== */

void test_defaults_returns_valid_config(void) {
    config_store_t cfg;
    config_store_get_defaults(&cfg);

    /* Alert defaults */
    TEST_ASSERT_TRUE(cfg.alert.sound_enabled);
    TEST_ASSERT_EQUAL_UINT32(500, cfg.alert.proximity_threshold_m);
    TEST_ASSERT_EQUAL_UINT32(10, cfg.alert.proximity_repeat_interval_s);
    TEST_ASSERT_EQUAL_UINT32(30, cfg.alert.out_of_range_timeout_s);

    /* Spectrum defaults */
    TEST_ASSERT_EQUAL_UINT32(915, cfg.spectrum.default_center_freq_mhz);
    TEST_ASSERT_EQUAL_UINT32(500, cfg.spectrum.default_bandwidth_khz);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, cfg.spectrum.default_gain_db);
    TEST_ASSERT_EQUAL_INT32(-60, cfg.spectrum.detection_threshold_dbm);

    /* Logging defaults */
    TEST_ASSERT_EQUAL_UINT32(10, cfg.logging.max_file_size_mb);
    TEST_ASSERT_EQUAL_UINT32(100, cfg.logging.buffer_size_records);

    /* Scan defaults */
    TEST_ASSERT_EQUAL_UINT32(3000, cfg.scan.remoteid_cycle_ms);
    TEST_ASSERT_EQUAL_UINT32(100, cfg.scan.nrf24_dwell_time_ms);
    TEST_ASSERT_EQUAL_UINT32(50, cfg.scan.lora_dwell_time_ms);
    TEST_ASSERT_EQUAL_UINT32(500, cfg.scan.module_poll_interval_ms);

    /* GPS defaults */
    TEST_ASSERT_EQUAL_UINT8(4, cfg.gps.min_satellites);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.0f, cfg.gps.max_hdop);
    TEST_ASSERT_EQUAL_UINT32(60, cfg.gps.fix_timeout_s);
    TEST_ASSERT_EQUAL_UINT32(5, cfg.gps.degraded_timeout_s);
}

/* ========================================================================
 * Tests: Valid JSON Parsing
 * ======================================================================== */

void test_parse_valid_full_config(void) {
    config_store_t cfg;
    int result = config_store_load_from_json(VALID_CONFIG_JSON, &cfg);

    TEST_ASSERT_EQUAL_INT(0, result);

    /* Alert */
    TEST_ASSERT_TRUE(cfg.alert.sound_enabled);
    TEST_ASSERT_EQUAL_UINT32(500, cfg.alert.proximity_threshold_m);
    TEST_ASSERT_EQUAL_UINT32(10, cfg.alert.proximity_repeat_interval_s);
    TEST_ASSERT_EQUAL_UINT32(30, cfg.alert.out_of_range_timeout_s);

    /* Spectrum */
    TEST_ASSERT_EQUAL_UINT32(915, cfg.spectrum.default_center_freq_mhz);
    TEST_ASSERT_EQUAL_UINT32(500, cfg.spectrum.default_bandwidth_khz);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, cfg.spectrum.default_gain_db);
    TEST_ASSERT_EQUAL_INT32(-60, cfg.spectrum.detection_threshold_dbm);

    /* Logging */
    TEST_ASSERT_EQUAL_UINT32(10, cfg.logging.max_file_size_mb);
    TEST_ASSERT_EQUAL_UINT32(100, cfg.logging.buffer_size_records);

    /* Scan */
    TEST_ASSERT_EQUAL_UINT32(3000, cfg.scan.remoteid_cycle_ms);
    TEST_ASSERT_EQUAL_UINT32(100, cfg.scan.nrf24_dwell_time_ms);
    TEST_ASSERT_EQUAL_UINT32(50, cfg.scan.lora_dwell_time_ms);
    TEST_ASSERT_EQUAL_UINT32(500, cfg.scan.module_poll_interval_ms);

    /* GPS */
    TEST_ASSERT_EQUAL_UINT8(4, cfg.gps.min_satellites);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.0f, cfg.gps.max_hdop);
    TEST_ASSERT_EQUAL_UINT32(60, cfg.gps.fix_timeout_s);
    TEST_ASSERT_EQUAL_UINT32(5, cfg.gps.degraded_timeout_s);
}

void test_parse_custom_values(void) {
    const char *json =
        "{"
        "  \"alert\": {"
        "    \"sound_enabled\": false,"
        "    \"proximity_threshold_m\": 1000,"
        "    \"proximity_repeat_interval_s\": 5,"
        "    \"out_of_range_timeout_s\": 60"
        "  },"
        "  \"spectrum\": {"
        "    \"default_center_freq_mhz\": 2400,"
        "    \"default_bandwidth_khz\": 200,"
        "    \"default_gain_db\": 30.5,"
        "    \"detection_threshold_dbm\": -70"
        "  },"
        "  \"gps\": {"
        "    \"min_satellites\": 6,"
        "    \"max_hdop\": 2.5,"
        "    \"fix_timeout_s\": 120,"
        "    \"degraded_timeout_s\": 10"
        "  }"
        "}";

    config_store_t cfg;
    int result = config_store_load_from_json(json, &cfg);
    TEST_ASSERT_EQUAL_INT(0, result);

    /* Alert was customized */
    TEST_ASSERT_FALSE(cfg.alert.sound_enabled);
    TEST_ASSERT_EQUAL_UINT32(1000, cfg.alert.proximity_threshold_m);
    TEST_ASSERT_EQUAL_UINT32(5, cfg.alert.proximity_repeat_interval_s);
    TEST_ASSERT_EQUAL_UINT32(60, cfg.alert.out_of_range_timeout_s);

    /* Spectrum: freq 2400 exceeds max 1766 → falls back to default */
    TEST_ASSERT_EQUAL_UINT32(915, cfg.spectrum.default_center_freq_mhz);
    TEST_ASSERT_EQUAL_UINT32(200, cfg.spectrum.default_bandwidth_khz);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 30.5f, cfg.spectrum.default_gain_db);
    TEST_ASSERT_EQUAL_INT32(-70, cfg.spectrum.detection_threshold_dbm);

    /* Logging missing → defaults */
    TEST_ASSERT_EQUAL_UINT32(10, cfg.logging.max_file_size_mb);
    TEST_ASSERT_EQUAL_UINT32(100, cfg.logging.buffer_size_records);

    /* Scan missing → defaults */
    TEST_ASSERT_EQUAL_UINT32(3000, cfg.scan.remoteid_cycle_ms);

    /* GPS custom */
    TEST_ASSERT_EQUAL_UINT8(6, cfg.gps.min_satellites);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.5f, cfg.gps.max_hdop);
    TEST_ASSERT_EQUAL_UINT32(120, cfg.gps.fix_timeout_s);
    TEST_ASSERT_EQUAL_UINT32(10, cfg.gps.degraded_timeout_s);
}

/* ========================================================================
 * Tests: Malformed/Absent JSON Handling
 * ======================================================================== */

void test_parse_null_json_returns_error_with_defaults(void) {
    config_store_t cfg;
    int result = config_store_load_from_json(NULL, &cfg);

    TEST_ASSERT_EQUAL_INT(ERR_CONFIG_PARSE_FAIL, result);
    /* Output should still be filled with defaults */
    TEST_ASSERT_EQUAL_UINT32(500, cfg.alert.proximity_threshold_m);
    TEST_ASSERT_EQUAL_UINT32(915, cfg.spectrum.default_center_freq_mhz);
}

void test_parse_empty_string_returns_error_with_defaults(void) {
    config_store_t cfg;
    int result = config_store_load_from_json("", &cfg);

    TEST_ASSERT_EQUAL_INT(ERR_CONFIG_PARSE_FAIL, result);
    TEST_ASSERT_EQUAL_UINT32(500, cfg.alert.proximity_threshold_m);
}

void test_parse_invalid_json_returns_error_with_defaults(void) {
    config_store_t cfg;
    int result = config_store_load_from_json("{not valid json!!!", &cfg);

    TEST_ASSERT_EQUAL_INT(ERR_CONFIG_PARSE_FAIL, result);
    TEST_ASSERT_EQUAL_UINT32(500, cfg.alert.proximity_threshold_m);
    TEST_ASSERT_EQUAL_UINT32(915, cfg.spectrum.default_center_freq_mhz);
}

void test_parse_empty_object_returns_defaults(void) {
    config_store_t cfg;
    int result = config_store_load_from_json("{}", &cfg);

    TEST_ASSERT_EQUAL_INT(0, result);
    /* All defaults since no sections present */
    TEST_ASSERT_TRUE(cfg.alert.sound_enabled);
    TEST_ASSERT_EQUAL_UINT32(500, cfg.alert.proximity_threshold_m);
    TEST_ASSERT_EQUAL_UINT32(915, cfg.spectrum.default_center_freq_mhz);
    TEST_ASSERT_EQUAL_UINT32(10, cfg.logging.max_file_size_mb);
    TEST_ASSERT_EQUAL_UINT32(3000, cfg.scan.remoteid_cycle_ms);
    TEST_ASSERT_EQUAL_UINT8(4, cfg.gps.min_satellites);
}

void test_parse_null_output_returns_error(void) {
    int result = config_store_load_from_json(VALID_CONFIG_JSON, NULL);
    TEST_ASSERT_EQUAL_INT(ERR_CONFIG_PARSE_FAIL, result);
}

/* ========================================================================
 * Tests: Per-field Validation (spectrum ranges per Req 12.2)
 * ======================================================================== */

void test_spectrum_freq_below_min_uses_default(void) {
    const char *json =
        "{ \"spectrum\": { \"default_center_freq_mhz\": 10 } }";
    config_store_t cfg;
    config_store_load_from_json(json, &cfg);
    /* 10 < 24 min → default 915 */
    TEST_ASSERT_EQUAL_UINT32(915, cfg.spectrum.default_center_freq_mhz);
}

void test_spectrum_freq_above_max_uses_default(void) {
    const char *json =
        "{ \"spectrum\": { \"default_center_freq_mhz\": 2000 } }";
    config_store_t cfg;
    config_store_load_from_json(json, &cfg);
    /* 2000 > 1766 max → default 915 */
    TEST_ASSERT_EQUAL_UINT32(915, cfg.spectrum.default_center_freq_mhz);
}

void test_spectrum_freq_at_boundaries_accepted(void) {
    const char *json_min =
        "{ \"spectrum\": { \"default_center_freq_mhz\": 24 } }";
    const char *json_max =
        "{ \"spectrum\": { \"default_center_freq_mhz\": 1766 } }";
    config_store_t cfg;

    config_store_load_from_json(json_min, &cfg);
    TEST_ASSERT_EQUAL_UINT32(24, cfg.spectrum.default_center_freq_mhz);

    config_store_load_from_json(json_max, &cfg);
    TEST_ASSERT_EQUAL_UINT32(1766, cfg.spectrum.default_center_freq_mhz);
}

void test_spectrum_bandwidth_below_min_uses_default(void) {
    const char *json =
        "{ \"spectrum\": { \"default_bandwidth_khz\": 5 } }";
    config_store_t cfg;
    config_store_load_from_json(json, &cfg);
    /* 5 < 10 min → default 500 */
    TEST_ASSERT_EQUAL_UINT32(500, cfg.spectrum.default_bandwidth_khz);
}

void test_spectrum_bandwidth_above_max_uses_default(void) {
    const char *json =
        "{ \"spectrum\": { \"default_bandwidth_khz\": 2000 } }";
    config_store_t cfg;
    config_store_load_from_json(json, &cfg);
    /* 2000 > 1000 max → default 500 */
    TEST_ASSERT_EQUAL_UINT32(500, cfg.spectrum.default_bandwidth_khz);
}

void test_spectrum_gain_below_min_uses_default(void) {
    const char *json =
        "{ \"spectrum\": { \"default_gain_db\": -1.0 } }";
    config_store_t cfg;
    config_store_load_from_json(json, &cfg);
    /* -1.0 < 0.0 min → default 20.0 */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, cfg.spectrum.default_gain_db);
}

void test_spectrum_gain_above_max_uses_default(void) {
    const char *json =
        "{ \"spectrum\": { \"default_gain_db\": 50.0 } }";
    config_store_t cfg;
    config_store_load_from_json(json, &cfg);
    /* 50.0 > 49.6 max → default 20.0 */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, cfg.spectrum.default_gain_db);
}

void test_spectrum_gain_at_boundaries_accepted(void) {
    const char *json_min =
        "{ \"spectrum\": { \"default_gain_db\": 0.0 } }";
    const char *json_max =
        "{ \"spectrum\": { \"default_gain_db\": 49.6 } }";
    config_store_t cfg;

    config_store_load_from_json(json_min, &cfg);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, cfg.spectrum.default_gain_db);

    config_store_load_from_json(json_max, &cfg);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 49.6f, cfg.spectrum.default_gain_db);
}

/* ========================================================================
 * Tests: Per-field Validation (other sections)
 * ======================================================================== */

void test_alert_zero_values_use_defaults(void) {
    const char *json =
        "{ \"alert\": {"
        "    \"proximity_threshold_m\": 0,"
        "    \"proximity_repeat_interval_s\": 0,"
        "    \"out_of_range_timeout_s\": 0"
        "} }";
    config_store_t cfg;
    config_store_load_from_json(json, &cfg);

    /* All must be > 0, so zero is rejected → defaults */
    TEST_ASSERT_EQUAL_UINT32(500, cfg.alert.proximity_threshold_m);
    TEST_ASSERT_EQUAL_UINT32(10, cfg.alert.proximity_repeat_interval_s);
    TEST_ASSERT_EQUAL_UINT32(30, cfg.alert.out_of_range_timeout_s);
}

void test_scan_zero_values_use_defaults(void) {
    const char *json =
        "{ \"scan\": {"
        "    \"remoteid_cycle_ms\": 0,"
        "    \"nrf24_dwell_time_ms\": 0,"
        "    \"lora_dwell_time_ms\": 0,"
        "    \"module_poll_interval_ms\": 0"
        "} }";
    config_store_t cfg;
    config_store_load_from_json(json, &cfg);

    TEST_ASSERT_EQUAL_UINT32(3000, cfg.scan.remoteid_cycle_ms);
    TEST_ASSERT_EQUAL_UINT32(100, cfg.scan.nrf24_dwell_time_ms);
    TEST_ASSERT_EQUAL_UINT32(50, cfg.scan.lora_dwell_time_ms);
    TEST_ASSERT_EQUAL_UINT32(500, cfg.scan.module_poll_interval_ms);
}

void test_gps_min_satellites_zero_uses_default(void) {
    const char *json =
        "{ \"gps\": { \"min_satellites\": 0 } }";
    config_store_t cfg;
    config_store_load_from_json(json, &cfg);
    /* min_satellites must be >= 1 → default 4 */
    TEST_ASSERT_EQUAL_UINT8(4, cfg.gps.min_satellites);
}

void test_gps_max_hdop_zero_uses_default(void) {
    const char *json =
        "{ \"gps\": { \"max_hdop\": 0.0 } }";
    config_store_t cfg;
    config_store_load_from_json(json, &cfg);
    /* max_hdop must be > 0 (min is 0.01) → default 5.0 */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.0f, cfg.gps.max_hdop);
}

void test_partial_section_keeps_other_defaults(void) {
    const char *json =
        "{ \"alert\": { \"sound_enabled\": false } }";
    config_store_t cfg;
    config_store_load_from_json(json, &cfg);

    /* Only sound_enabled was set; rest keeps default */
    TEST_ASSERT_FALSE(cfg.alert.sound_enabled);
    TEST_ASSERT_EQUAL_UINT32(500, cfg.alert.proximity_threshold_m);
    TEST_ASSERT_EQUAL_UINT32(10, cfg.alert.proximity_repeat_interval_s);
    TEST_ASSERT_EQUAL_UINT32(30, cfg.alert.out_of_range_timeout_s);
}

void test_wrong_type_field_uses_default(void) {
    const char *json =
        "{ \"alert\": { \"proximity_threshold_m\": \"not a number\" } }";
    config_store_t cfg;
    config_store_load_from_json(json, &cfg);
    /* String instead of number → default */
    TEST_ASSERT_EQUAL_UINT32(500, cfg.alert.proximity_threshold_m);
}

void test_section_not_object_uses_defaults(void) {
    const char *json =
        "{ \"alert\": 42, \"spectrum\": \"invalid\" }";
    config_store_t cfg;
    config_store_load_from_json(json, &cfg);
    /* Sections are not objects → all defaults */
    TEST_ASSERT_EQUAL_UINT32(500, cfg.alert.proximity_threshold_m);
    TEST_ASSERT_EQUAL_UINT32(915, cfg.spectrum.default_center_freq_mhz);
}

/* ========================================================================
 * Test Runner
 * ======================================================================== */

int main(void) {
    UNITY_BEGIN();

    /* Defaults */
    RUN_TEST(test_defaults_returns_valid_config);

    /* Valid parsing */
    RUN_TEST(test_parse_valid_full_config);
    RUN_TEST(test_parse_custom_values);

    /* Malformed input */
    RUN_TEST(test_parse_null_json_returns_error_with_defaults);
    RUN_TEST(test_parse_empty_string_returns_error_with_defaults);
    RUN_TEST(test_parse_invalid_json_returns_error_with_defaults);
    RUN_TEST(test_parse_empty_object_returns_defaults);
    RUN_TEST(test_parse_null_output_returns_error);

    /* Spectrum validation (Req 12.2) */
    RUN_TEST(test_spectrum_freq_below_min_uses_default);
    RUN_TEST(test_spectrum_freq_above_max_uses_default);
    RUN_TEST(test_spectrum_freq_at_boundaries_accepted);
    RUN_TEST(test_spectrum_bandwidth_below_min_uses_default);
    RUN_TEST(test_spectrum_bandwidth_above_max_uses_default);
    RUN_TEST(test_spectrum_gain_below_min_uses_default);
    RUN_TEST(test_spectrum_gain_above_max_uses_default);
    RUN_TEST(test_spectrum_gain_at_boundaries_accepted);

    /* Other field validation */
    RUN_TEST(test_alert_zero_values_use_defaults);
    RUN_TEST(test_scan_zero_values_use_defaults);
    RUN_TEST(test_gps_min_satellites_zero_uses_default);
    RUN_TEST(test_gps_max_hdop_zero_uses_default);
    RUN_TEST(test_partial_section_keeps_other_defaults);
    RUN_TEST(test_wrong_type_field_uses_default);
    RUN_TEST(test_section_not_object_uses_defaults);

    return UNITY_END();
}
