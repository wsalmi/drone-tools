/**
 * @file hw_manager.h
 * @brief Hardware Manager — state machine for RF module hot-swap.
 *
 * Manages the mutual exclusion between LoRa SX1262 and NRF24L01+ on the
 * shared SPI3 bus. Implements:
 * - Automatic detection of NRF24 connection/disconnection via periodic SPI poll
 * - Hot-swap transitions with rollback on failure
 * - Retry with backoff for modules in error state
 * - Status change notifications for the UI status bar
 *
 * State machine:
 *   Initializing → LoRa_Active (NRF24 not present)
 *   Initializing → NRF24_Active (NRF24 detected)
 *   Initializing → Error (all modules failed)
 *   LoRa_Active → Switching_to_NRF (NRF24 connected via poll)
 *   Switching_to_NRF → NRF24_Active (NRF24 activation OK)
 *   Switching_to_NRF → LoRa_Active (NRF24 activation failed — rollback)
 *   NRF24_Active → Switching_to_LoRa (NRF24 disconnected)
 *   Switching_to_LoRa → LoRa_Active (LoRa reactivation OK)
 *   LoRa_Active → LoRa_Recovery (LoRa timeout/error)
 *   LoRa_Recovery → LoRa_Active (reset OK, up to 3 attempts)
 *   LoRa_Recovery → Error (3 consecutive failures)
 *
 * Validates: Requirements 10.1, 10.2, 10.3, 10.4, 10.5
 */

#ifndef HW_MANAGER_H
#define HW_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "hal_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Configuration Constants
 * ======================================================================== */

/** @brief Interval for SPI poll to detect module connection/disconnection (ms) */
#define HW_MGR_POLL_INTERVAL_MS         500

/** @brief Maximum time allowed for a hot-swap transition (ms) */
#define HW_MGR_HOTSWAP_TIMEOUT_MS       2000

/** @brief Maximum retry attempts for LoRa recovery before entering Error state */
#define HW_MGR_LORA_RETRY_MAX           3

/** @brief Interval between LoRa retry attempts (ms) */
#define HW_MGR_LORA_RETRY_INTERVAL_MS   2000

/** @brief Stack size for the hardware manager FreeRTOS task (bytes) */
#define HW_MGR_TASK_STACK_SIZE          4096

/** @brief Priority of the hardware manager FreeRTOS task */
#define HW_MGR_TASK_PRIORITY            5

/* ========================================================================
 * State Machine Types
 * ======================================================================== */

/**
 * @brief States of the hardware manager state machine.
 */
typedef enum {
    HW_STATE_INITIALIZING = 0,      /**< System startup, probing modules */
    HW_STATE_LORA_ACTIVE,           /**< LoRa is the active RF module */
    HW_STATE_NRF24_ACTIVE,          /**< NRF24 is the active RF module */
    HW_STATE_SWITCHING_TO_NRF,      /**< Transitioning from LoRa to NRF24 */
    HW_STATE_SWITCHING_TO_LORA,     /**< Transitioning from NRF24 to LoRa */
    HW_STATE_LORA_RECOVERY,         /**< LoRa in retry/recovery phase */
    HW_STATE_ERROR                  /**< All modules failed, no RF available */
} hw_manager_state_t;

/**
 * @brief Events that trigger state transitions.
 */
typedef enum {
    HW_EVENT_NRF24_DETECTED = 0,    /**< NRF24 detected via SPI poll */
    HW_EVENT_NRF24_DISCONNECTED,    /**< NRF24 no longer responding */
    HW_EVENT_NRF24_INIT_OK,         /**< NRF24 initialization succeeded */
    HW_EVENT_NRF24_INIT_FAIL,       /**< NRF24 initialization failed */
    HW_EVENT_LORA_INIT_OK,          /**< LoRa initialization succeeded */
    HW_EVENT_LORA_INIT_FAIL,        /**< LoRa initialization failed */
    HW_EVENT_LORA_TIMEOUT,          /**< LoRa module stopped responding */
    HW_EVENT_LORA_RESET_OK,         /**< LoRa reset/recovery succeeded */
    HW_EVENT_LORA_RESET_FAIL,       /**< LoRa reset/recovery failed */
    HW_EVENT_ALL_MODULES_FAILED     /**< No RF module could be initialized */
} hw_manager_event_t;

