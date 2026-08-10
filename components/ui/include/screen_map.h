/**
 * @file screen_map.h
 * @brief Map Screen — 2D top-down positional view of aircraft and pilots.
 *
 * Renders a bird's-eye map view on the 240×135 px display with the
 * operator (monitor) centered. Aircraft and pilot positions are plotted
 * as icons at their relative positions based on the current map_scale_m
 * from ui_state. Scale is adjustable via zoom in/out keys (UP/DOWN).
 *
 * Content area: x=[0,239], y=[14,134] (below status bar)
 *
 * Validates: Requirements 9.4
 */

#ifndef SCREEN_MAP_H
#define SCREEN_MAP_H

#include <stdint.h>
#include "esp_err.h"
#include "aircraft_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Constants
 * ======================================================================== */

/** @brief Content area top Y coordinate (below status bar) */
#define MAP_CONTENT_Y_START     14

/** @brief Content area height in pixels */
#define MAP_CONTENT_HEIGHT      (135 - MAP_CONTENT_Y_START)

/** @brief Content area width in pixels */
#define MAP_CONTENT_WIDTH       240

/** @brief Center X of the map content area */
#define MAP_CENTER_X            (MAP_CONTENT_WIDTH / 2)

/** @brief Center Y of the map content area (relative to content start) */
#define MAP_CENTER_Y            (MAP_CONTENT_HEIGHT / 2)

/** @brief Operator icon size in pixels (cross-hair) */
#define MAP_OPERATOR_ICON_SIZE  5

/** @brief Aircraft icon size in pixels */
#define MAP_AIRCRAFT_ICON_SIZE  3

/** @brief Pilot icon size in pixels */
#define MAP_PILOT_ICON_SIZE     3

/* ========================================================================
 * Public API
 * ======================================================================== */

/**
 * @brief Render the map screen.
 *
 * Draws the 2D top-down map view with:
 *   - Operator cross-hair icon at center
 *   - Aircraft icons at relative positions (based on lat/lon delta → pixel)
 *   - Pilot icons at estimated positions (different visual style)
 *   - Scale indicator showing current range in meters
 *
 * Uses the current map_scale_m from ui_state to convert meters to pixels.
 * Positions outside the visible area are clipped (not drawn).
 *
 * @param[in] registry  Pointer to the aircraft registry (read-only access)
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_ARG if registry is NULL,
 *         ESP_ERR_INVALID_STATE if UI not initialized
 */
esp_err_t screen_map_render(const aircraft_registry_t *registry);

/**
 * @brief Render the map screen with no aircraft (empty state).
 *
 * Shows the operator icon, scale indicator, and a message indicating
 * no aircraft are detected.
 *
 * @return ESP_OK on success
 */
esp_err_t screen_map_render_empty(void);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_MAP_H */
