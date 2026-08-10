/**
 * @file test_pilot_locator.c
 * @brief Unit tests for the Pilot Locator service.
 *
 * Tests the priority-based pilot position determination:
 *   1. Operator Location → CONFIRMED
 *   2. Home Point → ESTIMATED
 *   3. RSSI Triangulation → ESTIMATED
 *   4. No source → UNKNOWN
 *
 * Validates: Requirements 6.1, 6.2, 6.3, 6.4, 6.5
 */

#include "unity.h"
#include "pilot_locator.h"

#include <string.h>
#include <math.h>

/* ========================================================================
 * Setup / Teardown
 * ======================================================================== */

void setUp(void)
{
    pilot_locator_init();
}

void tearDown(void)
{
    pilot_locator_reset();
}

/* ========================================================================
 * Test: UNKNOWN when no source available
 * ======================================================================== */

void test_get_position_unknown_when_no_data(void)
{
    /* Aircraft not tracked at all */
    pilot_position_t result;
    esp_err_t err = pilot_locator_get_position("UNKNOWN-001", &result);

    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, err);
    TEST_ASSERT_FALSE(result.position_available);
    TEST_ASSERT_EQUAL(PILOT_CONFIDENCE_UNKNOWN, result.confidence);
}

void test_get_position_unknown_after_update_without_sources(void)
{
    /* Update with only 1 RSSI reading (not enough for triangulation) */
    gps_position_t mon_pos = {
        .latitude = -23.55, .longitude = -46.63,
        .fix_valid = true, .hdop = 1.0f, .satellites_used = 8
    };

    esp_err_t err = pilot_locator_update("DRONE-001", NULL, -65, &mon_pos, 1000);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    pilot_position_t result;
    err = pilot_locator_get_position("DRONE-001", &result);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Only 1 RSSI reading → not enough for triangulation → UNKNOWN */
    TEST_ASSERT_FALSE(result.position_available);
    TEST_ASSERT_EQUAL(PILOT_CONFIDENCE_UNKNOWN, result.confidence);
}

/* ========================================================================
 * Test: Operator Location → CONFIRMED
 * ======================================================================== */

void test_operator_location_confirmed(void)
{
    double op_lat = -23.549;
    double op_lon = -46.631;

    esp_err_t err = pilot_locator_set_operator_location("DRONE-001", op_lat, op_lon);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    pilot_position_t result;
    err = pilot_locator_get_position("DRONE-001", &result);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    TEST_ASSERT_TRUE(result.position_available);
    TEST_ASSERT_EQUAL(PILOT_CONFIDENCE_CONFIRMED, result.confidence);
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, op_lat, result.lat);
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, op_lon, result.lon);
}

/* ========================================================================
 * Test: Home Point → ESTIMATED
 * ======================================================================== */

void test_home_point_estimated(void)
{
    double home_lat = -23.551;
    double home_lon = -46.635;

    esp_err_t err = pilot_locator_set_home_point("DRONE-002", home_lat, home_lon);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    pilot_position_t result;
    err = pilot_locator_get_position("DRONE-002", &result);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    TEST_ASSERT_TRUE(result.position_available);
    TEST_ASSERT_EQUAL(PILOT_CONFIDENCE_ESTIMATED, result.confidence);
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, home_lat, result.lat);
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, home_lon, result.lon);
}

/* ========================================================================
 * Test: Operator Location overrides Home Point
 * ======================================================================== */

void test_operator_location_overrides_home_point(void)
{
    double home_lat = -23.551;
    double home_lon = -46.635;
    double op_lat = -23.549;
    double op_lon = -46.631;

    /* Set home point first */
    pilot_locator_set_home_point("DRONE-003", home_lat, home_lon);

    /* Then set operator location */
    pilot_locator_set_operator_location("DRONE-003", op_lat, op_lon);

    pilot_position_t result;
    esp_err_t err = pilot_locator_get_position("DRONE-003", &result);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Operator location should win */
    TEST_ASSERT_TRUE(result.position_available);
    TEST_ASSERT_EQUAL(PILOT_CONFIDENCE_CONFIRMED, result.confidence);
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, op_lat, result.lat);
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, op_lon, result.lon);
}

/* ========================================================================
 * Test: RSSI Triangulation → ESTIMATED (with sufficient separated readings)
 * ======================================================================== */

