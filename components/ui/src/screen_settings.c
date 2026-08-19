/**
 * @file screen_settings.c
 * @brief Field configuration without SDR, USB host, SoftAP or NRF24 controls.
 */

#include "screen_settings.h"
#include "hal_display.h"
#include "ui_manager.h"
#include "config_store.h"
#include "alert_engine.h"
#include <string.h>
#include <stdio.h>

static screen_settings_state_t s_state;
static config_store_t s_working_config;
static bool s_config_loaded;

#define ALERT_ITEM_COUNT 3
#define MONITOR_ITEM_COUNT 3

static const char *s_category_labels[] = {"Alertas", "Monitor"};
static const char *s_alert_labels[] = {"Som", "Dist. Prox. (m)", "Repetir (s)"};
static const char *s_monitor_labels[] = {
    "Ciclo Wi-Fi/BLE (ms)", "Dwell SX1262 (ms)", "Atualiza estado (ms)"
};

static uint8_t get_item_count(settings_category_t category)
{
    return category == SETTINGS_CATEGORY_ALERT ? ALERT_ITEM_COUNT : MONITOR_ITEM_COUNT;
}

static const char **get_labels(settings_category_t category)
{
    return category == SETTINGS_CATEGORY_ALERT ? s_alert_labels : s_monitor_labels;
}

static void format_value(settings_category_t category, uint8_t item, char *out, size_t out_len)
{
    if (category == SETTINGS_CATEGORY_ALERT) {
        if (item == 0) snprintf(out, out_len, "%s", s_working_config.alert.sound_enabled ? "ON" : "OFF");
        else if (item == 1) snprintf(out, out_len, "%lu", (unsigned long)s_working_config.alert.proximity_threshold_m);
        else snprintf(out, out_len, "%lu", (unsigned long)s_working_config.alert.proximity_repeat_interval_s);
        return;
    }
    if (item == 0) snprintf(out, out_len, "%lu", (unsigned long)s_working_config.scan.remoteid_cycle_ms);
    else if (item == 1) snprintf(out, out_len, "%lu", (unsigned long)s_working_config.scan.lora_dwell_time_ms);
    else snprintf(out, out_len, "%lu", (unsigned long)s_working_config.scan.module_poll_interval_ms);
}

static void adjust_value(settings_category_t category, uint8_t item, int direction)
{
    if (category == SETTINGS_CATEGORY_ALERT) {
        if (item == 0) {
            s_working_config.alert.sound_enabled = !s_working_config.alert.sound_enabled;
            alert_engine_set_silent(!s_working_config.alert.sound_enabled);
        } else if (item == 1) {
            if (direction > 0 && s_working_config.alert.proximity_threshold_m < 5000) s_working_config.alert.proximity_threshold_m += 50;
            if (direction < 0 && s_working_config.alert.proximity_threshold_m > 50) s_working_config.alert.proximity_threshold_m -= 50;
        } else {
            if (direction > 0 && s_working_config.alert.proximity_repeat_interval_s < 60) s_working_config.alert.proximity_repeat_interval_s += 5;
            if (direction < 0 && s_working_config.alert.proximity_repeat_interval_s > 5) s_working_config.alert.proximity_repeat_interval_s -= 5;
        }
        return;
    }
    if (item == 0) {
        if (direction > 0 && s_working_config.scan.remoteid_cycle_ms < 10000) s_working_config.scan.remoteid_cycle_ms += 500;
        if (direction < 0 && s_working_config.scan.remoteid_cycle_ms > 1000) s_working_config.scan.remoteid_cycle_ms -= 500;
    } else if (item == 1) {
        if (direction > 0 && s_working_config.scan.lora_dwell_time_ms < 200) s_working_config.scan.lora_dwell_time_ms += 10;
        if (direction < 0 && s_working_config.scan.lora_dwell_time_ms > 10) s_working_config.scan.lora_dwell_time_ms -= 10;
    } else {
        if (direction > 0 && s_working_config.scan.module_poll_interval_ms < 2000) s_working_config.scan.module_poll_interval_ms += 100;
        if (direction < 0 && s_working_config.scan.module_poll_interval_ms > 100) s_working_config.scan.module_poll_interval_ms -= 100;
    }
}

