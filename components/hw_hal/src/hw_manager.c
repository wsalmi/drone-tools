/**
 * @file hw_manager.c
 * @brief Hardware Manager implementation — RF module hot-swap state machine.
 *
 * Implements the state machine for managing LoRa/NRF24 mutual exclusion
 * on the shared SPI3 bus, with periodic polling for module detection,
 * hot-swap transitions with rollback, and retry/backoff on errors.
 *
 * Validates: Requirements 10.1, 10.2, 10.3, 10.4, 10.5
 */

#include "hw_manager.h"
#include "hal_lora.h"
#include "hal_nrf24.h"
#include "error_codes.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "hw_manager";

/* ========================================================================
 * Internal State
 * ======================================================================== */

/** @brief Internal context for the hardware manager. */
typedef struct {
    hw_manager_state_t state;               /**< Current state */
    hal_status_t lora_status;               /**< LoRa module status */
    hal_status_t nrf24_status;              /**< NRF24 module status */
    uint8_t lora_retry_count;               /**< Current LoRa retry count */
    uint32_t last_poll_ms;                  /**< Last poll timestamp */
    uint32_t last_transition_ms;            /**< Last state transition timestamp */

    hw_manager_config_t config;             /**< Configuration */
    hw_manager_status_cb_t status_cb;       /**< Status change callback */

    TaskHandle_t poll_task_handle;           /**< FreeRTOS task handle */
    SemaphoreHandle_t mutex;                /**< Protects state transitions */
    bool initialized;                       /**< Whether manager is initialized */
    bool task_running;                      /**< Whether poll task should keep running */
} hw_manager_ctx_t;

static hw_manager_ctx_t s_ctx = {
    .state = HW_STATE_INITIALIZING,
    .lora_status = HAL_STATUS_INACTIVE,
    .nrf24_status = HAL_STATUS_INACTIVE,
    .lora_retry_count = 0,
    .last_poll_ms = 0,
    .last_transition_ms = 0,
    .status_cb = NULL,
    .poll_task_handle = NULL,
    .mutex = NULL,
    .initialized = false,
    .task_running = false,
};

/* ========================================================================
 * Helper Functions
 * ======================================================================== */

/**
 * @brief Get current time in milliseconds (from esp_timer).
 */
static uint32_t get_time_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

/**
 * @brief Notify status change via callback if registered.
 */
static void notify_status_change(void)
{
    if (s_ctx.status_cb != NULL) {
        s_ctx.status_cb(s_ctx.lora_status, s_ctx.nrf24_status, s_ctx.state);
    }
}

/**
 * @brief Transition to a new state with logging and notification.
 */
static void transition_to(hw_manager_state_t new_state)
{
    if (s_ctx.state != new_state) {
        ESP_LOGI(TAG, "State transition: %d -> %d", (int)s_ctx.state, (int)new_state);
        s_ctx.state = new_state;
        s_ctx.last_transition_ms = get_time_ms();
        notify_status_change();
    }
}

/**
 * @brief Attempt to initialize LoRa with default config.
 * @return ESP_OK on success, error code on failure.
 */
static esp_err_t try_init_lora(void)
{
    lora_config_t lora_cfg = {
        .frequency_hz = 915000000U,     /* Default: 915 MHz (ISM band) */
        .spreading_factor = 7,
        .bandwidth_hz = 125000,
        .coding_rate = 5,
        .tx_power_dbm = 0               /* Not used in RX mode */
    };

    s_ctx.lora_status = HAL_STATUS_INITIALIZING;
    esp_err_t err = hal_lora_init(&lora_cfg);

    if (err == ESP_OK) {
        s_ctx.lora_status = HAL_STATUS_ACTIVE;
        ESP_LOGI(TAG, "LoRa initialized successfully");
    } else {
        s_ctx.lora_status = HAL_STATUS_ERROR;
        ESP_LOGW(TAG, "LoRa init failed: 0x%x", (unsigned)err);
    }

    return err;
}

/**
 * @brief Attempt to initialize NRF24 with default config.
 * @return ESP_OK on success, error code on failure.
 */
static esp_err_t try_init_nrf24(void)
{
    nrf24_config_t nrf_cfg = {
        .channel = 0,
        .data_rate = NRF24_DATA_RATE_1MBPS,
        .address_width = 5
    };

    s_ctx.nrf24_status = HAL_STATUS_INITIALIZING;
    esp_err_t err = hal_nrf24_init(&nrf_cfg);

    if (err == ESP_OK) {
        s_ctx.nrf24_status = HAL_STATUS_ACTIVE;
        ESP_LOGI(TAG, "NRF24 initialized successfully");
    } else {
        s_ctx.nrf24_status = HAL_STATUS_ERROR;
        ESP_LOGW(TAG, "NRF24 init failed: 0x%x", (unsigned)err);
    }

    return err;
}

