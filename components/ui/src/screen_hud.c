/**
 * @file screen_hud.c
 * @brief Dynamic Real-Time HUD Dashboard Screen implementation.
 */

#include "screen_hud.h"
#include "hal_display.h"
#include "geolocation_service.h"
#include "ui_manager.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define HUD_RADAR_CX      38
#define HUD_RADAR_CY      74
#define HUD_RADAR_R       30

#define COLOR_HUD_BG      HAL_COLOR_BLACK
#define COLOR_HUD_GRID    0x0410  /* Dark Cyan/Green */
#define COLOR_HUD_SWEEP   0x07E0  /* Bright Green */
#define COLOR_HUD_BLIP    0xFFE0  /* Yellow */
#define COLOR_HUD_TEXT    HAL_COLOR_WHITE
#define COLOR_HUD_CYAN    HAL_COLOR_CYAN
#define COLOR_HUD_RED     HAL_COLOR_RED
#define COLOR_HUD_GREEN   HAL_COLOR_GREEN
#define COLOR_HUD_PANEL   0x18C7  /* Panel Blue/Gray */

static bool s_hud_initialized = false;
static uint8_t s_selected_ac_idx = 0;

esp_err_t screen_hud_init(void)
{
    s_hud_initialized = true;
    s_selected_ac_idx = 0;
    return ESP_OK;
}

static void draw_radar_scope(const aircraft_registry_t *registry, uint32_t now_ms)
{
    /* Range rings */
    for (int r = 10; r <= HUD_RADAR_R; r += 10) {
        for (int a = 0; a < 360; a += 15) {
            float rad = (float)a * 0.0174532925f;
            int16_t x = HUD_RADAR_CX + (int16_t)(cosf(rad) * (float)r);
            int16_t y = HUD_RADAR_CY + (int16_t)(sinf(rad) * (float)r);
            hal_display_draw_pixel((uint16_t)x, (uint16_t)y, COLOR_HUD_GRID);
        }
    }

    /* Cross hairs */
    for (int d = -HUD_RADAR_R; d <= HUD_RADAR_R; d += 2) {
        hal_display_draw_pixel((uint16_t)(HUD_RADAR_CX + d), HUD_RADAR_CY, COLOR_HUD_GRID);
        hal_display_draw_pixel(HUD_RADAR_CX, (uint16_t)(HUD_RADAR_CY + d), COLOR_HUD_GRID);
    }

    /* Rotating sweep line */
    float sweep_angle = (float)((now_ms / 10) % 360) * 0.0174532925f;
    for (int r = 0; r <= HUD_RADAR_R; r++) {
        int16_t sx = HUD_RADAR_CX + (int16_t)(cosf(sweep_angle) * (float)r);
        int16_t sy = HUD_RADAR_CY + (int16_t)(sinf(sweep_angle) * (float)r);
        hal_display_draw_pixel((uint16_t)sx, (uint16_t)sy, COLOR_HUD_SWEEP);
    }

    /* Operator center dot */
    hal_display_draw_pixel(HUD_RADAR_CX, HUD_RADAR_CY, COLOR_HUD_GREEN);
    hal_display_draw_pixel(HUD_RADAR_CX + 1, HUD_RADAR_CY, COLOR_HUD_GREEN);
    hal_display_draw_pixel(HUD_RADAR_CX, HUD_RADAR_CY + 1, COLOR_HUD_GREEN);
    hal_display_draw_pixel(HUD_RADAR_CX + 1, HUD_RADAR_CY + 1, COLOR_HUD_GREEN);

    /* Plot detected aircraft blips */
    if (registry != NULL) {
        for (int i = 0; i < MAX_AIRCRAFT; i++) {
            const aircraft_entry_t *entry = &registry->entries[i];
            if (!entry->slot_occupied || entry->status != AIRCRAFT_STATUS_ACTIVE) {
                continue;
            }

            float dist = entry->relative_pos.valid ? entry->relative_pos.distance_m : 500.0f;
            float az = entry->relative_pos.valid ? entry->relative_pos.azimuth_deg : (float)(i * 60);

            /* Scale distance to radar radius (0 - 2000m max range) */
            float norm_r = (dist / 2000.0f) * (float)HUD_RADAR_R;
            if (norm_r > (float)HUD_RADAR_R) norm_r = (float)HUD_RADAR_R;
            if (norm_r < 4.0f) norm_r = 4.0f;

            /* Azimuth 0 = North (-Y) */
            float az_rad = (az - 90.0f) * 0.0174532925f;
            int16_t bx = HUD_RADAR_CX + (int16_t)(cosf(az_rad) * norm_r);
            int16_t by = HUD_RADAR_CY + (int16_t)(sinf(az_rad) * norm_r);

            /* Blip dot with box */
            uint16_t blip_color = (i == s_selected_ac_idx) ? HAL_COLOR_YELLOW : HAL_COLOR_RED;
            hal_display_draw_rect((uint16_t)(bx - 1), (uint16_t)(by - 1), 3, 3, blip_color, true);
        }
    }
}