/**
 * @brief Callback type for module status change notifications.
 *
 * Called whenever a module's status changes (e.g., LoRa activated, NRF24 error).
 * Used by the UI to update the status bar.
 *
 * @param lora_status   Current LoRa module status.
 * @param nrf24_status  Current NRF24 module status.
 * @param state         Current state machine state.
 */
typedef void (*hw_manager_status_cb_t)(hal_status_t lora_status,
                                       hal_status_t nrf24_status,
                                       hw_manager_state_t state);

/**
 * @brief Configuration for the hardware manager.
 */
typedef struct {
    uint32_t poll_interval_ms;          /**< SPI poll interval (default: 500 ms) */
    uint32_t hotswap_timeout_ms;        /**< Max time for hot-swap (default: 2000 ms) */
    uint8_t lora_retry_max;             /**< Max LoRa recovery retries (default: 3) */
    uint32_t lora_retry_interval_ms;    /**< Interval between retries (default: 2000 ms) */
    hw_manager_status_cb_t status_cb;   /**< Optional callback for status changes */
} hw_manager_config_t;

/**
 * @brief Runtime status of the hardware manager.
 */
typedef struct {
    hw_manager_state_t state;           /**< Current state machine state */
    hal_status_t lora_status;           /**< Current LoRa module status */
    hal_status_t nrf24_status;          /**< Current NRF24 module status */
    uint8_t lora_retry_count;           /**< Current LoRa recovery attempt count */
    uint32_t last_poll_ms;              /**< Timestamp of last SPI poll */
    uint32_t last_transition_ms;        /**< Timestamp of last state change */
} hw_manager_status_t;

/* ========================================================================
 * Public API
 * ======================================================================== */

/**
 * @brief Initialize the hardware manager and start the polling task.
 *
 * Probes all RF modules and enters the appropriate initial state.
 * Creates a FreeRTOS task for periodic SPI polling.
 *
 * @param config Pointer to configuration. If NULL, defaults are used.
 * @return ESP_OK on success.
 * @return ESP_ERR_NO_MEM if task creation fails.
 * @return ESP_ERR_INVALID_STATE if already initialized.
 */
esp_err_t hw_manager_init(const hw_manager_config_t *config);

/**
 * @brief Stop the hardware manager and deinitialize active modules.
 *
 * Stops the polling task and deinitializes whichever RF module is active.
 *
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_STATE if not initialized.
 */
esp_err_t hw_manager_deinit(void);

/**
 * @brief Get the current status of the hardware manager.
 *
 * @param status Pointer to status structure to fill.
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_ARG if status is NULL.
 */
esp_err_t hw_manager_get_status(hw_manager_status_t *status);

/**
 * @brief Get the current state of the state machine.
 *
 * @return Current hw_manager_state_t value.
 */
hw_manager_state_t hw_manager_get_state(void);

/**
 * @brief Inject an event into the state machine (for testing/manual override).
 *
 * @param event Event to inject.
 * @return ESP_OK if event was processed.
 * @return ESP_ERR_INVALID_STATE if event is not valid in current state.
 */
esp_err_t hw_manager_inject_event(hw_manager_event_t event);

/**
 * @brief Register a callback for module status changes.
 *
 * @param cb Callback function. Pass NULL to unregister.
 * @return ESP_OK on success.
 */
esp_err_t hw_manager_register_status_cb(hw_manager_status_cb_t cb);

/**
 * @brief Get a default configuration with standard values.
 *
 * @param config Pointer to config structure to fill with defaults.
 */
void hw_manager_get_default_config(hw_manager_config_t *config);

#ifdef __cplusplus
}
#endif

#endif /* HW_MANAGER_H */
