/**
 * @file hal_wifi_scanner.h
 * @brief HAL WiFi Scanner for RemoteID detection.
 *
 * Provides passive WiFi scanning in promiscuous mode to capture
 * WiFi NAN (Neighbor Awareness Networking) and Beacon frames
 * carrying ASTM F3411 RemoteID data.
 *
 * The scanner uses the ESP-IDF WiFi driver in promiscuous mode
 * to intercept management frames without associating to any AP.
 */

#ifndef HAL_WIFI_SCANNER_H
#define HAL_WIFI_SCANNER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "hal_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback type for received WiFi frames.
 *
 * Called when a WiFi frame of interest (NAN Action frame or Beacon
 * with Vendor Specific IE matching RemoteID OUI) is captured.
 *
 * @param frame   Pointer to the raw 802.11 frame data (including header).
 * @param len     Length of the frame in bytes.
 * @param rssi    Received signal strength in dBm.
 */
typedef void (*wifi_scan_callback_t)(const uint8_t *frame, uint16_t len, int8_t rssi);

/**
 * @brief Initialize the WiFi scanner subsystem.
 *
 * Initializes the ESP-IDF WiFi driver in station mode (no connection)
 * and prepares for promiscuous mode operation. Must be called before
 * hal_wifi_scanner_start().
 *
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_STATE if already initialized.
 * @return ESP_FAIL on WiFi driver initialization failure.
 */
esp_err_t hal_wifi_scanner_init(void);

/**
 * @brief Start WiFi scanning in promiscuous mode.
 *
 * Enables promiscuous mode to capture WiFi NAN Action frames and
 * Beacon frames carrying RemoteID Vendor Specific IEs. Captured
 * frames are delivered via the registered callback.
 *
 * The scanner listens on channels 1, 6, and 11 (2.4 GHz) by default,
 * cycling through them to maximize coverage.
 *
 * @param callback Function to call when a relevant frame is captured.
 *                 Must not be NULL.
 *
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_ARG if callback is NULL.
 * @return ESP_ERR_INVALID_STATE if not initialized or already started.
 */
esp_err_t hal_wifi_scanner_start(wifi_scan_callback_t callback);

/**
 * @brief Stop WiFi scanning.
 *
 * Disables promiscuous mode and stops frame capture. The previously
 * registered callback will no longer be invoked.
 *
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_STATE if not started.
 */
esp_err_t hal_wifi_scanner_stop(void);

/**
 * @brief Get the current status of the WiFi scanner.
 *
 * @return Current operational status.
 */
hal_status_t hal_wifi_scanner_get_status(void);

/**
 * @brief Deinitialize the WiFi scanner subsystem.
 *
 * Stops scanning if active, deinitializes the WiFi driver, and
 * releases all allocated resources.
 *
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_STATE if not initialized.
 */
esp_err_t hal_wifi_scanner_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_WIFI_SCANNER_H */
