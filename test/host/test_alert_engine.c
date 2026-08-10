/**
 * @file test_alert_engine.c
 * @brief Unit tests for the Alert Engine service.
 *
 * Tests cover:
 *   - New detection alert: buzzer 1s + visual notification 3s
 *   - Proximity alert: distinct buzzer pattern + visual notification with distance
 *   - Proximity alert condition: only when GPS fix valid
 *   - Silent mode: suppresses buzzer, keeps visual notifications
 *   - Repeat interval logic for proximity alerts
 *
 * Validates: Requirements 13.1, 13.2, 13.4, 13.5
 */

#include "unity.h"
#include "alert_engine.h"
#include "hal_buzzer.h"
#include "hal_mocks.h"
#include "geolocation_service.h"
#include "hal_gps.h"

#include <string.h>

/* ========================================================================
 * Test Setup / Teardown
 * ======================================================================== */

void setUp(void)
{
    /* Reset mocks */
    mock_hal_gps_reset();
    mock_hal_buzzer_reset();

    /* Initialize GPS mock as active (required by geo_service_init) */
    hal_gps_init(9600);

    /* Set GPS fix to valid by default */
    mock_hal_gps_set_fix(true, -23.550520, -46.633309, 760.5f, 8, 1.2f);

    /* Initialize geolocation service */
    geo_service_init();

    /* Initialize buzzer */
    hal_buzzer_init();

    /* Initialize alert engine with default config */
    config_alert_t cfg = {
        .sound_enabled = true,
        .proximity_threshold_m = 500,
        .proximity_repeat_interval_s = 10,
        .out_of_range_timeout_s = 30
    };
    alert_engine_init(&cfg);
}

void tearDown(void)
{
    alert_engine_deinit();
}

/* ========================================================================
 * Test: New Detection Alert
 * ======================================================================== */

void test_new_detection_plays_buzzer(void)
{
    esp_err_t ret = alert_engine_new_detection("BRA-UAS-001", PROTOCOL_REMOTEID);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    /* Buzzer should have been called with new detection frequency and 1000ms */
    TEST_ASSERT_EQUAL_UINT32(1, mock_hal_buzzer_get_play_count());
    TEST_ASSERT_EQUAL_UINT32(ALERT_NEW_DETECTION_FREQ_HZ, mock_hal_buzzer_get_last_freq());
    TEST_ASSERT_EQUAL_UINT32(ALERT_NEW_DETECTION_BUZZER_MS, mock_hal_buzzer_get_last_duration());
}

void test_new_detection_creates_visual_notification(void)
{
    alert_engine_new_detection("ELRS-A1B2", PROTOCOL_ELRS);

    alert_notification_t notif;
    esp_err_t ret = alert_engine_get_notification(&notif);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(notif.active);
    TEST_ASSERT_EQUAL(ALERT_TYPE_NEW_DETECTION, notif.type);
    TEST_ASSERT_EQUAL_UINT32(ALERT_NEW_DETECTION_VISUAL_MS, notif.duration_ms);

    /* Notification text should contain aircraft_id and protocol */
    TEST_ASSERT_NOT_NULL(strstr(notif.text, "ELRS-A1B2"));
    TEST_ASSERT_NOT_NULL(strstr(notif.text, "ELRS"));
}

void test_new_detection_null_id_returns_invalid_arg(void)
{
    esp_err_t ret = alert_engine_new_detection(NULL, PROTOCOL_DJI);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
}

/* ========================================================================
 * Test: Proximity Alert
 * ======================================================================== */

void test_proximity_alert_fires_when_within_threshold(void)
{
    /* Aircraft at 300m (below 500m threshold) */
    esp_err_t ret = alert_engine_check_proximity("DRONE-01", 300.0f, 1000);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    /* Buzzer should fire with proximity frequency */
    TEST_ASSERT_EQUAL_UINT32(1, mock_hal_buzzer_get_play_count());
    TEST_ASSERT_EQUAL_UINT32(ALERT_PROXIMITY_FREQ_HZ, mock_hal_buzzer_get_last_freq());

    /* Visual notification should show proximity with distance */
    alert_notification_t notif;
    ret = alert_engine_get_notification(&notif);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(ALERT_TYPE_PROXIMITY, notif.type);
    TEST_ASSERT_NOT_NULL(strstr(notif.text, "DRONE-01"));
    TEST_ASSERT_NOT_NULL(strstr(notif.text, "300"));
}

