/**
 * @file screen_map.c
 * @brief Map Screen — 2D top-down positional view of aircraft and pilots.
 *
 * Renders a bird's-eye map centered on the operator's GPS position.
 * Aircraft and pilot positions are converted from lat/lon deltas to pixel
 * offsets using the current map_scale_m. The scale represents the distance
 * in meters from center to the edge of the shorter axis (height/2).
 *
 * Coordinate system:
 *   - Screen X increases to the right (East)
 *   - Screen Y increases downward (South in geographic terms)
 *   - Operator at center
 *
 * Validates: Requirements 9.4
 */

#include "screen_map.h"
#include "ui_manager.h"
#include "hal_display.h"
#include "geolocation_service.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ========================================================================
 * Internal Constants
 * ======================================================================== */

/** Earth radius in meters for lat/lon → meter conversion */
#define EARTH_RADIUS_M          6371000.0

/** Degrees to radians conversion factor */
#define DEG_TO_RAD              (M_PI / 180.0)

/** Color definitions for map elements */
#define MAP_COLOR_BG            HAL_COLOR_BLACK
#define MAP_COLOR_OPERATOR      HAL_COLOR_GREEN
#define MAP_COLOR_AIRCRAFT      HAL_COLOR_YELLOW
#define MAP_COLOR_PILOT         HAL_COLOR_CYAN
#define MAP_COLOR_SCALE         HAL_COLOR_WHITE
#define MAP_COLOR_GRID          0x2104  /* Dark gray RGB565 */
#define MAP_COLOR_NO_DATA       0x7BEF  /* Medium gray */
#define MAP_COLOR_DISTANCE      HAL_COLOR_WHITE

/** Scale bar position (bottom-left of content area) */
#define SCALE_BAR_X             4
#define SCALE_BAR_Y             (MAP_CONTENT_Y_START + MAP_CONTENT_HEIGHT - 12)
#define SCALE_BAR_MAX_WIDTH     60

/* ========================================================================
 * Internal Helpers
 * ======================================================================== */

/**
 * @brief Convert a lat/lon delta (in degrees) to pixel offset from center.
 *
 * Uses equirectangular approximation (valid for short distances):
 *   dx_meters = dlon_deg × cos(ref_lat) × (π/180) × R
 *   dy_meters = dlat_deg × (π/180) × R
 *
 * Then maps meters to pixels using the current scale:
 *   pixels_per_meter = (MAP_CONTENT_HEIGHT / 2) / map_scale_m
 *
 * @param[in]  dlat_deg   Latitude difference (target - operator) in degrees
 * @param[in]  dlon_deg   Longitude difference (target - operator) in degrees
 * @param[in]  ref_lat    Reference latitude (operator) for cos correction
 * @param[in]  scale_m    Current map scale (meters from center to edge)
 * @param[out] px         Pixel X offset from center (positive = right/East)
 * @param[out] py         Pixel Y offset from center (positive = down/South)
 */
static void latlon_delta_to_pixel(double dlat_deg, double dlon_deg,
                                  double ref_lat, float scale_m,
                                  int16_t *px, int16_t *py)
{
    if (scale_m <= 0.0f) {
        *px = 0;
        *py = 0;
        return;
    }

    /* Convert lat/lon deltas to meters (equirectangular approximation) */
    double cos_lat = cos(ref_lat * DEG_TO_RAD);
    double dx_m = dlon_deg * DEG_TO_RAD * EARTH_RADIUS_M * cos_lat;
    double dy_m = dlat_deg * DEG_TO_RAD * EARTH_RADIUS_M;

    /* Pixels per meter: half content height maps to scale_m */
    float pixels_per_meter = (float)MAP_CENTER_Y / scale_m;

    /* Convert to pixel offset. East → +X, North → -Y (screen Y inverted) */
    *px = (int16_t)(dx_m * pixels_per_meter);
    *py = (int16_t)(-dy_m * pixels_per_meter);
}

/**
 * @brief Check if a pixel coordinate is within the content area bounds.
 */
static bool is_in_content_area(int16_t screen_x, int16_t screen_y)
{
    return (screen_x >= 0 && screen_x < MAP_CONTENT_WIDTH &&
            screen_y >= MAP_CONTENT_Y_START &&
            screen_y < (MAP_CONTENT_Y_START + MAP_CONTENT_HEIGHT));
}

/**
 * @brief Draw the operator cross-hair icon at center of map.
 */
