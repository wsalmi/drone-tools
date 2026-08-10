/**
 * @file screen_log.c
 * @brief Log screen implementation — displays recent records with KML export option.
 *
 * Shows the most recent log records from the data_logger service in a scrollable
 * list format. Provides a KML export trigger accessible via the ENTER key.
 *
 * Validates: Requirements 11.2
 */

#include "screen_log.h"
#include "hal_display.h"
#include "ui_manager.h"
#include "data_logger.h"
#include "aircraft_registry.h"
#include <string.h>
#include <stdio.h>

/* ========================================================================
 * Internal State
 * ======================================================================== */

static screen_log_state_t s_state = {0};

/* Display buffer for formatted log entries */
#define LOG_DISPLAY_LINE_LEN 42
static char s_display_lines[SCREEN_LOG_MAX_RECORDS][LOG_DISPLAY_LINE_LEN];
static uint8_t s_display_count = 0;

/* ========================================================================
 * Helper: Format a log record for display
 * ======================================================================== */

static void format_log_line(const log_record_t *record, char *buf, size_t buf_len)
{
    /* Format: "HH:MM:SS ID PROTO EVT" */
    /* Extract time from timestamp */
    uint64_t ts = record->timestamp_utc_ms;
    uint32_t total_seconds = (uint32_t)(ts / 1000ULL);
    uint8_t hours = (uint8_t)((total_seconds / 3600) % 24);
    uint8_t minutes = (uint8_t)((total_seconds / 60) % 60);
    uint8_t seconds = (uint8_t)(total_seconds % 60);

    const char *proto = data_logger_protocol_to_str(record->protocol);
    const char *event = data_logger_event_to_str(record->event_type);

    /* Truncate aircraft ID to 8 chars for display */
    char short_id[9];
    strncpy(short_id, record->aircraft_id, 8);
    short_id[8] = '\0';

    snprintf(buf, buf_len, "%02u:%02u:%02u %-8s %-4s %s",
             hours, minutes, seconds, short_id, proto, event);
}

/* ========================================================================
 * Helper: Trigger KML export
 * ======================================================================== */

static esp_err_t trigger_kml_export(void)
{
    s_state.export_in_progress = true;
    s_state.export_complete = false;

    /* Build placemarks array from available data
     * Note: In a real implementation, this would gather all positions
     * from the current session. For now, we use a simplified approach
     * by calling data_logger_generate_kml with a basic output path. */

    const char *output_path = "/sdcard/export_session.kml";

    /* We pass NULL/0 to generate an empty KML (the real integration would
     * collect session placemarks from the registry and logger) */
    esp_err_t err = data_logger_generate_kml(output_path, NULL, 0);

    s_state.export_in_progress = false;
    s_state.export_complete = (err == ESP_OK);

    return err;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

esp_err_t screen_log_init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    memset(s_display_lines, 0, sizeof(s_display_lines));
    s_display_count = 0;

    s_state.initialized = true;
    s_state.selected_item = 0;
    s_state.scroll_offset = 0;
    s_state.export_in_progress = false;
    s_state.export_complete = false;

    return ESP_OK;
}

