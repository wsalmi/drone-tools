/**
 * @file ui_manager.c
 * @brief UI Manager implementation — screen state, navigation, status bar, notifications.
 *
 * Validates: Requirements 9.1, 9.2, 9.5, 9.6, 10.6
 */

#include "ui_manager.h"
#include "screen_splash.h"
#include "screen_menu.h"
#include "screen_hud.h"
#include "screen_modes.h"
#include "screen_spectrum.h"
#include "screen_settings.h"
#include "screen_log.h"
#include "hal_display.h"
#include <string.h>
#include <stdio.h>

/* ========================================================================
 * Internal State
 * ======================================================================== */

static ui_state_t s_ui_state;

/* ========================================================================
 * Status bar layout constants (240px wide, 12px tall)
 *
 * Layout: [LoRa][NRF][SDR] | AC:XX | [GPS] | [SD]
 * Each module indicator: 3 chars wide × 6px font = 18px + 2px gap = 20px
 * Module block: 5 modules × ~20px = ~100px
 * Aircraft count: "AC:XX" = 30px
 * GPS indicator: "GPS" = 18px
 * SD indicator: "SD" = 12px
 * Separators and padding fill remaining space
 * ======================================================================== */

#define STATUS_MODULE_X_START   2
#define STATUS_MODULE_SPACING   24
#define STATUS_AIRCRAFT_X       128
#define STATUS_GPS_X            180
#define STATUS_SD_X             216

/* Notification overlay position (bottom of screen) */
#define NOTIFICATION_Y          (HAL_DISPLAY_HEIGHT - 14)
#define NOTIFICATION_X          4
#define NOTIFICATION_BG_COLOR   HAL_COLOR_BLUE
#define NOTIFICATION_FG_COLOR   HAL_COLOR_WHITE

/* Menu item labels for main menu navigation (used by screen renderers) */
__attribute__((unused))
static const char *s_screen_labels[] = {
    "Scanner",
    "Mapa",
    "Aeronaves",
    "Spectrum",
    "Config",
    "Log",
    "Menu"
};

/* Short labels for module status bar */
static const char *s_module_labels[] = {
    "LR",   /* LoRa */
    "NR",   /* NRF24 */
    "SD",   /* SDR (using SD to save space, context makes it clear) */
    "GP",   /* GPS */
    "uSD"   /* SD card */
};

/* ========================================================================
 * Helper: get color for module status
 * ======================================================================== */

static uint16_t get_status_color(hal_status_t status)
{
    switch (status) {
        case HAL_STATUS_ACTIVE:       return HAL_COLOR_GREEN;
        case HAL_STATUS_INITIALIZING: return HAL_COLOR_YELLOW;
        case HAL_STATUS_ERROR:        return HAL_COLOR_RED;
        case HAL_STATUS_INACTIVE:
        default:                      return 0x7BEF; /* Gray (RGB565) */
    }
}

/* ========================================================================
 * Main Menu Navigation Logic
 * ======================================================================== */

/**
 * @brief Get the next screen in the menu order (wrapping).
 */
static ui_screen_t next_screen(ui_screen_t current)
{
    int next = (int)current + 1;
    if (next >= (int)UI_SCREEN_COUNT) {
        next = 0;
    }
    return (ui_screen_t)next;
}

/**
 * @brief Get the previous screen in the menu order (wrapping).
 */
static ui_screen_t prev_screen(ui_screen_t current)
{
    int prev = (int)current - 1;
    if (prev < 0) {
        prev = (int)UI_SCREEN_COUNT - 1;
    }
    return (ui_screen_t)prev;
}

/* ========================================================================
 * Key handling per screen
 * ======================================================================== */

static void handle_key_main_menu(ui_key_t key)
{
    screen_menu_handle_key((uint8_t)key);
}