void test_proximity_alert_does_not_fire_when_outside_threshold(void)
{
    /* Aircraft at 600m (above 500m threshold) */
    esp_err_t ret = alert_engine_check_proximity("DRONE-02", 600.0f, 1000);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    /* No buzzer should have been called */
    TEST_ASSERT_EQUAL_UINT32(0, mock_hal_buzzer_get_play_count());
}

void test_proximity_alert_repeats_after_interval(void)
{
    /* First alert at t=1000 */
    alert_engine_check_proximity("DRONE-03", 200.0f, 1000);
    TEST_ASSERT_EQUAL_UINT32(1, mock_hal_buzzer_get_play_count());

    /* Second check at t=5000 (5s later, before 10s interval) — should NOT re-alert */
    alert_engine_check_proximity("DRONE-03", 200.0f, 5000);
    TEST_ASSERT_EQUAL_UINT32(1, mock_hal_buzzer_get_play_count());

    /* Third check at t=11001 (after 10s interval) — should re-alert */
    alert_engine_check_proximity("DRONE-03", 200.0f, 11001);
    TEST_ASSERT_EQUAL_UINT32(2, mock_hal_buzzer_get_play_count());
}

void test_proximity_alert_distinct_from_new_detection(void)
{
    /* Fire a new detection alert */
    alert_engine_new_detection("AIR-1", PROTOCOL_MAVLINK);
    uint32_t detection_freq = mock_hal_buzzer_get_last_freq();

    /* Fire a proximity alert */
    alert_engine_check_proximity("AIR-2", 100.0f, 2000);
    uint32_t proximity_freq = mock_hal_buzzer_get_last_freq();

    /* Frequencies should be different (distinct pattern) */
    TEST_ASSERT_NOT_EQUAL(detection_freq, proximity_freq);
}

/* ========================================================================
 * Test: GPS Fix Condition for Proximity Alert
 * ======================================================================== */

void test_proximity_alert_suppressed_without_gps_fix(void)
{
    /* Remove GPS fix */
    mock_hal_gps_set_fix(false, 0.0, 0.0, 0.0f, 0, 99.0f);

    /* Aircraft at 100m — should NOT fire because GPS fix is invalid */
    alert_engine_check_proximity("DRONE-04", 100.0f, 1000);

    TEST_ASSERT_EQUAL_UINT32(0, mock_hal_buzzer_get_play_count());
}

void test_proximity_alert_fires_when_gps_fix_restored(void)
{
    /* No GPS fix */
    mock_hal_gps_set_fix(false, 0.0, 0.0, 0.0f, 0, 99.0f);
    alert_engine_check_proximity("DRONE-05", 100.0f, 1000);
    TEST_ASSERT_EQUAL_UINT32(0, mock_hal_buzzer_get_play_count());

    /* Restore GPS fix */
    mock_hal_gps_set_fix(true, -23.550520, -46.633309, 760.5f, 8, 1.2f);
    alert_engine_check_proximity("DRONE-05", 100.0f, 2000);
    TEST_ASSERT_EQUAL_UINT32(1, mock_hal_buzzer_get_play_count());
}

/* ========================================================================
 * Test: Silent Mode
 * ======================================================================== */

void test_silent_mode_suppresses_buzzer_on_new_detection(void)
{
    alert_engine_set_silent(true);
    TEST_ASSERT_TRUE(alert_engine_is_silent());

    alert_engine_new_detection("QUIET-01", PROTOCOL_DJI);

    /* Buzzer should NOT have been called */
    TEST_ASSERT_EQUAL_UINT32(0, mock_hal_buzzer_get_play_count());
}

void test_silent_mode_keeps_visual_notification_on_new_detection(void)
{
    alert_engine_set_silent(true);

    alert_engine_new_detection("QUIET-02", PROTOCOL_WIFI);

    /* Visual notification should still be created */
    alert_notification_t notif;
    esp_err_t ret = alert_engine_get_notification(&notif);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(notif.active);
    TEST_ASSERT_NOT_NULL(strstr(notif.text, "QUIET-02"));
}

void test_silent_mode_suppresses_buzzer_on_proximity(void)
{
    alert_engine_set_silent(true);

    alert_engine_check_proximity("QUIET-03", 100.0f, 1000);

    /* Buzzer should NOT have been called */
    TEST_ASSERT_EQUAL_UINT32(0, mock_hal_buzzer_get_play_count());
}

