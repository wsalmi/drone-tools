/**
 * @file screen_settings.h
 * @brief Settings screen — alert and field-monitor configuration interface.
 *
 * Provides a navigable menu for configuring system parameters:
 *   - Alert settings: sound on/off, proximity threshold
 *   - Monitor settings: Wi-Fi/BLE cycle and SX1262 dwell time
 *
 * Navigation via UP/DOWN to move between items, ENTER to edit/toggle,
 * LEFT/RIGHT to adjust numeric values.
 *
 * Content area: y=14 to y=135 (below status bar)
 * Display: 240×135 px, RGB565
 *
 * Validates: Requirements 9.1, 12.2
 */

#ifndef SCREEN_SETTINGS_H
#define SCREEN_SETTINGS_H

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
#define SCREEN_SETTINGS_CONTENT_Y       14

/** @brief Maximum number of visible menu items per page */
#define SCREEN_SETTINGS_ITEMS_PER_PAGE  7

/** @brief Line height for settings items in pixels */
#define SCREEN_SETTINGS_LINE_HEIGHT     16

/* ========================================================================
 * Types
 * ======================================================================== */

/**
 * @brief Settings category enumeration.
 */
typedef enum {
    SETTINGS_CATEGORY_ALERT = 0,    /**< Alert configuration */
    SETTINGS_CATEGORY_MONITOR,      /**< Wi-Fi, BLE and SX1262 monitor timing */
    SETTINGS_CATEGORY_COUNT         /**< Total categories */
} settings_category_t;

/**
 * @brief Settings screen internal state.
 */
typedef struct {
    bool initialized;                   /**< Screen initialized flag */
    settings_category_t category;       /**< Current category being viewed */
    uint8_t selected_item;              /**< Currently highlighted item index */
    uint8_t scroll_offset;              /**< Scroll position for long menus */
    bool editing;                       /**< Currently editing a value */
} screen_settings_state_t;

/* ========================================================================
 * API Functions
 * ======================================================================== */

/**
 * @brief Initialize the Settings screen.
 *
 * @return ESP_OK on success
 */
esp_err_t screen_settings_init(void);

/**
 * @brief Render the Settings screen.
 *
 * Displays the current settings category with menu items.
 * Highlights the selected item and shows current values.
 *
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t screen_settings_render(void);

/**
 * @brief Handle key input on the Settings screen.
 *
 * - UP/DOWN: Navigate between items
 * - LEFT/RIGHT: Adjust value (when editing) or switch category
 * - ENTER: Toggle edit mode or toggle boolean values
 * - BACK/MENU: Return to main menu (or exit edit mode if editing)
 *
 * @param[in] key  Key event from ui_manager
 * @return ESP_OK on success
 */
esp_err_t screen_settings_handle_key(uint8_t key);

/**
 * @brief Deinitialize the Settings screen.
 *
 * @return ESP_OK on success
 */
esp_err_t screen_settings_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_SETTINGS_H */
