/**
 * @file hal_mocks.h
 * @brief Control interface for HAL mock modules in host tests.
 *
 * Tests use these functions to configure mock behavior before
 * exercising the system under test. Each mock_hal_xxx_reset()
 * returns the module to its initial state.
 */

#ifndef HAL_MOCKS_H
#define HAL_MOCKS_H

#include "hal_common.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * LoRa SX1262 Mock Control
 * ======================================================================== */

/** @brief Reset LoRa mock to initial (inactive) state. */
void mock_hal_lora_reset(void);

/** @brief Override LoRa module status. */
void mock_hal_lora_set_status(hal_status_t status);

/** @brief Configure the result of hal_lora_init() calls. */
void mock_hal_lora_set_init_result(esp_err_t result);

/** @brief Inject a packet that will be returned by hal_lora_get_packet(). */
void mock_hal_lora_inject_packet(const uint8_t *payload, uint16_t len,
                                  int16_t rssi, int8_t snr);

/** @brief Check if init was called. */
bool mock_hal_lora_is_initialized(void);

/* ========================================================================
 * NRF24L01+ Mock Control
 * ======================================================================== */

/** @brief Reset NRF24 mock to initial (inactive) state. */
void mock_hal_nrf24_reset(void);

/** @brief Set whether NRF24 module is physically present. */
void mock_hal_nrf24_set_present(bool present);

/** @brief Override NRF24 module status. */
void mock_hal_nrf24_set_status(hal_status_t status);

/* ========================================================================
 * RTL-SDR Mock Control
 * ======================================================================== */

/** @brief Reset SDR mock to initial (inactive) state. */
void mock_hal_sdr_reset(void);

/** @brief Override SDR module status. */
void mock_hal_sdr_set_status(hal_status_t status);

/* ========================================================================
 * GPS ATGM336H Mock Control
 * ======================================================================== */

/** @brief Reset GPS mock to initial (no fix) state. */
void mock_hal_gps_reset(void);

/** @brief Set GPS fix with specific coordinates and quality. */
void mock_hal_gps_set_fix(bool has_fix, double lat, double lon, float alt,
                           uint8_t sats, float hdop);

/* ========================================================================
 * WiFi Scanner Mock Control
 * ======================================================================== */

/** @brief Reset WiFi scanner mock to initial (inactive) state. */
void mock_hal_wifi_scanner_reset(void);

/** @brief Override WiFi scanner module status. */
void mock_hal_wifi_scanner_set_status(hal_status_t status);

/** @brief Check if WiFi scanner init was called. */
bool mock_hal_wifi_scanner_is_initialized(void);

/** @brief Check if WiFi scanner is currently scanning. */
bool mock_hal_wifi_scanner_is_scanning(void);

/* ========================================================================
 * BLE Scanner Mock Control
 * ======================================================================== */

/** @brief Reset BLE scanner mock to initial (inactive) state. */
void mock_hal_ble_scanner_reset(void);

/** @brief Override BLE scanner module status. */
void mock_hal_ble_scanner_set_status(hal_status_t status);

/** @brief Check if BLE scanner init was called. */
bool mock_hal_ble_scanner_is_initialized(void);

/** @brief Check if BLE scanner is currently scanning. */
bool mock_hal_ble_scanner_is_scanning(void);

/* ========================================================================
 * Buzzer Mock Control
 * ======================================================================== */

/** @brief Reset buzzer mock to initial (inactive) state. */
void mock_hal_buzzer_reset(void);

/** @brief Check if the buzzer mock is currently playing a tone. */
bool mock_hal_buzzer_is_playing(void);

/** @brief Get the last frequency played by the buzzer mock. */
uint32_t mock_hal_buzzer_get_last_freq(void);

/** @brief Get the last duration played by the buzzer mock. */
uint32_t mock_hal_buzzer_get_last_duration(void);

/** @brief Get the total number of play_tone calls. */
uint32_t mock_hal_buzzer_get_play_count(void);

/** @brief Get the total number of stop calls. */
uint32_t mock_hal_buzzer_get_stop_count(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_MOCKS_H */
