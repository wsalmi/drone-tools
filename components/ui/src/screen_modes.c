/**
 * @file screen_modes.c
 * @brief Modes & Sensors Quick Toggles Screen implementation.
 */

#include "screen_modes.h"
#include "hal_display.h"
#include "ui_manager.h"
#include "alert_engine.h"
#include <stdio.h>
#include <string.h>

#define MODES_LINE_HEIGHT  14
#define MODES_START_Y      24

static bool s_modes_initialized = false;
static uint8_t s_selected_idx = 0;
static uint8_t s_scroll_offset = 0;

static bool s_modes_state[MODE_ITEM_COUNT] = {
    [MODE_ITEM_REMOTEID]   = true,
    [MODE_ITEM_LORA]       = true,
    [MODE_ITEM_NRF24]      = false,
    [MODE_ITEM_SDR]        = true,
    [MODE_ITEM_GPS]        = true,
    [MODE_ITEM_BUZZER]     = true,
    [MODE_ITEM_WEBSERVER]  = false,
    [MODE_ITEM_SIMULATION] = false,
};

static const char *s_mode_labels[MODE_ITEM_COUNT] = {
    "1. RemoteID (WiFi/BLE)",
    "2. LoRa 900MHz (SX1262)",
    "3. NRF24 2.4GHz Scanner",
    "4. RTL-SDR Spectrum",
    "5. GPS Monitor",
    "6. Buzzer / Alertas",
    "7. Wi-Fi AP & Web Server",
    "8. Modo Demo / Simulacao"
};

esp_err_t screen_modes_init(void)
{
    s_modes_initialized = true;
    s_selected_idx = 0;
    s_scroll_offset = 0;
    return ESP_OK;
}

bool screen_modes_is_enabled(mode_item_t item)
{
    if ((int)item >= 0 && item < MODE_ITEM_COUNT) {
        return s_modes_state[item];
    }
    return false;
}

void screen_modes_set_enabled(mode_item_t item, bool enabled)
{
    if ((int)item >= 0 && item < MODE_ITEM_COUNT) {
        s_modes_state[item] = enabled;
        if (item == MODE_ITEM_BUZZER) {
            alert_engine_set_silent(!enabled);
        }
    }
}

esp_err_t screen_modes_render(void)
{
    if (!s_modes_initialized) {
        screen_modes_init();
    }

    /* Clear content area (below status bar) */
    hal_display_draw_rect(0, UI_STATUS_BAR_HEIGHT, HAL_DISPLAY_WIDTH,
                          HAL_DISPLAY_HEIGHT - UI_STATUS_BAR_HEIGHT, HAL_COLOR_BLACK, true);

    /* Header banner */
    hal_display_draw_rect(0, UI_STATUS_BAR_HEIGHT, HAL_DISPLAY_WIDTH, 12, 0x0841, true);
    hal_display_draw_text(4, UI_STATUS_BAR_HEIGHT + 2, "MODOS & SENSORES", HAL_COLOR_CYAN, 0x0841);
    hal_display_draw_text(150, UI_STATUS_BAR_HEIGHT + 2, "[ENTER] TOGGLE", HAL_COLOR_YELLOW, 0x0841);

    /* Render visible items (up to 7 items fit on screen) */
    const uint8_t max_visible = 6;
    if (s_selected_idx < s_scroll_offset) {
        s_scroll_offset = s_selected_idx;
    } else if (s_selected_idx >= s_scroll_offset + max_visible) {
        s_scroll_offset = s_selected_idx - max_visible + 1;
    }

    uint16_t y = MODES_START_Y + 2;
    for (uint8_t i = s_scroll_offset; i < MODE_ITEM_COUNT && (i - s_scroll_offset) < max_visible; i++) {
        bool selected = (i == s_selected_idx);
        uint16_t row_bg = selected ? 0x2104 : HAL_COLOR_BLACK;
        uint16_t text_color = selected ? HAL_COLOR_YELLOW : HAL_COLOR_WHITE;

        hal_display_draw_rect(0, y - 1, HAL_DISPLAY_WIDTH, MODES_LINE_HEIGHT, row_bg, true);

        /* Indicator arrow */
        if (selected) {
            hal_display_draw_text(2, y + 2, ">", HAL_COLOR_GREEN, row_bg);
        }

        /* Label */
        hal_display_draw_text(10, y + 2, s_mode_labels[i], text_color, row_bg);

        /* Status badge */
        bool active = s_modes_state[i];
        const char *status_str = active ? "[ON ]" : "[OFF]";
        uint16_t badge_col = active ? HAL_COLOR_GREEN : 0x7BEF;
        hal_display_draw_text(HAL_DISPLAY_WIDTH - 42, y + 2, status_str, badge_col, row_bg);

        y += MODES_LINE_HEIGHT;
    }

    /* Footer instructions */
    hal_display_draw_rect(0, HAL_DISPLAY_HEIGHT - 14, HAL_DISPLAY_WIDTH, 14, 0x1084, true);
    hal_display_draw_text(4, HAL_DISPLAY_HEIGHT - 11, "SELECIONE E PRESSIONE ENTER", 0xBDF7, 0x1084);

    return ESP_OK;
}

esp_err_t screen_modes_handle_key(uint8_t key)
{
    switch ((ui_key_t)key) {
        case UI_KEY_UP:
            if (s_selected_idx > 0) {
                s_selected_idx--;
            } else {
                s_selected_idx = MODE_ITEM_COUNT - 1;
            }
            break;
        case UI_KEY_DOWN:
            if (s_selected_idx < (MODE_ITEM_COUNT - 1)) {
                s_selected_idx++;
            } else {
                s_selected_idx = 0;
            }
            break;
        case UI_KEY_ENTER:
        case UI_KEY_SPACE:
            /* Toggle current mode */
            s_modes_state[s_selected_idx] = !s_modes_state[s_selected_idx];
            if (s_selected_idx == MODE_ITEM_BUZZER) {
                alert_engine_set_silent(!s_modes_state[s_selected_idx]);
            }
            break;
        case UI_KEY_BACK:
        case UI_KEY_MENU:
            ui_manager_navigate_to(UI_SCREEN_MAIN_MENU);
            break;
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t screen_modes_deinit(void)
{
    s_modes_initialized = false;
    return ESP_OK;
}