void test_silent_mode_keeps_visual_notification_on_proximity(void)
{
    alert_engine_set_silent(true);

    alert_engine_check_proximity("QUIET-04", 100.0f, 1000);

    /* Visual notification should still be created */
    alert_notification_t notif;
    esp_err_t ret = alert_engine_get_notification(&notif);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(notif.active);
    TEST_ASSERT_EQUAL(ALERT_TYPE_PROXIMITY, notif.type);
}

void test_silent_mode_toggle(void)
{
    /* Initially sound is enabled */
    TEST_ASSERT_FALSE(alert_engine_is_silent());

    /* Enable silent mode */
    alert_engine_set_silent(true);
    TEST_ASSERT_TRUE(alert_engine_is_silent());

    /* Disable silent mode */
    alert_engine_set_silent(false);
    TEST_ASSERT_FALSE(alert_engine_is_silent());

    /* Now buzzer should work again */
    alert_engine_new_detection("LOUD-01", PROTOCOL_ELRS);
    TEST_ASSERT_EQUAL_UINT32(1, mock_hal_buzzer_get_play_count());
}

/* ========================================================================
 * Test: Configuration
 * ======================================================================== */

void test_init_with_null_config_uses_defaults(void)
{
    alert_engine_deinit();
    esp_err_t ret = alert_engine_init(NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    /* Default threshold is 500m — aircraft at 499m should fire */
    alert_engine_check_proximity("DEFAULT-01", 499.0f, 1000);
    TEST_ASSERT_EQUAL_UINT32(1, mock_hal_buzzer_get_play_count());
}

void test_update_config_changes_threshold(void)
{
    /* Update threshold to 200m */
    config_alert_t new_cfg = {
        .sound_enabled = true,
        .proximity_threshold_m = 200,
        .proximity_repeat_interval_s = 10,
        .out_of_range_timeout_s = 30
    };
    alert_engine_update_config(&new_cfg);

    /* Aircraft at 300m — should NOT fire (above new 200m threshold) */
    alert_engine_check_proximity("CFG-01", 300.0f, 1000);
    TEST_ASSERT_EQUAL_UINT32(0, mock_hal_buzzer_get_play_count());

    /* Aircraft at 150m — should fire */
    alert_engine_check_proximity("CFG-02", 150.0f, 2000);
    TEST_ASSERT_EQUAL_UINT32(1, mock_hal_buzzer_get_play_count());
}

/* ========================================================================
 * Test: Clear Proximity Tracking
 * ======================================================================== */

void test_clear_proximity_resets_repeat_timer(void)
{
    /* First alert at t=1000 */
    alert_engine_check_proximity("TRACK-01", 200.0f, 1000);
    TEST_ASSERT_EQUAL_UINT32(1, mock_hal_buzzer_get_play_count());

    /* Clear tracking for this aircraft */
    alert_engine_clear_proximity("TRACK-01");

    /* Next check at t=2000 (only 1s later) — should fire because tracking was cleared */
    alert_engine_check_proximity("TRACK-01", 200.0f, 2000);
    TEST_ASSERT_EQUAL_UINT32(2, mock_hal_buzzer_get_play_count());
}

/* ========================================================================
 * Test Runner
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* New Detection Alert */
    RUN_TEST(test_new_detection_plays_buzzer);
    RUN_TEST(test_new_detection_creates_visual_notification);
    RUN_TEST(test_new_detection_null_id_returns_invalid_arg);

    /* Proximity Alert */
    RUN_TEST(test_proximity_alert_fires_when_within_threshold);
    RUN_TEST(test_proximity_alert_does_not_fire_when_outside_threshold);
    RUN_TEST(test_proximity_alert_repeats_after_interval);
    RUN_TEST(test_proximity_alert_distinct_from_new_detection);

    /* GPS Fix Condition */
    RUN_TEST(test_proximity_alert_suppressed_without_gps_fix);
    RUN_TEST(test_proximity_alert_fires_when_gps_fix_restored);

    /* Silent Mode */
    RUN_TEST(test_silent_mode_suppresses_buzzer_on_new_detection);
    RUN_TEST(test_silent_mode_keeps_visual_notification_on_new_detection);
    RUN_TEST(test_silent_mode_suppresses_buzzer_on_proximity);
    RUN_TEST(test_silent_mode_keeps_visual_notification_on_proximity);
    RUN_TEST(test_silent_mode_toggle);

    /* Configuration */
    RUN_TEST(test_init_with_null_config_uses_defaults);
    RUN_TEST(test_update_config_changes_threshold);

    /* Clear Proximity */
    RUN_TEST(test_clear_proximity_resets_repeat_timer);

    return UNITY_END();
}
