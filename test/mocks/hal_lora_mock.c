/**
 * @file hal_lora_mock.c
 * @brief Mock implementation of HAL LoRa SX1262 for host tests.
 *
 * All functions return configurable status/results to allow tests
 * to simulate various hardware scenarios without real SX1262 hardware.
 */

#include "hal_lora.h"
#include <string.h>

/* Mock state */
static hal_module_state_t mock_lora_state = {
    .status = HAL_STATUS_INACTIVE,
    .last_activity_ms = 0,
    .error_count = 0
};

static bool mock_lora_initialized = false;
static bool mock_lora_in_rx = false;
static lora_config_t mock_lora_config = {0};

/* Configurable mock behavior */
static esp_err_t mock_init_result = ESP_OK;
static esp_err_t mock_get_packet_result = ESP_ERR_TIMEOUT;
static lora_packet_t mock_pending_packet = {0};
static uint8_t mock_pending_payload[HAL_LORA_MAX_PAYLOAD_LEN];

/* ========================================================================
 * Mock Control Functions (called by tests to set up scenarios)
 * ======================================================================== */

void mock_hal_lora_reset(void) {
    mock_lora_state.status = HAL_STATUS_INACTIVE;
    mock_lora_state.last_activity_ms = 0;
    mock_lora_state.error_count = 0;
    mock_lora_initialized = false;
    mock_lora_in_rx = false;
    mock_init_result = ESP_OK;
    mock_get_packet_result = ESP_ERR_TIMEOUT;
    memset(&mock_lora_config, 0, sizeof(lora_config_t));
    memset(&mock_pending_packet, 0, sizeof(lora_packet_t));
    memset(mock_pending_payload, 0, sizeof(mock_pending_payload));
}

void mock_hal_lora_set_status(hal_status_t status) {
    mock_lora_state.status = status;
}

void mock_hal_lora_set_init_result(esp_err_t result) {
    mock_init_result = result;
}

void mock_hal_lora_inject_packet(const uint8_t *payload, uint16_t len,
                                  int16_t rssi, int8_t snr) {
    if (len > HAL_LORA_MAX_PAYLOAD_LEN) {
        len = HAL_LORA_MAX_PAYLOAD_LEN;
    }
    memcpy(mock_pending_payload, payload, len);
    mock_pending_packet.payload = mock_pending_payload;
    mock_pending_packet.payload_len = len;
    mock_pending_packet.rssi_dbm = rssi;
    mock_pending_packet.snr_db = snr;
    mock_pending_packet.frequency_hz = mock_lora_config.frequency_hz;
    mock_pending_packet.timestamp_ms = 1000;
    mock_get_packet_result = ESP_OK;
}

bool mock_hal_lora_is_initialized(void) {
    return mock_lora_initialized;
}

const lora_config_t* mock_hal_lora_get_config(void) {
    return &mock_lora_config;
}

/* ========================================================================
 * HAL Interface Implementation (Mock)
 * ======================================================================== */

esp_err_t hal_lora_init(const lora_config_t *config) {
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (mock_init_result != ESP_OK) {
        mock_lora_state.status = HAL_STATUS_ERROR;
        mock_lora_state.error_count++;
        return mock_init_result;
    }

    memcpy(&mock_lora_config, config, sizeof(lora_config_t));
    mock_lora_initialized = true;
    mock_lora_state.status = HAL_STATUS_ACTIVE;
    mock_lora_state.last_activity_ms = 1000;
    return ESP_OK;
}

esp_err_t hal_lora_set_frequency(uint32_t freq_hz) {
    if (mock_lora_state.status != HAL_STATUS_ACTIVE) {
        return ESP_ERR_INVALID_STATE;
    }
    if (freq_hz < HAL_LORA_FREQ_MIN_HZ || freq_hz > HAL_LORA_FREQ_MAX_HZ) {
        return ESP_ERR_INVALID_ARG;
    }
    mock_lora_config.frequency_hz = freq_hz;
    mock_lora_in_rx = false;
    return ESP_OK;
}

esp_err_t hal_lora_start_rx(void) {
    if (mock_lora_state.status != HAL_STATUS_ACTIVE) {
        return ESP_ERR_INVALID_STATE;
    }
    mock_lora_in_rx = true;
    return ESP_OK;
}

esp_err_t hal_lora_get_packet(lora_packet_t *packet, uint32_t timeout_ms) {
    (void)timeout_ms;
    if (packet == NULL || packet->payload == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mock_lora_state.status != HAL_STATUS_ACTIVE || !mock_lora_in_rx) {
        return ESP_ERR_INVALID_STATE;
    }
    if (mock_get_packet_result != ESP_OK) {
        return mock_get_packet_result;
    }

    memcpy(packet->payload, mock_pending_packet.payload,
           mock_pending_packet.payload_len);
    packet->payload_len = mock_pending_packet.payload_len;
    packet->rssi_dbm = mock_pending_packet.rssi_dbm;
    packet->snr_db = mock_pending_packet.snr_db;
    packet->frequency_hz = mock_pending_packet.frequency_hz;
    packet->timestamp_ms = mock_pending_packet.timestamp_ms;

    /* Clear pending packet after delivery */
    mock_get_packet_result = ESP_ERR_TIMEOUT;
    return ESP_OK;
}

hal_status_t hal_lora_get_status(void) {
    return mock_lora_state.status;
}

esp_err_t hal_lora_deinit(void) {
    if (mock_lora_state.status == HAL_STATUS_INACTIVE) {
        return ESP_ERR_INVALID_STATE;
    }
    mock_lora_initialized = false;
    mock_lora_in_rx = false;
    mock_lora_state.status = HAL_STATUS_INACTIVE;
    return ESP_OK;
}
