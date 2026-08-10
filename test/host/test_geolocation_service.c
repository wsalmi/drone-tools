/**
 * @file test_geolocation_service.c
 * @brief Unit tests for the Geolocation Service.
 *
 * Tests the following:
 * - Service initialization
 * - Haversine distance calculation (known reference values)
 * - Forward azimuth (bearing) calculation
 * - Symmetry property: distance(A,B) == distance(B,A)
 * - Identity property: distance(A,A) == 0
 * - Azimuth normalization to [0, 360)
 * - Invalid fix handling (result.valid = false)
 * - NULL pointer handling
 */

#include "unity.h"
#include "geolocation_service.h"
#include "hal_mocks.h"
#include <math.h>
#include <string.h>

/* Tolerance for distance comparisons (meters) */
#define DISTANCE_TOLERANCE_M 1.0f

/* Tolerance for azimuth comparisons (degrees) */
#define AZIMUTH_TOLERANCE_DEG 0.5f

/* Tolerance for symmetry check (meters) - floating point rounding */
#define SYMMETRY_TOLERANCE_M 0.01f

void setUp(void)
{
    mock_hal_gps_reset();
    /* Initialize mock GPS HAL (simulates hal_gps_init() called at system startup) */
    hal_gps_init(9600);
    /* Set GPS to active with a valid fix for most tests */
    mock_hal_gps_set_fix(true, -23.550520, -46.633309, 760.5f, 8, 1.2f);
}

void tearDown(void)
{
    /* Nothing to tear down */
}

/* ========================================================================
 * Initialization Tests
 * ======================================================================== */

void test_geo_service_init_success(void)
{
    esp_err_t err = geo_service_init();
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

void test_geo_service_init_gps_inactive(void)
{
    mock_hal_gps_reset();
    /* GPS status is INACTIVE after reset (hal_gps_init not called) */
    esp_err_t err = geo_service_init();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, err);
}

/* ========================================================================
 * Distance Calculation Tests (known reference values)
 * ======================================================================== */

void test_geo_calculate_zero_distance(void)
{
    /* Identity: same point → distance = 0 */
    gps_position_t from = {
        .latitude = -23.550520,
        .longitude = -46.633309,
        .altitude_m = 760.5f,
        .hdop = 1.2f,
        .satellites_used = 8,
        .timestamp_utc_ms = 1000,
        .fix_valid = true
    };
    relative_position_t result;

    esp_err_t err = geo_calculate_relative(&from, -23.550520, -46.633309, &result);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(result.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, result.distance_m);
}

void test_geo_calculate_known_distance_short(void)
{
    /* São Paulo to a nearby point (~1.1 km away)
     * From: -23.550520, -46.633309
     * To:   -23.560520, -46.633309 (exactly 1° south = ~1.11 km south)
     * Expected: approximately 1112 meters (0.01° lat ≈ 1112 m)
     */
    gps_position_t from = {
        .latitude = -23.550520,
        .longitude = -46.633309,
        .altitude_m = 760.5f,
        .hdop = 1.2f,
        .satellites_used = 8,
        .timestamp_utc_ms = 1000,
        .fix_valid = true
    };
    relative_position_t result;

    esp_err_t err = geo_calculate_relative(&from, -23.560520, -46.633309, &result);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(result.valid);
    /* ~1112 m for 0.01° latitude */
    TEST_ASSERT_FLOAT_WITHIN(5.0f, 1112.0f, result.distance_m);
}

void test_geo_calculate_known_distance_long(void)
{
    /* London to Paris - well-known reference ~344 km
     * London: 51.5074, -0.1278
     * Paris:  48.8566,  2.3522
     */
    gps_position_t from = {
        .latitude = 51.5074,
        .longitude = -0.1278,
        .altitude_m = 0.0f,
        .hdop = 1.0f,
        .satellites_used = 10,
        .timestamp_utc_ms = 1000,
        .fix_valid = true
    };
    relative_position_t result;

    esp_err_t err = geo_calculate_relative(&from, 48.8566, 2.3522, &result);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(result.valid);
    /* Distance should be ~343.5 km (Haversine great-circle) */
    TEST_ASSERT_FLOAT_WITHIN(5000.0f, 343500.0f, result.distance_m);
}

/* ========================================================================
 * Azimuth Calculation Tests
 * ======================================================================== */

void test_geo_azimuth_due_north(void)
{
    /* Point directly north → azimuth ~0° (or ~360°) */
    gps_position_t from = {
        .latitude = 0.0,
        .longitude = 0.0,
        .altitude_m = 0.0f,
        .hdop = 1.0f,
        .satellites_used = 8,
        .timestamp_utc_ms = 1000,
        .fix_valid = true
    };
    relative_position_t result;

    esp_err_t err = geo_calculate_relative(&from, 1.0, 0.0, &result);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(result.valid);
    TEST_ASSERT_FLOAT_WITHIN(AZIMUTH_TOLERANCE_DEG, 0.0f, result.azimuth_deg);
}

