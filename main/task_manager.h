/**
 * @file task_manager.h
 * @brief Task Manager — FreeRTOS task lifecycle and stack monitoring.
 *
 * Manages FreeRTOS tasks distributed across the ESP32-S3 dual cores:
 *
 * Core 0 (PRO_CPU) — managed by Detection Service:
 *   - WiFi/BLE Scanner (priority 5, 4096 bytes stack)
 *   - RF Monitor LoRa/NRF24 (priority 6, 4096 bytes stack)
 *   - SDR Receiver (priority 4, 4096 bytes stack)
 *
 * Core 1 (APP_CPU) — managed by this module:
 *   - UI Render (priority 7, 8192 bytes stack)
 *   - Decoder Pipeline (priority 5, 8192 bytes stack)
 *   - GPS Reader (priority 3, 4096 bytes stack)
 *   - Logger (priority 2, 4096 bytes stack)
 *
 * Additionally provides:
 *   - Stack overflow monitoring via uxTaskGetStackHighWaterMark (periodic + on-demand)
 *   - Task Watchdog Timer (TWDT) integration for hang detection
 *   - Stack usage statistics for debugging
 *
 * Validates: Requirements 1.5, 9.2, 9.5
 */

#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Task Configuration Constants
 * ======================================================================== */

/** @brief Core assignments */
#define TASK_CORE_PRO_CPU       0   /**< Core 0 — RF/detection tasks */
#define TASK_CORE_APP_CPU       1   /**< Core 1 — application tasks */

/* --- Core 1 task priorities --- */
#define TASK_PRIO_UI_RENDER     7   /**< Highest on Core 1 — UI responsiveness */
#define TASK_PRIO_DECODER       5   /**< Decoder pipeline */
#define TASK_PRIO_GPS_READER    3   /**< GPS reader */
#define TASK_PRIO_LOGGER        2   /**< Logger — lowest priority */

/* --- Core 1 task stack sizes (bytes) --- */
#define TASK_STACK_UI_RENDER    8192    /**< UI needs more memory for rendering */
#define TASK_STACK_DECODER      8192    /**< Decoder needs memory for protocol parsing */
#define TASK_STACK_GPS_READER   4096    /**< GPS NMEA parsing is lightweight */
#define TASK_STACK_LOGGER       4096    /**< Logger writes to SD, moderate stack */

/* --- Core 0 task config (for reference, managed by Detection Service) --- */
#define TASK_PRIO_WIFI_BLE      5
#define TASK_PRIO_RF_MONITOR    6
#define TASK_PRIO_SDR_RECEIVER  4
#define TASK_STACK_WIFI_BLE     4096
#define TASK_STACK_RF_MONITOR   4096
#define TASK_STACK_SDR_RECEIVER 4096

/* --- Stack monitoring --- */
#define TASK_STACK_MONITOR_INTERVAL_MS  10000   /**< Check stack every 10 seconds */
#define TASK_STACK_WARNING_THRESHOLD    256     /**< Warn if high water mark < this (bytes) */

/* --- Number of managed tasks on Core 1 --- */
#define TASK_MANAGER_CORE1_COUNT    4

/* --- Task indices for stats array --- */
#define TASK_IDX_UI_RENDER      0
#define TASK_IDX_DECODER        1
#define TASK_IDX_GPS_READER     2
#define TASK_IDX_LOGGER         3

/* ========================================================================
 * Data Types
 * ======================================================================== */

/**
 * @brief Stack usage statistics for a single task.
 */
typedef struct {
    const char *task_name;          /**< Task name string */
    uint32_t stack_size;            /**< Configured stack size in bytes */
    uint32_t stack_high_water_mark; /**< Minimum free stack ever (bytes) */
    uint8_t core_id;                /**< Core the task is pinned to */
    uint8_t priority;               /**< Task priority */
    bool running;                   /**< Whether the task is currently running */
} task_stack_info_t;

/**
 * @brief Aggregate task manager statistics.
 */
typedef struct {
    task_stack_info_t tasks[TASK_MANAGER_CORE1_COUNT]; /**< Per-task info */
    uint32_t last_monitor_time_ms;  /**< Last time stack monitoring ran */
    bool stack_warning_active;      /**< True if any task is near overflow */
} task_manager_stats_t;

/* ========================================================================
 * Public API
 * ======================================================================== */

/**
 * @brief Initialize the Task Manager.
 *
 * Creates the periodic stack monitor timer and initializes the Task
 * Watchdog Timer. Does not create tasks — call task_manager_start()
 * after all services and the data pipeline are initialized.
 *
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_STATE if already initialized.
 */
esp_err_t task_manager_init(void);

/**
 * @brief Start all Core 1 tasks.
 *
 * Creates FreeRTOS tasks pinned to Core 1 (APP_CPU):
 *   - UI Render task (priority 7, 8192 bytes stack)
 *   - Decoder Pipeline task (priority 5, 8192 bytes stack)
 *   - GPS Reader task (priority 3, 4096 bytes stack)
 *   - Logger task (priority 2, 4096 bytes stack)
 *
 * Also starts the periodic stack monitoring timer (every 10s).
 *
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_STATE if not initialized or already started,
 *         ESP_ERR_NO_MEM if task creation fails.
 */
esp_err_t task_manager_start(void);

/**
 * @brief Stop all Core 1 tasks.
 *
 * Signals all managed tasks to terminate and waits for them to exit.
 * Stops the stack monitoring timer.
 *
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_STATE if not initialized or not started.
 */
esp_err_t task_manager_stop(void);

/**
 * @brief Deinitialize the Task Manager.
 *
 * Stops tasks if running, deletes the monitoring timer, and frees resources.
 *
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_STATE if not initialized.
 */
esp_err_t task_manager_deinit(void);

/**
 * @brief Get current stack usage statistics for all managed tasks.
 *
 * Reads uxTaskGetStackHighWaterMark for each task and returns detailed
 * stack usage information. Useful for tuning stack sizes and detecting
 * potential overflow conditions.
 *
 * @param[out] stats  Pointer to stats structure to fill.
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_ARG if stats is NULL,
 *         ESP_ERR_INVALID_STATE if not initialized.
 */
esp_err_t task_manager_get_stats(task_manager_stats_t *stats);

/**
 * @brief Manually trigger a stack monitoring check.
 *
 * Reads uxTaskGetStackHighWaterMark for each managed task and logs
 * warnings for tasks with dangerously low stack headroom (<256 bytes).
 *
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_STATE if not initialized.
 */
esp_err_t task_manager_check_stack_usage(void);

/**
 * @brief Check if the task manager is running.
 *
 * @return true if tasks are started and running, false otherwise.
 */
bool task_manager_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* TASK_MANAGER_H */
