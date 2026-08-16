/**
 * @file simulation_service.c
 * @brief Autonomous Simulation Service implementation.
 */

#include "simulation_service.h"
#include "telemetry_decoder.h"
#include "aircraft_registry.h"
#include "geolocation_service.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static const char *TAG = "simulation_svc";

static bool s_sim_initialized = false;
static bool s_sim_enabled = false;
static uint32_t s_sim_tick_count = 0;

typedef struct {
    char id[AIRCRAFT_ID_MAX_LEN];
    protocol_type_t protocol;
    double center_lat;
    double center_lon;
    float radius_m;
    float angle_deg;
    float angle_speed_deg;
    float alt_base_m;
    float speed_ms;
    float battery_pct;
    float battery_v;
    int16_t rssi_dbm;
} sim_drone_t;

#define SIM_DRONE_COUNT 3
static sim_drone_t s_drones[SIM_DRONE_COUNT] = {
    {
        .id = "BRA-UAS-001",
        .protocol = PROTOCOL_REMOTEID,
        .center_lat = -23.550520,
        .center_lon = -46.633309,
        .radius_m = 420.0f,
        .angle_deg = 30.0f,
        .angle_speed_deg = 2.5f,
        .alt_base_m = 125.0f,
        .speed_ms = 14.5f,
        .battery_pct = 88.0f,
        .battery_v = 15.6f,
        .rssi_dbm = -64
    },
    {
        .id = "ELRS-RACER-9",
        .protocol = PROTOCOL_ELRS,
        .center_lat = -23.550520,
        .center_lon = -46.633309,
        .radius_m = 650.0f,
        .angle_deg = 190.0f,
        .angle_speed_deg = 4.0f,
        .alt_base_m = 78.0f,
        .speed_ms = 24.0f,
        .battery_pct = 68.0f,
        .battery_v = 14.8f,
        .rssi_dbm = -75
    },
    {
        .id = "DJI-MAVIC-4K",
        .protocol = PROTOCOL_DJI,
        .center_lat = -23.550520,
        .center_lon = -46.633309,
        .radius_m = 280.0f,
        .angle_deg = 310.0f,
        .angle_speed_deg = 1.2f,
        .alt_base_m = 55.0f,
        .speed_ms = 7.2f,
        .battery_pct = 92.0f,
        .battery_v = 16.2f,
        .rssi_dbm = -58
    }
};

esp_err_t simulation_service_init(void)
{
    s_sim_initialized = true;
    s_sim_enabled = false;
    s_sim_tick_count = 0;
    ESP_LOGI(TAG, "Simulation service initialized");
    return ESP_OK;
}

void simulation_service_set_enabled(bool enabled)
{
    if (s_sim_enabled == enabled) {
        return;
    }
    s_sim_enabled = enabled;
    ESP_LOGI(TAG, "Simulation mode %s", enabled ? "ENABLED" : "DISABLED");
}

bool simulation_service_is_enabled(void)
{
    return s_sim_enabled;
}

esp_err_t simulation_service_tick(void)
{
    if (!s_sim_initialized || !s_sim_enabled) {
        return ESP_OK;
    }

    aircraft_registry_t *reg = telemetry_decoder_get_registry();
    if (reg == NULL) {
        return ESP_OK;
    }

    uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000ULL);
    s_sim_tick_count++;

    /* Use monitor GPS position if available */
    const gps_position_t *mon_pos = geo_get_monitor_position();
    double ref_lat = (mon_pos && mon_pos->fix_valid) ? mon_pos->latitude : -23.550520;
    double ref_lon = (mon_pos && mon_pos->fix_valid) ? mon_pos->longitude : -46.633309;

    for (int i = 0; i < SIM_DRONE_COUNT; i++) {
        sim_drone_t *d = &s_drones[i];

        /* Advance position along orbit */
        d->angle_deg += d->angle_speed_deg;
        if (d->angle_deg >= 360.0f) {
            d->angle_deg -= 360.0f;
        }

        /* Earth coords conversion */
        float rad = d->angle_deg * 0.0174532925f;
        double dlat = (d->radius_m * cosf(rad)) / 111320.0;
        double dlon = (d->radius_m * sinf(rad)) / (111320.0 * cos(ref_lat * 0.0174532925));

        double drone_lat = ref_lat + dlat;
        double drone_lon = ref_lon + dlon;
        float drone_alt = d->alt_base_m + sinf((float)s_sim_tick_count * 0.1f + (float)i) * 5.0f;

        /* Find or create entry in registry */
        aircraft_entry_t *entry = registry_find_or_create(reg, d->id);
        if (entry != NULL) {
            entry->protocol = d->protocol;
            entry->protocol_confidence = CONFIDENCE_HIGH;
            entry->status = AIRCRAFT_STATUS_ACTIVE;
            entry->last_rssi_dbm = d->rssi_dbm + (int16_t)(sinf((float)s_sim_tick_count * 0.2f) * 4.0f);
            entry->last_seen_utc_ms = now_ms;
            if (entry->first_seen_utc_ms == 0) {
                entry->first_seen_utc_ms = now_ms;
            }

            /* Telemetry */
            strncpy(entry->last_telemetry.uas_id, d->id, sizeof(entry->last_telemetry.uas_id) - 1);
            entry->last_telemetry.lat = drone_lat;
            entry->last_telemetry.lon = drone_lon;
            entry->last_telemetry.altitude_m = drone_alt;
            entry->last_telemetry.speed_ms = d->speed_ms;
            entry->last_telemetry.battery_pct = d->battery_pct;
            entry->last_telemetry.battery_voltage = d->battery_v;
            entry->last_telemetry.rssi_dbm = entry->last_rssi_dbm;
            entry->last_telemetry.has_position = true;
            entry->last_telemetry.has_altitude = true;
            entry->last_telemetry.has_speed = true;
            entry->last_telemetry.has_battery = true;

            /* Relative position */
            gps_position_t fake_mon = {
                .latitude = ref_lat,
                .longitude = ref_lon,
                .altitude_m = 760.0f,
                .fix_valid = true
            };
            geo_calculate_relative(&fake_mon, drone_lat, drone_lon, &entry->relative_pos);

            /* Pilot position */
            entry->pilot.lat = ref_lat + 0.0002;
            entry->pilot.lon = ref_lon + 0.0002;
            entry->pilot.position_available = true;
            entry->pilot.confidence = PILOT_CONFIDENCE_CONFIRMED;
            geo_calculate_relative(&fake_mon, entry->pilot.lat, entry->pilot.lon, &entry->pilot.relative_to_monitor);
        }
    }

    return ESP_OK;
}

esp_err_t simulation_service_deinit(void)
{
    s_sim_initialized = false;
    s_sim_enabled = false;
    return ESP_OK;
}
