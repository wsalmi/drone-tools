/**
 * @file hal_nrf24_mock.c
 * @brief Mock implementation of HAL NRF24L01+ for host tests.
 */

#include "hal_nrf24.h"
#include "hal_common.h"
#include <stdbool.h>

/* Mock state */
static hal_module_state_t mock_nrf24_state = {
    .status = HAL_STATUS_INACTIVE,
    .last_activity_ms = 0,
    .error_count = 0
};

static bool mock_nrf24_present = false;
static bool mock_nrf24_initialized = false;
static esp_err_t mock_nrf24_init_result = ESP_OK;

/* Mock control functions */
void mock_hal_nrf24_reset(void) {
    mock_nrf24_state.status = HAL_STATUS_INACTIVE;
    mock_nrf24_state.last_activity_ms = 0;
    mock_nrf24_state.error_count = 0;
    mock_nrf24_present = false;
    mock_nrf24_initialized = false;
    mock_nrf24_init_result = ESP_OK;
}

void mock_hal_nrf24_set_present(bool present) {
    mock_nrf24_present = present;
}

void mock_hal_nrf24_set_status(hal_status_t status) {
    mock_nrf24_state.status = status;
}

void mock_hal_nrf24_set_init_result(esp_err_t result) {
    mock_nrf24_init_result = result;
}

/* HAL interface stubs */
esp_err_t hal_nrf24_init(const nrf24_config_t *config) {
    (void)config;
    if (!mock_nrf24_present) {
        mock_nrf24_state.error_count++;
        return ESP_ERR_NOT_FOUND;
    }
    if (mock_nrf24_init_result != ESP_OK) {
        mock_nrf24_state.status = HAL_STATUS_ERROR;
        mock_nrf24_state.error_count++;
        return mock_nrf24_init_result;
    }
    mock_nrf24_initialized = true;
    mock_nrf24_state.status = HAL_STATUS_ACTIVE;
    return ESP_OK;
}

esp_err_t hal_nrf24_deinit(void) {
    mock_nrf24_initialized = false;
    mock_nrf24_state.status = HAL_STATUS_INACTIVE;
    return ESP_OK;
}

bool hal_nrf24_is_present(void) {
    return mock_nrf24_present;
}

hal_status_t hal_nrf24_get_status(void) {
    return mock_nrf24_state.status;
}

esp_err_t hal_nrf24_scan_spectrum(nrf24_spectrum_t *result) {
    (void)result;
    if (!mock_nrf24_initialized) return ESP_ERR_INVALID_STATE;
    return ESP_OK;
}

esp_err_t hal_nrf24_listen_channel(uint8_t channel, nrf24_packet_t *packet,
                                    uint32_t timeout_ms) {
    (void)channel;
    (void)packet;
    (void)timeout_ms;
    if (!mock_nrf24_initialized) return ESP_ERR_INVALID_STATE;
    return ESP_ERR_TIMEOUT;
}