static void draw_operator_icon(void)
{
    uint16_t cx = MAP_CENTER_X;
    uint16_t cy = MAP_CONTENT_Y_START + MAP_CENTER_Y;
    int s = MAP_OPERATOR_ICON_SIZE;

    /* Horizontal line */
    for (int dx = -s; dx <= s; dx++) {
        hal_display_draw_pixel((uint16_t)(cx + dx), cy, MAP_COLOR_OPERATOR);
    }
    /* Vertical line */
    for (int dy = -s; dy <= s; dy++) {
        hal_display_draw_pixel(cx, (uint16_t)(cy + dy), MAP_COLOR_OPERATOR);
    }
    /* Small circle at center (4 corner pixels) */
    hal_display_draw_pixel(cx - 1, cy - 1, MAP_COLOR_OPERATOR);
    hal_display_draw_pixel(cx + 1, cy - 1, MAP_COLOR_OPERATOR);
    hal_display_draw_pixel(cx - 1, cy + 1, MAP_COLOR_OPERATOR);
    hal_display_draw_pixel(cx + 1, cy + 1, MAP_COLOR_OPERATOR);
}

/**
 * @brief Draw an aircraft icon (filled diamond) at the given screen position.
 */
static void draw_aircraft_icon(int16_t sx, int16_t sy)
{
    int s = MAP_AIRCRAFT_ICON_SIZE;

    /* Draw a diamond shape */
    for (int d = 0; d <= s; d++) {
        /* Top half */
        if (is_in_content_area(sx - d, sy - (s - d))) {
            hal_display_draw_pixel((uint16_t)(sx - d),
                                   (uint16_t)(sy - (s - d)),
                                   MAP_COLOR_AIRCRAFT);
        }
        if (d > 0 && is_in_content_area(sx + d, sy - (s - d))) {
            hal_display_draw_pixel((uint16_t)(sx + d),
                                   (uint16_t)(sy - (s - d)),
                                   MAP_COLOR_AIRCRAFT);
        }
        /* Bottom half */
        if (is_in_content_area(sx - d, sy + (s - d))) {
            hal_display_draw_pixel((uint16_t)(sx - d),
                                   (uint16_t)(sy + (s - d)),
                                   MAP_COLOR_AIRCRAFT);
        }
        if (d > 0 && is_in_content_area(sx + d, sy + (s - d))) {
            hal_display_draw_pixel((uint16_t)(sx + d),
                                   (uint16_t)(sy + (s - d)),
                                   MAP_COLOR_AIRCRAFT);
        }
    }
}

/**
 * @brief Draw a pilot icon (small square outline) at the given screen position.
 */
static void draw_pilot_icon(int16_t sx, int16_t sy)
{
    int s = MAP_PILOT_ICON_SIZE;

    /* Draw square outline */
    for (int i = -s; i <= s; i++) {
        /* Top edge */
        if (is_in_content_area(sx + i, sy - s)) {
            hal_display_draw_pixel((uint16_t)(sx + i), (uint16_t)(sy - s),
                                   MAP_COLOR_PILOT);
        }
        /* Bottom edge */
        if (is_in_content_area(sx + i, sy + s)) {
            hal_display_draw_pixel((uint16_t)(sx + i), (uint16_t)(sy + s),
                                   MAP_COLOR_PILOT);
        }
        /* Left edge */
        if (is_in_content_area(sx - s, sy + i)) {
            hal_display_draw_pixel((uint16_t)(sx - s), (uint16_t)(sy + i),
                                   MAP_COLOR_PILOT);
        }
        /* Right edge */
        if (is_in_content_area(sx + s, sy + i)) {
            hal_display_draw_pixel((uint16_t)(sx + s), (uint16_t)(sy + i),
                                   MAP_COLOR_PILOT);
        }
    }
}

/**
 * @brief Draw the scale bar and label at the bottom-left of the map.
 *
 * Shows a horizontal bar with a label indicating the distance it represents.
 */
