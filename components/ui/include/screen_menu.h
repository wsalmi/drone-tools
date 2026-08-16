/**
 * @file screen_menu.h
 * @brief Main Menu screen — navigation hub for all application screens.
 *
 * Presents the main menu with options to navigate to:
 *   - Scanner (aircraft detection list)
 *   - Mapa (2D map view)
 *   - Aeronaves (aircraft details)
 *   - Spectrum (spectrum analyzer)
 *   - Config (settings)
 *   - Log (data log / export)
 *
 * Navigation via UP/DOWN to select, ENTER to navigate to selected screen.
 *
 * Content area: y=14 to y=135 (below status bar)
 * Display: 240×135 px, RGB565
 *
 * Validates: Requirements 9.1
 */

#ifndef SCREEN_MENU_H
#define SCREEN_MENU_H

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
#define SCREEN_MENU_CONTENT_Y           14

/** @brief Number of menu items */
#define SCREEN_MENU_ITEM_COUNT          7

/** @brief Line height for menu items in pixels */
#define SCREEN_MENU_LINE_HEIGHT         14

/** @brief Left margin for menu text */
#define SCREEN_MENU_TEXT_X              20

/* ========================================================================
 * Types
 * ======================================================================== */

/**
 * @brief Menu item definition.
 */
typedef struct {
    const char *label;              /**< Display label */
    uint8_t target_screen;          /**< Target ui_screen_t to navigate to */
} menu_item_t;

/**
 * @brief Main menu screen internal state.
 */
typedef struct {
    bool initialized;               /**< Screen initialized flag */
    uint8_t selected_item;          /**< Currently highlighted menu item */
} screen_menu_state_t;

/* ========================================================================
 * API Functions
 * ======================================================================== */

/**
 * @brief Initialize the Main Menu screen.
 *
 * @return ESP_OK on success
 */
esp_err_t screen_menu_init(void);

/**
 * @brief Render the Main Menu screen.
 *
 * Displays all menu items with the selected item highlighted.
 *
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t screen_menu_render(void);

/**
 * @brief Handle key input on the Main Menu screen.
 *
 * - UP/DOWN: Navigate between menu items
 * - ENTER: Navigate to selected screen
 *
 * @param[in] key  Key event from ui_manager
 * @return ESP_OK on success
 */
esp_err_t screen_menu_handle_key(uint8_t key);

/**
 * @brief Deinitialize the Main Menu screen.
 *
 * @return ESP_OK on success
 */
esp_err_t screen_menu_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_MENU_H */