void test_rssi_triangulation_estimated(void)
{
    /* Add 3 RSSI readings with >10m separation */
    gps_position_t pos1 = {.latitude = -23.5500, .longitude = -46.6300,
                           .fix_valid = true, .altitude_m = 760.0f,
                           .hdop = 1.0f, .satellites_used = 8};
    gps_position_t pos2 = {.latitude = -23.5502, .longitude = -46.6300,
                           .fix_valid = true, .altitude_m = 760.0f,
                           .hdop = 1.0f, .satellites_used = 8};
    gps_position_t pos3 = {.latitude = -23.5500, .longitude = -46.6302,
                           .fix_valid = true, .altitude_m = 760.0f,
                           .hdop = 1.0f, .satellites_used = 8};

    /* These positions are roughly 20m apart (0.0002 degrees ≈ 22m) */
    pilot_locator_add_rssi_reading("DRONE-004", -50, &pos1, 1000);
    pilot_locator_add_rssi_reading("DRONE-004", -60, &pos2, 2000);
    pilot_locator_add_rssi_reading("DRONE-004", -70, &pos3, 3000);

    pilot_position_t result;
    esp_err_t err = pilot_locator_get_position("DRONE-004", &result);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    TEST_ASSERT_TRUE(result.position_available);
    TEST_ASSERT_EQUAL(PILOT_CONFIDENCE_ESTIMATED, result.confidence);

    /* Result should be a weighted centroid biased toward stronger signals */
    TEST_ASSERT_DOUBLE_WITHIN(0.01, -23.55, result.lat);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, -46.63, result.lon);
}

/* ========================================================================
 * Test: RSSI readings not enough separation → UNKNOWN
 * ======================================================================== */

void test_rssi_insufficient_separation_unknown(void)
{
    /* Add 3 RSSI readings at nearly the same position (<10m apart) */
    gps_position_t pos1 = {.latitude = -23.55000, .longitude = -46.63000,
                           .fix_valid = true, .altitude_m = 760.0f,
                           .hdop = 1.0f, .satellites_used = 8};
    gps_position_t pos2 = {.latitude = -23.55000, .longitude = -46.63000,
                           .fix_valid = true, .altitude_m = 760.0f,
                           .hdop = 1.0f, .satellites_used = 8};
    gps_position_t pos3 = {.latitude = -23.55000, .longitude = -46.63000,
                           .fix_valid = true, .altitude_m = 760.0f,
                           .hdop = 1.0f, .satellites_used = 8};

    pilot_locator_add_rssi_reading("DRONE-005", -50, &pos1, 1000);
    pilot_locator_add_rssi_reading("DRONE-005", -60, &pos2, 2000);
    pilot_locator_add_rssi_reading("DRONE-005", -70, &pos3, 3000);

    pilot_position_t result;
    esp_err_t err = pilot_locator_get_position("DRONE-005", &result);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Same location for all readings → no separation → UNKNOWN */
    TEST_ASSERT_FALSE(result.position_available);
    TEST_ASSERT_EQUAL(PILOT_CONFIDENCE_UNKNOWN, result.confidence);
}

/* ========================================================================
 * Test: Home Point overrides RSSI Triangulation
 * ======================================================================== */

void test_home_point_overrides_rssi(void)
{
    /* Add RSSI readings for triangulation */
    gps_position_t pos1 = {.latitude = -23.5500, .longitude = -46.6300,
                           .fix_valid = true, .altitude_m = 760.0f,
                           .hdop = 1.0f, .satellites_used = 8};
    gps_position_t pos2 = {.latitude = -23.5502, .longitude = -46.6300,
                           .fix_valid = true, .altitude_m = 760.0f,
                           .hdop = 1.0f, .satellites_used = 8};
    gps_position_t pos3 = {.latitude = -23.5500, .longitude = -46.6302,
                           .fix_valid = true, .altitude_m = 760.0f,
                           .hdop = 1.0f, .satellites_used = 8};

    pilot_locator_add_rssi_reading("DRONE-006", -50, &pos1, 1000);
    pilot_locator_add_rssi_reading("DRONE-006", -60, &pos2, 2000);
    pilot_locator_add_rssi_reading("DRONE-006", -70, &pos3, 3000);

    /* Now set home point — should override RSSI */
    double home_lat = -23.560;
    double home_lon = -46.640;
    pilot_locator_set_home_point("DRONE-006", home_lat, home_lon);

    pilot_position_t result;
    esp_err_t err = pilot_locator_get_position("DRONE-006", &result);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    TEST_ASSERT_TRUE(result.position_available);
    TEST_ASSERT_EQUAL(PILOT_CONFIDENCE_ESTIMATED, result.confidence);
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, home_lat, result.lat);
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, home_lon, result.lon);
}

/* ========================================================================
 * Test: Invalid arguments
 * ======================================================================== */

void test_null_aircraft_id_returns_error(void)
{
    pilot_position_t result;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, pilot_locator_get_position(NULL, &result));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, pilot_locator_update(NULL, NULL, 0, NULL, 0));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, pilot_locator_set_operator_location(NULL, 0, 0));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, pilot_locator_set_home_point(NULL, 0, 0));
}

void test_null_result_returns_error(void)
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, pilot_locator_get_position("DRONE-001", NULL));
}

/* ========================================================================
 * Test: Multiple aircraft tracked independently
 * ======================================================================== */

void test_multiple_aircraft_independent(void)
{
    pilot_locator_set_operator_location("DRONE-A", 10.0, 20.0);
    pilot_locator_set_home_point("DRONE-B", 30.0, 40.0);

    pilot_position_t result_a, result_b;
    pilot_locator_get_position("DRONE-A", &result_a);
    pilot_locator_get_position("DRONE-B", &result_b);

    /* Drone A: operator location */
    TEST_ASSERT_EQUAL(PILOT_CONFIDENCE_CONFIRMED, result_a.confidence);
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, 10.0, result_a.lat);

    /* Drone B: home point */
    TEST_ASSERT_EQUAL(PILOT_CONFIDENCE_ESTIMATED, result_b.confidence);
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, 30.0, result_b.lat);
}

