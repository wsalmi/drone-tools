/**
 * @file screen_splash.h
 * @brief Splash screen with pixel-art logo displayed at firmware boot.
 *
 * Renders the "Drone Telemetry Monitor" logo centered on the display
 * for a configurable duration before transitioning to the main menu.
 *
 * Display: 240×135 px, RGB565
 */

#ifndef SCREEN_SPLASH_H
#define SCREEN_SPLASH_H

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Default splash screen display duration in milliseconds */
#define SPLASH_DURATION_MS  2500

/**
 * @brief Render the splash screen with the pixel-art logo.
 *
 * Draws the logo centered on the display with the project name
 * and version text below. Call hal_display_flush() after this
 * function to send the framebuffer to the display.
 *
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_STATE if display not initialized
 */
esp_err_t screen_splash_render(void);

/**
 * @brief Show the full splash sequence (render + delay + flush).
 *
 * Convenience function that renders the splash, flushes to display,
 * waits for SPLASH_DURATION_MS, and returns. Intended to be called
 * once during boot before the main UI loop starts.
 *
 * @param duration_ms  Duration to display splash (0 = use default 2500ms)
 * @return ESP_OK on success
 */
esp_err_t screen_splash_show(uint32_t duration_ms);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_SPLASH_H */
