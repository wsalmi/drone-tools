/**
 * @file detection_service.h
 * @brief Detection Service — orchestrates all RF detection modules.
 *
 * Manages WiFi Remote ID, BLE Remote ID, and the SX1262 passive monitor,
 * producing
 * unified raw_detection_t events into a FreeRTOS queue for downstream
 * processing (Telemetry Decoder, Protocol Classifier, Logger).
 *
 * Architecture:
 *   - detection_service_init() probes each module via HAL status
 *   - detection_service_start() creates FreeRTOS tasks per active module
 *   - Each task reads from its HAL source and enqueues raw_detection_t
 *   - Queue capacity: 64 items, drop silently when full (no blocking)
 *   - Consumers retrieve detections via detection_service_receive()
 *
 * The monitor's GPS position is attached to each detection before enqueuing,
 * providing spatial context for downstream geolocation calculations.
 *
 * Validates: Requirements 1.1, 1.2, 1.5, 2.1, 3.1, 4.3
 */

#ifndef DETECTION_SERVICE_H
#define DETECTION_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "hal_gps.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Configuration Constants
 * ======================================================================== */

/** @brief Maximum number of items in the detection queue */
#define DETECTION_QUEUE_SIZE        12

/** @brief Maximum raw payload size in bytes */
#define DETECTION_MAX_PAYLOAD_LEN   256

/** @brief Stack size for detection source tasks (bytes) */
#define DETECTION_TASK_STACK_SIZE   3584

/** @brief Default priority for WiFi/BLE scanner task */
#define DETECTION_TASK_PRIO_WIFI    5

/** @brief Default priority for the SX1262 passive monitor task */
#define DETECTION_TASK_PRIO_RF      6

/** @brief Number of detection source types */
#define DETECTION_SOURCE_COUNT      3

/* ========================================================================
 * Data Types
 * ======================================================================== */

/**
 * @brief Source module that produced a detection.
 */
typedef enum {
    DETECTION_SOURCE_WIFI_RID = 0,  /**< WiFi NAN/Beacon RemoteID */
    DETECTION_SOURCE_BLE_RID,       /**< BLE Legacy Advertisement RemoteID */
    DETECTION_SOURCE_LORA           /**< SX1262 passive monitor (862–928 MHz) */
} detection_source_t;

/**
 * @brief Unified raw detection event.
 *
 * All detection sources produce this common structure before
 * downstream protocol classification and telemetry decoding.
 */
typedef struct {
    detection_source_t source;              /**< Which module produced this detection */
    uint8_t raw_payload[DETECTION_MAX_PAYLOAD_LEN]; /**< Raw packet data */
    uint16_t payload_len;                   /**< Actual payload length (0..256) */
    int16_t rssi_dbm;                       /**< Signal strength in dBm */
    int8_t snr_db;                          /**< Signal-to-noise ratio in dB (LoRa only, 0 otherwise) */
    uint32_t frequency_hz;                  /**< Reception frequency in Hz */
    uint64_t timestamp_utc_ms;              /**< UTC timestamp in milliseconds */
    gps_position_t monitor_position;        /**< Monitor GPS position at time of detection */
} raw_detection_t;

/* ========================================================================
 * Public API
 * ======================================================================== */

/**
 * @brief Initialize the Detection Service.
 *
 * Creates the internal FreeRTOS queue (64 items) and probes each detection
 * module (WiFi, BLE, SX1262) via their HAL status functions to
 * determine which modules are available.
 *
 * Must be called after HAL modules and the GPS service are initialized.
 *
 * @return ESP_OK on success.
 * @return ESP_ERR_NO_MEM if queue creation fails.
 * @return ESP_ERR_INVALID_STATE if already initialized.
 */
esp_err_t detection_service_init(void);

/**
 * @brief Start detection tasks for all available modules.
 *
 * Creates a FreeRTOS task for each module that was found available during
 * init. Each task runs on the appropriate CPU core:
 *   - WiFi/BLE scanner task → Core 0 (PRO_CPU), priority 5
 *   - SX1262 passive monitor task → Core 0 (PRO_CPU), priority 6
 *
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_STATE if not initialized or already started.
 * @return ESP_ERR_NO_MEM if task creation fails.
 */
esp_err_t detection_service_start(void);

/**
 * @brief Stop all detection tasks and release resources.
 *
 * Signals all running detection tasks to terminate, waits for them to
 * finish, and clears the detection queue.
 *
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_STATE if not initialized or not started.
 */
esp_err_t detection_service_stop(void);

/**
 * @brief Deinitialize the Detection Service and free all resources.
 *
 * Stops tasks if running, deletes the queue, and resets internal state.
 * Must call detection_service_init() again to reuse the service.
 *
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_STATE if not initialized.
 */
esp_err_t detection_service_deinit(void);

/**
 * @brief Receive a detection from the queue.
 *
 * Blocks up to timeout_ms waiting for a raw_detection_t from any source.
 * Used by the Telemetry Decoder task to consume detections from the pipeline.
 *
 * @param[out] out        Pointer to detection structure to fill.
 * @param[in]  timeout_ms Maximum time to wait (0 = non-blocking poll).
 * @return ESP_OK if a detection was received.
 * @return ESP_ERR_TIMEOUT if no detection available within timeout.
 * @return ESP_ERR_INVALID_ARG if out is NULL.
 * @return ESP_ERR_INVALID_STATE if service is not initialized.
 */
esp_err_t detection_service_receive(raw_detection_t *out, uint32_t timeout_ms);

/**
 * @brief Get the number of items currently in the detection queue.
 *
 * @return Number of pending detections, or 0 if service is not initialized.
 */
uint32_t detection_service_get_queue_count(void);

/**
 * @brief Check if a specific detection source is available.
 *
 * @param source The detection source to check.
 * @return true if the source module was found available during init.
 */
bool detection_service_is_source_available(detection_source_t source);

#ifdef __cplusplus
}
#endif

#endif /* DETECTION_SERVICE_H */
