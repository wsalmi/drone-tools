/**
 * @file test_data_pipeline.c
 * @brief Unit tests for the Data Pipeline inter-task communication module.
 *
 * Tests the pipeline initialization, queue operations (enqueue/dequeue),
 * event group notifications, GPS shared state, and silent drop policy.
 *
 * Validates: Requirements 1.3, 8.4, 9.5, 11.1
 */

#include "unity.h"
#include "data_pipeline.h"
#include <string.h>

/* ========================================================================
 * Test Setup / Teardown
 * ======================================================================== */

void setUp(void)
{
    /* Ensure pipeline is deinitialized before each test */
    data_pipeline_deinit();
}

void tearDown(void)
{
    data_pipeline_deinit();
}

/* ========================================================================
 * Initialization Tests
 * ======================================================================== */

void test_init_succeeds(void)
{
    esp_err_t err = data_pipeline_init();
    TEST_ASSERT_EQUAL(ESP_OK, err);

    const data_pipeline_t *state = data_pipeline_get_state();
    TEST_ASSERT_NOT_NULL(state);
    TEST_ASSERT_TRUE(state->initialized);
    TEST_ASSERT_NOT_NULL(state->logger_queue);
    TEST_ASSERT_NOT_NULL(state->ui_event_group);
    TEST_ASSERT_NOT_NULL(state->gps_mutex);
}

void test_init_already_initialized_returns_error(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, data_pipeline_init());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, data_pipeline_init());
}

void test_deinit_when_not_initialized_returns_error(void)
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, data_pipeline_deinit());
}

void test_deinit_clears_state(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, data_pipeline_init());
    TEST_ASSERT_EQUAL(ESP_OK, data_pipeline_deinit());

    const data_pipeline_t *state = data_pipeline_get_state();
    TEST_ASSERT_NULL(state);
}

/* ========================================================================
 * Logger Queue Tests
 * ======================================================================== */

void test_enqueue_log_before_init_returns_error(void)
{
    log_record_t record = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, data_pipeline_enqueue_log(&record));
}

void test_enqueue_log_null_record_returns_error(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, data_pipeline_init());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, data_pipeline_enqueue_log(NULL));
}

void test_enqueue_and_dequeue_log_record(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, data_pipeline_init());

    log_record_t record_in = {
        .timestamp_utc_ms = 1710500000000ULL,
        .monitor_lat = -23.550520,
        .monitor_lon = -46.633309,
        .monitor_alt = 760.5f,
        .protocol = PROTOCOL_ELRS,
        .rssi_dbm = -72,
        .event_type = LOG_EVENT_DETECTION
    };
    strncpy(record_in.aircraft_id, "ELRS-A1B2C3", AIRCRAFT_ID_MAX_LEN - 1);

    TEST_ASSERT_EQUAL(ESP_OK, data_pipeline_enqueue_log(&record_in));
    TEST_ASSERT_EQUAL(1, data_pipeline_get_logger_queue_count());

    log_record_t record_out = {0};
    TEST_ASSERT_EQUAL(ESP_OK, data_pipeline_dequeue_log(&record_out, 0));

    TEST_ASSERT_EQUAL(record_in.timestamp_utc_ms, record_out.timestamp_utc_ms);
    TEST_ASSERT_EQUAL_DOUBLE(record_in.monitor_lat, record_out.monitor_lat);
    TEST_ASSERT_EQUAL_DOUBLE(record_in.monitor_lon, record_out.monitor_lon);
    TEST_ASSERT_EQUAL_STRING(record_in.aircraft_id, record_out.aircraft_id);
    TEST_ASSERT_EQUAL(record_in.protocol, record_out.protocol);
    TEST_ASSERT_EQUAL(record_in.rssi_dbm, record_out.rssi_dbm);
    TEST_ASSERT_EQUAL(record_in.event_type, record_out.event_type);
}

