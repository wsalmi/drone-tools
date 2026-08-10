/**
 * @file test_spectrum_analyzer.c
 * @brief Unit tests for the Spectrum Analyzer Service.
 *
 * Tests configuration validation, initialization, peak detection,
 * frequency classification, and marker overlay functionality.
 *
 * Validates: Requirements 12.1, 12.2, 12.3, 12.4
 */

#include "unity.h"
#include "spectrum_analyzer.h"
#include "hal_mocks.h"
#include <string.h>
#include <math.h>

/* ========================================================================
 * Test Setup / Teardown
 * ======================================================================== */

void setUp(void) {
    mock_hal_sdr_reset();
    /* Set SDR status to ACTIVE so init finds it available */
    mock_hal_sdr_set_status(HAL_STATUS_ACTIVE);
}

void tearDown(void) {
    /* Attempt to deinit (ignore errors if not initialized) */
    spectrum_analyzer_deinit();
    mock_hal_sdr_reset();
}

/* ========================================================================
 * Configuration Validation Tests
 * ======================================================================== */

void test_validate_config_null_returns_error(void) {
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, spectrum_analyzer_validate_config(NULL));
}

void test_validate_config_valid_default(void) {
    spectrum_config_t cfg = {
        .center_freq_mhz = 915,
        .bandwidth_khz = 500,
        .gain_db = 20.0f,
        .detection_threshold_dbm = -60
    };
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_validate_config(&cfg));
}

void test_validate_config_freq_below_min(void) {
    spectrum_config_t cfg = {
        .center_freq_mhz = 23,  /* Below 24 MHz */
        .bandwidth_khz = 500,
        .gain_db = 20.0f,
        .detection_threshold_dbm = -60
    };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, spectrum_analyzer_validate_config(&cfg));
}

void test_validate_config_freq_above_max(void) {
    spectrum_config_t cfg = {
        .center_freq_mhz = 1767,  /* Above 1766 MHz */
        .bandwidth_khz = 500,
        .gain_db = 20.0f,
        .detection_threshold_dbm = -60
    };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, spectrum_analyzer_validate_config(&cfg));
}

void test_validate_config_freq_at_min_boundary(void) {
    spectrum_config_t cfg = {
        .center_freq_mhz = 24,
        .bandwidth_khz = 500,
        .gain_db = 20.0f,
        .detection_threshold_dbm = -60
    };
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_validate_config(&cfg));
}

void test_validate_config_freq_at_max_boundary(void) {
    spectrum_config_t cfg = {
        .center_freq_mhz = 1766,
        .bandwidth_khz = 500,
        .gain_db = 20.0f,
        .detection_threshold_dbm = -60
    };
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_validate_config(&cfg));
}

void test_validate_config_bw_below_min(void) {
    spectrum_config_t cfg = {
        .center_freq_mhz = 915,
        .bandwidth_khz = 9,  /* Below 10 kHz */
        .gain_db = 20.0f,
        .detection_threshold_dbm = -60
    };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, spectrum_analyzer_validate_config(&cfg));
}

void test_validate_config_bw_above_max(void) {
    spectrum_config_t cfg = {
        .center_freq_mhz = 915,
        .bandwidth_khz = 1001,  /* Above 1000 kHz */
        .gain_db = 20.0f,
        .detection_threshold_dbm = -60
    };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, spectrum_analyzer_validate_config(&cfg));
}

void test_validate_config_gain_below_min(void) {
    spectrum_config_t cfg = {
        .center_freq_mhz = 915,
        .bandwidth_khz = 500,
        .gain_db = -0.1f,  /* Below 0 dB */
        .detection_threshold_dbm = -60
    };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, spectrum_analyzer_validate_config(&cfg));
}

void test_validate_config_gain_above_max(void) {
    spectrum_config_t cfg = {
        .center_freq_mhz = 915,
        .bandwidth_khz = 500,
        .gain_db = 49.7f,  /* Above 49.6 dB */
        .detection_threshold_dbm = -60
    };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, spectrum_analyzer_validate_config(&cfg));
}

void test_validate_config_gain_at_zero(void) {
    spectrum_config_t cfg = {
        .center_freq_mhz = 915,
        .bandwidth_khz = 500,
        .gain_db = 0.0f,
        .detection_threshold_dbm = -60
    };
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_validate_config(&cfg));
}