static void draw_drone_silhouette(uint16_t cx, uint16_t cy, bool active)
{
    uint16_t col_frame = active ? COLOR_HUD_CYAN : 0x4208;
    uint16_t col_rotor = active ? HAL_COLOR_YELLOW : 0x7BEF;

    /* Central body */
    hal_display_draw_rect(cx - 3, cy - 3, 7, 7, col_frame, true);
    hal_display_draw_pixel(cx, cy, active ? HAL_COLOR_GREEN : HAL_COLOR_RED);

    /* Arms */
    for (int i = 3; i <= 8; i++) {
        hal_display_draw_pixel((uint16_t)(cx - i), (uint16_t)(cy - i), col_frame);
        hal_display_draw_pixel((uint16_t)(cx + i), (uint16_t)(cy - i), col_frame);
        hal_display_draw_pixel((uint16_t)(cx - i), (uint16_t)(cy + i), col_frame);
        hal_display_draw_pixel((uint16_t)(cx + i), (uint16_t)(cy + i), col_frame);
    }

    /* Rotors */
    hal_display_draw_rect(cx - 10, cy - 10, 5, 2, col_rotor, true);
    hal_display_draw_rect(cx + 6, cy - 10, 5, 2, col_rotor, true);
    hal_display_draw_rect(cx - 10, cy + 9, 5, 2, col_rotor, true);
    hal_display_draw_rect(cx + 6, cy + 9, 5, 2, col_rotor, true);
}

