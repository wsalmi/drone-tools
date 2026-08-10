/**
 * @file hal_buzzer.h
 * @brief HAL interface for the NS4150B buzzer/amplifier.
 *
 * Provides tone generation for audible alerts (proximity warnings,
 * new detection notifications). The NS4150B is an integrated
 * I2S/I2C amplifier that drives a small speaker.
 *
 * Tones are played asynchronously — hal_buzzer_play_tone() starts
 * the tone and returns immediately. The tone stops automatically
 * after the specified duration, or can be stopped early with
 * hal_buzzer_stop().
 */

#ifndef HAL_BUZZER_H
#define HAL_BUZZER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "hal_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Constants
 * ======================================================================== */

/** @brief Minimum supported frequency in Hz */
#define HAL_BUZZER_FREQ_MIN     100

/** @brief Maximum supported frequency in Hz */
#define HAL_BUZZER_FREQ_MAX     8000

/** @brief Maximum tone duration in milliseconds */
#define HAL_BUZZER_DURATION_MAX 10000

/* ========================================================================
 * Public API
 * ======================================================================== */

/**
 * @brief Initialize the buzzer hardware.
 *
 * Configures the I2S peripheral and NS4150B amplifier for tone generation.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already initialized
 */
esp_err_t hal_buzzer_init(void);

/**
 * @brief Play a tone at the specified frequency and duration.
 *
 * Starts generating a square wave at the given frequency. The tone
 * plays asynchronously and stops automatically after duration_ms.
 * If a tone is already playing, it is replaced by the new tone.
 *
 * @param frequency_hz  Tone frequency in Hz (100–8000)
 * @param duration_ms   Tone duration in milliseconds (1–10000)
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if frequency or duration
 *         is out of range, ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t hal_buzzer_play_tone(uint32_t frequency_hz, uint32_t duration_ms);

/**
 * @brief Stop any currently playing tone immediately.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t hal_buzzer_stop(void);

/**
 * @brief Get the current operational status of the buzzer module.
 *
 * @return Current hal_status_t value
 */
hal_status_t hal_buzzer_get_status(void);

/**
 * @brief Deinitialize the buzzer and release resources.
 *
 * Stops any playing tone and releases the I2S peripheral.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t hal_buzzer_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_BUZZER_H */
