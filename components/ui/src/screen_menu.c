/**
 * @file screen_menu.c
 * @brief Main Menu screen implementation — navigation hub for all screens.
 *
 * Displays the main menu with 6 options (Scanner, Mapa, Aeronaves,
 * Spectrum, Config, Log) and handles navigation to the selected screen.
 *
 * Validates: Requirements 9.1
 */

#include "screen_menu.h"
#include "hal_display.h"
#include "ui_manager.h"
#include <string.h>
#include <stdio.h>

/* ========================================================================
 * Internal State
 * ======================================================================== */

static screen_menu_state_t s_state = {0};

/* ========================================================================
 * Menu Items Definition
 * ======================================================================== */

static const menu_item_t s_menu_items[SCREEN_MENU_ITEM_COUNT] = {
    { .label = "1. Scanner",         .target_screen = (uint8_t)UI_SCREEN_SCANNER },
    { .label = "2. Mapa",            .target_screen = (uint8_t)UI_SCREEN_MAP },
    { .label = "3. HUD Tatico",      .target_screen = (uint8_t)UI_SCREEN_HUD },
    { .label = "4. Monitor RF",      .target_screen = (uint8_t)UI_SCREEN_SPECTRUM },
    { .label = "5. Modos / Sensores",.target_screen = (uint8_t)UI_SCREEN_MODES },
    { .label = "6. Configuracoes",   .target_screen = (uint8_t)UI_SCREEN_SETTINGS },
    { .label = "7. Logs & Status",   .target_screen = (uint8_t)UI_SCREEN_LOG },
};

/* Menu item descriptions (secondary text) */
static const char *s_menu_descriptions[SCREEN_MENU_ITEM_COUNT] = {
    "Varredura e lista",
    "Vista 2D posicional",
    "Radar e telemetria",
    "Canais WiFi, BLE, LoRa",
    "Interruptores rapidos",
    "Parametros do sistema",
    "Registros e KML"
};

/* ========================================================================
 * Public API
 * ======================================================================== */

esp_err_t screen_menu_init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_state.initialized = true;
    s_state.selected_item = 0;
    return ESP_OK;
}

esp_err_t screen_menu_render(void)
{
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Clear content area */
    hal_display_draw_rect(0, SCREEN_MENU_CONTENT_Y,
                          HAL_DISPLAY_WIDTH,
                          HAL_DISPLAY_HEIGHT - SCREEN_MENU_CONTENT_Y,
                          HAL_COLOR_BLACK, true);

    /* Draw title */
    hal_display_draw_text(4, SCREEN_MENU_CONTENT_Y + 2, "DRONE MONITOR",
                          HAL_COLOR_CYAN, HAL_COLOR_BLACK);

    /* Draw menu items */
    uint16_t y = SCREEN_MENU_CONTENT_Y + 14;

    for (uint8_t i = 0; i < SCREEN_MENU_ITEM_COUNT; i++) {
        uint16_t fg_color = HAL_COLOR_WHITE;
        uint16_t bg_color = HAL_COLOR_BLACK;

        if (i == s_state.selected_item) {
            /* Highlight selected item */
            bg_color = 0x2104; /* dark gray */
            fg_color = HAL_COLOR_YELLOW;
            hal_display_draw_rect(0, y - 1, HAL_DISPLAY_WIDTH, SCREEN_MENU_LINE_HEIGHT,
                                  bg_color, true);

            /* Draw selection indicator */
            hal_display_draw_text(4, y, ">", HAL_COLOR_GREEN, bg_color);
        }

        /* Draw menu label */
        hal_display_draw_text(SCREEN_MENU_TEXT_X, y, s_menu_items[i].label,
                              fg_color, bg_color);

        /* Draw description for selected item */
        if (i == s_state.selected_item) {
            uint16_t desc_x = SCREEN_MENU_TEXT_X + (uint16_t)(strlen(s_menu_items[i].label) * 6) + 12;
            if (desc_x < HAL_DISPLAY_WIDTH - 10) {
                hal_display_draw_text(desc_x, y, s_menu_descriptions[i],
                                      HAL_COLOR_CYAN, bg_color);
            }
        }

        y += SCREEN_MENU_LINE_HEIGHT;
    }

    return ESP_OK;
}

esp_err_t screen_menu_handle_key(uint8_t key)
{
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    switch ((ui_key_t)key) {
        case UI_KEY_UP:
            if (s_state.selected_item > 0) {
                s_state.selected_item--;
            } else {
                /* Wrap around to last item */
                s_state.selected_item = SCREEN_MENU_ITEM_COUNT - 1;
            }
            break;

        case UI_KEY_DOWN:
            if (s_state.selected_item < SCREEN_MENU_ITEM_COUNT - 1) {
                s_state.selected_item++;
            } else {
                /* Wrap around to first item */
                s_state.selected_item = 0;
            }
            break;

        case UI_KEY_ENTER:
        case UI_KEY_SPACE:
            /* Navigate to the target screen */
            ui_manager_navigate_to((ui_screen_t)s_menu_items[s_state.selected_item].target_screen);
            break;

        case UI_KEY_1:
            ui_manager_navigate_to(UI_SCREEN_SCANNER);
            break;
        case UI_KEY_2:
            ui_manager_navigate_to(UI_SCREEN_MAP);
            break;
        case UI_KEY_3:
            ui_manager_navigate_to(UI_SCREEN_HUD);
            break;
        case UI_KEY_4:
            ui_manager_navigate_to(UI_SCREEN_SPECTRUM);
            break;
        case UI_KEY_5:
            ui_manager_navigate_to(UI_SCREEN_MODES);
            break;
        case UI_KEY_6:
            ui_manager_navigate_to(UI_SCREEN_SETTINGS);
            break;
        case UI_KEY_7:
            ui_manager_navigate_to(UI_SCREEN_LOG);
            break;

        default:
            break;
    }

    return ESP_OK;
}

esp_err_t screen_menu_deinit(void)
{
    s_state.initialized = false;
    return ESP_OK;
}