static void draw_scale_indicator(float scale_m)
{
    /* Determine a nice round distance for the scale bar */
    /* The bar should be about 1/4 of the visible width in distance */
    float quarter_scale = scale_m * 0.5f;

    /* Find a nice round number */
    float nice_distance;
    if (quarter_scale >= 5000.0f) {
        nice_distance = 5000.0f;
    } else if (quarter_scale >= 2000.0f) {
        nice_distance = 2000.0f;
    } else if (quarter_scale >= 1000.0f) {
        nice_distance = 1000.0f;
    } else if (quarter_scale >= 500.0f) {
        nice_distance = 500.0f;
    } else if (quarter_scale >= 200.0f) {
        nice_distance = 200.0f;
    } else if (quarter_scale >= 100.0f) {
        nice_distance = 100.0f;
    } else if (quarter_scale >= 50.0f) {
        nice_distance = 50.0f;
    } else if (quarter_scale >= 20.0f) {
        nice_distance = 20.0f;
    } else {
        nice_distance = 10.0f;
    }

    /* Calculate bar width in pixels */
    float pixels_per_meter = (float)MAP_CENTER_Y / scale_m;
    int bar_width = (int)(nice_distance * pixels_per_meter);
    if (bar_width > SCALE_BAR_MAX_WIDTH) {
        bar_width = SCALE_BAR_MAX_WIDTH;
    }
    if (bar_width < 10) {
        bar_width = 10;
    }

    /* Draw the scale bar */
    uint16_t bar_y = SCALE_BAR_Y + 8;
    hal_display_draw_rect(SCALE_BAR_X, bar_y, (uint16_t)bar_width, 2,
                          MAP_COLOR_SCALE, true);

    /* Draw end ticks */
    hal_display_draw_rect(SCALE_BAR_X, bar_y - 2, 1, 6,
                          MAP_COLOR_SCALE, true);
    hal_display_draw_rect(SCALE_BAR_X + (uint16_t)bar_width - 1, bar_y - 2, 1, 6,
                          MAP_COLOR_SCALE, true);

    /* Draw scale label */
    char label[16];
    if (nice_distance >= 1000.0f) {
        snprintf(label, sizeof(label), "%.0fkm", nice_distance / 1000.0f);
    } else {
        snprintf(label, sizeof(label), "%.0fm", nice_distance);
    }
    hal_display_draw_text(SCALE_BAR_X, SCALE_BAR_Y, label,
                          MAP_COLOR_SCALE, MAP_COLOR_BG);
}

/**
 * @brief Draw distance text next to an aircraft icon.
 *
 * Shows the distance in meters (or km if >1000m) next to the icon.
 */
static void draw_distance_label(int16_t sx, int16_t sy, float distance_m)
{
    char dist_buf[12];
    if (distance_m >= 1000.0f) {
        snprintf(dist_buf, sizeof(dist_buf), "%.1fkm", distance_m / 1000.0f);
    } else {
        snprintf(dist_buf, sizeof(dist_buf), "%.0fm", distance_m);
    }

    /* Draw label to the right of the icon, offset by icon size + 2px */
    int16_t label_x = sx + MAP_AIRCRAFT_ICON_SIZE + 2;
    int16_t label_y = sy - 3; /* Vertically center text with icon */

    if (label_x >= 0 && label_x < (MAP_CONTENT_WIDTH - 20) &&
        label_y >= MAP_CONTENT_Y_START && label_y < (MAP_CONTENT_Y_START + MAP_CONTENT_HEIGHT)) {
        hal_display_draw_text((uint16_t)label_x, (uint16_t)label_y, dist_buf,
                              MAP_COLOR_DISTANCE, MAP_COLOR_BG);
    }
}

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

