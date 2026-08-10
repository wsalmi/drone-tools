/**
 * @file test_hal_gps.c
 * @brief Unit tests for HAL GPS NMEA parser and fix validation logic.
 *
 * Tests the following:
 * - NMEA checksum validation
 * - GGA sentence parsing (position, altitude, satellites, HDOP)
 * - RMC sentence parsing (position, time)
 * - Fix validation logic (sats >= 4 AND hdop < 5.0)
 * - Last valid position preservation on fix loss
 * - Edge cases (empty fields, invalid data)
 */

#include "unity.h"
#include "hal_gps.h"
#include <string.h>
#include <math.h>

/* Tolerance for floating-point comparisons */
#define FLOAT_TOLERANCE 0.001f
#define DOUBLE_TOLERANCE 0.0001

void setUp(void) {
    /* Nothing to set up */
}

void tearDown(void) {
    /* Nothing to tear down */
}

/* ========================================================================
 * NMEA Checksum Tests
 * ======================================================================== */

void test_nmea_checksum_valid_gga(void)
{
    /* Real GGA sentence with valid checksum */
    const char *sentence = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,47.0,M,,*4F";
    TEST_ASSERT_TRUE(nmea_validate_checksum(sentence));
}

void test_nmea_checksum_valid_rmc(void)
{
    /* Real RMC sentence with valid checksum */
    const char *sentence = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";
    TEST_ASSERT_TRUE(nmea_validate_checksum(sentence));
}

void test_nmea_checksum_invalid(void)
{
    /* Corrupted checksum */
    const char *sentence = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,47.0,M,,*FF";
    TEST_ASSERT_FALSE(nmea_validate_checksum(sentence));
}

void test_nmea_checksum_null_input(void)
{
    TEST_ASSERT_FALSE(nmea_validate_checksum(NULL));
}

void test_nmea_checksum_no_dollar(void)
{
    const char *sentence = "GPGGA,123519,4807.038*47";
    TEST_ASSERT_FALSE(nmea_validate_checksum(sentence));
}

void test_nmea_checksum_no_star(void)
{
    const char *sentence = "$GPGGA,123519,4807.038";
    TEST_ASSERT_FALSE(nmea_validate_checksum(sentence));
}

/* ========================================================================
 * GGA Parsing Tests
 * ======================================================================== */

void test_parse_gga_valid_position(void)
{
    /* Latitude: 48°07.038'N = 48.1173° N
     * Longitude: 011°31.000'E = 11.516667° E
     * Fix quality: 1 (GPS fix)
     * Satellites: 8
     * HDOP: 0.9
     * Altitude: 545.4 m
     */
    const char *sentence = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,47.0,M,,*4F";
    gps_position_t pos;
    memset(&pos, 0, sizeof(pos));

    esp_err_t err = nmea_parse_sentence(sentence, &pos);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Verify latitude: 48 + 7.038/60 = 48.1173 */
    TEST_ASSERT_DOUBLE_WITHIN(DOUBLE_TOLERANCE, 48.1173, pos.latitude);

    /* Verify longitude: 11 + 31.0/60 = 11.516667 */
    TEST_ASSERT_DOUBLE_WITHIN(DOUBLE_TOLERANCE, 11.51667, pos.longitude);

    /* Verify altitude */
    TEST_ASSERT_FLOAT_WITHIN(FLOAT_TOLERANCE, 545.4f, pos.altitude_m);

    /* Verify satellites */
    TEST_ASSERT_EQUAL_UINT8(8, pos.satellites_used);

    /* Verify HDOP */
    TEST_ASSERT_FLOAT_WITHIN(FLOAT_TOLERANCE, 0.9f, pos.hdop);

    /* Fix should be valid (sats=8 >= 4, hdop=0.9 < 5.0) */
    TEST_ASSERT_TRUE(pos.fix_valid);
}

void test_parse_gga_southern_hemisphere(void)
{
    /* Latitude: 23°33.031'S = -23.55052° */
    const char *sentence = "$GPGGA,103045,2333.031,S,04637.999,W,1,06,1.5,760.0,M,-3.0,M,,*5F";
    gps_position_t pos;
    memset(&pos, 0, sizeof(pos));

    esp_err_t err = nmea_parse_sentence(sentence, &pos);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* South latitude should be negative */
    TEST_ASSERT_TRUE(pos.latitude < 0);
    TEST_ASSERT_DOUBLE_WITHIN(DOUBLE_TOLERANCE, -23.55052, pos.latitude);

    /* West longitude should be negative */
    TEST_ASSERT_TRUE(pos.longitude < 0);
    TEST_ASSERT_DOUBLE_WITHIN(DOUBLE_TOLERANCE, -46.63332, pos.longitude);
}

