/**
 * @file screen_settings.c
 * @brief Settings screen implementation — configuration of alerts, spectrum, and scan.
 *
 * Provides a navigable menu interface for configuring system parameters.
 * Reads current config from config_store defaults and applies changes
 * to the relevant services at runtime.
 *
 * Validates: Requirements 9.1, 12.2
 */

#include "screen_settings.h"
#include "hal_display.h"
#include "ui_manager.h"
#include "config_store.h"
#include "alert_engine.h"
#include "spectrum_analyzer.h"
#include <string.h>
#include <stdio.h>

/* ========================================================================
 * Internal State
 * ======================================================================== */

static screen_settings_state_t s_state = {0};

/* Working copy of configuration that the user edits */
static config_store_t s_working_config;
static bool s_config_loaded = false;

/* ========================================================================
 * Category Definitions
 * ======================================================================== */

/** @brief Number of items in each category */
#define ALERT_ITEM_COUNT    3
#define SPECTRUM_ITEM_COUNT 4
#define SCAN_ITEM_COUNT     4

static const char *s_category_labels[] = {
    "Alertas",
    "Espectro",
    "Scan"
};

/* Alert items */
static const char *s_alert_labels[] = {
    "Som",
    "Dist. Prox. (m)",
    "Intervalo (s)"
};

/* Spectrum items */
static const char *s_spectrum_labels[] = {
    "Freq. Central (MHz)",
    "Bandwidth (kHz)",
    "Ganho (dB)",
    "Limiar (dBm)"
};

/* Scan items */
static const char *s_scan_labels[] = {
    "Ciclo RemoteID (ms)",
    "Dwell NRF24 (ms)",
    "Dwell LoRa (ms)",
    "Poll Modulo (ms)"
};

/* ========================================================================
 * Helpers
 * ======================================================================== */

static uint8_t get_category_item_count(settings_category_t cat)
{
    switch (cat) {
        case SETTINGS_CATEGORY_ALERT:    return ALERT_ITEM_COUNT;
        case SETTINGS_CATEGORY_SPECTRUM: return SPECTRUM_ITEM_COUNT;
        case SETTINGS_CATEGORY_SCAN:     return SCAN_ITEM_COUNT;
        default:                         return 0;
    }
}

static void format_value(settings_category_t cat, uint8_t item, char *buf, size_t buf_len)
{
    switch (cat) {
        case SETTINGS_CATEGORY_ALERT:
            switch (item) {
                case 0: snprintf(buf, buf_len, "%s", s_working_config.alert.sound_enabled ? "ON" : "OFF"); break;
                case 1: snprintf(buf, buf_len, "%lu", (unsigned long)s_working_config.alert.proximity_threshold_m); break;
                case 2: snprintf(buf, buf_len, "%lu", (unsigned long)s_working_config.alert.proximity_repeat_interval_s); break;
                default: buf[0] = '\0'; break;
            }
            break;

        case SETTINGS_CATEGORY_SPECTRUM:
            switch (item) {
                case 0: snprintf(buf, buf_len, "%lu", (unsigned long)s_working_config.spectrum.default_center_freq_mhz); break;
                case 1: snprintf(buf, buf_len, "%lu", (unsigned long)s_working_config.spectrum.default_bandwidth_khz); break;
                case 2: snprintf(buf, buf_len, "%.1f", (double)s_working_config.spectrum.default_gain_db); break;
                case 3: snprintf(buf, buf_len, "%ld", (long)s_working_config.spectrum.detection_threshold_dbm); break;
                default: buf[0] = '\0'; break;
            }
            break;

        case SETTINGS_CATEGORY_SCAN:
            switch (item) {
                case 0: snprintf(buf, buf_len, "%lu", (unsigned long)s_working_config.scan.remoteid_cycle_ms); break;
                case 1: snprintf(buf, buf_len, "%lu", (unsigned long)s_working_config.scan.nrf24_dwell_time_ms); break;
                case 2: snprintf(buf, buf_len, "%lu", (unsigned long)s_working_config.scan.lora_dwell_time_ms); break;
                case 3: snprintf(buf, buf_len, "%lu", (unsigned long)s_working_config.scan.module_poll_interval_ms); break;
                default: buf[0] = '\0'; break;
            }
            break;

        default:
            buf[0] = '\0';
            break;
    }
}

