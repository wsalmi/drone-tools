/**
 * @file data_pipeline.h
 * @brief Data Pipeline — inter-task queues, events, and shared state.
 *
 * Defines the interconnects between FreeRTOS tasks in the decoder pipeline:
 *
 *   RF Tasks ──(detection_service queue, 64 items)──→ Decoder Pipeline Task
 *   Decoder Pipeline ──(logger_queue)──→ Logger Task
 *   Decoder Pipeline ──(event group bits)──→ UI Task
 *   GPS HAL ──(mutex-protected shared state)──→ Decoder Pipeline + Geolocation
 *
 * Queue Drop Policy:
 *   All queues use a SILENT DROP policy when full. Enqueue operations use
 *   xQueueSend() with timeout = 0 (non-blocking). If the queue is full,
 *   the item is discarded without logging or blocking the producer. This
 *   prevents backpressure from slow consumers from stalling time-critical
 *   RF reception tasks.
 *
 * The RF→Decoder queue (64 items of raw_detection_t) is defined in
 * detection_service.h and created by detection_service_init(). This module
 * creates the remaining pipeline interconnects.
 *
 * Validates: Requirements 1.3, 8.4, 9.5, 11.1
 */

#ifndef DATA_PIPELINE_H
#define DATA_PIPELINE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "hal_gps.h"
#include "data_logger.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Configuration Constants
 * ======================================================================== */

/**
 * @brief Decoder→Logger queue capacity.
 *
 * Sized to absorb bursts from the decoder without blocking. Each item
 * is a log_record_t. The Logger task drains this queue asynchronously
 * and writes to SD card or RAM buffer.
 */
#define PIPELINE_LOGGER_QUEUE_SIZE      32

/* ========================================================================
 * Decoder→UI Event Group Bits
 *
 * The Decoder Pipeline task sets event bits when it has new data for
 * the UI task. The UI task waits on the event group and refreshes the
 * appropriate screen elements. Bits are auto-cleared on read by the UI.
 * ======================================================================== */

/** @brief A new aircraft was added to the registry */
#define PIPELINE_EVT_NEW_AIRCRAFT       (1 << 0)

/** @brief Telemetry for an existing aircraft was updated */
#define PIPELINE_EVT_TELEMETRY_UPDATED  (1 << 1)

/** @brief An aircraft transitioned to OUT_OF_RANGE status */
#define PIPELINE_EVT_AIRCRAFT_LOST      (1 << 2)

/** @brief An aircraft returned from OUT_OF_RANGE to ACTIVE */
#define PIPELINE_EVT_AIRCRAFT_RETURNED  (1 << 3)

/** @brief Protocol classification completed for an aircraft */
#define PIPELINE_EVT_PROTOCOL_CLASSIFIED (1 << 4)

/** @brief Pilot position was updated or determined */
#define PIPELINE_EVT_PILOT_UPDATED      (1 << 5)

/** @brief GPS shared state was updated with a new position */
#define PIPELINE_EVT_GPS_UPDATED        (1 << 6)

/** @brief Mask of all pipeline event bits */
#define PIPELINE_EVT_ALL                (PIPELINE_EVT_NEW_AIRCRAFT       | \
                                         PIPELINE_EVT_TELEMETRY_UPDATED  | \
                                         PIPELINE_EVT_AIRCRAFT_LOST      | \
                                         PIPELINE_EVT_AIRCRAFT_RETURNED  | \
                                         PIPELINE_EVT_PROTOCOL_CLASSIFIED | \
                                         PIPELINE_EVT_PILOT_UPDATED      | \
                                         PIPELINE_EVT_GPS_UPDATED)

/* ========================================================================
 * Data Types
 * ======================================================================== */

/**
 * @brief Pipeline state handle.
 *
 * Holds all FreeRTOS primitives for the data pipeline interconnects.
 * Created by data_pipeline_init() and destroyed by data_pipeline_deinit().
 */
typedef struct {
    /* Decoder→Logger queue: asynchronous log record delivery */
    QueueHandle_t logger_queue;

    /* Decoder→UI event group: signals UI refresh triggers */
    EventGroupHandle_t ui_event_group;

    /* GPS shared state: mutex-protected position accessible by Decoder */
    gps_position_t gps_shared_state;
    SemaphoreHandle_t gps_mutex;

    /* Pipeline status tracking */
    uint32_t logger_queue_drops;    /**< Count of records dropped (queue full) */
    uint32_t ui_events_sent;        /**< Total events signaled to UI */

    bool initialized;
} data_pipeline_t;

/* ========================================================================
 * Public API
 * ======================================================================== */

/**
 * @brief Initialize the data pipeline interconnects.
 *
 * Creates:
 *   - Decoder→Logger queue (PIPELINE_LOGGER_QUEUE_SIZE items of log_record_t)
 *   - Decoder→UI event group
 *   - GPS shared state with mutex
 *
 * The RF→Decoder queue (raw_detection_t, 64 items) is managed by
 * detection_service and is NOT created here.
 *
 * @return ESP_OK on success,
 *         ESP_ERR_NO_MEM if resource creation fails,
 *         ESP_ERR_INVALID_STATE if already initialized.
 */
esp_err_t data_pipeline_init(void);