void test_parse_gga_no_fix(void)
{
    /* Fix quality = 0 (no fix) */
    const char *sentence = "$GPGGA,123519,,,,,,0,00,99.9,,M,,M,,*50";
    gps_position_t pos;
    memset(&pos, 0, sizeof(pos));

    esp_err_t err = nmea_parse_sentence(sentence, &pos);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_FALSE(pos.fix_valid);
    TEST_ASSERT_EQUAL_UINT8(0, pos.satellites_used);
}

void test_parse_gga_few_satellites(void)
{
    /* Only 3 satellites - fix should be invalid */
    const char *sentence = "$GPGGA,123519,4807.038,N,01131.000,E,1,03,4.5,545.4,M,47.0,M,,*4C";
    gps_position_t pos;
    memset(&pos, 0, sizeof(pos));

    esp_err_t err = nmea_parse_sentence(sentence, &pos);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_UINT8(3, pos.satellites_used);
    TEST_ASSERT_FALSE(pos.fix_valid);
}

void test_parse_gga_high_hdop(void)
{
    /* HDOP = 6.0, too high for valid fix */
    const char *sentence = "$GPGGA,123519,4807.038,N,01131.000,E,1,05,6.0,545.4,M,47.0,M,,*4D";
    gps_position_t pos;
    memset(&pos, 0, sizeof(pos));

    esp_err_t err = nmea_parse_sentence(sentence, &pos);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_UINT8(5, pos.satellites_used);
    TEST_ASSERT_FLOAT_WITHIN(FLOAT_TOLERANCE, 6.0f, pos.hdop);
    TEST_ASSERT_FALSE(pos.fix_valid);
}

/* ========================================================================
 * RMC Parsing Tests
 * ======================================================================== */

void test_parse_rmc_valid(void)
{
    const char *sentence = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";
    gps_position_t pos;
    memset(&pos, 0, sizeof(pos));

    esp_err_t err = nmea_parse_sentence(sentence, &pos);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* RMC updates position */
    TEST_ASSERT_DOUBLE_WITHIN(DOUBLE_TOLERANCE, 48.1173, pos.latitude);
    TEST_ASSERT_DOUBLE_WITHIN(DOUBLE_TOLERANCE, 11.51667, pos.longitude);
}

void test_parse_rmc_void_status(void)
{
    /* Status = V (void/invalid data) - position should not be updated */
    const char *sentence = "$GPRMC,123519,V,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*7D";
    gps_position_t pos;
    memset(&pos, 0, sizeof(pos));
    pos.latitude = 10.0;  /* Pre-set to verify no change */

    esp_err_t err = nmea_parse_sentence(sentence, &pos);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Position should remain unchanged */
    TEST_ASSERT_DOUBLE_WITHIN(DOUBLE_TOLERANCE, 10.0, pos.latitude);
}

/* ========================================================================
 * Fix Validation Tests
 * ======================================================================== */

void test_fix_valid_minimum_criteria(void)
{
    gps_position_t pos = {
        .satellites_used = 4,
        .hdop = 4.9f
    };
    TEST_ASSERT_TRUE(gps_evaluate_fix(&pos));
}

void test_fix_invalid_too_few_sats(void)
{
    gps_position_t pos = {
        .satellites_used = 3,
        .hdop = 1.0f
    };
    TEST_ASSERT_FALSE(gps_evaluate_fix(&pos));
}

void test_fix_invalid_hdop_too_high(void)
{
    gps_position_t pos = {
        .satellites_used = 10,
        .hdop = 5.0f  /* Exactly 5.0 should be invalid (must be < 5.0) */
    };
    TEST_ASSERT_FALSE(gps_evaluate_fix(&pos));
}

void test_fix_valid_many_sats_low_hdop(void)
{
    gps_position_t pos = {
        .satellites_used = 12,
        .hdop = 0.8f
    };
    TEST_ASSERT_TRUE(gps_evaluate_fix(&pos));
}