esp_err_t screen_map_render(const aircraft_registry_t *registry)
{
    if (registry == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const ui_state_t *state = ui_manager_get_state();
    if (state == NULL || !state->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    float scale_m = state->map_scale_m;

    /* Clear the content area */
    hal_display_draw_rect(0, MAP_CONTENT_Y_START, MAP_CONTENT_WIDTH,
                          MAP_CONTENT_HEIGHT, MAP_COLOR_BG, true);

    /* Get operator position for coordinate conversion */
    const gps_position_t *monitor_pos = geo_get_monitor_position();
    double ref_lat = 0.0;
    double ref_lon = 0.0;
    bool has_monitor_pos = false;

    if (monitor_pos != NULL && monitor_pos->fix_valid) {
        ref_lat = monitor_pos->latitude;
        ref_lon = monitor_pos->longitude;
        has_monitor_pos = true;
    }

    /* Draw range rings (concentric circles approximated with rect outlines) */
    /* Draw a crosshair grid through center for orientation */
    uint16_t center_screen_x = MAP_CENTER_X;
    uint16_t center_screen_y = MAP_CONTENT_Y_START + MAP_CENTER_Y;

    /* Horizontal center line (dotted) */
    for (int x = 0; x < MAP_CONTENT_WIDTH; x += 4) {
        hal_display_draw_pixel((uint16_t)x, center_screen_y, MAP_COLOR_GRID);
    }
    /* Vertical center line (dotted) */
    for (int y = MAP_CONTENT_Y_START; y < MAP_CONTENT_Y_START + MAP_CONTENT_HEIGHT; y += 4) {
        hal_display_draw_pixel(center_screen_x, (uint16_t)y, MAP_COLOR_GRID);
    }

    /* Draw aircraft positions */
    for (int i = 0; i < MAX_AIRCRAFT; i++) {
        const aircraft_entry_t *entry = &registry->entries[i];
        if (!entry->slot_occupied) {
            continue;
        }
        if (entry->status != AIRCRAFT_STATUS_ACTIVE) {
            continue;
        }

        /* Only draw if aircraft has a known position */
        if (!entry->last_telemetry.has_position) {
            continue;
        }

        if (!has_monitor_pos) {
            /* Without operator position, we cannot plot relative positions */
            continue;
        }

        /* Calculate pixel offset from center */
        double dlat = entry->last_telemetry.lat - ref_lat;
        double dlon = entry->last_telemetry.lon - ref_lon;

        int16_t px, py;
        latlon_delta_to_pixel(dlat, dlon, ref_lat, scale_m, &px, &py);

        /* Convert to screen coordinates */
        int16_t screen_x = (int16_t)center_screen_x + px;
        int16_t screen_y = (int16_t)center_screen_y + py;

        /* Draw aircraft icon if within bounds */
        if (is_in_content_area(screen_x, screen_y)) {
            draw_aircraft_icon(screen_x, screen_y);

            /* Draw distance label if relative position is valid */
            if (entry->relative_pos.valid) {
                draw_distance_label(screen_x, screen_y,
                                    entry->relative_pos.distance_m);
            }
        }

        /* Draw pilot position if available */
        if (entry->pilot.position_available && has_monitor_pos) {
            double pilot_dlat = entry->pilot.lat - ref_lat;
            double pilot_dlon = entry->pilot.lon - ref_lon;

            int16_t pilot_px, pilot_py;
            latlon_delta_to_pixel(pilot_dlat, pilot_dlon, ref_lat, scale_m,
                                  &pilot_px, &pilot_py);

            int16_t pilot_sx = (int16_t)center_screen_x + pilot_px;
            int16_t pilot_sy = (int16_t)center_screen_y + pilot_py;

            if (is_in_content_area(pilot_sx, pilot_sy)) {
                draw_pilot_icon(pilot_sx, pilot_sy);
            }
        }
    }

    /* Draw operator icon (on top of everything else) */
    draw_operator_icon();

    /* Draw scale indicator */
    draw_scale_indicator(scale_m);

    /* If no GPS fix, show warning */
    if (!has_monitor_pos) {
        hal_display_draw_text(MAP_CENTER_X - 48, center_screen_y - 4,
                              "GPS sem fix", MAP_COLOR_NO_DATA, MAP_COLOR_BG);
    }

    return ESP_OK;
}

esp_err_t screen_map_render_empty(void)
{
    const ui_state_t *state = ui_manager_get_state();
    if (state == NULL || !state->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    float scale_m = state->map_scale_m;

    /* Clear the content area */
    hal_display_draw_rect(0, MAP_CONTENT_Y_START, MAP_CONTENT_WIDTH,
                          MAP_CONTENT_HEIGHT, MAP_COLOR_BG, true);

    /* Draw center grid lines */
    uint16_t center_screen_x = MAP_CENTER_X;
    uint16_t center_screen_y = MAP_CONTENT_Y_START + MAP_CENTER_Y;

    for (int x = 0; x < MAP_CONTENT_WIDTH; x += 4) {
        hal_display_draw_pixel((uint16_t)x, center_screen_y, MAP_COLOR_GRID);
    }
    for (int y = MAP_CONTENT_Y_START; y < MAP_CONTENT_Y_START + MAP_CONTENT_HEIGHT; y += 4) {
        hal_display_draw_pixel(center_screen_x, (uint16_t)y, MAP_COLOR_GRID);
    }

    /* Draw operator icon */
    draw_operator_icon();

    /* Draw scale indicator */
    draw_scale_indicator(scale_m);

    /* Show empty message */
    hal_display_draw_text(MAP_CENTER_X - 60, center_screen_y + 20,
                          "Nenhuma aeronave", MAP_COLOR_NO_DATA, MAP_COLOR_BG);

    return ESP_OK;
}