static void adjust_value(settings_category_t cat, uint8_t item, int direction)
{
    switch (cat) {
        case SETTINGS_CATEGORY_ALERT:
            switch (item) {
                case 0: /* Toggle sound */
                    s_working_config.alert.sound_enabled = !s_working_config.alert.sound_enabled;
                    alert_engine_set_silent(!s_working_config.alert.sound_enabled);
                    break;
                case 1: /* Proximity threshold */
                    if (direction > 0 && s_working_config.alert.proximity_threshold_m < 5000) {
                        s_working_config.alert.proximity_threshold_m += 50;
                    } else if (direction < 0 && s_working_config.alert.proximity_threshold_m > 50) {
                        s_working_config.alert.proximity_threshold_m -= 50;
                    }
                    break;
                case 2: /* Repeat interval */
                    if (direction > 0 && s_working_config.alert.proximity_repeat_interval_s < 60) {
                        s_working_config.alert.proximity_repeat_interval_s += 5;
                    } else if (direction < 0 && s_working_config.alert.proximity_repeat_interval_s > 5) {
                        s_working_config.alert.proximity_repeat_interval_s -= 5;
                    }
                    break;
                default: break;
            }
            break;

        case SETTINGS_CATEGORY_SPECTRUM:
            switch (item) {
                case 0: /* Center frequency */
                    if (direction > 0 && s_working_config.spectrum.default_center_freq_mhz < SPECTRUM_FREQ_MAX_MHZ) {
                        s_working_config.spectrum.default_center_freq_mhz += 10;
                        if (s_working_config.spectrum.default_center_freq_mhz > SPECTRUM_FREQ_MAX_MHZ) {
                            s_working_config.spectrum.default_center_freq_mhz = SPECTRUM_FREQ_MAX_MHZ;
                        }
                    } else if (direction < 0 && s_working_config.spectrum.default_center_freq_mhz > SPECTRUM_FREQ_MIN_MHZ) {
                        if (s_working_config.spectrum.default_center_freq_mhz > SPECTRUM_FREQ_MIN_MHZ + 10) {
                            s_working_config.spectrum.default_center_freq_mhz -= 10;
                        } else {
                            s_working_config.spectrum.default_center_freq_mhz = SPECTRUM_FREQ_MIN_MHZ;
                        }
                    }
                    break;
                case 1: /* Bandwidth */
                    if (direction > 0 && s_working_config.spectrum.default_bandwidth_khz < SPECTRUM_BW_MAX_KHZ) {
                        s_working_config.spectrum.default_bandwidth_khz += 50;
                        if (s_working_config.spectrum.default_bandwidth_khz > SPECTRUM_BW_MAX_KHZ) {
                            s_working_config.spectrum.default_bandwidth_khz = SPECTRUM_BW_MAX_KHZ;
                        }
                    } else if (direction < 0 && s_working_config.spectrum.default_bandwidth_khz > SPECTRUM_BW_MIN_KHZ) {
                        if (s_working_config.spectrum.default_bandwidth_khz > SPECTRUM_BW_MIN_KHZ + 50) {
                            s_working_config.spectrum.default_bandwidth_khz -= 50;
                        } else {
                            s_working_config.spectrum.default_bandwidth_khz = SPECTRUM_BW_MIN_KHZ;
                        }
                    }
                    break;
                case 2: /* Gain */
                    if (direction > 0 && s_working_config.spectrum.default_gain_db < SPECTRUM_GAIN_MAX_DB) {
                        s_working_config.spectrum.default_gain_db += 1.0f;
                        if (s_working_config.spectrum.default_gain_db > SPECTRUM_GAIN_MAX_DB) {
                            s_working_config.spectrum.default_gain_db = SPECTRUM_GAIN_MAX_DB;
                        }
                    } else if (direction < 0 && s_working_config.spectrum.default_gain_db > SPECTRUM_GAIN_MIN_DB) {
                        s_working_config.spectrum.default_gain_db -= 1.0f;
                        if (s_working_config.spectrum.default_gain_db < SPECTRUM_GAIN_MIN_DB) {
                            s_working_config.spectrum.default_gain_db = SPECTRUM_GAIN_MIN_DB;
                        }
                    }
                    break;
                case 3: /* Detection threshold */
                    if (direction > 0 && s_working_config.spectrum.detection_threshold_dbm < -20) {
                        s_working_config.spectrum.detection_threshold_dbm += 5;
                    } else if (direction < 0 && s_working_config.spectrum.detection_threshold_dbm > -120) {
                        s_working_config.spectrum.detection_threshold_dbm -= 5;
                    }
                    break;
                default: break;
            }
            break;

        case SETTINGS_CATEGORY_SCAN:
            switch (item) {
                case 0: /* RemoteID cycle */
                    if (direction > 0 && s_working_config.scan.remoteid_cycle_ms < 10000) {
                        s_working_config.scan.remoteid_cycle_ms += 500;
                    } else if (direction < 0 && s_working_config.scan.remoteid_cycle_ms > 1000) {
                        s_working_config.scan.remoteid_cycle_ms -= 500;
                    }
                    break;
                case 1: /* NRF24 dwell */
                    if (direction > 0 && s_working_config.scan.nrf24_dwell_time_ms < 500) {
                        s_working_config.scan.nrf24_dwell_time_ms += 10;
                    } else if (direction < 0 && s_working_config.scan.nrf24_dwell_time_ms > 10) {
                        s_working_config.scan.nrf24_dwell_time_ms -= 10;
                    }
                    break;
                case 2: /* LoRa dwell */
                    if (direction > 0 && s_working_config.scan.lora_dwell_time_ms < 200) {
                        s_working_config.scan.lora_dwell_time_ms += 10;
                    } else if (direction < 0 && s_working_config.scan.lora_dwell_time_ms > 10) {
                        s_working_config.scan.lora_dwell_time_ms -= 10;
                    }
                    break;
                case 3: /* Module poll */
                    if (direction > 0 && s_working_config.scan.module_poll_interval_ms < 2000) {
                        s_working_config.scan.module_poll_interval_ms += 100;
                    } else if (direction < 0 && s_working_config.scan.module_poll_interval_ms > 100) {
                        s_working_config.scan.module_poll_interval_ms -= 100;
                    }
                    break;
                default: break;
            }
            break;

        default:
            break;
    }
}

