/**
 * @file screen_scanner.c
 * @brief Scanner Screen — paginated aircraft list rendering.
 *
 * Implements the scanner screen which shows detected aircraft in a paginated
 * list. Each row shows RSSI, protocol, distance, and bearing. Pagination
 * is driven by the ui_manager state (scanner_page, selected_aircraft_idx).
 *
 * Validates: Requirements 9.3, 9.6, 9.7
 */

#include "screen_scanner.h"
#include "ui_manager.h"
#include "hal_display.h"

#include <stdio.h>
#include <string.h>

/* ========================================================================
 * Private Constants
 * ======================================================================== */

/** @brief X margin for text content */
#define CONTENT_X_MARGIN        4

/** @brief Width of a character in the built-in 6×8 font */
#define CHAR_WIDTH              6

/** @brief Height of a character in the built-in 6×8 font */
#define CHAR_HEIGHT             8

/** @brief Color for selected row background */
#define COLOR_SELECTED_BG       0x2104  /* Dark gray */

/** @brief Color for normal row background */
#define COLOR_NORMAL_BG         HAL_COLOR_BLACK

/** @brief Color for normal row text */
#define COLOR_NORMAL_TEXT       HAL_COLOR_WHITE

/** @brief Color for RSSI text (signal strength) */
#define COLOR_RSSI_TEXT         HAL_COLOR_GREEN

/** @brief Color for protocol text */
#define COLOR_PROTO_TEXT        HAL_COLOR_CYAN

/** @brief Color for distance text */
#define COLOR_DIST_TEXT         HAL_COLOR_YELLOW

/** @brief Color for "no aircraft" message */
#define COLOR_EMPTY_TEXT        0x7BEF  /* Light gray */

/** @brief Color for page indicator */
#define COLOR_PAGE_TEXT         0x7BEF  /* Light gray */

/** @brief Y position for the page indicator line */
#define PAGE_INDICATOR_Y        (HAL_DISPLAY_HEIGHT - CHAR_HEIGHT - 2)

/* ========================================================================
 * Private Helpers
 * ======================================================================== */

const char *screen_scanner_protocol_name(protocol_type_t protocol)
{
    switch (protocol) {
        case PROTOCOL_ELRS:      return "ELRS";
        case PROTOCOL_DJI:       return "DJI";
        case PROTOCOL_WIFI:      return "WiFi";
        case PROTOCOL_MAVLINK:   return "MAVLink";
        case PROTOCOL_CROSSFIRE: return "CRSF";
        case PROTOCOL_FRSKY:     return "FrSky";
        case PROTOCOL_REMOTEID:  return "RID";
        case PROTOCOL_UNKNOWN:
        default:                 return "???";
    }
}

void screen_scanner_format_distance(char *buf, uint8_t buf_len, float dist_m)
{
    if (buf == NULL || buf_len == 0) {
        return;
    }

    if (dist_m < 0.0f) {
        snprintf(buf, buf_len, "N/D");
    } else if (dist_m <= 1000.0f) {
        snprintf(buf, buf_len, "%.0fm", dist_m);
    } else {
        snprintf(buf, buf_len, "%.1fkm", dist_m / 1000.0f);
    }
}

/**
 * @brief Collect active aircraft entries sorted by last_seen (most recent first).
 *
 * Copies pointers to active entries into a flat array for rendering.
 *
 * @param[in]  registry     Aircraft registry.
 * @param[out] out_entries  Array of pointers to fill.
 * @param[in]  max_entries  Maximum entries to collect.
 * @return Number of entries collected.
 */
static uint8_t collect_active_entries(const aircraft_registry_t *registry,
                                      const aircraft_entry_t **out_entries,
                                      uint8_t max_entries)
{
    uint8_t count = 0;

    for (uint8_t i = 0; i < MAX_AIRCRAFT && count < max_entries; i++) {
        if (registry->entries[i].slot_occupied &&
            registry->entries[i].status == AIRCRAFT_STATUS_ACTIVE) {
            out_entries[count++] = &registry->entries[i];
        }
    }

    return count;
}

/**
 * @brief Render a single aircraft row.
 *
 * Layout per row (240px wide):
 *   [RSSI 6ch] [Proto 7ch] [Dist 7ch] [Dir 5ch]
 *   Example: "-72dBm ELRS   450m  125°"
 *
 * @param[in] entry      Aircraft entry to render.
 * @param[in] row_idx    Row index (0-4) within the page.
 * @param[in] selected   true if this row is the selected item.
 */