void test_geo_azimuth_due_east(void)
{
    /* Point directly east → azimuth ~90° */
    gps_position_t from = {
        .latitude = 0.0,
        .longitude = 0.0,
        .altitude_m = 0.0f,
        .hdop = 1.0f,
        .satellites_used = 8,
        .timestamp_utc_ms = 1000,
        .fix_valid = true
    };
    relative_position_t result;

    esp_err_t err = geo_calculate_relative(&from, 0.0, 1.0, &result);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(result.valid);
    TEST_ASSERT_FLOAT_WITHIN(AZIMUTH_TOLERANCE_DEG, 90.0f, result.azimuth_deg);
}

void test_geo_azimuth_due_south(void)
{
    /* Point directly south → azimuth ~180° */
    gps_position_t from = {
        .latitude = 0.0,
        .longitude = 0.0,
        .altitude_m = 0.0f,
        .hdop = 1.0f,
        .satellites_used = 8,
        .timestamp_utc_ms = 1000,
        .fix_valid = true
    };
    relative_position_t result;

    esp_err_t err = geo_calculate_relative(&from, -1.0, 0.0, &result);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(result.valid);
    TEST_ASSERT_FLOAT_WITHIN(AZIMUTH_TOLERANCE_DEG, 180.0f, result.azimuth_deg);
}

void test_geo_azimuth_due_west(void)
{
    /* Point directly west → azimuth ~270° */
    gps_position_t from = {
        .latitude = 0.0,
        .longitude = 0.0,
        .altitude_m = 0.0f,
        .hdop = 1.0f,
        .satellites_used = 8,
        .timestamp_utc_ms = 1000,
        .fix_valid = true
    };
    relative_position_t result;

    esp_err_t err = geo_calculate_relative(&from, 0.0, -1.0, &result);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(result.valid);
    TEST_ASSERT_FLOAT_WITHIN(AZIMUTH_TOLERANCE_DEG, 270.0f, result.azimuth_deg);
}

void test_geo_azimuth_always_in_range(void)
{
    /* Test several directions to ensure azimuth ∈ [0, 360) */
    gps_position_t from = {
        .latitude = 45.0,
        .longitude = 90.0,
        .altitude_m = 0.0f,
        .hdop = 1.0f,
        .satellites_used = 8,
        .timestamp_utc_ms = 1000,
        .fix_valid = true
    };
    relative_position_t result;

    /* NE direction */
    geo_calculate_relative(&from, 46.0, 91.0, &result);
    TEST_ASSERT_TRUE(result.azimuth_deg >= 0.0f && result.azimuth_deg < 360.0f);

    /* SE direction */
    geo_calculate_relative(&from, 44.0, 91.0, &result);
    TEST_ASSERT_TRUE(result.azimuth_deg >= 0.0f && result.azimuth_deg < 360.0f);

    /* SW direction */
    geo_calculate_relative(&from, 44.0, 89.0, &result);
    TEST_ASSERT_TRUE(result.azimuth_deg >= 0.0f && result.azimuth_deg < 360.0f);

    /* NW direction */
    geo_calculate_relative(&from, 46.0, 89.0, &result);
    TEST_ASSERT_TRUE(result.azimuth_deg >= 0.0f && result.azimuth_deg < 360.0f);
}

/* ========================================================================
 * Symmetry Tests
 * ======================================================================== */

void test_geo_distance_symmetry(void)
{
    /* distance(A,B) == distance(B,A) */
    gps_position_t posA = {
        .latitude = -23.550520,
        .longitude = -46.633309,
        .altitude_m = 760.5f,
        .hdop = 1.2f,
        .satellites_used = 8,
        .timestamp_utc_ms = 1000,
        .fix_valid = true
    };

    gps_position_t posB = {
        .latitude = -23.549800,
        .longitude = -46.632100,
        .altitude_m = 780.0f,
        .hdop = 1.0f,
        .satellites_used = 10,
        .timestamp_utc_ms = 2000,
        .fix_valid = true
    };

    relative_position_t resultAB, resultBA;

    esp_err_t errAB = geo_calculate_relative(&posA, posB.latitude, posB.longitude, &resultAB);
    esp_err_t errBA = geo_calculate_relative(&posB, posA.latitude, posA.longitude, &resultBA);

    TEST_ASSERT_EQUAL(ESP_OK, errAB);
    TEST_ASSERT_EQUAL(ESP_OK, errBA);
    TEST_ASSERT_TRUE(resultAB.valid);
    TEST_ASSERT_TRUE(resultBA.valid);

    /* Distances should be equal (symmetry) */
    TEST_ASSERT_FLOAT_WITHIN(SYMMETRY_TOLERANCE_M, resultAB.distance_m, resultBA.distance_m);
}

