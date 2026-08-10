/**
 * @file ui_manager.h
 * @brief UI Manager — screen state, navigation, status bar, and notifications.
 *
 * Manages the top-level UI state for the drone telemetry monitor:
 *   - Screen navigation via keyboard input (latency ≤ 200ms)
 *   - Status bar rendering (module status, aircraft count, GPS, SD)
 *   - Notification system with automatic timeout (3s default)
 *   - State model for active screen, pagination, selection, and map scale
 *
 * Display: ST7789V2, 240×135 pixels, RGB565
 *
 * Validates: Requirements 9.1, 9.2, 9.5, 9.6, 10.6
 */

#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "hal_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Constants
 * ======================================================================== */

/** @brief Number of items per page in scanner view */
#define UI_PAGE_SIZE                5

/** @brief Default notification display duration in milliseconds */
#define UI_NOTIFICATION_DURATION_MS 3000

/** @brief Default map scale in meters */
#define UI_MAP_DEFAULT_SCALE_M      500.0f

/** @brief Map minimum zoom scale (meters) */
#define UI_MAP_MIN_SCALE_M          50.0f

/** @brief Map maximum zoom scale (meters) */
#define UI_MAP_MAX_SCALE_M          10000.0f

/** @brief Maximum notification text length */
#define UI_NOTIFICATION_TEXT_MAX    64

/** @brief Number of module status slots (LoRa, NRF24, SDR, GPS, SD) */
#define UI_MODULE_COUNT             5

/** @brief Status bar height in pixels */
#define UI_STATUS_BAR_HEIGHT        12

/** @brief Maximum acceptable UI input latency in milliseconds */
#define UI_MAX_INPUT_LATENCY_MS     200

/* Module indices for module_status array */
#define UI_MODULE_IDX_LORA          0
#define UI_MODULE_IDX_NRF24         1
#define UI_MODULE_IDX_SDR           2
#define UI_MODULE_IDX_GPS           3
#define UI_MODULE_IDX_SD            4

/* ========================================================================
 * Screen Enumeration
 * ======================================================================== */

/**
 * @brief Available UI screens.
 */
typedef enum {
    UI_SCREEN_SCANNER = 0,      /**< Scanner: paginated aircraft list */
    UI_SCREEN_MAP,              /**< Map: 2D positional view */
    UI_SCREEN_AIRCRAFT_LIST,    /**< Aircraft list (detailed) */
    UI_SCREEN_SPECTRUM,         /**< Spectrum analyzer waterfall */
    UI_SCREEN_SETTINGS,         /**< Configuration settings */
    UI_SCREEN_LOG,              /**< Log viewer / export */
    UI_SCREEN_MAIN_MENU,       /**< Main menu navigation */
    UI_SCREEN_COUNT             /**< Total number of screens */
} ui_screen_t;

/* ========================================================================
 * Key Input Enumeration
 * ======================================================================== */

/**
 * @brief Keyboard input events for UI navigation.
 */
typedef enum {
    UI_KEY_NONE = 0,
    UI_KEY_UP,
    UI_KEY_DOWN,
    UI_KEY_LEFT,
    UI_KEY_RIGHT,
    UI_KEY_ENTER,
    UI_KEY_BACK,
    UI_KEY_MENU
} ui_key_t;

/* ========================================================================
 * UI State Model
 * ======================================================================== */

/**
 * @brief Main UI state structure.
 *
 * Holds all runtime state needed by the UI system: current screen,
 * pagination, selection, map scale, active notification, and hardware
 * module status for the top status bar.
 */
typedef struct {
    /* Screen navigation */
    ui_screen_t current_screen;         /**< Currently active screen */
    ui_screen_t previous_screen;        /**< Previous screen for back navigation */

    /* Scanner pagination */
    uint8_t scanner_page;               /**< Current page (0-based) */
    uint8_t selected_aircraft_idx;      /**< Selected item index within page */

    /* Map state */
    float map_scale_m;                  /**< Map scale in meters (viewport radius) */

    /* Notification system */
    bool notification_visible;          /**< Whether a notification is currently shown */
    char notification_text[UI_NOTIFICATION_TEXT_MAX]; /**< Active notification text */
    uint32_t notification_expire_ms;    /**< Absolute tick when notification expires */
    uint32_t notification_duration_ms;  /**< Duration requested (used to compute expire) */
    bool notification_expire_set;       /**< Whether expire_ms has been resolved to absolute */

    /* Status bar data */
    hal_status_t module_status[UI_MODULE_COUNT]; /**< LoRa, NRF24, SDR, GPS, SD */
    uint8_t aircraft_count;             /**< Number of active aircraft */
    bool gps_fix_valid;                 /**< GPS has valid fix */
    bool sd_available;                  /**< SD card is mounted and writable */

    /* Internal */
    bool initialized;                   /**< Whether UI manager has been initialized */
} ui_state_t;

