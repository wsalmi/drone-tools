/**
 * @file screen_hud.h
 * @brief Dynamic Real-Time HUD Dashboard Screen.
 */

#ifndef SCREEN_HUD_H
#define SCREEN_HUD_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "aircraft_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the HUD screen state.
 *
 * @return ESP_OK on success.
 */
esp_err_t screen_hud_init(void);

/**
 * @brief Render the Dynamic HUD Dashboard on the display.
 *
 * Draws live polar radar with sweep and blips, drone telemetry silhouette,
 * signal levels, altitude/speed/battery stats, and real sensor statuses.
 *
 * @param[in] registry  Pointer to global aircraft registry (can be NULL for demo/empty).
 * @return ESP_OK on success.
 */
esp_err_t screen_hud_render(const aircraft_registry_t *registry);

/**
 * @brief Handle keyboard input while on HUD screen.
 *
 * @param key UI key event.
 * @return ESP_OK on success.
 */
esp_err_t screen_hud_handle_key(uint8_t key);

/**
 * @brief Deinitialize the HUD screen.
 *
 * @return ESP_OK on success.
 */
esp_err_t screen_hud_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_HUD_H */
