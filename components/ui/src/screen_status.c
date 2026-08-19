/**
 * @file screen_status.c
 * @brief High-contrast field status display; no spectrum or SDR state.
 */
#include "screen_status.h"
#include "ui_manager.h"
#include "hal_display.h"
#include <stdio.h>

static bool s_initialized;

static const char *status_text(hal_status_t status)
{
    switch (status) {
        case HAL_STATUS_ACTIVE: return "ATIVO";
        case HAL_STATUS_INITIALIZING: return "INICIANDO";
        case HAL_STATUS_ERROR: return "ERRO";
        default: return "INATIVO";
    }
}

static uint16_t status_color(hal_status_t status)
{
    return status == HAL_STATUS_ACTIVE ? HAL_COLOR_GREEN :
        status == HAL_STATUS_ERROR ? HAL_COLOR_RED : HAL_COLOR_YELLOW;
}

esp_err_t screen_status_init(void)
{
    s_initialized = true;
    return ESP_OK;
}

esp_err_t screen_status_render(void)
{
    if (!s_initialized) screen_status_init();
    const ui_state_t *state = ui_manager_get_state();
    static const char *labels[] = {"WI-FI RID", "BLE RID", "SX1262", "GPS", "CARTAO SD"};
    hal_display_draw_rect(0, UI_STATUS_BAR_HEIGHT, HAL_DISPLAY_WIDTH,
                          HAL_DISPLAY_HEIGHT - UI_STATUS_BAR_HEIGHT, HAL_COLOR_BLACK, true);
    hal_display_draw_text(4, 16, "7. ESTADO DO SISTEMA", HAL_COLOR_CYAN, HAL_COLOR_BLACK);
    for (uint8_t i = 0; i < UI_MODULE_COUNT; ++i) {
        char line[34];
        snprintf(line, sizeof(line), "%s: %s", labels[i], status_text(state->module_status[i]));
        hal_display_draw_text(8, 31 + (i * 16), line, status_color(state->module_status[i]), HAL_COLOR_BLACK);
    }
    char aircraft[24];
    snprintf(aircraft, sizeof(aircraft), "ALVOS ATIVOS: %u", state->aircraft_count);
    hal_display_draw_text(8, 113, aircraft, HAL_COLOR_WHITE, HAL_COLOR_BLACK);
    hal_display_draw_text(8, 125, "ESC: VOLTAR  USB: ATUALIZACAO", HAL_COLOR_YELLOW, HAL_COLOR_BLACK);
    return ESP_OK;
}

esp_err_t screen_status_deinit(void)
{
    s_initialized = false;
    return ESP_OK;
}