/* ========================================================================
 * Public API
 * ======================================================================== */

/**
 * @brief Initialize the UI Manager.
 *
 * Sets up the UI state to default values (MAIN_MENU screen, page 0,
 * default map scale, no notification, all modules INACTIVE).
 * Must be called after hal_display_init().
 *
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_STATE if display not initialized
 */
esp_err_t ui_manager_init(void);

/**
 * @brief Process a keyboard input event.
 *
 * Handles navigation between screens and within-screen actions.
 * Must complete within UI_MAX_INPUT_LATENCY_MS (200ms).
 *
 * @param[in] key  The key event to process
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t ui_manager_handle_key(ui_key_t key);

/**
 * @brief Navigate directly to a specific screen.
 *
 * Saves current screen as previous_screen for back navigation.
 *
 * @param[in] screen  Target screen to switch to
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_ARG if screen >= UI_SCREEN_COUNT,
 *         ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t ui_manager_navigate_to(ui_screen_t screen);

/**
 * @brief Show a notification with the specified text.
 *
 * Displays a notification overlay for UI_NOTIFICATION_DURATION_MS (3s).
 * If a notification is already visible, it is replaced.
 *
 * @param[in] text         Notification text (max UI_NOTIFICATION_TEXT_MAX-1 chars)
 * @param[in] duration_ms  Display duration in ms (0 for default 3000ms)
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_ARG if text is NULL,
 *         ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t ui_manager_show_notification(const char *text, uint32_t duration_ms);

/**
 * @brief Dismiss the current notification immediately.
 */
void ui_manager_dismiss_notification(void);

/**
 * @brief Update the notification system (check expiration).
 *
 * Should be called periodically (e.g., every UI render cycle) to
 * dismiss expired notifications.
 *
 * @param[in] current_tick_ms  Current system tick in milliseconds
 */
void ui_manager_update_notifications(uint32_t current_tick_ms);

/**
 * @brief Update the status bar module indicators.
 *
 * @param[in] lora_status   LoRa module status
 * @param[in] nrf24_status  NRF24 module status
 * @param[in] sdr_status    SDR module status
 * @param[in] gps_status    GPS module status
 * @param[in] sd_status     SD card status
 */
void ui_manager_update_module_status(hal_status_t lora_status,
                                     hal_status_t nrf24_status,
                                     hal_status_t sdr_status,
                                     hal_status_t gps_status,
                                     hal_status_t sd_status);

/**
 * @brief Update aircraft count shown in status bar.
 *
 * @param[in] count  Number of active aircraft
 */
void ui_manager_update_aircraft_count(uint8_t count);

/**
 * @brief Update GPS fix status shown in status bar.
 *
 * @param[in] fix_valid  true if GPS has valid fix
 */
void ui_manager_update_gps_fix(bool fix_valid);

/**
 * @brief Update SD card availability shown in status bar.
 *
 * @param[in] available  true if SD card is available
 */
void ui_manager_update_sd_status(bool available);

/**
 * @brief Render the status bar at the top of the display.
 *
 * Draws module status indicators, aircraft count, GPS fix icon,
 * and SD availability on the top UI_STATUS_BAR_HEIGHT pixels.
 *
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t ui_manager_render_status_bar(void);

/**
 * @brief Render the notification overlay (if visible).
 *
 * Draws the notification text at the bottom of the screen if
 * notification_visible is true.
 *
 * @return ESP_OK on success
 */
esp_err_t ui_manager_render_notification(void);

/**
 * @brief Get a read-only pointer to the current UI state.
 *
 * @return Pointer to the internal ui_state_t (never NULL after init)
 */
const ui_state_t *ui_manager_get_state(void);

/**
 * @brief Get the current screen.
 *
 * @return Current ui_screen_t value
 */
ui_screen_t ui_manager_get_current_screen(void);

/**
 * @brief Deinitialize the UI Manager.
 *
 * @return ESP_OK on success
 */
esp_err_t ui_manager_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_MANAGER_H */
