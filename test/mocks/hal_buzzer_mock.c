/**
 * @file hal_buzzer_mock.c
 * @brief Mock implementation of HAL Buzzer for host tests.
 */

#include "hal_buzzer.h"
#include "hal_common.h"
#include <stdbool.h>

/* Mock state */
static bool mock_buzzer_initialized = false;
static bool mock_buzzer_playing = false;
static uint32_t mock_buzzer_last_freq_hz = 0;
static uint32_t mock_buzzer_last_duration_ms = 0;
static uint32_t mock_buzzer_play_count = 0;
static uint32_t mock_buzzer_stop_count = 0;

/* Mock control functions */
void mock_hal_buzzer_reset(void)
{
    mock_buzzer_initialized = false;
    mock_buzzer_playing = false;
    mock_buzzer_last_freq_hz = 0;
    mock_buzzer_last_duration_ms = 0;
    mock_buzzer_play_count = 0;
    mock_buzzer_stop_count = 0;
}

bool mock_hal_buzzer_is_playing(void)
{
    return mock_buzzer_playing;
}

uint32_t mock_hal_buzzer_get_last_freq(void)
{
    return mock_buzzer_last_freq_hz;
}

uint32_t mock_hal_buzzer_get_last_duration(void)
{
    return mock_buzzer_last_duration_ms;
}

uint32_t mock_hal_buzzer_get_play_count(void)
{
    return mock_buzzer_play_count;
}

uint32_t mock_hal_buzzer_get_stop_count(void)
{
    return mock_buzzer_stop_count;
}

/* HAL interface stubs */
esp_err_t hal_buzzer_init(void)
{
    if (mock_buzzer_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    mock_buzzer_initialized = true;
    return ESP_OK;
}

esp_err_t hal_buzzer_play_tone(uint32_t frequency_hz, uint32_t duration_ms)
{
    if (!mock_buzzer_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (frequency_hz < HAL_BUZZER_FREQ_MIN || frequency_hz > HAL_BUZZER_FREQ_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (duration_ms == 0 || duration_ms > HAL_BUZZER_DURATION_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    mock_buzzer_playing = true;
    mock_buzzer_last_freq_hz = frequency_hz;
    mock_buzzer_last_duration_ms = duration_ms;
    mock_buzzer_play_count++;
    return ESP_OK;
}

esp_err_t hal_buzzer_stop(void)
{
    if (!mock_buzzer_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    mock_buzzer_playing = false;
    mock_buzzer_stop_count++;
    return ESP_OK;
}

hal_status_t hal_buzzer_get_status(void)
{
    if (!mock_buzzer_initialized) {
        return HAL_STATUS_INACTIVE;
    }
    return HAL_STATUS_ACTIVE;
}

esp_err_t hal_buzzer_deinit(void)
{
    if (!mock_buzzer_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    mock_buzzer_initialized = false;
    mock_buzzer_playing = false;
    return ESP_OK;
}