/**
 * @brief Deinitialize the data pipeline and free all resources.
 *
 * Deletes queues, event groups, and mutexes. Must stop all consumer/producer
 * tasks before calling this function.
 *
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_STATE if not initialized.
 */
esp_err_t data_pipeline_deinit(void);

/* ------------------------------------------------------------------------
 * Decoder→Logger Queue Operations
 * ---------------------------------------------------------------------- */

/**
 * @brief Enqueue a log record for asynchronous writing by the Logger task.
 *
 * Uses SILENT DROP policy: if the queue is full, the record is discarded
 * without blocking the caller. The drop counter is incremented for
 * diagnostics.
 *
 * @param[in] record  Pointer to log record to enqueue (copied into queue).
 * @return ESP_OK if enqueued successfully,
 *         ESP_ERR_INVALID_ARG if record is NULL,
 *         ESP_FAIL if queue is full (record silently dropped),
 *         ESP_ERR_INVALID_STATE if pipeline not initialized.
 */
esp_err_t data_pipeline_enqueue_log(const log_record_t *record);

/**
 * @brief Dequeue a log record (called by the Logger task).
 *
 * Blocks up to timeout_ms waiting for a record from the Decoder.
 *
 * @param[out] record     Pointer to record struct to populate.
 * @param[in]  timeout_ms Maximum wait time (portMAX_DELAY for infinite).
 * @return ESP_OK if a record was received,
 *         ESP_ERR_TIMEOUT if no record within timeout,
 *         ESP_ERR_INVALID_ARG if record is NULL,
 *         ESP_ERR_INVALID_STATE if pipeline not initialized.
 */
esp_err_t data_pipeline_dequeue_log(log_record_t *record, uint32_t timeout_ms);

/**
 * @brief Get the current number of items in the logger queue.
 *
 * @return Number of pending log records, or 0 if not initialized.
 */
uint32_t data_pipeline_get_logger_queue_count(void);

/**
 * @brief Get the total number of log records dropped due to full queue.
 *
 * @return Cumulative drop count since initialization.
 */
uint32_t data_pipeline_get_logger_drop_count(void);

/* ------------------------------------------------------------------------
 * Decoder→UI Event Operations
 * ---------------------------------------------------------------------- */

/**
 * @brief Signal UI event bits from the Decoder Pipeline.
 *
 * Sets the specified bits in the event group. The UI task reads and
 * clears these bits to know which elements need refreshing.
 * Non-blocking — always returns immediately.
 *
 * @param[in] event_bits  OR-combination of PIPELINE_EVT_* bits to set.
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_ARG if event_bits is 0,
 *         ESP_ERR_INVALID_STATE if pipeline not initialized.
 */
esp_err_t data_pipeline_notify_ui(uint32_t event_bits);

/**
 * @brief Wait for UI events (called by the UI task).
 *
 * Blocks until at least one of the specified bits is set or timeout
 * expires. Bits are auto-cleared on return.
 *
 * @param[in]  wait_bits   OR-combination of PIPELINE_EVT_* bits to wait for.
 * @param[in]  timeout_ms  Maximum wait time (portMAX_DELAY for infinite).
 * @param[out] set_bits    Bits that were actually set (can be NULL).
 * @return ESP_OK if at least one bit was set,
 *         ESP_ERR_TIMEOUT if no bits set within timeout,
 *         ESP_ERR_INVALID_STATE if pipeline not initialized.
 */
esp_err_t data_pipeline_wait_ui_events(uint32_t wait_bits, uint32_t timeout_ms,
                                       uint32_t *set_bits);

/* ------------------------------------------------------------------------
 * GPS Shared State (Mutex-Protected)
 * ---------------------------------------------------------------------- */

/**
 * @brief Update the GPS shared state with a new position.
 *
 * Called by the GPS Reader task whenever a new position is available.
 * Thread-safe: acquires the GPS mutex before writing.
 *
 * @param[in] position  Pointer to the new GPS position.
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_ARG if position is NULL,
 *         ESP_ERR_INVALID_STATE if pipeline not initialized,
 *         ESP_ERR_TIMEOUT if mutex cannot be acquired within 100ms.
 */
esp_err_t data_pipeline_update_gps(const gps_position_t *position);

/**
 * @brief Read the current GPS shared state.
 *
 * Called by the Decoder Pipeline task and Geolocation Service to get
 * the latest monitor position. Thread-safe: acquires the GPS mutex.
 *
 * @param[out] position  Pointer to struct where position will be copied.
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_ARG if position is NULL,
 *         ESP_ERR_INVALID_STATE if pipeline not initialized,
 *         ESP_ERR_TIMEOUT if mutex cannot be acquired within 100ms.
 */
esp_err_t data_pipeline_get_gps(gps_position_t *position);

/* ------------------------------------------------------------------------
 * Pipeline Diagnostics
 * ---------------------------------------------------------------------- */

/**
 * @brief Get a read-only pointer to the pipeline state for diagnostics.
 *
 * @return Pointer to pipeline state, or NULL if not initialized.
 */
const data_pipeline_t *data_pipeline_get_state(void);

#ifdef __cplusplus
}
#endif

#endif /* DATA_PIPELINE_H */
