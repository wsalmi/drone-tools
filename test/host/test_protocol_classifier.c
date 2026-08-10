/**
 * @file test_protocol_classifier.c
 * @brief Unit tests for the Protocol Classifier service.
 *
 * Tests classification logic including:
 * - HIGH confidence when header AND frequency both match
 * - LOW confidence when header matches but frequency is "any" (0/0)
 * - LOW confidence when header matches but frequency is outside range
 * - PROTOCOL_UNKNOWN when no signature matches
 * - NULL input handling
 * - Empty payload handling
 *
 * Validates: Requirements 7.1, 7.2, 7.3, 7.6
 */

#include "unity.h"
#include "protocol_classifier.h"
#include "protocol_signatures.h"

#include <string.h>

/* ========================================================================
 * Setup / Teardown
 * ======================================================================== */

void setUp(void)
{
    /* Re-initialize with embedded defaults for each test */
    classifier_init(NULL);
}

void tearDown(void)
{
}

/* ========================================================================
 * Helper: build a raw_detection_t
 * ======================================================================== */

static raw_detection_t make_detection(const uint8_t *payload, uint16_t len,
                                      uint32_t frequency_hz)
{
    raw_detection_t det;
    memset(&det, 0, sizeof(det));
    det.source = DETECTION_SOURCE_LORA;
    det.frequency_hz = frequency_hz;
    det.payload_len = len;
    if (payload && len > 0) {
        uint16_t copy_len = len > 256 ? 256 : len;
        memcpy(det.raw_payload, payload, copy_len);
    }
    return det;
}

/* ========================================================================
 * Test: classifier_init succeeds
 * ======================================================================== */

