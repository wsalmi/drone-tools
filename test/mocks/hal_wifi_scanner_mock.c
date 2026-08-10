/**
 * @file hal_wifi_scanner_mock.c
 * @brief Mock implementation of HAL WiFi Scanner for host-based testing.
 *
 * Provides a controllable mock that implements the hal_wifi_scanner.h
 * interface without any real hardware dependencies.
 */

#include "hal_wifi_scanner.h"
#include "hal_mocks.h"

#include <stdbool.h>
#include <stddef.h>

/* ---------------------------------------------------------------------------
 * Mock Internal State
 * --------------------------------------------------------------------------- */

static struct {
    hal_status_t status;
    bool initialized;
    bool scanning;
    wifi_scan_callback_t callback;
} s_mock = {
    .status = HAL_STATUS_INACTIVE,
    .initialized = false,
    .scanning = false,
    .callback = NULL,
};

/* ---------------------------------------------------------------------------
 * Mock Control Functions
 * --------------------------------------------------------------------------- */

void mock_hal_wifi_scanner_reset(void)
{
    s_mock.status = HAL_STATUS_INACTIVE;
    s_mock.initialized = false;
    s_mock.scanning = false;
    s_mock.callback = NULL;
}

void mock_hal_wifi_scanner_set_status(hal_status_t status)
{
    s_mock.status = status;
}

bool mock_hal_wifi_scanner_is_initialized(void)
{
    return s_mock.initialized;
}

bool mock_hal_wifi_scanner_is_scanning(void)
{
    return s_mock.scanning;
}

/* ---------------------------------------------------------------------------
 * HAL Interface Implementation (Mock)
 * --------------------------------------------------------------------------- */

esp_err_t hal_wifi_scanner_init(void)
{
    if (s_mock.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    s_mock.initialized = true;
    s_mock.status = HAL_STATUS_INACTIVE;
    return ESP_OK;
}

esp_err_t hal_wifi_scanner_start(wifi_scan_callback_t callback)
{
    if (!s_mock.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_mock.scanning) {
        return ESP_ERR_INVALID_STATE;
    }
    if (callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_mock.callback = callback;
    s_mock.scanning = true;
    s_mock.status = HAL_STATUS_ACTIVE;
    return ESP_OK;
}

esp_err_t hal_wifi_scanner_stop(void)
{
    if (!s_mock.scanning) {
        return ESP_ERR_INVALID_STATE;
    }

    s_mock.scanning = false;
    s_mock.callback = NULL;
    s_mock.status = HAL_STATUS_INACTIVE;
    return ESP_OK;
}

hal_status_t hal_wifi_scanner_get_status(void)
{
    return s_mock.status;
}

esp_err_t hal_wifi_scanner_deinit(void)
{
    if (!s_mock.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_mock.scanning) {
        hal_wifi_scanner_stop();
    }

    s_mock.initialized = false;
    s_mock.status = HAL_STATUS_INACTIVE;
    return ESP_OK;
}