static void handle_key_scanner(ui_key_t key)
{
    switch (key) {
        case UI_KEY_UP:
            if (s_ui_state.selected_aircraft_idx > 0) {
                s_ui_state.selected_aircraft_idx--;
            }
            break;
        case UI_KEY_DOWN:
            if (s_ui_state.selected_aircraft_idx < (UI_PAGE_SIZE - 1)) {
                s_ui_state.selected_aircraft_idx++;
            }
            break;
        case UI_KEY_LEFT:
            if (s_ui_state.scanner_page > 0) {
                s_ui_state.scanner_page--;
                s_ui_state.selected_aircraft_idx = 0;
            }
            break;
        case UI_KEY_RIGHT:
            /* Page forward — actual max pages determined by aircraft count at render time */
            s_ui_state.scanner_page++;
            s_ui_state.selected_aircraft_idx = 0;
            break;
        case UI_KEY_BACK:
        case UI_KEY_MENU:
            s_ui_state.previous_screen = s_ui_state.current_screen;
            s_ui_state.current_screen = UI_SCREEN_MAIN_MENU;
            break;
        default:
            break;
    }
}

static void handle_key_map(ui_key_t key)
{
    switch (key) {
        case UI_KEY_UP:
            /* Zoom in — decrease scale */
            if (s_ui_state.map_scale_m > UI_MAP_MIN_SCALE_M) {
                s_ui_state.map_scale_m *= 0.5f;
                if (s_ui_state.map_scale_m < UI_MAP_MIN_SCALE_M) {
                    s_ui_state.map_scale_m = UI_MAP_MIN_SCALE_M;
                }
            }
            break;
        case UI_KEY_DOWN:
            /* Zoom out — increase scale */
            if (s_ui_state.map_scale_m < UI_MAP_MAX_SCALE_M) {
                s_ui_state.map_scale_m *= 2.0f;
                if (s_ui_state.map_scale_m > UI_MAP_MAX_SCALE_M) {
                    s_ui_state.map_scale_m = UI_MAP_MAX_SCALE_M;
                }
            }
            break;
        case UI_KEY_BACK:
        case UI_KEY_MENU:
            s_ui_state.previous_screen = s_ui_state.current_screen;
            s_ui_state.current_screen = UI_SCREEN_MAIN_MENU;
            break;
        default:
            break;
    }
}

static void handle_key_generic(ui_key_t key)
{
    /* Generic handler for screens without specialized navigation */
    switch (key) {
        case UI_KEY_BACK:
        case UI_KEY_MENU:
        case UI_KEY_ENTER:
        case UI_KEY_SPACE:
            s_ui_state.previous_screen = s_ui_state.current_screen;
            s_ui_state.current_screen = UI_SCREEN_MAIN_MENU;
            break;
        case UI_KEY_LEFT:
        case UI_KEY_UP:
            s_ui_state.previous_screen = s_ui_state.current_screen;
            s_ui_state.current_screen = prev_screen(s_ui_state.current_screen);
            break;
        case UI_KEY_RIGHT:
        case UI_KEY_DOWN:
            s_ui_state.previous_screen = s_ui_state.current_screen;
            s_ui_state.current_screen = next_screen(s_ui_state.current_screen);
            break;
        default:
            break;
    }
}

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

esp_err_t ui_manager_init(void)
{
    memset(&s_ui_state, 0, sizeof(ui_state_t));

    /* Initialize all screen subsystems */
    screen_menu_init();
    screen_hud_init();
    screen_modes_init();
    screen_spectrum_init();
    screen_settings_init();
    screen_log_init();

    /* Show splash screen at boot */
    screen_splash_show(0);

    s_ui_state.current_screen = UI_SCREEN_MAIN_MENU;
    s_ui_state.previous_screen = UI_SCREEN_MAIN_MENU;
    s_ui_state.scanner_page = 0;
    s_ui_state.selected_aircraft_idx = 0;
    s_ui_state.map_scale_m = UI_MAP_DEFAULT_SCALE_M;
    s_ui_state.notification_visible = false;
    s_ui_state.notification_text[0] = '\0';
    s_ui_state.notification_expire_ms = 0;
    s_ui_state.aircraft_count = 0;
    s_ui_state.gps_fix_valid = false;
    s_ui_state.sd_available = false;

    for (int i = 0; i < UI_MODULE_COUNT; i++) {
        s_ui_state.module_status[i] = HAL_STATUS_INACTIVE;
    }

    s_ui_state.initialized = true;
    return ESP_OK;
}

