/**
 * @file screen_log.h
 * @brief Log screen — display recent log records with KML export option.
 *
 * Shows the most recent log records in a scrollable list and provides
 * an option to trigger KML export of the current session.
 *
 * Features:
 *   - Scrollable list of recent log entries
 *   - Displays: timestamp, aircraft ID, protocol, event type
 *   - "Export KML" action accessible via ENTER key
 *   - Export progress/status indicator
 *
 * Content area: y=14 to y=135 (below status bar)
 * Display: 240×135 px, RGB565
 *
 * Validates: Requirements 11.2
 */

#ifndef SCREEN_LOG_H
#define SCREEN_LOG_H

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
#define SCREEN_LOG_CONTENT_Y            14

/** @brief Maximum visible log entries per page */
#define SCREEN_LOG_ITEMS_PER_PAGE       7

/** @brief Line height for log entries in pixels */
#define SCREEN_LOG_LINE_HEIGHT          16

/** @brief Maximum number of recent log records kept for display */
#define SCREEN_LOG_MAX_RECORDS          50

/* ========================================================================
 * Types
 * ======================================================================== */

/**
 * @brief Log screen internal state.
 */
typedef struct {
    bool initialized;               /**< Screen initialized flag */
    uint8_t selected_item;          /**< Currently highlighted item index */
    uint8_t scroll_offset;          /**< Scroll position */
    uint8_t total_records;          /**< Number of records available */
    bool export_in_progress;        /**< KML export currently running */
    bool export_complete;           /**< Last export completed successfully */
} screen_log_state_t;

/* ========================================================================
 * API Functions
 * ======================================================================== */

/**
 * @brief Initialize the Log screen.
 *
 * @return ESP_OK on success
 */
esp_err_t screen_log_init(void);

/**
 * @brief Render the Log screen.
 *
 * Displays the list of recent log records with scrolling support.
 * Shows export status and the "Export KML" option at the top.
 *
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t screen_log_render(void);

/**
 * @brief Handle key input on the Log screen.
 *
 * - UP/DOWN: Scroll through log entries
 * - ENTER: Trigger KML export (when on export option)
 * - BACK/MENU: Return to main menu
 *
 * @param[in] key  Key event from ui_manager
 * @return ESP_OK on success
 */
esp_err_t screen_log_handle_key(uint8_t key);

/**
 * @brief Deinitialize the Log screen.
 *
 * @return ESP_OK on success
 */
esp_err_t screen_log_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_LOG_H */