/**
 * @brief Deactivate LoRa module.
 * @return ESP_OK on success.
 */
static esp_err_t deactivate_lora(void)
{
    esp_err_t err = hal_lora_deinit();
    if (err == ESP_OK) {
        s_ctx.lora_status = HAL_STATUS_INACTIVE;
        ESP_LOGI(TAG, "LoRa deactivated");
    } else {
        ESP_LOGW(TAG, "LoRa deinit failed: 0x%x", (unsigned)err);
    }
    return err;
}

/**
 * @brief Deactivate NRF24 module.
 * @return ESP_OK on success.
 */
static esp_err_t deactivate_nrf24(void)
{
    esp_err_t err = hal_nrf24_deinit();
    if (err == ESP_OK) {
        s_ctx.nrf24_status = HAL_STATUS_INACTIVE;
        ESP_LOGI(TAG, "NRF24 deactivated");
    } else {
        ESP_LOGW(TAG, "NRF24 deinit failed: 0x%x", (unsigned)err);
    }
    return err;
}

/* ========================================================================
 * State Machine Event Handlers
 * ======================================================================== */

/**
 * @brief Handle event in INITIALIZING state.
 *
 * During initialization, probes NRF24 first (higher priority when present),
 * then falls back to LoRa.
 */
static esp_err_t handle_initializing(void)
{
    /* Check if NRF24 is physically present */
    if (hal_nrf24_is_present()) {
        ESP_LOGI(TAG, "NRF24 detected during init");
        esp_err_t err = try_init_nrf24();
        if (err == ESP_OK) {
            transition_to(HW_STATE_NRF24_ACTIVE);
            return ESP_OK;
        }
        /* NRF24 present but failed — fall through to try LoRa */
        ESP_LOGW(TAG, "NRF24 present but init failed, trying LoRa");
    }

    /* Try LoRa */
    esp_err_t err = try_init_lora();
    if (err == ESP_OK) {
        transition_to(HW_STATE_LORA_ACTIVE);
        return ESP_OK;
    }

    /* All modules failed */
    ESP_LOGE(TAG, "All RF modules failed to initialize");
    transition_to(HW_STATE_ERROR);
    return ESP_FAIL;
}

/* Forward declaration: handle_switch_to_nrf24() reuses the LoRa recovery
 * retry policy on rollback, but handle_lora_recovery() is defined later
 * in this file. */
static esp_err_t handle_lora_recovery(void);

/**
 * @brief Handle NRF24 detection while LoRa is active.
 *
 * Performs hot-swap: deactivates LoRa, activates NRF24.
 * On NRF24 failure, performs rollback to LoRa (Req 10.3), retrying up to
 * the configured maximum (same policy as handle_lora_recovery) before
 * giving up and entering the error state.
 */
static esp_err_t handle_switch_to_nrf24(void)
{
    transition_to(HW_STATE_SWITCHING_TO_NRF);

    /* Step 1: Deactivate LoRa to free the SPI bus */
    esp_err_t err = deactivate_lora();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LoRa deinit failed during switch, attempting NRF24 anyway");
    }

    /* Step 2: Activate NRF24 */
    err = try_init_nrf24();
    if (err == ESP_OK) {
        transition_to(HW_STATE_NRF24_ACTIVE);
        return ESP_OK;
    }

    /* Step 3: Rollback — re-initialize LoRa with retry (Req 10.3).
     * Reuses the same retry/backoff policy as handle_lora_recovery so a
     * transient failure during rollback doesn't strand the system in
     * HW_STATE_ERROR after a single attempt. transition_to() already
     * notifies on every state change, so no extra notification is needed
     * here. */
    ESP_LOGW(TAG, "NRF24 activation failed, rolling back to LoRa");
    s_ctx.nrf24_status = HAL_STATUS_ERROR;

    err = handle_lora_recovery();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Rollback to LoRa successful");
    } else {
        ESP_LOGE(TAG, "Rollback to LoRa also failed");
    }

    return err;
}

/**
 * @brief Handle NRF24 disconnection while NRF24 is active.
 *
 * Deactivates NRF24 and reactivates LoRa (Req 10.4).
 */