/* ========================================================================
 * Public API
 * ======================================================================== */

esp_err_t screen_settings_init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_state.initialized = true;
    s_state.category = SETTINGS_CATEGORY_ALERT;
    s_state.selected_item = 0;
    s_state.scroll_offset = 0;
    s_state.editing = false;

    /* Load current config as working copy */
    if (!s_config_loaded) {
        config_store_get_defaults(&s_working_config);
        s_config_loaded = true;
    }

    return ESP_OK;
}

esp_err_t screen_settings_render(void)
{
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Clear content area */
    hal_display_draw_rect(0, SCREEN_SETTINGS_CONTENT_Y,
                          HAL_DISPLAY_WIDTH,
                          HAL_DISPLAY_HEIGHT - SCREEN_SETTINGS_CONTENT_Y,
                          HAL_COLOR_BLACK, true);

    /* Draw category header */
    char header_buf[32];
    snprintf(header_buf, sizeof(header_buf), "[%s]", s_category_labels[s_state.category]);
    hal_display_draw_text(2, SCREEN_SETTINGS_CONTENT_Y + 2, header_buf,
                          HAL_COLOR_CYAN, HAL_COLOR_BLACK);

    /* Draw navigation arrows for category switching */
    hal_display_draw_text(HAL_DISPLAY_WIDTH - 18, SCREEN_SETTINGS_CONTENT_Y + 2,
                          "<>", HAL_COLOR_WHITE, HAL_COLOR_BLACK);

    /* Get labels and item count for current category */
    const char **labels = NULL;
    uint8_t item_count = get_category_item_count(s_state.category);

    switch (s_state.category) {
        case SETTINGS_CATEGORY_ALERT:    labels = s_alert_labels; break;
        case SETTINGS_CATEGORY_SPECTRUM: labels = s_spectrum_labels; break;
        case SETTINGS_CATEGORY_SCAN:     labels = s_scan_labels; break;
        default: return ESP_OK;
    }

    /* Draw menu items */
    uint16_t y = SCREEN_SETTINGS_CONTENT_Y + SCREEN_SETTINGS_LINE_HEIGHT + 4;
    for (uint8_t i = 0; i < item_count && y < HAL_DISPLAY_HEIGHT - 10; i++) {
        uint16_t fg_color = HAL_COLOR_WHITE;
        uint16_t bg_color = HAL_COLOR_BLACK;

        /* Highlight selected item */
        if (i == s_state.selected_item) {
            bg_color = s_state.editing ? HAL_COLOR_BLUE : 0x2104; /* dark gray */
            fg_color = HAL_COLOR_YELLOW;
            hal_display_draw_rect(0, y - 1, HAL_DISPLAY_WIDTH, SCREEN_SETTINGS_LINE_HEIGHT,
                                  bg_color, true);
        }

        /* Draw label */
        hal_display_draw_text(4, y, labels[i], fg_color, bg_color);

        /* Draw current value */
        char val_buf[16];
        format_value(s_state.category, i, val_buf, sizeof(val_buf));
        /* Right-align value */
        uint16_t val_x = HAL_DISPLAY_WIDTH - (uint16_t)(strlen(val_buf) * 6) - 4;
        hal_display_draw_text(val_x, y, val_buf, fg_color, bg_color);

        y += SCREEN_SETTINGS_LINE_HEIGHT;
    }

    /* Draw edit mode indicator */
    if (s_state.editing) {
        hal_display_draw_text(2, HAL_DISPLAY_HEIGHT - 12, "< AJUSTAR >",
                              HAL_COLOR_YELLOW, HAL_COLOR_BLACK);
    }

    return ESP_OK;
}