esp_err_t screen_hud_render(const aircraft_registry_t *registry)
{
    if (!s_hud_initialized) {
        screen_hud_init();
    }

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

    /* Clear HUD content area (below status bar) */
    hal_display_draw_rect(0, UI_STATUS_BAR_HEIGHT, HAL_DISPLAY_WIDTH,
                          HAL_DISPLAY_HEIGHT - UI_STATUS_BAR_HEIGHT, COLOR_HUD_BG, true);

    /* Top HUD banner */
    hal_display_draw_rect(0, UI_STATUS_BAR_HEIGHT, HAL_DISPLAY_WIDTH, 12, 0x0841, true);
    hal_display_draw_text(4, UI_STATUS_BAR_HEIGHT + 2, "TACTICAL HUD", HAL_COLOR_CYAN, 0x0841);
    
    char count_str[16];
    uint8_t total_active = registry ? registry_get_active_count(registry) : 0;
    snprintf(count_str, sizeof(count_str), "TARGETS: %u", total_active);
    hal_display_draw_text(160, UI_STATUS_BAR_HEIGHT + 2, count_str,
                          total_active > 0 ? HAL_COLOR_YELLOW : COLOR_HUD_TEXT, 0x0841);

    /* Draw Radar Scope */
    draw_radar_scope(registry, now_ms);
    hal_display_draw_text(16, HUD_RADAR_CY + HUD_RADAR_R + 2, "RADAR 2KM", COLOR_HUD_CYAN, COLOR_HUD_BG);

    /* Vertical separator */
    hal_display_draw_rect(76, UI_STATUS_BAR_HEIGHT + 13, 1, 92, COLOR_HUD_PANEL, true);

    /* Find selected or first active aircraft */
    const aircraft_entry_t *target = NULL;
    if (registry != NULL && total_active > 0) {
        uint8_t current_idx = 0;
        for (int i = 0; i < MAX_AIRCRAFT; i++) {
            if (registry->entries[i].slot_occupied &&
                registry->entries[i].status == AIRCRAFT_STATUS_ACTIVE) {
                if (current_idx == s_selected_ac_idx) {
                    target = &registry->entries[i];
                    break;
                }
                current_idx++;
            }
        }
        if (target == NULL) {
            /* Fallback to first available */
            for (int i = 0; i < MAX_AIRCRAFT; i++) {
                if (registry->entries[i].slot_occupied &&
                    registry->entries[i].status == AIRCRAFT_STATUS_ACTIVE) {
                    target = &registry->entries[i];
                    s_selected_ac_idx = 0;
                    break;
                }
            }
        }
    }

    if (target != NULL) {
        /* Draw Drone Silhouette */
        draw_drone_silhouette(92, UI_STATUS_BAR_HEIGHT + 30, true);

        /* Target ID & Protocol */
        char id_buf[32];
        snprintf(id_buf, sizeof(id_buf), "ID: %s", target->id);
        hal_display_draw_text(108, UI_STATUS_BAR_HEIGHT + 16, id_buf, HAL_COLOR_YELLOW, COLOR_HUD_BG);

        const char *proto_name = "UNK";
        switch (target->protocol) {
            case PROTOCOL_ELRS: proto_name = "ELRS 900/2.4"; break;
            case PROTOCOL_DJI: proto_name = "DJI OCUSYNC"; break;
            case PROTOCOL_WIFI: proto_name = "WIFI RID"; break;
            case PROTOCOL_MAVLINK: proto_name = "MAVLINK"; break;
            case PROTOCOL_CROSSFIRE: proto_name = "CROSSFIRE"; break;
            case PROTOCOL_FRSKY: proto_name = "FRSKY"; break;
            case PROTOCOL_REMOTEID: proto_name = "REMOTE ID"; break;
            default: proto_name = "UNKNOWN"; break;
        }
        char proto_buf[32];
        snprintf(proto_buf, sizeof(proto_buf), "LINK: %s", proto_name);
        hal_display_draw_text(108, UI_STATUS_BAR_HEIGHT + 26, proto_buf, COLOR_HUD_CYAN, COLOR_HUD_BG);

        /* RSSI & Signal bar */
        char rssi_buf[20];
        snprintf(rssi_buf, sizeof(rssi_buf), "RSSI: %ddBm", (int)target->last_rssi_dbm);
        hal_display_draw_text(82, UI_STATUS_BAR_HEIGHT + 44, rssi_buf, HAL_COLOR_WHITE, COLOR_HUD_BG);

        /* RSSI bar meter (width 40px) */
        int16_t rssi_clamped = target->last_rssi_dbm;
        if (rssi_clamped < -100) rssi_clamped = -100;
        if (rssi_clamped > -40) rssi_clamped = -40;
        int bar_w = (int)((rssi_clamped + 100) * 40 / 60);
        hal_display_draw_rect(170, UI_STATUS_BAR_HEIGHT + 45, 40, 6, 0x2104, true);
        hal_display_draw_rect(170, UI_STATUS_BAR_HEIGHT + 45, (uint16_t)bar_w, 6, COLOR_HUD_GREEN, true);

        /* Altitude & Speed */
        char alt_spd[36];
        float alt = target->last_telemetry.has_altitude ? target->last_telemetry.altitude_m : 0.0f;
        float spd = target->last_telemetry.has_speed ? target->last_telemetry.speed_ms : 0.0f;
        snprintf(alt_spd, sizeof(alt_spd), "ALT:%4.1fm  SPD:%3.1fm/s", (double)alt, (double)spd);
        hal_display_draw_text(82, UI_STATUS_BAR_HEIGHT + 56, alt_spd, COLOR_HUD_TEXT, COLOR_HUD_BG);

        /* Distance & Bearing */
        char dist_az[36];
        float dist = target->relative_pos.valid ? target->relative_pos.distance_m : -1.0f;
        float az = target->relative_pos.valid ? target->relative_pos.azimuth_deg : 0.0f;
        if (dist >= 0) {
            if (dist < 1000.0f) {
                snprintf(dist_az, sizeof(dist_az), "DIST:%3.0fm  AZ:%03.0f*", (double)dist, (double)az);
            } else {
                snprintf(dist_az, sizeof(dist_az), "DIST:%3.1fkm AZ:%03.0f*", (double)(dist / 1000.0f), (double)az);
            }
        } else {
            snprintf(dist_az, sizeof(dist_az), "DIST: N/D    AZ: N/D");
        }
        hal_display_draw_text(82, UI_STATUS_BAR_HEIGHT + 68, dist_az, COLOR_HUD_TEXT, COLOR_HUD_BG);

        /* Battery Level */
        char bat_buf[32];
        if (target->last_telemetry.has_battery) {
            snprintf(bat_buf, sizeof(bat_buf), "BAT: %2.0f%% (%3.1fV)",
                     (double)target->last_telemetry.battery_pct,
                     (double)target->last_telemetry.battery_voltage);
        } else {
            snprintf(bat_buf, sizeof(bat_buf), "BAT: N/D");
        }
        hal_display_draw_text(82, UI_STATUS_BAR_HEIGHT + 80, bat_buf, COLOR_HUD_GREEN, COLOR_HUD_BG);
    } else {
        /* No active targets — display stand-by HUD graphic */
        draw_drone_silhouette(150, UI_STATUS_BAR_HEIGHT + 35, false);
        hal_display_draw_text(100, UI_STATUS_BAR_HEIGHT + 58, "PROCURANDO DRONES...", HAL_COLOR_YELLOW, COLOR_HUD_BG);
        hal_display_draw_text(92, UI_STATUS_BAR_HEIGHT + 72, "VARREDURA RF / RID ATIVA", 0x7BEF, COLOR_HUD_BG);
    }

    /* Bottom Instrument Panel Strip */
    hal_display_draw_rect(0, HAL_DISPLAY_HEIGHT - 16, HAL_DISPLAY_WIDTH, 16, 0x1084, true);
    
    /* GPS summary */
    const gps_position_t *gps = geo_get_monitor_position();
    char gps_str[36];
    if (gps && gps->fix_valid) {
        snprintf(gps_str, sizeof(gps_str), "GPS FIX (%u Sats)", gps->satellites_used);
        hal_display_draw_text(4, HAL_DISPLAY_HEIGHT - 13, gps_str, COLOR_HUD_GREEN, 0x1084);
    } else {
        hal_display_draw_text(4, HAL_DISPLAY_HEIGHT - 13, "GPS SEM FIX", COLOR_HUD_RED, 0x1084);
    }

    /* Shortcut hint */
    hal_display_draw_text(140, HAL_DISPLAY_HEIGHT - 13, "[1-7] TELAS", COLOR_HUD_CYAN, 0x1084);

    return ESP_OK;
}

esp_err_t screen_hud_handle_key(uint8_t key)
{
    switch ((ui_key_t)key) {
        case UI_KEY_UP:
        case UI_KEY_LEFT:
            if (s_selected_ac_idx > 0) {
                s_selected_ac_idx--;
            }
            break;
        case UI_KEY_DOWN:
        case UI_KEY_RIGHT:
            if (s_selected_ac_idx < (MAX_AIRCRAFT - 1)) {
                s_selected_ac_idx++;
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

esp_err_t screen_hud_deinit(void)
{
    s_hud_initialized = false;
    return ESP_OK;
}