esp_err_t ui_manager_handle_key(ui_key_t key)
{
    if (!s_ui_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (key == UI_KEY_NONE) {
        return ESP_OK;
    }

    /* Global direct single-key shortcuts (1-7) */
    switch (key) {
        case UI_KEY_1:
            return ui_manager_navigate_to(UI_SCREEN_SCANNER);
        case UI_KEY_2:
            return ui_manager_navigate_to(UI_SCREEN_MAP);
        case UI_KEY_3:
            return ui_manager_navigate_to(UI_SCREEN_HUD);
        case UI_KEY_4:
            return ui_manager_navigate_to(UI_SCREEN_SPECTRUM);
        case UI_KEY_5:
            return ui_manager_navigate_to(UI_SCREEN_MODES);
        case UI_KEY_6:
            return ui_manager_navigate_to(UI_SCREEN_SETTINGS);
        case UI_KEY_7:
            return ui_manager_navigate_to(UI_SCREEN_LOG);
        case UI_KEY_MENU:
            return ui_manager_navigate_to(UI_SCREEN_MAIN_MENU);
        case UI_KEY_BACK:
            if (s_ui_state.current_screen != UI_SCREEN_MAIN_MENU) {
                return ui_manager_navigate_to(UI_SCREEN_MAIN_MENU);
            }
            break;
        case UI_KEY_TAB:
            return ui_manager_navigate_to(next_screen(s_ui_state.current_screen));
        default:
            break;
    }

    switch (s_ui_state.current_screen) {
        case UI_SCREEN_MAIN_MENU:
            handle_key_main_menu(key);
            break;
        case UI_SCREEN_SCANNER:
            handle_key_scanner(key);
            break;
        case UI_SCREEN_MAP:
            handle_key_map(key);
            break;
        case UI_SCREEN_HUD:
        case UI_SCREEN_MODES:
        case UI_SCREEN_AIRCRAFT_LIST:
        case UI_SCREEN_SPECTRUM:
        case UI_SCREEN_SETTINGS:
        case UI_SCREEN_LOG:
        default:
            handle_key_generic(key);
            break;
    }

    return ESP_OK;
}

esp_err_t ui_manager_navigate_to(ui_screen_t screen)
{
    if (!s_ui_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if ((int)screen >= (int)UI_SCREEN_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    s_ui_state.previous_screen = s_ui_state.current_screen;
    s_ui_state.current_screen = screen;
    return ESP_OK;
}

esp_err_t ui_manager_show_notification(const char *text, uint32_t duration_ms)
{
    if (!s_ui_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Use default duration if 0 */
    if (duration_ms == 0) {
        duration_ms = UI_NOTIFICATION_DURATION_MS;
    }

    strncpy(s_ui_state.notification_text, text, UI_NOTIFICATION_TEXT_MAX - 1);
    s_ui_state.notification_text[UI_NOTIFICATION_TEXT_MAX - 1] = '\0';
    s_ui_state.notification_visible = true;
    s_ui_state.notification_duration_ms = duration_ms;
    s_ui_state.notification_expire_ms = 0;
    s_ui_state.notification_expire_set = false;

    return ESP_OK;
}

void ui_manager_dismiss_notification(void)
{
    s_ui_state.notification_visible = false;
    s_ui_state.notification_text[0] = '\0';
    s_ui_state.notification_expire_ms = 0;
    s_ui_state.notification_duration_ms = 0;
    s_ui_state.notification_expire_set = false;
}

void ui_manager_update_notifications(uint32_t current_tick_ms)
{
    if (!s_ui_state.notification_visible) {
        return;
    }

    /* On first update after show_notification, resolve the absolute expire time */
    if (!s_ui_state.notification_expire_set) {
        s_ui_state.notification_expire_ms = current_tick_ms + s_ui_state.notification_duration_ms;
        s_ui_state.notification_expire_set = true;
        return;
    }

    if (current_tick_ms >= s_ui_state.notification_expire_ms) {
        ui_manager_dismiss_notification();
    }
}

void ui_manager_update_module_status(hal_status_t lora_status,
                                     hal_status_t nrf24_status,
                                     hal_status_t sdr_status,
                                     hal_status_t gps_status,
                                     hal_status_t sd_status)
{
    s_ui_state.module_status[UI_MODULE_IDX_LORA] = lora_status;
    s_ui_state.module_status[UI_MODULE_IDX_NRF24] = nrf24_status;
    s_ui_state.module_status[UI_MODULE_IDX_SDR] = sdr_status;
    s_ui_state.module_status[UI_MODULE_IDX_GPS] = gps_status;
    s_ui_state.module_status[UI_MODULE_IDX_SD] = sd_status;

    /* Derive convenience flags */
    s_ui_state.gps_fix_valid = (gps_status == HAL_STATUS_ACTIVE);
    s_ui_state.sd_available = (sd_status == HAL_STATUS_ACTIVE);
}

void ui_manager_update_aircraft_count(uint8_t count)
{
    s_ui_state.aircraft_count = count;
}

void ui_manager_update_gps_fix(bool fix_valid)
{
    s_ui_state.gps_fix_valid = fix_valid;
}

void ui_manager_update_sd_status(bool available)
{
    s_ui_state.sd_available = available;
}

esp_err_t ui_manager_render_status_bar(void)
{
    if (!s_ui_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Clear status bar area */
    hal_display_draw_rect(0, 0, HAL_DISPLAY_WIDTH, UI_STATUS_BAR_HEIGHT,
                          HAL_COLOR_BLACK, true);

    /* Draw module status indicators */
    for (int i = 0; i < UI_MODULE_COUNT; i++) {
        uint16_t x = STATUS_MODULE_X_START + (i * STATUS_MODULE_SPACING);
        uint16_t color = get_status_color(s_ui_state.module_status[i]);
        hal_display_draw_text(x, 2, s_module_labels[i], color, HAL_COLOR_BLACK);
    }

    /* Draw aircraft count */
    char ac_buf[8];
    snprintf(ac_buf, sizeof(ac_buf), "A:%u", s_ui_state.aircraft_count);
    hal_display_draw_text(STATUS_AIRCRAFT_X, 2, ac_buf, HAL_COLOR_WHITE, HAL_COLOR_BLACK);

    /* Draw GPS fix indicator */
    uint16_t gps_color = s_ui_state.gps_fix_valid ? HAL_COLOR_GREEN : HAL_COLOR_RED;
    hal_display_draw_text(STATUS_GPS_X, 2, "GPS", gps_color, HAL_COLOR_BLACK);

    /* Draw SD status indicator */
    uint16_t sd_color = s_ui_state.sd_available ? HAL_COLOR_GREEN : HAL_COLOR_RED;
    hal_display_draw_text(STATUS_SD_X, 2, "SD", sd_color, HAL_COLOR_BLACK);

    return ESP_OK;
}

esp_err_t ui_manager_render_notification(void)
{
    if (!s_ui_state.notification_visible) {
        return ESP_OK;
    }

    /* Draw notification background bar at bottom of screen */
    hal_display_draw_rect(0, NOTIFICATION_Y - 2, HAL_DISPLAY_WIDTH, 14,
                          NOTIFICATION_BG_COLOR, true);

    /* Draw notification text */
    hal_display_draw_text(NOTIFICATION_X, NOTIFICATION_Y,
                          s_ui_state.notification_text,
                          NOTIFICATION_FG_COLOR, NOTIFICATION_BG_COLOR);

    return ESP_OK;
}

const ui_state_t *ui_manager_get_state(void)
{
    return &s_ui_state;
}

ui_screen_t ui_manager_get_current_screen(void)
{
    return s_ui_state.current_screen;
}

esp_err_t ui_manager_deinit(void)
{
    s_ui_state.initialized = false;
    memset(&s_ui_state, 0, sizeof(ui_state_t));
    return ESP_OK;
}
