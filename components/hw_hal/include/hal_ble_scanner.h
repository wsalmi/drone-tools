/**
 * @file hal_ble_scanner.h
 * @brief HAL BLE Scanner for RemoteID detection.
 *
 * Provides passive BLE scanning to capture Legacy Advertisement
 * packets (BLE 4.x/5.x) carrying ASTM F3411 RemoteID data.
 *
 * The scanner uses the NimBLE stack on ESP-IDF for passive BLE
 * observation without establishing connections.
 */

#ifndef HAL_BLE_SCANNER_H
#define HAL_BLE_SCANNER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "hal_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback type for received BLE advertisements.
 *
 * Called when a BLE Legacy Advertisement containing RemoteID
 * service data (UUID 0xFFFA per ASTM F3411) is captured.
 *
 * @param adv_data  Pointer to the raw advertisement data payload.
 * @param len       Length of the advertisement data in bytes.
 * @param rssi      Received signal strength in dBm.
 * @param addr      Pointer to the 6-byte BLE MAC address of the advertiser.
 */
typedef void (*ble_scan_callback_t)(const uint8_t *adv_data, uint16_t len, int8_t rssi, const uint8_t *addr);

/**
 * @brief Initialize the BLE scanner subsystem.
 *
 * Initializes the NimBLE host stack and prepares for passive
 * scanning. Must be called before hal_ble_scanner_start().
 *
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_STATE if already initialized.
 * @return ESP_FAIL on BLE stack initialization failure.
 */
esp_err_t hal_ble_scanner_init(void);

/**
 * @brief Start BLE passive scanning.
 *
 * Begins passive BLE scanning to capture Legacy Advertisement
 * packets. The scanner filters for advertisements containing
 * the RemoteID service UUID (0xFFFA). Matching packets are
 * delivered via the registered callback.
 *
 * @param callback Function to call when a RemoteID advertisement is captured.
 *                 Must not be NULL.
 *
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_ARG if callback is NULL.
 * @return ESP_ERR_INVALID_STATE if not initialized or already started.
 */
esp_err_t hal_ble_scanner_start(ble_scan_callback_t callback);

/**
 * @brief Stop BLE scanning.
 *
 * Stops the passive BLE scan. The previously registered callback
 * will no longer be invoked.
 *
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_STATE if not started.
 */
esp_err_t hal_ble_scanner_stop(void);

/**
 * @brief Get the current status of the BLE scanner.
 *
 * @return Current operational status.
 */
hal_status_t hal_ble_scanner_get_status(void);

/**
 * @brief Deinitialize the BLE scanner subsystem.
 *
 * Stops scanning if active, deinitializes the NimBLE stack, and
 * releases all allocated resources.
 *
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_STATE if not initialized.
 */
esp_err_t hal_ble_scanner_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_BLE_SCANNER_H */
