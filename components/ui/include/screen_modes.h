/**
 * @file screen_modes.h
 * @brief Modes & Sensors Quick Toggles screen.
 */

#ifndef SCREEN_MODES_H
#define SCREEN_MODES_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MODE_ITEM_REMOTEID = 0,
    MODE_ITEM_LORA,
    MODE_ITEM_NRF24,
    MODE_ITEM_SDR,
    MODE_ITEM_GPS,
    MODE_ITEM_BUZZER,
    MODE_ITEM_WEBSERVER,
    MODE_ITEM_SIMULATION,
    MODE_ITEM_COUNT
} mode_item_t;

/**
 * @brief Initialize the Modes screen state.
 *
 * @return ESP_OK on success.
 */
esp_err_t screen_modes_init(void);

/**
 * @brief Render the Modes / Sensors Quick Toggles screen.
 *
 * @return ESP_OK on success.
 */
esp_err_t screen_modes_render(void);

/**
 * @brief Handle keyboard navigation on the Modes screen.
 *
 * @param key Key code.
 * @return ESP_OK on success.
 */
esp_err_t screen_modes_handle_key(uint8_t key);

/**
 * @brief Check if a specific mode is currently enabled.
 *
 * @param item The mode item to query.
 * @return true if enabled, false otherwise.
 */
bool screen_modes_is_enabled(mode_item_t item);

/**
 * @brief Set the enabled state of a specific mode.
 *
 * @param item The mode item.
 * @param enabled New state.
 */
void screen_modes_set_enabled(mode_item_t item, bool enabled);

/**
 * @brief Deinitialize the Modes screen.
 *
 * @return ESP_OK on success.
 */
esp_err_t screen_modes_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_MODES_H */