void test_classifier_init_succeeds(void)
{
    esp_err_t ret = classifier_init(NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_GREATER_THAN(0, classifier_get_signature_count());
}

/* ========================================================================
 * Test: ELRS 900 MHz — HIGH confidence (header + frequency match)
 * ======================================================================== */

void test_classify_elrs_900_high_confidence(void)
{
    /* ELRS header: first byte masked with 0xFC should be 0x00.
     * So any byte with lower 2 bits set (e.g., 0x03) works: 0x03 & 0xFC = 0x00 */
    uint8_t payload[] = {0x03, 0xAA, 0xBB};
    raw_detection_t det = make_detection(payload, sizeof(payload), 915000000); /* 915 MHz */

    classification_result_t result;
    esp_err_t ret = classifier_classify(&det, &result);

    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(PROTOCOL_ELRS, result.protocol);
    TEST_ASSERT_EQUAL(CONFIDENCE_HIGH, result.confidence);
    TEST_ASSERT_EQUAL_STRING("LORA", result.modulation_info);
}

/* ========================================================================
 * Test: MAVLink v1 — LOW confidence (header matches, freq "any")
 * ======================================================================== */

void test_classify_mavlink_v1_low_confidence(void)
{
    /* MAVLink v1 STX = 0xFE, mask = 0xFF, freq = any (0/0) */
    uint8_t payload[] = {0xFE, 0x09, 0x00};
    raw_detection_t det = make_detection(payload, sizeof(payload), 433000000);

    classification_result_t result;
    esp_err_t ret = classifier_classify(&det, &result);

    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(PROTOCOL_MAVLINK, result.protocol);
    TEST_ASSERT_EQUAL(CONFIDENCE_LOW, result.confidence);
    TEST_ASSERT_EQUAL_STRING("ANY", result.modulation_info);
}

/* ========================================================================
 * Test: MAVLink v2 — LOW confidence
 * ======================================================================== */

void test_classify_mavlink_v2_low_confidence(void)
{
    /* MAVLink v2 STX = 0xFD */
    uint8_t payload[] = {0xFD, 0x1A, 0x00};
    raw_detection_t det = make_detection(payload, sizeof(payload), 868000000);

    classification_result_t result;
    esp_err_t ret = classifier_classify(&det, &result);

    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(PROTOCOL_MAVLINK, result.protocol);
    TEST_ASSERT_EQUAL(CONFIDENCE_LOW, result.confidence);
}

/* ========================================================================
 * Test: DJI 2.4 GHz — HIGH confidence
 * ======================================================================== */

void test_classify_dji_high_confidence(void)
{
    /* DJI header: 0x55 with mask 0xFF in 2400–2500 MHz range */
    uint8_t payload[] = {0x55, 0x12, 0x34};
    raw_detection_t det = make_detection(payload, sizeof(payload), 2450000000UL);

    classification_result_t result;
    esp_err_t ret = classifier_classify(&det, &result);

    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(PROTOCOL_DJI, result.protocol);
    TEST_ASSERT_EQUAL(CONFIDENCE_HIGH, result.confidence);
    TEST_ASSERT_EQUAL_STRING("OFDM", result.modulation_info);
}

/* ========================================================================
 * Test: DJI header at wrong frequency — LOW confidence
 * ======================================================================== */

void test_classify_dji_wrong_freq_low_confidence(void)
{
    /* DJI header: 0x55, but at 900 MHz (outside 2400–2500 MHz range) */
    uint8_t payload[] = {0x55, 0x12, 0x34};
    raw_detection_t det = make_detection(payload, sizeof(payload), 900000000);

    classification_result_t result;
    esp_err_t ret = classifier_classify(&det, &result);

    TEST_ASSERT_EQUAL(ESP_OK, ret);
    /* Should still classify as DJI, but with LOW confidence */
    TEST_ASSERT_EQUAL(PROTOCOL_DJI, result.protocol);
    TEST_ASSERT_EQUAL(CONFIDENCE_LOW, result.confidence);
}

/* ========================================================================
 * Test: Crossfire 868 MHz — HIGH confidence
 * ======================================================================== */

void test_classify_crossfire_high_confidence(void)
{
    /* Crossfire: 0xC8 with mask 0xFF in 862–928 MHz */
    uint8_t payload[] = {0xC8, 0x10, 0x20};
    raw_detection_t det = make_detection(payload, sizeof(payload), 868000000);

    classification_result_t result;
    esp_err_t ret = classifier_classify(&det, &result);

    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(PROTOCOL_CROSSFIRE, result.protocol);
    TEST_ASSERT_EQUAL(CONFIDENCE_HIGH, result.confidence);
    TEST_ASSERT_EQUAL_STRING("LORA", result.modulation_info);
}

/* ========================================================================
 * Test: FrSky 2.4 GHz — HIGH confidence
 * ======================================================================== */

void test_classify_frsky_high_confidence(void)
{
    /* FrSky: 0x7E with mask 0xFF in 2400–2500 MHz */
    uint8_t payload[] = {0x7E, 0x0A, 0x01};
    raw_detection_t det = make_detection(payload, sizeof(payload), 2420000000UL);

    classification_result_t result;
    esp_err_t ret = classifier_classify(&det, &result);

    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(PROTOCOL_FRSKY, result.protocol);
    TEST_ASSERT_EQUAL(CONFIDENCE_HIGH, result.confidence);
    TEST_ASSERT_EQUAL_STRING("FHSS", result.modulation_info);
}

/* ========================================================================
 * Test: No match — PROTOCOL_UNKNOWN
 * ======================================================================== */

void test_classify_no_match_unknown(void)
{
    /* A byte that doesn't match any known signature header */
    uint8_t payload[] = {0xAB, 0xCD, 0xEF};
    raw_detection_t det = make_detection(payload, sizeof(payload), 500000000);

    classification_result_t result;
    esp_err_t ret = classifier_classify(&det, &result);

    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(PROTOCOL_UNKNOWN, result.protocol);
}

/* ========================================================================
 * Test: Empty payload → PROTOCOL_UNKNOWN
 * ======================================================================== */

void test_classify_empty_payload_unknown(void)
{
    raw_detection_t det = make_detection(NULL, 0, 915000000);

    classification_result_t result;
    esp_err_t ret = classifier_classify(&det, &result);

    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(PROTOCOL_UNKNOWN, result.protocol);
}

/* ========================================================================
 * Test: NULL inputs → ESP_ERR_INVALID_ARG
 * ======================================================================== */

void test_classify_null_raw_returns_error(void)
{
    classification_result_t result;
    esp_err_t ret = classifier_classify(NULL, &result);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
}

void test_classify_null_result_returns_error(void)
{
    uint8_t payload[] = {0xFE};
    raw_detection_t det = make_detection(payload, sizeof(payload), 0);

    esp_err_t ret = classifier_classify(&det, NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
}

/* ========================================================================
 * Test: Frequency field is preserved in result
 * ======================================================================== */

void test_classify_preserves_frequency_in_result(void)
{
    uint8_t payload[] = {0xFE, 0x00}; /* MAVLink v1 */
    raw_detection_t det = make_detection(payload, sizeof(payload), 433920000);

    classification_result_t result;
    classifier_classify(&det, &result);

    TEST_ASSERT_EQUAL_UINT32(433920000, result.frequency_hz);
}

/* ========================================================================
 * Test: ELRS at 2.4 GHz — HIGH confidence
 * ======================================================================== */

void test_classify_elrs_2400_high_confidence(void)
{
    /* ELRS header: byte masked with 0xFC = 0x00 → e.g., 0x02
     * At 2450 MHz → within 2400–2500 MHz range */
    uint8_t payload[] = {0x02, 0xFF};
    raw_detection_t det = make_detection(payload, sizeof(payload), 2450000000UL);

    classification_result_t result;
    esp_err_t ret = classifier_classify(&det, &result);

    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(PROTOCOL_ELRS, result.protocol);
    TEST_ASSERT_EQUAL(CONFIDENCE_HIGH, result.confidence);
}

/* ========================================================================
 * Test: ELRS header at a completely unrelated frequency → LOW confidence
 * ======================================================================== */

void test_classify_elrs_header_wrong_freq_low_confidence(void)
{
    /* ELRS header: 0x00 & 0xFC = 0x00 — matches ELRS pattern.
     * But frequency 500 MHz is not in either 862-928 or 2400-2500 range */
    uint8_t payload[] = {0x00, 0x55};
    raw_detection_t det = make_detection(payload, sizeof(payload), 500000000);

    classification_result_t result;
    esp_err_t ret = classifier_classify(&det, &result);

    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(PROTOCOL_ELRS, result.protocol);
    TEST_ASSERT_EQUAL(CONFIDENCE_LOW, result.confidence);
}

/* ========================================================================
 * Test: get_signature_count reflects loaded table
 * ======================================================================== */

void test_get_signature_count(void)
{
    /* With defaults loaded, should have 7 entries */
    uint16_t count = classifier_get_signature_count();
    TEST_ASSERT_EQUAL_UINT16(7, count);
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_classifier_init_succeeds);
    RUN_TEST(test_classify_elrs_900_high_confidence);
    RUN_TEST(test_classify_mavlink_v1_low_confidence);
    RUN_TEST(test_classify_mavlink_v2_low_confidence);
    RUN_TEST(test_classify_dji_high_confidence);
    RUN_TEST(test_classify_dji_wrong_freq_low_confidence);
    RUN_TEST(test_classify_crossfire_high_confidence);
    RUN_TEST(test_classify_frsky_high_confidence);
    RUN_TEST(test_classify_no_match_unknown);
    RUN_TEST(test_classify_empty_payload_unknown);
    RUN_TEST(test_classify_null_raw_returns_error);
    RUN_TEST(test_classify_null_result_returns_error);
    RUN_TEST(test_classify_preserves_frequency_in_result);
    RUN_TEST(test_classify_elrs_2400_high_confidence);
    RUN_TEST(test_classify_elrs_header_wrong_freq_low_confidence);
    RUN_TEST(test_get_signature_count);

    return UNITY_END();
}
