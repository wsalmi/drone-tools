/**
 * @file screen_spectrum.h
 * @brief Spectrum Analyzer screen — waterfall display with protocol frequency markers.
 *
 * Renders the spectrum analyzer view on the 240×135 display:
 *   - Power vs frequency graph (waterfall style)
 *   - Frequency markers overlay for known protocols (ELRS 900, ELRS 2.4G, DJI, WiFi)
 *   - Detected peaks with classification labels
 *   - Current configuration info (center freq, bandwidth, gain)
 *
 * Content area: y=14 to y=135 (below status bar)
 * Display: 240×135 px, RGB565
 *
 * Validates: Requirements 12.1, 12.4
 */

#ifndef SCREEN_SPECTRUM_H
#define SCREEN_SPECTRUM_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Constants
 * ======================================================================== */

/** @brief Content area top Y coordinate (below status bar) */
#define SCREEN_SPECTRUM_CONTENT_Y       14

/** @brief Spectrum graph height in pixels */
#define SCREEN_SPECTRUM_GRAPH_HEIGHT     90

/** @brief Waterfall history rows */
#define SCREEN_SPECTRUM_WATERFALL_ROWS   30

/** @brief Info bar height at bottom */
#define SCREEN_SPECTRUM_INFO_HEIGHT      18

/** @brief Minimum displayed power in dBm (graph floor) */
#define SCREEN_SPECTRUM_POWER_MIN_DBM    (-120)

/** @brief Maximum displayed power in dBm (graph ceiling) */
#define SCREEN_SPECTRUM_POWER_MAX_DBM    (-20)

/* ========================================================================
 * Types
 * ======================================================================== */

/**
 * @brief Spectrum screen internal state.
 */
typedef struct {
    bool initialized;               /**< Screen has been initialized */
    uint8_t waterfall_row_idx;      /**< Current waterfall insertion row */
} screen_spectrum_state_t;

/* ========================================================================
 * API Functions
 * ======================================================================== */

/**
 * @brief Initialize the Spectrum screen.
 *
 * Sets up internal state for waterfall rendering.
 *
 * @return ESP_OK on success
 */
esp_err_t screen_spectrum_init(void);

/**
 * @brief Render the Spectrum screen.
 *
 * Draws the power vs frequency graph using data from spectrum_analyzer,
 * overlays frequency markers for known protocols, and shows current
 * configuration info at the bottom.
 *
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t screen_spectrum_render(void);

/**
 * @brief Handle key input on the Spectrum screen.
 *
 * - LEFT/RIGHT: Unused (handled by ui_manager for screen switching)
 * - UP/DOWN: Adjust gain
 * - ENTER: Toggle between spectrum and waterfall modes
 * - BACK/MENU: Return to main menu
 *
 * @param[in] key  Key event from ui_manager
 * @return ESP_OK on success
 */
esp_err_t screen_spectrum_handle_key(uint8_t key);

/**
 * @brief Deinitialize the Spectrum screen.
 *
 * @return ESP_OK on success
 */
esp_err_t screen_spectrum_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_SPECTRUM_H */
