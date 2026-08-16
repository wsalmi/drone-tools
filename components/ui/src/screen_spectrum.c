/**
 * @file screen_spectrum.c
 * @brief RF Activity Monitor screen implementation — live RF channels & receivers status.
 *
 * Displays live RF channel activity (WiFi 2.4GHz hopping, BLE, LoRa 915MHz, GPS)
 * with signal indicators and status metrics.
 */

#include "screen_spectrum.h"
#include "hal_display.h"
#include "hal_lora.h"
#include "hal_wifi_scanner.h"
#include "hal_ble_scanner.h"
#include "hal_gps.h"
#include "hw_manager.h"
#include "ui_manager.h"
#include <string.h>
#include <stdio.h>

/* ========================================================================
 * Internal State
 * ======================================================================== */

static screen_spectrum_state_t s_state = {0};

/* ========================================================================
 * Public API
 * ======================================================================== */

esp_err_t screen_spectrum_init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_state.initialized = true;
    return ESP_OK;
}

esp_err_t screen_spectrum_render(void)
{
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Clear content area */
    hal_display_draw_rect(0, SCREEN_SPECTRUM_CONTENT_Y,
                          HAL_DISPLAY_WIDTH,
                          HAL_DISPLAY_HEIGHT - SCREEN_SPECTRUM_CONTENT_Y,
                          HAL_COLOR_BLACK, true);

    /* Title bar */
    hal_display_draw_text(4, SCREEN_SPECTRUM_CONTENT_Y + 2, "MONITOR DE CANAIS RF",
                          HAL_COLOR_CYAN, HAL_COLOR_BLACK);

    /* Row 1: WiFi 2.4GHz Sniffer Status */
    uint16_t y = SCREEN_SPECTRUM_CONTENT_Y + 16;
    hal_display_draw_text(6, y, "WiFi 2.4G:", HAL_COLOR_YELLOW, HAL_COLOR_BLACK);
    bool wifi_ok = (hal_wifi_scanner_get_status() == HAL_STATUS_ACTIVE);
    if (wifi_ok) {
        hal_display_draw_text(76, y, "CH [1] [6] [11] ATIVO", HAL_COLOR_GREEN, HAL_COLOR_BLACK);
    } else {
        hal_display_draw_text(76, y, "STANDBY", 0x7BEF, HAL_COLOR_BLACK);
    }

    /* Signal activity bar for WiFi */
    hal_display_draw_rect(6, y + 11, 228, 4, 0x18E3, true);
    if (wifi_ok) {
        hal_display_draw_rect(6, y + 11, 160, 4, HAL_COLOR_GREEN, true);
    }

    /* Row 2: BLE Scanner Status */
    y += 19;
    hal_display_draw_text(6, y, "BLE Adv :", HAL_COLOR_YELLOW, HAL_COLOR_BLACK);
    bool ble_ok = (hal_ble_scanner_get_status() == HAL_STATUS_ACTIVE);
    if (ble_ok) {
        hal_display_draw_text(76, y, "Legacy ADV ATIVO (2.4G)", HAL_COLOR_GREEN, HAL_COLOR_BLACK);
    } else {
        hal_display_draw_text(76, y, "INATIVO", 0x7BEF, HAL_COLOR_BLACK);
    }
    hal_display_draw_rect(6, y + 11, 228, 4, 0x18E3, true);
    if (ble_ok) {
        hal_display_draw_rect(6, y + 11, 180, 4, HAL_COLOR_CYAN, true);
    }

    /* Row 3: LoRa SX1262 915MHz Status */
    y += 19;
    hal_display_draw_text(6, y, "LoRa RF :", HAL_COLOR_YELLOW, HAL_COLOR_BLACK);
    bool lora_ok = (hal_lora_get_status() == HAL_STATUS_ACTIVE);
    if (lora_ok) {
        hal_display_draw_text(76, y, "915 MHz (SF7/BW125/CR45)", HAL_COLOR_GREEN, HAL_COLOR_BLACK);
    } else {
        hal_display_draw_text(76, y, "MODULO AUSENTE", HAL_COLOR_RED, HAL_COLOR_BLACK);
    }
    hal_display_draw_rect(6, y + 11, 228, 4, 0x18E3, true);
    if (lora_ok) {
        hal_display_draw_rect(6, y + 11, 220, 4, HAL_COLOR_MAGENTA, true);
    }

    /* Row 4: GPS Position & Fix */
    y += 19;
    hal_display_draw_text(6, y, "GPS POS :", HAL_COLOR_YELLOW, HAL_COLOR_BLACK);
    gps_position_t gps_pos;
    if (hal_gps_get_position(&gps_pos) == ESP_OK && gps_pos.fix_valid) {
        char gps_buf[36];
        snprintf(gps_buf, sizeof(gps_buf), "FIX OK: SATS=%u", (unsigned)gps_pos.satellites_used);
        hal_display_draw_text(76, y, gps_buf, HAL_COLOR_GREEN, HAL_COLOR_BLACK);
    } else {
        hal_display_draw_text(76, y, "AGUARDANDO FIX (9600)", HAL_COLOR_YELLOW, HAL_COLOR_BLACK);
    }

    /* Footer: Return instructions */
    uint16_t footer_y = HAL_DISPLAY_HEIGHT - 12;
    hal_display_draw_rect(0, footer_y - 2, HAL_DISPLAY_WIDTH, 14, 0x18C3, true);
    hal_display_draw_text(18, footer_y, "[ENTER / ESC / 1-7] Voltar ao Menu",
                          HAL_COLOR_WHITE, 0x18C3);

    return ESP_OK;
}

esp_err_t screen_spectrum_handle_key(uint8_t key)
{
    (void)key;
    /* Any key press on RF monitor returns to Main Menu */
    ui_manager_navigate_to(UI_SCREEN_MAIN_MENU);
    return ESP_OK;
}

esp_err_t screen_spectrum_deinit(void)
{
    s_state.initialized = false;
    return ESP_OK;
}