void test_silent_drop_when_logger_queue_full(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, data_pipeline_init());

    log_record_t record = {
        .timestamp_utc_ms = 1710500000000ULL,
        .protocol = PROTOCOL_MAVLINK,
        .rssi_dbm = -60,
        .event_type = LOG_EVENT_TELEMETRY
    };

    /* Fill the queue to capacity */
    for (int i = 0; i < PIPELINE_LOGGER_QUEUE_SIZE; i++) {
        record.timestamp_utc_ms = 1710500000000ULL + i;
        TEST_ASSERT_EQUAL(ESP_OK, data_pipeline_enqueue_log(&record));
    }

    TEST_ASSERT_EQUAL(PIPELINE_LOGGER_QUEUE_SIZE, data_pipeline_get_logger_queue_count());

    /* Next enqueue should fail silently (drop) */
    record.timestamp_utc_ms = 9999999ULL;
    TEST_ASSERT_EQUAL(ESP_FAIL, data_pipeline_enqueue_log(&record));

    /* Drop counter should increment */
    TEST_ASSERT_EQUAL(1, data_pipeline_get_logger_drop_count());

    /* Queue count unchanged */
    TEST_ASSERT_EQUAL(PIPELINE_LOGGER_QUEUE_SIZE, data_pipeline_get_logger_queue_count());
}

void test_dequeue_empty_queue_returns_timeout(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, data_pipeline_init());

    log_record_t record = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, data_pipeline_dequeue_log(&record, 0));
}

void test_dequeue_null_record_returns_error(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, data_pipeline_init());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, data_pipeline_dequeue_log(NULL, 0));
}

/* ========================================================================
 * UI Event Tests
 * ======================================================================== */

void test_notify_ui_before_init_returns_error(void)
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      data_pipeline_notify_ui(PIPELINE_EVT_NEW_AIRCRAFT));
}

void test_notify_ui_zero_bits_returns_error(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, data_pipeline_init());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, data_pipeline_notify_ui(0));
}

void test_notify_and_wait_ui_events(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, data_pipeline_init());

    /* Signal new aircraft event */
    TEST_ASSERT_EQUAL(ESP_OK, data_pipeline_notify_ui(PIPELINE_EVT_NEW_AIRCRAFT));

    /* Wait for it */
    uint32_t set_bits = 0;
    TEST_ASSERT_EQUAL(ESP_OK,
                      data_pipeline_wait_ui_events(PIPELINE_EVT_ALL, 0, &set_bits));
    TEST_ASSERT_BITS_HIGH(PIPELINE_EVT_NEW_AIRCRAFT, set_bits);
}

void test_notify_multiple_events(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, data_pipeline_init());

    /* Signal multiple events */
    TEST_ASSERT_EQUAL(ESP_OK,
                      data_pipeline_notify_ui(PIPELINE_EVT_NEW_AIRCRAFT |
                                             PIPELINE_EVT_TELEMETRY_UPDATED));

    uint32_t set_bits = 0;
    TEST_ASSERT_EQUAL(ESP_OK,
                      data_pipeline_wait_ui_events(PIPELINE_EVT_ALL, 0, &set_bits));
    TEST_ASSERT_BITS_HIGH(PIPELINE_EVT_NEW_AIRCRAFT, set_bits);
    TEST_ASSERT_BITS_HIGH(PIPELINE_EVT_TELEMETRY_UPDATED, set_bits);
}

void test_wait_ui_events_clears_on_read(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, data_pipeline_init());

    TEST_ASSERT_EQUAL(ESP_OK, data_pipeline_notify_ui(PIPELINE_EVT_AIRCRAFT_LOST));

    /* First wait should get the bits */
    uint32_t set_bits = 0;
    TEST_ASSERT_EQUAL(ESP_OK,
                      data_pipeline_wait_ui_events(PIPELINE_EVT_ALL, 0, &set_bits));
    TEST_ASSERT_BITS_HIGH(PIPELINE_EVT_AIRCRAFT_LOST, set_bits);

    /* Second wait should timeout (bits were cleared) */
    set_bits = 0;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT,
                      data_pipeline_wait_ui_events(PIPELINE_EVT_AIRCRAFT_LOST, 0, &set_bits));
}

void test_wait_ui_events_before_init_returns_error(void)
{
    uint32_t set_bits = 0;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      data_pipeline_wait_ui_events(PIPELINE_EVT_ALL, 0, &set_bits));
}

/* ========================================================================
 * GPS Shared State Tests
 * ======================================================================== */

void test_update_gps_before_init_returns_error(void)
{
    gps_position_t pos = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, data_pipeline_update_gps(&pos));
}

void test_update_gps_null_returns_error(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, data_pipeline_init());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, data_pipeline_update_gps(NULL));
}

void test_get_gps_null_returns_error(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, data_pipeline_init());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, data_pipeline_get_gps(NULL));
}