void test_validate_config_gain_at_max(void) {
    spectrum_config_t cfg = {
        .center_freq_mhz = 915,
        .bandwidth_khz = 500,
        .gain_db = 49.6f,
        .detection_threshold_dbm = -60
    };
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_validate_config(&cfg));
}

/* ========================================================================
 * Initialization Tests
 * ======================================================================== */

void test_init_with_default_config(void) {
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_init(NULL));
}

void test_init_with_custom_config(void) {
    config_spectrum_t cfg = {
        .default_center_freq_mhz = 900,
        .default_bandwidth_khz = 250,
        .default_gain_db = 10.0f,
        .detection_threshold_dbm = -50
    };
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_init(&cfg));
}

void test_init_fails_when_sdr_inactive(void) {
    mock_hal_sdr_set_status(HAL_STATUS_INACTIVE);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, spectrum_analyzer_init(NULL));
}

void test_init_double_init_fails(void) {
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_init(NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, spectrum_analyzer_init(NULL));
}

void test_init_with_invalid_config_rejected(void) {
    config_spectrum_t cfg = {
        .default_center_freq_mhz = 0,  /* Invalid frequency */
        .default_bandwidth_khz = 500,
        .default_gain_db = 20.0f,
        .detection_threshold_dbm = -60
    };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, spectrum_analyzer_init(&cfg));
}

/* ========================================================================
 * Start / Stop Tests
 * ======================================================================== */

void test_start_succeeds_after_init(void) {
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_init(NULL));
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_start());
}

void test_start_fails_without_init(void) {
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, spectrum_analyzer_start());
}

void test_start_double_start_fails(void) {
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_init(NULL));
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_start());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, spectrum_analyzer_start());
}

void test_stop_succeeds_when_running(void) {
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_init(NULL));
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_start());
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_stop());
}

void test_stop_fails_when_not_running(void) {
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_init(NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, spectrum_analyzer_stop());
}

/* ========================================================================
 * Deinit Tests
 * ======================================================================== */

void test_deinit_succeeds_after_init(void) {
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_init(NULL));
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_deinit());
}

void test_deinit_stops_running_service(void) {
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_init(NULL));
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_start());
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_deinit());
}

void test_deinit_fails_without_init(void) {
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, spectrum_analyzer_deinit());
}

/* ========================================================================
 * Configuration Get/Set Tests
 * ======================================================================== */

void test_get_config_after_init(void) {
    config_spectrum_t init_cfg = {
        .default_center_freq_mhz = 900,
        .default_bandwidth_khz = 250,
        .default_gain_db = 15.0f,
        .detection_threshold_dbm = -55
    };
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_init(&init_cfg));

    spectrum_config_t result;
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_get_config(&result));
    TEST_ASSERT_EQUAL_UINT32(900, result.center_freq_mhz);
    TEST_ASSERT_EQUAL_UINT32(250, result.bandwidth_khz);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 15.0f, result.gain_db);
    TEST_ASSERT_EQUAL_INT32(-55, result.detection_threshold_dbm);
}

void test_set_config_updates_values(void) {
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_init(NULL));

    spectrum_config_t new_cfg = {
        .center_freq_mhz = 433,
        .bandwidth_khz = 100,
        .gain_db = 30.0f,
        .detection_threshold_dbm = -70
    };
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_set_config(&new_cfg));

    spectrum_config_t result;
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_get_config(&result));
    TEST_ASSERT_EQUAL_UINT32(433, result.center_freq_mhz);
    TEST_ASSERT_EQUAL_UINT32(100, result.bandwidth_khz);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 30.0f, result.gain_db);
}

void test_set_config_rejects_invalid(void) {
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_init(NULL));

    spectrum_config_t bad_cfg = {
        .center_freq_mhz = 2000,  /* Out of range */
        .bandwidth_khz = 500,
        .gain_db = 20.0f,
        .detection_threshold_dbm = -60
    };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, spectrum_analyzer_set_config(&bad_cfg));
}

/* ========================================================================
 * Frequency Classification Tests
 * ======================================================================== */

