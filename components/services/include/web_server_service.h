/**
 * @file web_server_service.h
 * @brief Wi-Fi SoftAP and HTTP Web Server service for wireless data export.
 */

#ifndef WEB_SERVER_SERVICE_H
#define WEB_SERVER_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the Web Server service.
 *
 * @return ESP_OK on success.
 */
esp_err_t web_server_service_init(void);

/**
 * @brief Start the SoftAP and HTTP server.
 *
 * Starts Wi-Fi SoftAP (SSID: DRONE-MON-XXXX) and serves web pages & file downloads.
 *
 * @return ESP_OK on success.
 */
esp_err_t web_server_service_start(void);

/**
 * @brief Stop the HTTP server and disable SoftAP.
 *
 * @return ESP_OK on success.
 */
esp_err_t web_server_service_stop(void);

/**
 * @brief Check if the Web Server is currently active.
 *
 * @return true if active, false otherwise.
 */
bool web_server_service_is_active(void);

/**
 * @brief Get the generated SoftAP SSID.
 *
 * @param[out] buf     Output buffer for SSID string.
 * @param[in]  buf_len Size of buffer.
 */
void web_server_service_get_ssid(char *buf, size_t buf_len);

/**
 * @brief Deinitialize the Web Server service.
 *
 * @return ESP_OK on success.
 */
esp_err_t web_server_service_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* WEB_SERVER_SERVICE_H */
