/**
 * @file screen_scanner.h
 * @brief Scanner Screen — paginated aircraft list display.
 *
 * Renders the aircraft registry entries in a paginated list (5 per page).
 * Each entry shows: RSSI (dBm), protocol name, distance (m or km), and
 * direction (degrees). Uses the ui_manager state for pagination
 * (scanner_page, selected_aircraft_idx).
 *
 * Display layout (240×135 px):
 *   - Status bar: y = 0..11 (rendered by ui_manager)
 *   - Content area: y = 14..134 (5 rows, ~24px each)
 *
 * Validates: Requirements 9.3, 9.6, 9.7
 */

#ifndef SCREEN_SCANNER_H
#define SCREEN_SCANNER_H

#include <stdint.h>
#include "esp_err.h"
#include "aircraft_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Constants
 * ======================================================================== */

/** @brief Number of items displayed per page */
#define SCANNER_ITEMS_PER_PAGE      5

/** @brief Y offset where content area begins (below status bar) */
#define SCANNER_CONTENT_Y_START     14

/** @brief Height in pixels of each list row */
#define SCANNER_ROW_HEIGHT          24

/* ========================================================================
 * Public API
 * ======================================================================== */

/**
 * @brief Render the scanner screen content area.
 *
 * Reads the aircraft registry, computes the current page from the UI
 * state, and draws up to SCANNER_ITEMS_PER_PAGE entries. If the registry
 * is empty, displays "Nenhuma aeronave detectada".
 *
 * The status bar is rendered separately by ui_manager.
 *
 * @param[in] registry  Pointer to the aircraft registry (must be initialized).
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_ARG if registry is NULL,
 *         ESP_ERR_INVALID_STATE if UI not initialized.
 */
esp_err_t screen_scanner_render(const aircraft_registry_t *registry);

/**
 * @brief Get the total number of pages for the current registry state.
 *
 * @param[in] active_count  Number of active aircraft.
 * @return Number of pages (0 if no aircraft, ceil(active_count/5) otherwise).
 */
uint8_t screen_scanner_get_total_pages(uint8_t active_count);

/**
 * @brief Get protocol name as a short string.
 *
 * @param[in] protocol  Protocol type enum value.
 * @return Null-terminated string (e.g., "ELRS", "DJI", "MAVLink").
 */
const char *screen_scanner_protocol_name(protocol_type_t protocol);

/**
 * @brief Format distance as human-readable string.
 *
 * Distances ≤ 1000 m are shown in meters (e.g., "450m").
 * Distances > 1000 m are shown in km with one decimal (e.g., "1.5km").
 *
 * @param[out] buf      Output buffer (must be at least 12 bytes).
 * @param[in]  buf_len  Size of output buffer.
 * @param[in]  dist_m   Distance in meters.
 */
void screen_scanner_format_distance(char *buf, uint8_t buf_len, float dist_m);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_SCANNER_H */
