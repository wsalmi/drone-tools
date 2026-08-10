/**
 * @file data_pipeline.c
 * @brief Data Pipeline — inter-task queues, events, and shared GPS state.
 *
 * Creates and manages the FreeRTOS interconnects between pipeline tasks:
 *   - Decoder→Logger queue (32 items of log_record_t, silent drop on full)
 *   - Decoder→UI event group (bit-based notifications)
 *   - GPS shared state (mutex-protected gps_position_t)
 *
 * Validates: Requirements 1.3, 8.4, 9.5, 11.1
 */

#include "data_pipeline.h"

#include "esp_log.h"
#include <string.h>

static const char *TAG = "data_pipeline";

/* ========================================================================
 * Internal State
 * ======================================================================== */

static data_pipeline_t s_pipeline = {0};

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

esp_err_t data_pipeline_init(void)
{
    if (s_pipeline.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_pipeline, 0, sizeof(s_pipeline));

    /* Create Decoder→Logger queue */
    s_pipeline.logger_queue = xQueueCreate(PIPELINE_LOGGER_QUEUE_SIZE, sizeof(log_record_t));
    if (s_pipeline.logger_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create logger queue");
        return ESP_ERR_NO_MEM;
    }

    /* Create Decoder→UI event group */
    s_pipeline.ui_event_group = xEventGroupCreate();
    if (s_pipeline.ui_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create UI event group");
        vQueueDelete(s_pipeline.logger_queue);
        s_pipeline.logger_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    /* Create GPS shared state mutex */
    s_pipeline.gps_mutex = xSemaphoreCreateMutex();
    if (s_pipeline.gps_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create GPS mutex");
        vQueueDelete(s_pipeline.logger_queue);
        vEventGroupDelete(s_pipeline.ui_event_group);
        s_pipeline.logger_queue = NULL;
        s_pipeline.ui_event_group = NULL;
        return ESP_ERR_NO_MEM;
    }

    /* Initialize GPS shared state to invalid */
    memset(&s_pipeline.gps_shared_state, 0, sizeof(gps_position_t));
    s_pipeline.gps_shared_state.fix_valid = false;

    s_pipeline.initialized = true;
    ESP_LOGI(TAG, "Data pipeline initialized (logger_q=%d, events OK, GPS mutex OK)",
             PIPELINE_LOGGER_QUEUE_SIZE);
    return ESP_OK;
}

esp_err_t data_pipeline_deinit(void)
{
    if (!s_pipeline.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_pipeline.logger_queue) {
        vQueueDelete(s_pipeline.logger_queue);
        s_pipeline.logger_queue = NULL;
    }
    if (s_pipeline.ui_event_group) {
        vEventGroupDelete(s_pipeline.ui_event_group);
        s_pipeline.ui_event_group = NULL;
    }
    if (s_pipeline.gps_mutex) {
        vSemaphoreDelete(s_pipeline.gps_mutex);
        s_pipeline.gps_mutex = NULL;
    }

    s_pipeline.initialized = false;
    ESP_LOGI(TAG, "Data pipeline deinitialized");
    return ESP_OK;
}

/* --- Decoder→Logger Queue --- */

esp_err_t data_pipeline_enqueue_log(const log_record_t *record)
{
    if (record == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_pipeline.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Silent drop policy: non-blocking send */
    if (xQueueSend(s_pipeline.logger_queue, record, 0) != pdTRUE) {
        s_pipeline.logger_queue_drops++;
        return ESP_FAIL;  /* Queue full — silently dropped */
    }

    return ESP_OK;
}

esp_err_t data_pipeline_dequeue_log(log_record_t *record, uint32_t timeout_ms)
{
    if (record == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_pipeline.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    TickType_t ticks = (timeout_ms == portMAX_DELAY) ? portMAX_DELAY
                                                     : pdMS_TO_TICKS(timeout_ms);
    if (xQueueReceive(s_pipeline.logger_queue, record, ticks) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

uint32_t data_pipeline_get_logger_queue_count(void)
{
    if (!s_pipeline.initialized || !s_pipeline.logger_queue) {
        return 0;
    }
    return (uint32_t)uxQueueMessagesWaiting(s_pipeline.logger_queue);
}

uint32_t data_pipeline_get_logger_drop_count(void)
{
    return s_pipeline.logger_queue_drops;
}

/* --- Decoder→UI Events --- */

esp_err_t data_pipeline_notify_ui(uint32_t event_bits)
{
    if (event_bits == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_pipeline.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xEventGroupSetBits(s_pipeline.ui_event_group, event_bits);
    s_pipeline.ui_events_sent++;
    return ESP_OK;
}

esp_err_t data_pipeline_wait_ui_events(uint32_t wait_bits, uint32_t timeout_ms,
                                       uint32_t *set_bits)
{
    if (!s_pipeline.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    TickType_t ticks = (timeout_ms == portMAX_DELAY) ? portMAX_DELAY
                                                     : pdMS_TO_TICKS(timeout_ms);
    EventBits_t bits = xEventGroupWaitBits(
        s_pipeline.ui_event_group,
        wait_bits,
        pdTRUE,     /* Clear bits on exit */
        pdFALSE,    /* Wait for any bit */
        ticks);

    if (set_bits) {
        *set_bits = (uint32_t)bits;
    }

    if ((bits & wait_bits) == 0) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

/* --- GPS Shared State --- */

esp_err_t data_pipeline_update_gps(const gps_position_t *position)
{
    if (position == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_pipeline.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_pipeline.gps_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    memcpy(&s_pipeline.gps_shared_state, position, sizeof(gps_position_t));
    xSemaphoreGive(s_pipeline.gps_mutex);

    /* Signal UI that GPS state was updated */
    data_pipeline_notify_ui(PIPELINE_EVT_GPS_UPDATED);

    return ESP_OK;
}

esp_err_t data_pipeline_get_gps(gps_position_t *position)
{
    if (position == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_pipeline.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_pipeline.gps_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    memcpy(position, &s_pipeline.gps_shared_state, sizeof(gps_position_t));
    xSemaphoreGive(s_pipeline.gps_mutex);
    return ESP_OK;
}

/* --- Diagnostics --- */

const data_pipeline_t *data_pipeline_get_state(void)
{
    if (!s_pipeline.initialized) {
        return NULL;
    }
    return &s_pipeline;
}