void test_update_and_get_gps_position(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, data_pipeline_init());

    gps_position_t pos_in = {
        .latitude = -23.550520,
        .longitude = -46.633309,
        .altitude_m = 760.5f,
        .hdop = 1.2f,
        .satellites_used = 8,
        .timestamp_utc_ms = 123456789U,
        .fix_valid = true
    };

    TEST_ASSERT_EQUAL(ESP_OK, data_pipeline_update_gps(&pos_in));

    gps_position_t pos_out = {0};
    TEST_ASSERT_EQUAL(ESP_OK, data_pipeline_get_gps(&pos_out));

    TEST_ASSERT_EQUAL_DOUBLE(pos_in.latitude, pos_out.latitude);
    TEST_ASSERT_EQUAL_DOUBLE(pos_in.longitude, pos_out.longitude);
    TEST_ASSERT_EQUAL_FLOAT(pos_in.altitude_m, pos_out.altitude_m);
    TEST_ASSERT_EQUAL_FLOAT(pos_in.hdop, pos_out.hdop);
    TEST_ASSERT_EQUAL(pos_in.satellites_used, pos_out.satellites_used);
    TEST_ASSERT_EQUAL_UINT32(pos_in.timestamp_utc_ms, pos_out.timestamp_utc_ms);
    TEST_ASSERT_TRUE(pos_out.fix_valid);
}

void test_gps_initial_state_has_no_fix(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, data_pipeline_init());

    gps_position_t pos = {0};
    pos.fix_valid = true; /* set to true to verify it gets overwritten */
    TEST_ASSERT_EQUAL(ESP_OK, data_pipeline_get_gps(&pos));

    TEST_ASSERT_FALSE(pos.fix_valid);
}

void test_gps_update_triggers_ui_event(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, data_pipeline_init());

    gps_position_t pos = {
        .latitude = -23.0,
        .longitude = -46.0,
        .fix_valid = true
    };

    TEST_ASSERT_EQUAL(ESP_OK, data_pipeline_update_gps(&pos));

    /* Check that GPS_UPDATED event was signaled */
    uint32_t set_bits = 0;
    esp_err_t err = data_pipeline_wait_ui_events(PIPELINE_EVT_GPS_UPDATED, 0, &set_bits);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_NOT_EQUAL(0, set_bits & PIPELINE_EVT_GPS_UPDATED);
}

/* ========================================================================
 * Diagnostics Tests
 * ======================================================================== */

void test_get_state_before_init_returns_null(void)
{
    TEST_ASSERT_NULL(data_pipeline_get_state());
}

void test_logger_queue_count_before_init_returns_zero(void)
{
    TEST_ASSERT_EQUAL(0, data_pipeline_get_logger_queue_count());
}

/* ========================================================================
 * Unity Main
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* Initialization */
    RUN_TEST(test_init_succeeds);
    RUN_TEST(test_init_already_initialized_returns_error);
    RUN_TEST(test_deinit_when_not_initialized_returns_error);
    RUN_TEST(test_deinit_clears_state);

    /* Logger Queue */
    RUN_TEST(test_enqueue_log_before_init_returns_error);
    RUN_TEST(test_enqueue_log_null_record_returns_error);
    RUN_TEST(test_enqueue_and_dequeue_log_record);
    RUN_TEST(test_silent_drop_when_logger_queue_full);
    RUN_TEST(test_dequeue_empty_queue_returns_timeout);
    RUN_TEST(test_dequeue_null_record_returns_error);

    /* UI Events */
    RUN_TEST(test_notify_ui_before_init_returns_error);
    RUN_TEST(test_notify_ui_zero_bits_returns_error);
    RUN_TEST(test_notify_and_wait_ui_events);
    RUN_TEST(test_notify_multiple_events);
    RUN_TEST(test_wait_ui_events_clears_on_read);
    RUN_TEST(test_wait_ui_events_before_init_returns_error);

    /* GPS Shared State */
    RUN_TEST(test_update_gps_before_init_returns_error);
    RUN_TEST(test_update_gps_null_returns_error);
    RUN_TEST(test_get_gps_null_returns_error);
    RUN_TEST(test_update_and_get_gps_position);
    RUN_TEST(test_gps_initial_state_has_no_fix);
    RUN_TEST(test_gps_update_triggers_ui_event);

    /* Diagnostics */
    RUN_TEST(test_get_state_before_init_returns_null);
    RUN_TEST(test_logger_queue_count_before_init_returns_zero);

    return UNITY_END();
}