void test_fix_null_pointer(void)
{
    TEST_ASSERT_FALSE(gps_evaluate_fix(NULL));
}

/* ========================================================================
 * Edge Cases
 * ======================================================================== */

void test_parse_null_sentence(void)
{
    gps_position_t pos;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, nmea_parse_sentence(NULL, &pos));
}

void test_parse_null_position(void)
{
    const char *sentence = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,47.0,M,,*47";
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, nmea_parse_sentence(sentence, NULL));
}

void test_parse_unsupported_sentence(void)
{
    /* $GPVTG is not supported */
    const char *sentence = "$GPVTG,054.7,T,034.4,M,005.5,N,010.2,K*48";
    gps_position_t pos;
    memset(&pos, 0, sizeof(pos));

    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, nmea_parse_sentence(sentence, &pos));
}

void test_parse_gngga_talker_id(void)
{
    /* GNGGA uses multi-GNSS talker ID - should be supported */
    const char *sentence = "$GNGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,47.0,M,,*51";
    gps_position_t pos;
    memset(&pos, 0, sizeof(pos));

    esp_err_t err = nmea_parse_sentence(sentence, &pos);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_DOUBLE_WITHIN(DOUBLE_TOLERANCE, 48.1173, pos.latitude);
}

/* ========================================================================
 * HAL API Tests (with mock mode)
 * ======================================================================== */

void test_hal_gps_init_deinit(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, hal_gps_init(9600));
    TEST_ASSERT_EQUAL(HAL_STATUS_ACTIVE, hal_gps_get_status());
    TEST_ASSERT_EQUAL(ESP_OK, hal_gps_deinit());
    TEST_ASSERT_EQUAL(HAL_STATUS_INACTIVE, hal_gps_get_status());
}

void test_hal_gps_double_init(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, hal_gps_init(9600));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, hal_gps_init(9600));
    TEST_ASSERT_EQUAL(ESP_OK, hal_gps_deinit());
}

void test_hal_gps_get_position_not_initialized(void)
{
    gps_position_t pos;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, hal_gps_get_position(&pos));
}

void test_hal_gps_get_position_null(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, hal_gps_init(9600));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, hal_gps_get_position(NULL));
    TEST_ASSERT_EQUAL(ESP_OK, hal_gps_deinit());
}

void test_hal_gps_has_fix_not_initialized(void)
{
    TEST_ASSERT_FALSE(hal_gps_has_fix());
}

/* ========================================================================
 * Test Runner
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* Checksum tests */
    RUN_TEST(test_nmea_checksum_valid_gga);
    RUN_TEST(test_nmea_checksum_valid_rmc);
    RUN_TEST(test_nmea_checksum_invalid);
    RUN_TEST(test_nmea_checksum_null_input);
    RUN_TEST(test_nmea_checksum_no_dollar);
    RUN_TEST(test_nmea_checksum_no_star);

    /* GGA parsing tests */
    RUN_TEST(test_parse_gga_valid_position);
    RUN_TEST(test_parse_gga_southern_hemisphere);
    RUN_TEST(test_parse_gga_no_fix);
    RUN_TEST(test_parse_gga_few_satellites);
    RUN_TEST(test_parse_gga_high_hdop);

    /* RMC parsing tests */
    RUN_TEST(test_parse_rmc_valid);
    RUN_TEST(test_parse_rmc_void_status);

    /* Fix validation tests */
    RUN_TEST(test_fix_valid_minimum_criteria);
    RUN_TEST(test_fix_invalid_too_few_sats);
    RUN_TEST(test_fix_invalid_hdop_too_high);
    RUN_TEST(test_fix_valid_many_sats_low_hdop);
    RUN_TEST(test_fix_null_pointer);

    /* Edge cases */
    RUN_TEST(test_parse_null_sentence);
    RUN_TEST(test_parse_null_position);
    RUN_TEST(test_parse_unsupported_sentence);
    RUN_TEST(test_parse_gngga_talker_id);

    /* HAL API tests */
    RUN_TEST(test_hal_gps_init_deinit);
    RUN_TEST(test_hal_gps_double_init);
    RUN_TEST(test_hal_gps_get_position_not_initialized);
    RUN_TEST(test_hal_gps_get_position_null);
    RUN_TEST(test_hal_gps_has_fix_not_initialized);

    return UNITY_END();
}