static esp_err_t handle_switch_to_lora(void)
{
    transition_to(HW_STATE_SWITCHING_TO_LORA);

    /* Step 1: Deactivate NRF24 */
    deactivate_nrf24();

    /* Step 2: Reactivate LoRa */
    esp_err_t err = try_init_lora();
    if (err == ESP_OK) {
        transition_to(HW_STATE_LORA_ACTIVE);
        ESP_LOGI(TAG, "Switched back to LoRa after NRF24 disconnect");
    } else {
        ESP_LOGE(TAG, "Failed to reactivate LoRa after NRF24 disconnect");
        transition_to(HW_STATE_ERROR);
    }

    return err;
}

/**
 * @brief Handle LoRa error/timeout — enter recovery with retry.
 *
 * Attempts to reset LoRa up to 3 times with 2s intervals.
 * After 3 failures, transitions to ERROR state (Req 3.5).
 */
static esp_err_t handle_lora_recovery(void)
{
    transition_to(HW_STATE_LORA_RECOVERY);
    s_ctx.lora_retry_count = 0;

    for (uint8_t attempt = 0; attempt < s_ctx.config.lora_retry_max; attempt++) {
        s_ctx.lora_retry_count = attempt + 1;
        ESP_LOGI(TAG, "LoRa recovery attempt %d/%d",
                 s_ctx.lora_retry_count, s_ctx.config.lora_retry_max);

        /* Deinit first to clean up */
        hal_lora_deinit();
        s_ctx.lora_status = HAL_STATUS_ERROR;

        /* Wait before retry */
        vTaskDelay(pdMS_TO_TICKS(s_ctx.config.lora_retry_interval_ms));

        /* Attempt re-init */
        esp_err_t err = try_init_lora();
        if (err == ESP_OK) {
            s_ctx.lora_retry_count = 0;
            transition_to(HW_STATE_LORA_ACTIVE);
            ESP_LOGI(TAG, "LoRa recovery successful");
            return ESP_OK;
        }
    }

    /* All retries exhausted */
    ESP_LOGE(TAG, "LoRa recovery failed after %d attempts",
             s_ctx.config.lora_retry_max);
    transition_to(HW_STATE_ERROR);
    return ESP_FAIL;
}

/* ========================================================================
 * Poll Task
 * ======================================================================== */

/**
 * @brief Periodic poll logic — detect NRF24 connection/disconnection.
 *
 * Called every HW_MGR_POLL_INTERVAL_MS to check module states and
 * trigger appropriate state transitions (Req 10.5).
 */
static void poll_modules(void)
{
    bool nrf24_present = hal_nrf24_is_present();

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);

    switch (s_ctx.state) {
    case HW_STATE_LORA_ACTIVE:
        /* Check if NRF24 was connected */
        if (nrf24_present) {
            ESP_LOGI(TAG, "NRF24 connected — initiating hot-swap");
            /* Release mutex before the long operation */
            xSemaphoreGive(s_ctx.mutex);
            handle_switch_to_nrf24();
            return;
        }

        /* Check LoRa health */
        if (hal_lora_get_status() == HAL_STATUS_ERROR) {
            ESP_LOGW(TAG, "LoRa error detected — entering recovery");
            xSemaphoreGive(s_ctx.mutex);
            handle_lora_recovery();
            return;
        }
        break;

    case HW_STATE_NRF24_ACTIVE:
        /* Check if NRF24 was disconnected */
        if (!nrf24_present) {
            ESP_LOGI(TAG, "NRF24 disconnected — switching to LoRa");
            xSemaphoreGive(s_ctx.mutex);
            handle_switch_to_lora();
            return;
        }
        break;

    case HW_STATE_LORA_RECOVERY:
        /* Recovery is handled synchronously in handle_lora_recovery —
         * if we see this state during poll, it means recovery is in progress
         * from another path. Do nothing. */
        break;

    case HW_STATE_ERROR:
        /* In error state, periodically check if NRF24 becomes available */
        if (nrf24_present) {
            ESP_LOGI(TAG, "NRF24 detected while in Error state — attempting recovery");
            xSemaphoreGive(s_ctx.mutex);
            esp_err_t err = try_init_nrf24();
            if (err == ESP_OK) {
                xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
                transition_to(HW_STATE_NRF24_ACTIVE);
                xSemaphoreGive(s_ctx.mutex);
            }
            return;
        }
        break;

    case HW_STATE_INITIALIZING:
    case HW_STATE_SWITCHING_TO_NRF:
    case HW_STATE_SWITCHING_TO_LORA:
        /* Transient states — do nothing during poll */
        break;
    }

    s_ctx.last_poll_ms = get_time_ms();
    xSemaphoreGive(s_ctx.mutex);
}