esp_err_t screen_settings_handle_key(uint8_t key)
{
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t item_count = get_category_item_count(s_state.category);

    if (s_state.editing) {
        /* In edit mode */
        switch ((ui_key_t)key) {
            case UI_KEY_LEFT:
                adjust_value(s_state.category, s_state.selected_item, -1);
                break;
            case UI_KEY_RIGHT:
                adjust_value(s_state.category, s_state.selected_item, 1);
                break;
            case UI_KEY_ENTER:
            case UI_KEY_BACK:
                s_state.editing = false;
                break;
            default:
                break;
        }
    } else {
        /* Navigation mode */
        switch ((ui_key_t)key) {
            case UI_KEY_UP:
                if (s_state.selected_item > 0) {
                    s_state.selected_item--;
                }
                break;
            case UI_KEY_DOWN:
                if (s_state.selected_item < item_count - 1) {
                    s_state.selected_item++;
                }
                break;
            case UI_KEY_LEFT:
                /* Switch to previous category */
                if (s_state.category > 0) {
                    s_state.category--;
                } else {
                    s_state.category = SETTINGS_CATEGORY_COUNT - 1;
                }
                s_state.selected_item = 0;
                break;
            case UI_KEY_RIGHT:
                /* Switch to next category */
                s_state.category = (settings_category_t)(((int)s_state.category + 1) % SETTINGS_CATEGORY_COUNT);
                s_state.selected_item = 0;
                break;
            case UI_KEY_ENTER:
                /* Special case: toggle for boolean items */
                if (s_state.category == SETTINGS_CATEGORY_ALERT && s_state.selected_item == 0) {
                    adjust_value(s_state.category, s_state.selected_item, 1);
                } else {
                    s_state.editing = true;
                }
                break;
            default:
                break;
        }
    }

    return ESP_OK;
}

esp_err_t screen_settings_deinit(void)
{
    s_state.initialized = false;
    return ESP_OK;
}
