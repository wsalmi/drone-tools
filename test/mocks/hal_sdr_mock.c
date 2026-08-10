/**
 * @file hal_sdr_mock.c
 * @brief Mock implementation of HAL RTL-SDR for host tests.
 */

#include "hal_sdr.h"
#include "hal_common.h"
#include <stdbool.h>
#include <string.h>

/* Mock state */
static hal_module_state_t mock_sdr_state = {
    .status = HAL_STATUS_INACTIVE,
    .last_activity_ms = 0,
    .error_count = 0
};

static bool mock_sdr_initialized = false;
static sdr_config_t mock_sdr_config;

/* Mock control functions */
void mock_hal_sdr_reset(void) {
    mock_sdr_state.status = HAL_STATUS_INACTIVE;
    mock_sdr_state.last_activity_ms = 0;
    mock_sdr_state.error_count = 0;
    mock_sdr_initialized = false;
    memset(&mock_sdr_config, 0, sizeof(mock_sdr_config));
}

void mock_hal_sdr_set_status(hal_status_t status) {
    mock_sdr_state.status = status;
}

/* HAL interface stubs */
esp_err_t hal_sdr_init(const sdr_config_t *config) {
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(&mock_sdr_config, config, sizeof(sdr_config_t));
    mock_sdr_initialized = true;
    mock_sdr_state.status = HAL_STATUS_ACTIVE;
    return ESP_OK;
}

esp_err_t hal_sdr_set_frequency(uint32_t freq_hz) {
    if (!mock_sdr_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (freq_hz < SDR_FREQ_MIN_HZ || freq_hz > SDR_FREQ_MAX_HZ) {
        return ESP_ERR_INVALID_ARG;
    }
    mock_sdr_config.center_freq_hz = freq_hz;
    return ESP_OK;
}

esp_err_t hal_sdr_set_sample_rate(uint32_t rate_hz) {
    if (!mock_sdr_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (rate_hz < SDR_SAMPLE_RATE_MIN_HZ || rate_hz > SDR_SAMPLE_RATE_MAX_HZ) {
        return ESP_ERR_INVALID_ARG;
    }
    mock_sdr_config.sample_rate_hz = rate_hz;
    return ESP_OK;
}

esp_err_t hal_sdr_read_iq(sdr_iq_buffer_t *buffer, uint32_t timeout_ms) {
    (void)timeout_ms;
    if (buffer == NULL || buffer->iq_samples == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!mock_sdr_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Fill with zeros (silence) in mock */
    memset(buffer->iq_samples, 0, buffer->num_samples * 2);
    buffer->center_freq_hz = mock_sdr_config.center_freq_hz;
    buffer->timestamp_ms = 0;
    return ESP_OK;
}

esp_err_t hal_sdr_compute_spectrum(const sdr_iq_buffer_t *iq,
                                    sdr_spectrum_t *spectrum) {
    if (iq == NULL || spectrum == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!mock_sdr_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Mock: set basic metadata */
    spectrum->num_bins = SDR_FFT_SIZE;
    spectrum->freq_start_hz = iq->center_freq_hz -
                              (mock_sdr_config.sample_rate_hz / 2);
    spectrum->freq_step_hz = mock_sdr_config.sample_rate_hz / SDR_FFT_SIZE;
    return ESP_OK;
}

hal_status_t hal_sdr_get_status(void) {
    return mock_sdr_state.status;
}

esp_err_t hal_sdr_deinit(void) {
    if (!mock_sdr_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    mock_sdr_initialized = false;
    mock_sdr_state.status = HAL_STATUS_INACTIVE;
    return ESP_OK;
}