/**
 * @brief FreeRTOS task for periodic module polling.
 *
 * Runs continuously, polling every config.poll_interval_ms (default 500ms).
 */
static void hw_manager_poll_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Poll task started (interval: %lu ms)",
             (unsigned long)s_ctx.config.poll_interval_ms);

    while (s_ctx.task_running) {
        poll_modules();
        vTaskDelay(pdMS_TO_TICKS(s_ctx.config.poll_interval_ms));
    }

    ESP_LOGI(TAG, "Poll task stopped");
    vTaskDelete(NULL);
}

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

void hw_manager_get_default_config(hw_manager_config_t *config)
{
    if (config == NULL) {
        return;
    }

    config->poll_interval_ms = HW_MGR_POLL_INTERVAL_MS;
    config->hotswap_timeout_ms = HW_MGR_HOTSWAP_TIMEOUT_MS;
    config->lora_retry_max = HW_MGR_LORA_RETRY_MAX;
    config->lora_retry_interval_ms = HW_MGR_LORA_RETRY_INTERVAL_MS;
    config->status_cb = NULL;
}

esp_err_t hw_manager_init(const hw_manager_config_t *config)
{
    if (s_ctx.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    /* Apply configuration */
    if (config != NULL) {
        s_ctx.config = *config;
        s_ctx.status_cb = config->status_cb;
    } else {
        hw_manager_get_default_config(&s_ctx.config);
    }

    /* Create mutex */
    s_ctx.mutex = xSemaphoreCreateMutex();
    if (s_ctx.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    /* Reset state */
    s_ctx.state = HW_STATE_INITIALIZING;
    s_ctx.lora_status = HAL_STATUS_INACTIVE;
    s_ctx.nrf24_status = HAL_STATUS_INACTIVE;
    s_ctx.lora_retry_count = 0;
    s_ctx.last_poll_ms = get_time_ms();
    s_ctx.last_transition_ms = get_time_ms();

    /* Run initial probe */
    handle_initializing();

    /* Start poll task */
    s_ctx.task_running = true;
    BaseType_t ret = xTaskCreatePinnedToCore(
        hw_manager_poll_task,
        "hw_mgr_poll",
        HW_MGR_TASK_STACK_SIZE,
        NULL,
        HW_MGR_TASK_PRIORITY,
        &s_ctx.poll_task_handle,
        0   /* Core 0 — PRO_CPU, same as RF tasks */
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create poll task");
        vSemaphoreDelete(s_ctx.mutex);
        s_ctx.mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_ctx.initialized = true;
    ESP_LOGI(TAG, "Hardware manager initialized, state: %d", (int)s_ctx.state);

    return ESP_OK;
}

esp_err_t hw_manager_deinit(void)
{
    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Stop the poll task */
    s_ctx.task_running = false;

    /* Give the task time to exit gracefully */
    vTaskDelay(pdMS_TO_TICKS(s_ctx.config.poll_interval_ms + 100));

    /* Deinitialize active modules */
    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);

    if (s_ctx.lora_status == HAL_STATUS_ACTIVE) {
        deactivate_lora();
    }
    if (s_ctx.nrf24_status == HAL_STATUS_ACTIVE) {
        deactivate_nrf24();
    }

    s_ctx.state = HW_STATE_INITIALIZING;
    xSemaphoreGive(s_ctx.mutex);

    /* Clean up resources */
    vSemaphoreDelete(s_ctx.mutex);
    s_ctx.mutex = NULL;
    s_ctx.poll_task_handle = NULL;
    s_ctx.initialized = false;

    ESP_LOGI(TAG, "Hardware manager deinitialized");
    return ESP_OK;
}

esp_err_t hw_manager_get_status(hw_manager_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_ctx.initialized) {
        /* Return safe defaults if not initialized */
        status->state = HW_STATE_INITIALIZING;
        status->lora_status = HAL_STATUS_INACTIVE;
        status->nrf24_status = HAL_STATUS_INACTIVE;
        status->lora_retry_count = 0;
        status->last_poll_ms = 0;
        status->last_transition_ms = 0;
        return ESP_OK;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    status->state = s_ctx.state;
    status->lora_status = s_ctx.lora_status;
    status->nrf24_status = s_ctx.nrf24_status;
    status->lora_retry_count = s_ctx.lora_retry_count;
    status->last_poll_ms = s_ctx.last_poll_ms;
    status->last_transition_ms = s_ctx.last_transition_ms;
    xSemaphoreGive(s_ctx.mutex);

    return ESP_OK;
}

hw_manager_state_t hw_manager_get_state(void)
{
    if (!s_ctx.initialized) {
        return HW_STATE_INITIALIZING;
    }

    hw_manager_state_t state;
    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    state = s_ctx.state;
    xSemaphoreGive(s_ctx.mutex);

    return state;
}

esp_err_t hw_manager_inject_event(hw_manager_event_t event)
{
    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);

    esp_err_t result = ESP_OK;

    switch (event) {
    case HW_EVENT_NRF24_DETECTED:
        if (s_ctx.state == HW_STATE_LORA_ACTIVE) {
            xSemaphoreGive(s_ctx.mutex);
            return handle_switch_to_nrf24();
        }
        result = ESP_ERR_INVALID_STATE;
        break;

    case HW_EVENT_NRF24_DISCONNECTED:
        if (s_ctx.state == HW_STATE_NRF24_ACTIVE) {
            xSemaphoreGive(s_ctx.mutex);
            return handle_switch_to_lora();
        }
        result = ESP_ERR_INVALID_STATE;
        break;

    case HW_EVENT_LORA_TIMEOUT:
        if (s_ctx.state == HW_STATE_LORA_ACTIVE) {
            xSemaphoreGive(s_ctx.mutex);
            return handle_lora_recovery();
        }
        result = ESP_ERR_INVALID_STATE;
        break;

    case HW_EVENT_NRF24_INIT_OK:
        if (s_ctx.state == HW_STATE_SWITCHING_TO_NRF) {
            s_ctx.nrf24_status = HAL_STATUS_ACTIVE;
            transition_to(HW_STATE_NRF24_ACTIVE);
        } else {
            result = ESP_ERR_INVALID_STATE;
        }
        break;

    case HW_EVENT_NRF24_INIT_FAIL:
        if (s_ctx.state == HW_STATE_SWITCHING_TO_NRF) {
            s_ctx.nrf24_status = HAL_STATUS_ERROR;
            /* Rollback */
            xSemaphoreGive(s_ctx.mutex);
            try_init_lora();
            xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
            if (s_ctx.lora_status == HAL_STATUS_ACTIVE) {
                transition_to(HW_STATE_LORA_ACTIVE);
            } else {
                transition_to(HW_STATE_ERROR);
            }
        } else {
            result = ESP_ERR_INVALID_STATE;
        }
        break;

    case HW_EVENT_LORA_INIT_OK:
        if (s_ctx.state == HW_STATE_SWITCHING_TO_LORA ||
            s_ctx.state == HW_STATE_LORA_RECOVERY) {
            s_ctx.lora_status = HAL_STATUS_ACTIVE;
            s_ctx.lora_retry_count = 0;
            transition_to(HW_STATE_LORA_ACTIVE);
        } else {
            result = ESP_ERR_INVALID_STATE;
        }
        break;

    case HW_EVENT_LORA_INIT_FAIL:
        if (s_ctx.state == HW_STATE_SWITCHING_TO_LORA) {
            s_ctx.lora_status = HAL_STATUS_ERROR;
            transition_to(HW_STATE_ERROR);
        } else {
            result = ESP_ERR_INVALID_STATE;
        }
        break;

    case HW_EVENT_LORA_RESET_OK:
        if (s_ctx.state == HW_STATE_LORA_RECOVERY) {
            s_ctx.lora_status = HAL_STATUS_ACTIVE;
            s_ctx.lora_retry_count = 0;
            transition_to(HW_STATE_LORA_ACTIVE);
        } else {
            result = ESP_ERR_INVALID_STATE;
        }
        break;

    case HW_EVENT_LORA_RESET_FAIL:
        if (s_ctx.state == HW_STATE_LORA_RECOVERY) {
            s_ctx.lora_retry_count++;
            if (s_ctx.lora_retry_count >= s_ctx.config.lora_retry_max) {
                transition_to(HW_STATE_ERROR);
            }
            /* else stay in LORA_RECOVERY */
        } else {
            result = ESP_ERR_INVALID_STATE;
        }
        break;

    case HW_EVENT_ALL_MODULES_FAILED:
        transition_to(HW_STATE_ERROR);
        break;
    }

    xSemaphoreGive(s_ctx.mutex);
    return result;
}

esp_err_t hw_manager_register_status_cb(hw_manager_status_cb_t cb)
{
    if (s_ctx.initialized && s_ctx.mutex != NULL) {
        xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
        s_ctx.status_cb = cb;
        xSemaphoreGive(s_ctx.mutex);
    } else {
        s_ctx.status_cb = cb;
    }

    return ESP_OK;
}
