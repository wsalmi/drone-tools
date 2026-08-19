/**
 * @file test_detection_service.c
 * @brief Unit tests for the Detection Service.
 *
 * Tests initialization, source availability detection, queue operations,
 * start/stop lifecycle, and the receive API.
 */

#include "unity.h"
#include "detection_service.h"
#include "hal_mocks.h"
#include "hal_wifi_scanner.h"
#include "hal_ble_scanner.h"

#include <string.h>

/* ========================================================================
 * Test Setup / Teardown
 * ======================================================================== */

void setUp(void)
{
    /* Reset all mocks to known state */
    mock_hal_wifi_scanner_reset();
    mock_hal_ble_scanner_reset();
    mock_hal_lora_reset();
    mock_hal_gps_reset();

    /* Initialize WiFi and BLE scanners so they're not in ERROR state */
    hal_wifi_scanner_init();
    hal_ble_scanner_init();

    /* Set GPS with a valid fix */
    mock_hal_gps_set_fix(true, -23.550520, -46.633309, 760.5f, 8, 1.2f);

    /* SX1262 is the supplementary passive monitor. */
    mock_hal_lora_set_init_result(ESP_OK);
    mock_hal_lora_set_status(HAL_STATUS_ACTIVE);
}

void tearDown(void)
{
    /* Fully deinitialize detection service to reset state between tests */
    detection_service_deinit();

}

/* ========================================================================
 * Test: Initialization
 * ======================================================================== */

void test_detection_service_init_creates_queue(void)
{
    esp_err_t err = detection_service_init();
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Queue should exist and be empty */
    TEST_ASSERT_EQUAL_UINT32(0, detection_service_get_queue_count());
}

void test_detection_service_init_detects_wifi_available(void)
{
    /* WiFi scanner was initialized in setUp (not ERROR) */
    esp_err_t err = detection_service_init();
    TEST_ASSERT_EQUAL(ESP_OK, err);

    TEST_ASSERT_TRUE(detection_service_is_source_available(DETECTION_SOURCE_WIFI_RID));
}

void test_detection_service_init_detects_ble_available(void)
{
    esp_err_t err = detection_service_init();
    TEST_ASSERT_EQUAL(ESP_OK, err);

    TEST_ASSERT_TRUE(detection_service_is_source_available(DETECTION_SOURCE_BLE_RID));
}

void test_detection_service_init_detects_lora_available(void)
{
    /* SX1262 mock is active */
    esp_err_t err = detection_service_init();
    TEST_ASSERT_EQUAL(ESP_OK, err);

    TEST_ASSERT_TRUE(detection_service_is_source_available(DETECTION_SOURCE_LORA));
}

void test_detection_service_init_wifi_unavailable_when_error(void)
{
    mock_hal_wifi_scanner_set_status(HAL_STATUS_ERROR);

    esp_err_t err = detection_service_init();
    TEST_ASSERT_EQUAL(ESP_OK, err);

    TEST_ASSERT_FALSE(detection_service_is_source_available(DETECTION_SOURCE_WIFI_RID));
}

void test_detection_service_init_double_init_fails(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, detection_service_init());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, detection_service_init());
}

/* ========================================================================
 * Test: Start / Stop
 * ======================================================================== */

void test_detection_service_start_without_init_fails(void)
{
    /* Don't call init */
    esp_err_t err = detection_service_start();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, err);
}

void test_detection_service_start_creates_tasks(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, detection_service_init());
    esp_err_t err = detection_service_start();
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

void test_detection_service_start_double_start_fails(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, detection_service_init());
    TEST_ASSERT_EQUAL(ESP_OK, detection_service_start());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, detection_service_start());
}

void test_detection_service_stop_resets_state(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, detection_service_init());
    TEST_ASSERT_EQUAL(ESP_OK, detection_service_start());
    TEST_ASSERT_EQUAL(ESP_OK, detection_service_stop());

    /* Can start again after stop */
    TEST_ASSERT_EQUAL(ESP_OK, detection_service_start());
}

/* ========================================================================
 * Test: Receive API
 * ======================================================================== */

void test_detection_service_receive_null_arg_fails(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, detection_service_init());

    esp_err_t err = detection_service_receive(NULL, 0);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

void test_detection_service_receive_empty_queue_returns_timeout(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, detection_service_init());

    raw_detection_t det;
    esp_err_t err = detection_service_receive(&det, 0);
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, err);
}

void test_detection_service_receive_not_initialized_fails(void)
{
    raw_detection_t det;
    esp_err_t err = detection_service_receive(&det, 0);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, err);
}

/* ========================================================================
 * Test: Queue operations and source availability edge cases
 * ======================================================================== */

void test_detection_service_queue_count_zero_when_not_initialized(void)
{
    /* Service not initialized */
    TEST_ASSERT_EQUAL_UINT32(0, detection_service_get_queue_count());
}

void test_detection_service_source_available_invalid_source(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, detection_service_init());

    /* Invalid source index beyond the enum */
    TEST_ASSERT_FALSE(detection_service_is_source_available((detection_source_t)99));
}

void test_detection_service_sx1262_unavailable_when_inactive(void)
{
    mock_hal_lora_set_status(HAL_STATUS_INACTIVE);

    esp_err_t err = detection_service_init();
    TEST_ASSERT_EQUAL(ESP_OK, err);

    TEST_ASSERT_FALSE(detection_service_is_source_available(DETECTION_SOURCE_LORA));
}

void test_detection_service_does_not_expose_legacy_sources(void)
{
    esp_err_t err = detection_service_init();
    TEST_ASSERT_EQUAL(ESP_OK, err);

    TEST_ASSERT_FALSE(detection_service_is_source_available((detection_source_t)DETECTION_SOURCE_COUNT));
}

/* ========================================================================
 * Unity Test Runner
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* Initialization tests */
    RUN_TEST(test_detection_service_init_creates_queue);
    RUN_TEST(test_detection_service_init_detects_wifi_available);
    RUN_TEST(test_detection_service_init_detects_ble_available);
    RUN_TEST(test_detection_service_init_detects_lora_available);
    RUN_TEST(test_detection_service_init_wifi_unavailable_when_error);
    RUN_TEST(test_detection_service_init_double_init_fails);

    /* Start / Stop tests */
    RUN_TEST(test_detection_service_start_without_init_fails);
    RUN_TEST(test_detection_service_start_creates_tasks);
    RUN_TEST(test_detection_service_start_double_start_fails);
    RUN_TEST(test_detection_service_stop_resets_state);

    /* Receive API tests */
    RUN_TEST(test_detection_service_receive_null_arg_fails);
    RUN_TEST(test_detection_service_receive_empty_queue_returns_timeout);
    RUN_TEST(test_detection_service_receive_not_initialized_fails);

    /* Queue and source tests */
    RUN_TEST(test_detection_service_queue_count_zero_when_not_initialized);
    RUN_TEST(test_detection_service_source_available_invalid_source);
    RUN_TEST(test_detection_service_sx1262_unavailable_when_inactive);
    RUN_TEST(test_detection_service_does_not_expose_legacy_sources);

    return UNITY_END();
}