esp_err_t screen_settings_init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_state.initialized = true;
    if (!s_config_loaded) {
        config_store_get_defaults(&s_working_config);
        s_config_loaded = true;
    }
    return ESP_OK;
}

esp_err_t screen_settings_render(void)
{
    if (!s_state.initialized) return ESP_ERR_INVALID_STATE;
    hal_display_draw_rect(0, SCREEN_SETTINGS_CONTENT_Y, HAL_DISPLAY_WIDTH,
                          HAL_DISPLAY_HEIGHT - SCREEN_SETTINGS_CONTENT_Y, HAL_COLOR_BLACK, true);
    char header[32];
    snprintf(header, sizeof(header), "5. CONFIG [%s] <>", s_category_labels[s_state.category]);
    hal_display_draw_text(3, SCREEN_SETTINGS_CONTENT_Y + 2, header, HAL_COLOR_CYAN, HAL_COLOR_BLACK);
    const char **labels = get_labels(s_state.category);
    uint8_t count = get_item_count(s_state.category);
    for (uint8_t i = 0; i < count; ++i) {
        uint16_t y = SCREEN_SETTINGS_CONTENT_Y + 20 + i * SCREEN_SETTINGS_LINE_HEIGHT;
        bool selected = i == s_state.selected_item;
        uint16_t background = selected ? (s_state.editing ? HAL_COLOR_BLUE : 0x2104) : HAL_COLOR_BLACK;
        uint16_t foreground = selected ? HAL_COLOR_YELLOW : HAL_COLOR_WHITE;
        if (selected) hal_display_draw_rect(0, y - 1, HAL_DISPLAY_WIDTH, SCREEN_SETTINGS_LINE_HEIGHT, background, true);
        hal_display_draw_text(4, y, labels[i], foreground, background);
        char value[16];
        format_value(s_state.category, i, value, sizeof(value));
        hal_display_draw_text(HAL_DISPLAY_WIDTH - (uint16_t)(strlen(value) * 6) - 4, y, value, foreground, background);
    }
    hal_display_draw_text(3, HAL_DISPLAY_HEIGHT - 12,
                          s_state.editing ? "< AJUSTAR > ENTER: SALVAR" : "ENTER: EDITAR  ESC: MENU",
                          HAL_COLOR_YELLOW, HAL_COLOR_BLACK);
    return ESP_OK;
}

esp_err_t screen_settings_handle_key(uint8_t key)
{
    if (!s_state.initialized) return ESP_ERR_INVALID_STATE;
    uint8_t count = get_item_count(s_state.category);
    ui_key_t ui_key = (ui_key_t)key;
    if (s_state.editing) {
        if (ui_key == UI_KEY_LEFT) adjust_value(s_state.category, s_state.selected_item, -1);
        else if (ui_key == UI_KEY_RIGHT) adjust_value(s_state.category, s_state.selected_item, 1);
        else if (ui_key == UI_KEY_ENTER || ui_key == UI_KEY_BACK) s_state.editing = false;
        return ESP_OK;
    }
    if (ui_key == UI_KEY_UP) s_state.selected_item = s_state.selected_item == 0 ? count - 1 : s_state.selected_item - 1;
    else if (ui_key == UI_KEY_DOWN) s_state.selected_item = (s_state.selected_item + 1) % count;
    else if (ui_key == UI_KEY_LEFT || ui_key == UI_KEY_RIGHT) {
        s_state.category = s_state.category == SETTINGS_CATEGORY_ALERT ? SETTINGS_CATEGORY_MONITOR : SETTINGS_CATEGORY_ALERT;
        s_state.selected_item = 0;
    } else if (ui_key == UI_KEY_ENTER || ui_key == UI_KEY_SPACE) {
        if (s_state.category == SETTINGS_CATEGORY_ALERT && s_state.selected_item == 0) adjust_value(s_state.category, 0, 1);
        else s_state.editing = true;
    } else if (ui_key == UI_KEY_BACK || ui_key == UI_KEY_MENU) ui_manager_navigate_to(UI_SCREEN_MAIN_MENU);
    return ESP_OK;
}

esp_err_t screen_settings_deinit(void)
{
    s_state.initialized = false;
    return ESP_OK;
}