/* ========================================================================
 * Test: RSSI readings without valid GPS fix are ignored
 * ======================================================================== */

void test_rssi_no_fix_ignored(void)
{
    gps_position_t pos_no_fix = {.latitude = -23.55, .longitude = -46.63,
                                 .fix_valid = false};

    esp_err_t err = pilot_locator_add_rssi_reading("DRONE-007", -50, &pos_no_fix, 1000);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    pilot_position_t result;
    err = pilot_locator_get_position("DRONE-007", &result);

    /* No valid readings stored → aircraft not tracked */
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, err);
}

/* ========================================================================
 * Test: pilot_locator_update with detection adds RSSI
 * ======================================================================== */

void test_update_with_detection_adds_rssi(void)
{
    /* Add 3 detections with sufficient separation via update() */
    gps_position_t pos1 = {.latitude = -23.5500, .longitude = -46.6300,
                           .fix_valid = true, .hdop = 1.0f, .satellites_used = 8};
    gps_position_t pos2 = {.latitude = -23.5502, .longitude = -46.6300,
                           .fix_valid = true, .hdop = 1.0f, .satellites_used = 8};
    gps_position_t pos3 = {.latitude = -23.5500, .longitude = -46.6302,
                           .fix_valid = true, .hdop = 1.0f, .satellites_used = 8};

    pilot_locator_update("DRONE-008", NULL, -50, &pos1, 1000);
    pilot_locator_update("DRONE-008", NULL, -60, &pos2, 2000);
    pilot_locator_update("DRONE-008", NULL, -70, &pos3, 3000);

    pilot_position_t result;
    esp_err_t err = pilot_locator_get_position("DRONE-008", &result);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Should have enough RSSI for triangulation */
    TEST_ASSERT_TRUE(result.position_available);
    TEST_ASSERT_EQUAL(PILOT_CONFIDENCE_ESTIMATED, result.confidence);
}

/* ========================================================================
 * Test: Operator location set after home point still takes priority
 * ======================================================================== */

void test_operator_location_always_highest_priority(void)
{
    /* Set all three sources */
    gps_position_t pos1 = {.latitude = -23.5500, .longitude = -46.6300,
                           .fix_valid = true, .hdop = 1.0f, .satellites_used = 8};
    gps_position_t pos2 = {.latitude = -23.5502, .longitude = -46.6300,
                           .fix_valid = true, .hdop = 1.0f, .satellites_used = 8};
    gps_position_t pos3 = {.latitude = -23.5500, .longitude = -46.6302,
                           .fix_valid = true, .hdop = 1.0f, .satellites_used = 8};

    /* RSSI first */
    pilot_locator_add_rssi_reading("DRONE-009", -50, &pos1, 1000);
    pilot_locator_add_rssi_reading("DRONE-009", -60, &pos2, 2000);
    pilot_locator_add_rssi_reading("DRONE-009", -70, &pos3, 3000);

    /* Then home point */
    pilot_locator_set_home_point("DRONE-009", -23.560, -46.640);

    /* Then operator location */
    pilot_locator_set_operator_location("DRONE-009", -23.570, -46.650);

    pilot_position_t result;
    pilot_locator_get_position("DRONE-009", &result);

    /* Operator location always wins */
    TEST_ASSERT_TRUE(result.position_available);
    TEST_ASSERT_EQUAL(PILOT_CONFIDENCE_CONFIRMED, result.confidence);
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, -23.570, result.lat);
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, -46.650, result.lon);
}

/* ========================================================================
 * Test Runner
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* UNKNOWN state tests */
    RUN_TEST(test_get_position_unknown_when_no_data);
    RUN_TEST(test_get_position_unknown_after_update_without_sources);

    /* Operator Location (CONFIRMED) tests */
    RUN_TEST(test_operator_location_confirmed);

    /* Home Point (ESTIMATED) tests */
    RUN_TEST(test_home_point_estimated);

    /* Priority override tests */
    RUN_TEST(test_operator_location_overrides_home_point);
    RUN_TEST(test_home_point_overrides_rssi);
    RUN_TEST(test_operator_location_always_highest_priority);

    /* RSSI Triangulation tests */
    RUN_TEST(test_rssi_triangulation_estimated);
    RUN_TEST(test_rssi_insufficient_separation_unknown);

    /* Error handling tests */
    RUN_TEST(test_null_aircraft_id_returns_error);
    RUN_TEST(test_null_result_returns_error);

    /* Multiple aircraft */
    RUN_TEST(test_multiple_aircraft_independent);

    /* RSSI edge cases */
    RUN_TEST(test_rssi_no_fix_ignored);
    RUN_TEST(test_update_with_detection_adds_rssi);

    return UNITY_END();
}