void test_classify_elrs_900_band(void) {
    /* 862 MHz - lower bound of ELRS 900 band */
    TEST_ASSERT_EQUAL(PEAK_CLASS_ELRS_900, spectrum_analyzer_classify_frequency(862000000U));
    /* 915 MHz - center of ELRS 900 band */
    TEST_ASSERT_EQUAL(PEAK_CLASS_ELRS_900, spectrum_analyzer_classify_frequency(915000000U));
    /* 928 MHz - upper bound of ELRS 900 band */
    TEST_ASSERT_EQUAL(PEAK_CLASS_ELRS_900, spectrum_analyzer_classify_frequency(928000000U));
}

void test_classify_elrs_2400_band(void) {
    /* 2400 MHz - lower bound */
    TEST_ASSERT_EQUAL(PEAK_CLASS_ELRS_2400, spectrum_analyzer_classify_frequency(2400000000U));
    /* 2450 MHz - center */
    TEST_ASSERT_EQUAL(PEAK_CLASS_ELRS_2400, spectrum_analyzer_classify_frequency(2450000000U));
    /* 2500 MHz - upper bound */
    TEST_ASSERT_EQUAL(PEAK_CLASS_ELRS_2400, spectrum_analyzer_classify_frequency(2500000000U));
}

void test_classify_unclassified_frequencies(void) {
    /* Below ELRS 900 band */
    TEST_ASSERT_EQUAL(PEAK_CLASS_UNCLASSIFIED, spectrum_analyzer_classify_frequency(500000000U));
    /* Between 900 and 2400 band */
    TEST_ASSERT_EQUAL(PEAK_CLASS_UNCLASSIFIED, spectrum_analyzer_classify_frequency(1200000000U));
    /* Just above the 928 MHz band */
    TEST_ASSERT_EQUAL(PEAK_CLASS_UNCLASSIFIED, spectrum_analyzer_classify_frequency(929000000U));
    /* Very low frequency */
    TEST_ASSERT_EQUAL(PEAK_CLASS_UNCLASSIFIED, spectrum_analyzer_classify_frequency(24000000U));
}

/* ========================================================================
 * Frequency Markers Tests
 * ======================================================================== */

void test_get_markers_returns_known_protocols(void) {
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_init(NULL));

    frequency_marker_t markers[SPECTRUM_MAX_MARKERS];
    uint8_t count = 0;
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_get_markers(markers, SPECTRUM_MAX_MARKERS, &count));

    /* Should have markers for known protocol bands */
    TEST_ASSERT_GREATER_THAN(0, count);
    TEST_ASSERT_LESS_OR_EQUAL(SPECTRUM_MAX_MARKERS, count);

    /* Verify each marker has valid data */
    for (uint8_t i = 0; i < count; i++) {
        TEST_ASSERT_NOT_NULL(markers[i].label);
        TEST_ASSERT_GREATER_THAN(0, markers[i].freq_start_hz);
        TEST_ASSERT_GREATER_THAN(markers[i].freq_start_hz, markers[i].freq_end_hz);
    }
}

void test_get_markers_null_params_fail(void) {
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_init(NULL));

    uint8_t count;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, spectrum_analyzer_get_markers(NULL, 10, &count));

    frequency_marker_t markers[10];
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, spectrum_analyzer_get_markers(markers, 10, NULL));
}

void test_get_markers_fails_without_init(void) {
    frequency_marker_t markers[10];
    uint8_t count;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, spectrum_analyzer_get_markers(markers, 10, &count));
}

/* ========================================================================
 * Peaks Tests
 * ======================================================================== */

void test_get_peaks_fails_without_init(void) {
    spectrum_peak_t peaks[10];
    uint8_t count;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, spectrum_analyzer_get_peaks(peaks, 10, &count));
}

void test_get_peaks_returns_empty_initially(void) {
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_init(NULL));

    spectrum_peak_t peaks[10];
    uint8_t count = 99;
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_get_peaks(peaks, 10, &count));
    TEST_ASSERT_EQUAL(0, count);
}

void test_get_peaks_null_params_fail(void) {
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_init(NULL));

    uint8_t count;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, spectrum_analyzer_get_peaks(NULL, 10, &count));

    spectrum_peak_t peaks[10];
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, spectrum_analyzer_get_peaks(peaks, 10, NULL));
}