void test_geo_distance_symmetry_long_range(void)
{
    /* Test symmetry with larger distance (NYC to Tokyo) */
    gps_position_t posA = {
        .latitude = 40.7128,
        .longitude = -74.0060,
        .altitude_m = 0.0f,
        .hdop = 1.0f,
        .satellites_used = 8,
        .timestamp_utc_ms = 1000,
        .fix_valid = true
    };

    gps_position_t posB = {
        .latitude = 35.6762,
        .longitude = 139.6503,
        .altitude_m = 0.0f,
        .hdop = 1.0f,
        .satellites_used = 8,
        .timestamp_utc_ms = 2000,
        .fix_valid = true
    };

    relative_position_t resultAB, resultBA;

    geo_calculate_relative(&posA, posB.latitude, posB.longitude, &resultAB);
    geo_calculate_relative(&posB, posA.latitude, posA.longitude, &resultBA);

    TEST_ASSERT_FLOAT_WITHIN(SYMMETRY_TOLERANCE_M, resultAB.distance_m, resultBA.distance_m);
}

/* ========================================================================
 * Invalid Fix Tests
 * ======================================================================== */

void test_geo_calculate_no_fix_returns_invalid(void)
{
    /* When from has no valid fix, result should be invalid */
    gps_position_t from = {
        .latitude = -23.550520,
        .longitude = -46.633309,
        .altitude_m = 760.5f,
        .hdop = 10.0f,
        .satellites_used = 2,
        .timestamp_utc_ms = 1000,
        .fix_valid = false
    };
    relative_position_t result;

    esp_err_t err = geo_calculate_relative(&from, -23.549800, -46.632100, &result);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_FALSE(result.valid);
}

/* ========================================================================
 * NULL Pointer Tests
 * ======================================================================== */

void test_geo_calculate_null_from(void)
{
    relative_position_t result;
    esp_err_t err = geo_calculate_relative(NULL, 0.0, 0.0, &result);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

void test_geo_calculate_null_result(void)
{
    gps_position_t from = {
        .latitude = 0.0,
        .longitude = 0.0,
        .fix_valid = true
    };
    esp_err_t err = geo_calculate_relative(&from, 1.0, 1.0, NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

/* ========================================================================
 * Monitor Position Tests
 * ======================================================================== */

void test_geo_get_monitor_position_initialized(void)
{
    geo_service_init();
    const gps_position_t *pos = geo_get_monitor_position();
    TEST_ASSERT_NOT_NULL(pos);
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, -23.550520, pos->latitude);
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, -46.633309, pos->longitude);
}

void test_geo_has_valid_fix_with_fix(void)
{
    mock_hal_gps_set_fix(true, -23.550520, -46.633309, 760.5f, 8, 1.2f);
    geo_service_init();
    TEST_ASSERT_TRUE(geo_has_valid_fix());
}

void test_geo_has_valid_fix_without_fix(void)
{
    mock_hal_gps_set_fix(false, 0.0, 0.0, 0.0f, 0, 99.0f);
    TEST_ASSERT_FALSE(geo_has_valid_fix());
}

/* ========================================================================
 * Distance non-negative Tests
 * ======================================================================== */

void test_geo_distance_always_non_negative(void)
{
    gps_position_t from = {
        .latitude = -89.99,
        .longitude = 179.99,
        .altitude_m = 0.0f,
        .hdop = 1.0f,
        .satellites_used = 8,
        .timestamp_utc_ms = 1000,
        .fix_valid = true
    };
    relative_position_t result;

    esp_err_t err = geo_calculate_relative(&from, 89.99, -179.99, &result);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(result.valid);
    TEST_ASSERT_TRUE(result.distance_m >= 0.0f);
}

/* ========================================================================
 * Test Runner
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* Initialization tests */
    RUN_TEST(test_geo_service_init_success);
    RUN_TEST(test_geo_service_init_gps_inactive);

    /* Distance calculation tests */
    RUN_TEST(test_geo_calculate_zero_distance);
    RUN_TEST(test_geo_calculate_known_distance_short);
    RUN_TEST(test_geo_calculate_known_distance_long);

    /* Azimuth tests */
    RUN_TEST(test_geo_azimuth_due_north);
    RUN_TEST(test_geo_azimuth_due_east);
    RUN_TEST(test_geo_azimuth_due_south);
    RUN_TEST(test_geo_azimuth_due_west);
    RUN_TEST(test_geo_azimuth_always_in_range);

    /* Symmetry tests */
    RUN_TEST(test_geo_distance_symmetry);
    RUN_TEST(test_geo_distance_symmetry_long_range);

    /* Invalid fix tests */
    RUN_TEST(test_geo_calculate_no_fix_returns_invalid);

    /* NULL pointer tests */
    RUN_TEST(test_geo_calculate_null_from);
    RUN_TEST(test_geo_calculate_null_result);

    /* Monitor position tests */
    RUN_TEST(test_geo_get_monitor_position_initialized);
    RUN_TEST(test_geo_has_valid_fix_with_fix);
    RUN_TEST(test_geo_has_valid_fix_without_fix);

    /* Distance non-negative */
    RUN_TEST(test_geo_distance_always_non_negative);

    return UNITY_END();
}
