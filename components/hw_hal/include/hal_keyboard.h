/**
 * @file hal_keyboard.h
 * @brief HAL driver for M5Stack Cardputer ADV keyboard matrix & input.
 */

#ifndef HAL_KEYBOARD_H
#define HAL_KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "ui_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the Cardputer keyboard matrix GPIOs and console reader.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t hal_keyboard_init(void);

/**
 * @brief Scan the keyboard matrix and read any pending key event.
 *
 * Checks both physical keyboard matrix (with debounce) and serial console input.
 *
 * @param[out] key  Pointer to receive the mapped ui_key_t event.
 * @return ESP_OK if scanned successfully (key may be UI_KEY_NONE if no key pressed).
 */
esp_err_t hal_keyboard_read(ui_key_t *key);

/**
 * @brief Inject a simulated key event (useful for tests or macro playback).
 *
 * @param key Key to inject.
 * @return ESP_OK on success.
 */
esp_err_t hal_keyboard_inject_key(ui_key_t key);

/**
 * @brief Deinitialize the keyboard driver.
 *
 * @return ESP_OK on success.
 */
esp_err_t hal_keyboard_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_KEYBOARD_H */