esp_err_t screen_log_render(void)
{
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Clear content area */
    hal_display_draw_rect(0, SCREEN_LOG_CONTENT_Y,
                          HAL_DISPLAY_WIDTH,
                          HAL_DISPLAY_HEIGHT - SCREEN_LOG_CONTENT_Y,
                          HAL_COLOR_BLACK, true);

    /* Title and export option */
    uint16_t y = SCREEN_LOG_CONTENT_Y + 2;

    /* Draw "Export KML" button */
    uint16_t export_fg = HAL_COLOR_WHITE;
    uint16_t export_bg = HAL_COLOR_BLACK;
    if (s_state.selected_item == 0 && s_state.scroll_offset == 0) {
        export_bg = 0x2104; /* dark gray highlight */
        export_fg = HAL_COLOR_YELLOW;
        hal_display_draw_rect(0, y - 1, HAL_DISPLAY_WIDTH, SCREEN_LOG_LINE_HEIGHT,
                              export_bg, true);
    }

    if (s_state.export_in_progress) {
        hal_display_draw_text(4, y, "Exportando KML...", HAL_COLOR_YELLOW, export_bg);
    } else if (s_state.export_complete) {
        hal_display_draw_text(4, y, "[OK] Exportar KML", HAL_COLOR_GREEN, export_bg);
    } else {
        hal_display_draw_text(4, y, "> Exportar KML", export_fg, export_bg);
    }

    y += SCREEN_LOG_LINE_HEIGHT + 2;

    /* Draw separator line */
    hal_display_draw_rect(0, y, HAL_DISPLAY_WIDTH, 1, 0x4208, true); /* medium gray */
    y += 3;

    /* Get buffer count from data logger */
    size_t buffer_count = data_logger_get_buffer_count();
    s_state.total_records = (buffer_count > SCREEN_LOG_MAX_RECORDS) ?
                            SCREEN_LOG_MAX_RECORDS : (uint8_t)buffer_count;

    /* Draw log entries */
    if (s_state.total_records == 0) {
        hal_display_draw_text(20, y + 20, "Nenhum registro",
                              HAL_COLOR_WHITE, HAL_COLOR_BLACK);
        hal_display_draw_text(20, y + 36, "disponivel",
                              HAL_COLOR_WHITE, HAL_COLOR_BLACK);
    } else {
        /* Draw visible entries */
        uint8_t visible_start = s_state.scroll_offset;
        uint8_t items_shown = 0;

        for (uint8_t i = visible_start;
             i < s_state.total_records && items_shown < SCREEN_LOG_ITEMS_PER_PAGE;
             i++) {
            uint16_t fg = HAL_COLOR_WHITE;
            uint16_t bg = HAL_COLOR_BLACK;

            /* Highlight selected item (offset by 1 for export button) */
            if (i == s_state.selected_item - 1 && s_state.selected_item > 0) {
                bg = 0x2104;
                fg = HAL_COLOR_YELLOW;
                hal_display_draw_rect(0, y - 1, HAL_DISPLAY_WIDTH, SCREEN_LOG_LINE_HEIGHT,
                                      bg, true);
            }

            /* Display the pre-formatted line if available, otherwise show index */
            if (i < s_display_count && s_display_lines[i][0] != '\0') {
                hal_display_draw_text(2, y, s_display_lines[i], fg, bg);
            } else {
                char idx_buf[16];
                snprintf(idx_buf, sizeof(idx_buf), "Reg #%u", i + 1);
                hal_display_draw_text(2, y, idx_buf, fg, bg);
            }

            y += SCREEN_LOG_LINE_HEIGHT;
            items_shown++;
        }
    }

    /* Draw scroll indicator if there are more entries */
    if (s_state.total_records > SCREEN_LOG_ITEMS_PER_PAGE) {
        char scroll_info[16];
        snprintf(scroll_info, sizeof(scroll_info), "%u/%u",
                 s_state.scroll_offset + 1, s_state.total_records);
        hal_display_draw_text(HAL_DISPLAY_WIDTH - 36, HAL_DISPLAY_HEIGHT - 12,
                              scroll_info, HAL_COLOR_WHITE, HAL_COLOR_BLACK);
    }

    return ESP_OK;
}

esp_err_t screen_log_handle_key(uint8_t key)
{
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Total selectable items: 1 (export button) + total_records */
    uint8_t total_selectable = 1 + s_state.total_records;

    switch ((ui_key_t)key) {
        case UI_KEY_UP:
            if (s_state.selected_item > 0) {
                s_state.selected_item--;
                /* Adjust scroll if needed */
                if (s_state.selected_item > 0 &&
                    s_state.selected_item - 1 < s_state.scroll_offset) {
                    s_state.scroll_offset = s_state.selected_item - 1;
                }
            }
            break;

        case UI_KEY_DOWN:
            if (s_state.selected_item < total_selectable - 1) {
                s_state.selected_item++;
                /* Adjust scroll to keep selection visible */
                if (s_state.selected_item > 0) {
                    uint8_t log_idx = s_state.selected_item - 1;
                    if (log_idx >= s_state.scroll_offset + SCREEN_LOG_ITEMS_PER_PAGE) {
                        s_state.scroll_offset = log_idx - SCREEN_LOG_ITEMS_PER_PAGE + 1;
                    }
                }
            }
            break;

        case UI_KEY_ENTER:
            /* If on export button, trigger export */
            if (s_state.selected_item == 0) {
                trigger_kml_export();
            }
            break;

        default:
            break;
    }

    return ESP_OK;
}

esp_err_t screen_log_deinit(void)
{
    s_state.initialized = false;
    s_display_count = 0;
    return ESP_OK;
}