/* ========================================================================
 * State Snapshot Tests
 * ======================================================================== */

void test_get_state_after_init(void) {
    config_spectrum_t init_cfg = {
        .default_center_freq_mhz = 915,
        .default_bandwidth_khz = 500,
        .default_gain_db = 20.0f,
        .detection_threshold_dbm = -60
    };
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_init(&init_cfg));

    spectrum_state_t state;
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_get_state(&state));

    TEST_ASSERT_FALSE(state.running);
    TEST_ASSERT_EQUAL_UINT32(915, state.config.center_freq_mhz);
    TEST_ASSERT_EQUAL_UINT32(500, state.config.bandwidth_khz);
    TEST_ASSERT_EQUAL(0, state.peak_count);
}

void test_get_state_shows_running_after_start(void) {
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_init(NULL));
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_start());

    spectrum_state_t state;
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_get_state(&state));
    TEST_ASSERT_TRUE(state.running);
}

void test_get_state_null_param_fails(void) {
    TEST_ASSERT_EQUAL(ESP_OK, spectrum_analyzer_init(NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, spectrum_analyzer_get_state(NULL));
}

void test_get_state_fails_without_init(void) {
    spectrum_state_t state;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, spectrum_analyzer_get_state(&state));
}

/* ========================================================================
 * Test Runner
 * ======================================================================== */

int main(void) {
    UNITY_BEGIN();

    /* Configuration Validation */
    RUN_TEST(test_validate_config_null_returns_error);
    RUN_TEST(test_validate_config_valid_default);
    RUN_TEST(test_validate_config_freq_below_min);
    RUN_TEST(test_validate_config_freq_above_max);
    RUN_TEST(test_validate_config_freq_at_min_boundary);
    RUN_TEST(test_validate_config_freq_at_max_boundary);
    RUN_TEST(test_validate_config_bw_below_min);
    RUN_TEST(test_validate_config_bw_above_max);
    RUN_TEST(test_validate_config_gain_below_min);
    RUN_TEST(test_validate_config_gain_above_max);
    RUN_TEST(test_validate_config_gain_at_zero);
    RUN_TEST(test_validate_config_gain_at_max);

    /* Initialization */
    RUN_TEST(test_init_with_default_config);
    RUN_TEST(test_init_with_custom_config);
    RUN_TEST(test_init_fails_when_sdr_inactive);
    RUN_TEST(test_init_double_init_fails);
    RUN_TEST(test_init_with_invalid_config_rejected);

    /* Start / Stop */
    RUN_TEST(test_start_succeeds_after_init);
    RUN_TEST(test_start_fails_without_init);
    RUN_TEST(test_start_double_start_fails);
    RUN_TEST(test_stop_succeeds_when_running);
    RUN_TEST(test_stop_fails_when_not_running);

    /* Deinit */
    RUN_TEST(test_deinit_succeeds_after_init);
    RUN_TEST(test_deinit_stops_running_service);
    RUN_TEST(test_deinit_fails_without_init);

    /* Configuration Get/Set */
    RUN_TEST(test_get_config_after_init);
    RUN_TEST(test_set_config_updates_values);
    RUN_TEST(test_set_config_rejects_invalid);

    /* Frequency Classification */
    RUN_TEST(test_classify_elrs_900_band);
    RUN_TEST(test_classify_elrs_2400_band);
    RUN_TEST(test_classify_unclassified_frequencies);

    /* Frequency Markers */
    RUN_TEST(test_get_markers_returns_known_protocols);
    RUN_TEST(test_get_markers_null_params_fail);
    RUN_TEST(test_get_markers_fails_without_init);

    /* Peaks */
    RUN_TEST(test_get_peaks_fails_without_init);
    RUN_TEST(test_get_peaks_returns_empty_initially);
    RUN_TEST(test_get_peaks_null_params_fail);

    /* State */
    RUN_TEST(test_get_state_after_init);
    RUN_TEST(test_get_state_shows_running_after_start);
    RUN_TEST(test_get_state_null_param_fails);
    RUN_TEST(test_get_state_fails_without_init);

    return UNITY_END();
}