static void render_row(const aircraft_entry_t *entry, uint8_t row_idx, bool selected)
{
    uint16_t y = SCANNER_CONTENT_Y_START + (row_idx * SCANNER_ROW_HEIGHT);
    uint16_t bg_color = selected ? COLOR_SELECTED_BG : COLOR_NORMAL_BG;

    /* Clear row background */
    hal_display_draw_rect(0, y, HAL_DISPLAY_WIDTH, SCANNER_ROW_HEIGHT, bg_color, true);

    /* --- Line 1: RSSI + Protocol + ID (short) --- */
    char line1[42];
    char id_short[9];  /* 8 chars max from ID */
    strncpy(id_short, entry->id, 8);
    id_short[8] = '\0';

    snprintf(line1, sizeof(line1), "%4ddBm %-7s %s",
             (int)entry->last_rssi_dbm,
             screen_scanner_protocol_name(entry->protocol),
             id_short);

    hal_display_draw_text(CONTENT_X_MARGIN, y + 2, line1,
                          COLOR_NORMAL_TEXT, bg_color);

    /* --- Line 2: Distance + Direction --- */
    char dist_buf[12];
    char line2[32];

    if (entry->relative_pos.valid) {
        screen_scanner_format_distance(dist_buf, sizeof(dist_buf),
                                       entry->relative_pos.distance_m);
        snprintf(line2, sizeof(line2), " %s  %03.0f%c",
                 dist_buf, entry->relative_pos.azimuth_deg, 0xB0);  /* degree symbol */
    } else {
        snprintf(line2, sizeof(line2), " N/D");
    }

    hal_display_draw_text(CONTENT_X_MARGIN, y + 2 + CHAR_HEIGHT + 2, line2,
                          COLOR_DIST_TEXT, bg_color);
}

/**
 * @brief Render the "no aircraft" empty state.
 */
static void render_empty_state(void)
{
    const char *msg = "Nenhuma aeronave detectada";
    /* Center the message horizontally */
    uint16_t text_width = (uint16_t)(strlen(msg) * CHAR_WIDTH);
    uint16_t x = (HAL_DISPLAY_WIDTH - text_width) / 2;
    uint16_t y = SCANNER_CONTENT_Y_START +
                 ((HAL_DISPLAY_HEIGHT - SCANNER_CONTENT_Y_START) / 2) -
                 (CHAR_HEIGHT / 2);

    hal_display_draw_text(x, y, msg, COLOR_EMPTY_TEXT, HAL_COLOR_BLACK);
}

/**
 * @brief Render page indicator at the bottom.
 *
 * Shows "Pag X/Y" format.
 */
static void render_page_indicator(uint8_t current_page, uint8_t total_pages)
{
    char indicator[16];
    snprintf(indicator, sizeof(indicator), "Pag %u/%u",
             (unsigned)(current_page + 1), (unsigned)total_pages);

    /* Right-align the indicator */
    uint16_t text_width = (uint16_t)(strlen(indicator) * CHAR_WIDTH);
    uint16_t x = HAL_DISPLAY_WIDTH - text_width - CONTENT_X_MARGIN;

    hal_display_draw_text(x, PAGE_INDICATOR_Y, indicator,
                          COLOR_PAGE_TEXT, HAL_COLOR_BLACK);
}

/* ========================================================================
 * Public API
 * ======================================================================== */

uint8_t screen_scanner_get_total_pages(uint8_t active_count)
{
    if (active_count == 0) {
        return 0;
    }
    return (active_count + SCANNER_ITEMS_PER_PAGE - 1) / SCANNER_ITEMS_PER_PAGE;
}

esp_err_t screen_scanner_render(const aircraft_registry_t *registry)
{
    if (registry == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const ui_state_t *state = ui_manager_get_state();
    if (state == NULL || !state->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Clear content area (below status bar) */
    hal_display_draw_rect(0, SCANNER_CONTENT_Y_START,
                          HAL_DISPLAY_WIDTH,
                          HAL_DISPLAY_HEIGHT - SCANNER_CONTENT_Y_START,
                          HAL_COLOR_BLACK, true);

    /* Collect active entries */
    const aircraft_entry_t *active_entries[MAX_AIRCRAFT];
    uint8_t active_count = collect_active_entries(registry, active_entries, MAX_AIRCRAFT);

    /* Handle empty state */
    if (active_count == 0) {
        render_empty_state();
        return ESP_OK;
    }

    /* Compute pagination */
    uint8_t total_pages = screen_scanner_get_total_pages(active_count);
    uint8_t current_page = state->scanner_page;

    /* Clamp page to valid range */
    if (current_page >= total_pages) {
        current_page = total_pages - 1;
    }

    /* Calculate entries for current page */
    uint8_t start_idx = current_page * SCANNER_ITEMS_PER_PAGE;
    uint8_t end_idx = start_idx + SCANNER_ITEMS_PER_PAGE;
    if (end_idx > active_count) {
        end_idx = active_count;
    }

    uint8_t items_on_page = end_idx - start_idx;

    /* Clamp selected index to valid range for this page */
    uint8_t selected_idx = state->selected_aircraft_idx;
    if (selected_idx >= items_on_page) {
        selected_idx = items_on_page - 1;
    }

    /* Render each row */
    for (uint8_t i = 0; i < items_on_page; i++) {
        bool is_selected = (i == selected_idx);
        render_row(active_entries[start_idx + i], i, is_selected);
    }

    /* Render page indicator */
    render_page_indicator(current_page, total_pages);

    return ESP_OK;
}
