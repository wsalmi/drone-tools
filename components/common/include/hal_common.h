/**
 * @file hal_common.h
 * @brief Common types and interfaces for the Hardware Abstraction Layer.
 *
 * Defines the base types used across all HAL modules:
 * - hal_status_t: Module operational status
 * - hal_module_state_t: Runtime state tracking for each module
 * - hal_rf_interface_t: Generic function table for RF modules
 */

#ifndef HAL_COMMON_H
#define HAL_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Operational status of a HAL module.
 *
 * Used to indicate the current state of each hardware module
 * in the system status bar and for internal logic decisions.
 */
typedef enum {
    HAL_STATUS_INACTIVE = 0,    /**< Module is not initialized or powered off */
    HAL_STATUS_ACTIVE,          /**< Module is operational and functioning normally */
    HAL_STATUS_ERROR,           /**< Module encountered an unrecoverable error */
    HAL_STATUS_INITIALIZING     /**< Module is in the process of initialization */
} hal_status_t;

/**
 * @brief Runtime state tracking for a HAL module.
 *
 * Each hardware module maintains this structure to report its current
 * status, track activity, and count errors for diagnostics.
 */
typedef struct {
    hal_status_t status;        /**< Current operational status */
    uint32_t last_activity_ms;  /**< Timestamp (ms) of last successful operation */
    uint32_t error_count;       /**< Cumulative error count since initialization */
} hal_module_state_t;

/**
 * @brief Generic function table (vtable) for RF modules.
 *
 * Provides a uniform interface for managing RF hardware modules
 * (LoRa, NRF24, SDR). This enables the hardware manager to operate
 * on any RF module through a common API, supporting hot-swap and
 * dynamic module selection.
 *
 * All function pointers accept opaque config/params to allow
 * module-specific configuration without breaking the generic interface.
 */
typedef struct {
    /**
     * @brief Initialize the RF module with given configuration.
     * @param config Pointer to module-specific configuration struct.
     * @return ESP_OK on success, or an error code.
     */
    esp_err_t (*init)(void *config);

    /**
     * @brief Deinitialize the RF module and release resources.
     * @return ESP_OK on success, or an error code.
     */
    esp_err_t (*deinit)(void);

    /**
     * @brief Start scanning/receiving on the RF module.
     * @param scan_params Pointer to module-specific scan parameters.
     * @return ESP_OK on success, or an error code.
     */
    esp_err_t (*start_scan)(void *scan_params);

    /**
     * @brief Stop scanning/receiving on the RF module.
     * @return ESP_OK on success, or an error code.
     */
    esp_err_t (*stop_scan)(void);

    /**
     * @brief Get the current operational status of the module.
     * @return Current hal_status_t value.
     */
    hal_status_t (*get_status)(void);

    /**
     * @brief Reset the RF module to a known good state.
     * @return ESP_OK on success, or an error code.
     */
    esp_err_t (*reset)(void);
} hal_rf_interface_t;

#ifdef __cplusplus
}
#endif

#endif /* HAL_COMMON_H */
