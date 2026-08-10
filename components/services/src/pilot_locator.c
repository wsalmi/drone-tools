/**
 * @file pilot_locator.c
 * @brief Pilot Locator Service implementation.
 *
 * Determines the remote pilot's position using multiple sources with
 * a priority hierarchy:
 *   1. Operator Location from RemoteID → CONFIRMED
 *   2. Home Point from MAVLink HOME_POSITION → ESTIMATED
 *   3. RSSI triangulation (≥3 readings with ≥10m GPS separation) → ESTIMATED
 *   4. No source → UNKNOWN
 *
 * Maintains per-aircraft tracking state in a fixed-size array.
 * RSSI triangulation uses a weighted centroid approach based on signal
 * strength readings from spatially separated monitor positions.
 *
 * Validates: Requirements 6.1, 6.2, 6.3, 6.4, 6.5
 */

#include "pilot_locator.h"
#include "hal_gps.h"

#include <string.h>
#include <math.h>

/* ========================================================================
 * Extern declarations for geolocation service functions.
 * We use these directly rather than including geolocation_service.h
 * to avoid type redefinition conflicts.
 * ======================================================================== */

extern esp_err_t geo_calculate_relative(const gps_position_t *from,
                                        double to_lat, double to_lon,
                                        relative_position_t *result);
extern const gps_position_t* geo_get_monitor_position(void);

/* ========================================================================
 * Module State
 * ======================================================================== */

static pilot_tracking_entry_t s_tracking[PILOT_LOCATOR_MAX_AIRCRAFT];
static bool s_initialized = false;

/* ========================================================================
 * Internal Helpers
 * ======================================================================== */

/**
 * @brief Find tracking entry by aircraft ID.
 * @return Pointer to entry, or NULL if not found.
 */
static pilot_tracking_entry_t *find_entry(const char *aircraft_id)
{
    for (int i = 0; i < PILOT_LOCATOR_MAX_AIRCRAFT; i++) {
        if (s_tracking[i].slot_occupied &&
            strncmp(s_tracking[i].aircraft_id, aircraft_id, AIRCRAFT_ID_MAX_LEN) == 0) {
            return &s_tracking[i];
        }
    }
    return NULL;
}

/**
 * @brief Find or create a tracking entry for an aircraft.
 * @return Pointer to entry, or NULL if no free slots.
 */
static pilot_tracking_entry_t *find_or_create_entry(const char *aircraft_id)
{
    /* Check if already exists */
    pilot_tracking_entry_t *entry = find_entry(aircraft_id);
    if (entry != NULL) {
        return entry;
    }

    /* Find a free slot */
    for (int i = 0; i < PILOT_LOCATOR_MAX_AIRCRAFT; i++) {
        if (!s_tracking[i].slot_occupied) {
            memset(&s_tracking[i], 0, sizeof(pilot_tracking_entry_t));
            strncpy(s_tracking[i].aircraft_id, aircraft_id, AIRCRAFT_ID_MAX_LEN - 1);
            s_tracking[i].aircraft_id[AIRCRAFT_ID_MAX_LEN - 1] = '\0';
            s_tracking[i].slot_occupied = true;
            s_tracking[i].position.confidence = PILOT_CONFIDENCE_UNKNOWN;
            s_tracking[i].position.position_available = false;
            s_tracking[i].source = PILOT_SOURCE_NONE;
            return &s_tracking[i];
        }
    }

    return NULL;
}

/**
 * @brief Calculate distance between two GPS positions in meters.
 *
 * Simple Haversine calculation for determining separation between
 * two monitor positions used in RSSI triangulation.
 */
static float calculate_distance_m(const gps_position_t *a, const gps_position_t *b)
{
    if (!a->fix_valid || !b->fix_valid) {
        return 0.0f;
    }

    const double EARTH_RADIUS = 6371000.0;
    const double DEG_TO_RAD_CONST = M_PI / 180.0;

    double lat1 = a->latitude * DEG_TO_RAD_CONST;
    double lon1 = a->longitude * DEG_TO_RAD_CONST;
    double lat2 = b->latitude * DEG_TO_RAD_CONST;
    double lon2 = b->longitude * DEG_TO_RAD_CONST;

    double dlat = lat2 - lat1;
    double dlon = lon2 - lon1;

    double sin_dlat_2 = sin(dlat / 2.0);
    double sin_dlon_2 = sin(dlon / 2.0);

    double ha = sin_dlat_2 * sin_dlat_2 +
                cos(lat1) * cos(lat2) * sin_dlon_2 * sin_dlon_2;

    if (ha < 0.0) ha = 0.0;
    if (ha > 1.0) ha = 1.0;

    double c = 2.0 * atan2(sqrt(ha), sqrt(1.0 - ha));
    return (float)(EARTH_RADIUS * c);
}

/**
 * @brief Count RSSI readings with sufficient spatial separation.
 *
 * For triangulation to be valid, we need ≥3 readings where each
 * reading's monitor position is ≥10m from at least one other reading.
 *
 * @return Number of spatially-separated readings.
 */
static uint8_t count_separated_readings(const pilot_tracking_entry_t *entry)
{
    if (entry->rssi_count < PILOT_LOCATOR_MIN_RSSI_FOR_TRIANGULATION) {
        return 0;
    }

    /* Count readings that are ≥10m from at least one other reading */
    uint8_t separated_count = 0;
    bool counted[PILOT_LOCATOR_MAX_RSSI_READINGS] = {false};

    for (uint8_t i = 0; i < entry->rssi_count; i++) {
        if (!entry->rssi_readings[i].monitor_pos.fix_valid) {
            continue;
        }

        for (uint8_t j = i + 1; j < entry->rssi_count; j++) {
            if (!entry->rssi_readings[j].monitor_pos.fix_valid) {
                continue;
            }

            float dist = calculate_distance_m(&entry->rssi_readings[i].monitor_pos,
                                              &entry->rssi_readings[j].monitor_pos);

            if (dist >= PILOT_LOCATOR_MIN_SEPARATION_M) {
                if (!counted[i]) {
                    counted[i] = true;
                    separated_count++;
                }
                if (!counted[j]) {
                    counted[j] = true;
                    separated_count++;
                }
            }
        }
    }

    return separated_count;
}

/**
 * @brief Estimate pilot position using RSSI-weighted centroid.
 *
 * Uses the strongest RSSI readings to estimate a weighted centroid
 * position. Readings with higher RSSI (closer to 0) receive more weight,
 * as they indicate the monitor was closer to the pilot at that time.
 *
 * @param[in]  entry      Tracking entry with RSSI history.
 * @param[out] out_lat    Estimated pilot latitude.
 * @param[out] out_lon    Estimated pilot longitude.
 * @return true if estimation was successful, false otherwise.
 */
static bool estimate_by_rssi_triangulation(const pilot_tracking_entry_t *entry,
                                           double *out_lat, double *out_lon)
{
    if (count_separated_readings(entry) < PILOT_LOCATOR_MIN_RSSI_FOR_TRIANGULATION) {
        return false;
    }

    /*
     * Weighted centroid approach:
     * Convert RSSI to linear weight. Stronger signal (less negative) means
     * the pilot is more likely near that monitor position.
     * Weight = 10^((rssi_dbm + 100) / 20)
     * This gives higher weight to stronger (less negative) signals.
     */
    double sum_lat = 0.0;
    double sum_lon = 0.0;
    double sum_weight = 0.0;

    for (uint8_t i = 0; i < entry->rssi_count; i++) {
        if (!entry->rssi_readings[i].monitor_pos.fix_valid) {
            continue;
        }

        /* Convert RSSI to weight: stronger signal → higher weight */
        double rssi_adjusted = (double)(entry->rssi_readings[i].rssi_dbm + 100);
        if (rssi_adjusted < 0.0) rssi_adjusted = 0.1;  /* floor for very weak signals */

        double weight = pow(10.0, rssi_adjusted / 20.0);

        sum_lat += entry->rssi_readings[i].monitor_pos.latitude * weight;
        sum_lon += entry->rssi_readings[i].monitor_pos.longitude * weight;
        sum_weight += weight;
    }

    if (sum_weight <= 0.0) {
        return false;
    }

    *out_lat = sum_lat / sum_weight;
    *out_lon = sum_lon / sum_weight;
    return true;
}

/**
 * @brief Resolve the best pilot position based on source priority.
 *
 * Updates the entry's position field based on available sources.
 */
static void resolve_best_position(pilot_tracking_entry_t *entry)
{
    /* Priority 1: Operator Location (CONFIRMED) */
    if (entry->has_operator_location) {
        entry->position.lat = entry->operator_lat;
        entry->position.lon = entry->operator_lon;
        entry->position.confidence = PILOT_CONFIDENCE_CONFIRMED;
        entry->position.position_available = true;
        entry->source = PILOT_SOURCE_OPERATOR_LOCATION;

        /* Calculate relative position to monitor */
        const gps_position_t *monitor = geo_get_monitor_position();
        if (monitor != NULL) {
            geo_calculate_relative(monitor, entry->operator_lat, entry->operator_lon,
                                   &entry->position.relative_to_monitor);
        } else {
            entry->position.relative_to_monitor.valid = false;
        }
        return;
    }

    /* Priority 2: Home Point (ESTIMATED) */
    if (entry->has_home_point) {
        entry->position.lat = entry->home_lat;
        entry->position.lon = entry->home_lon;
        entry->position.confidence = PILOT_CONFIDENCE_ESTIMATED;
        entry->position.position_available = true;
        entry->source = PILOT_SOURCE_HOME_POINT;

        /* Calculate relative position to monitor */
        const gps_position_t *monitor = geo_get_monitor_position();
        if (monitor != NULL) {
            geo_calculate_relative(monitor, entry->home_lat, entry->home_lon,
                                   &entry->position.relative_to_monitor);
        } else {
            entry->position.relative_to_monitor.valid = false;
        }
        return;
    }

    /* Priority 3: RSSI Triangulation (ESTIMATED) */
    double tri_lat, tri_lon;
    if (estimate_by_rssi_triangulation(entry, &tri_lat, &tri_lon)) {
        entry->position.lat = tri_lat;
        entry->position.lon = tri_lon;
        entry->position.confidence = PILOT_CONFIDENCE_ESTIMATED;
        entry->position.position_available = true;
        entry->source = PILOT_SOURCE_RSSI_TRIANGULATION;

        /* Calculate relative position to monitor */
        const gps_position_t *monitor = geo_get_monitor_position();
        if (monitor != NULL) {
            geo_calculate_relative(monitor, tri_lat, tri_lon,
                                   &entry->position.relative_to_monitor);
        } else {
            entry->position.relative_to_monitor.valid = false;
        }
        return;
    }

    /* No source available → UNKNOWN */
    entry->position.lat = 0.0;
    entry->position.lon = 0.0;
    entry->position.confidence = PILOT_CONFIDENCE_UNKNOWN;
    entry->position.position_available = false;
    entry->position.relative_to_monitor.valid = false;
    entry->source = PILOT_SOURCE_NONE;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

esp_err_t pilot_locator_init(void)
{
    memset(s_tracking, 0, sizeof(s_tracking));
    s_initialized = true;
    return ESP_OK;
}

esp_err_t pilot_locator_update(const char *aircraft_id,
                               const decoded_telemetry_t *telemetry,
                               int16_t rssi_dbm,
                               const gps_position_t *monitor_pos,
                               uint64_t timestamp_ms)
{
    if (aircraft_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    (void)telemetry; /* Reserved for future use by the pipeline */

    pilot_tracking_entry_t *entry = find_or_create_entry(aircraft_id);
    if (entry == NULL) {
        return ESP_ERR_NO_MEM;
    }

    /* Add RSSI reading for triangulation if valid data provided */
    if (rssi_dbm != 0 && monitor_pos != NULL && monitor_pos->fix_valid) {
        pilot_locator_add_rssi_reading(aircraft_id, rssi_dbm,
                                       monitor_pos, timestamp_ms);
    }

    /* Resolve the best position based on all available data */
    resolve_best_position(entry);

    return ESP_OK;
}

esp_err_t pilot_locator_get_position(const char *aircraft_id, pilot_position_t *result)
{
    if (aircraft_id == NULL || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    pilot_tracking_entry_t *entry = find_entry(aircraft_id);
    if (entry == NULL) {
        /* Aircraft not tracked — return UNKNOWN */
        result->lat = 0.0;
        result->lon = 0.0;
        result->confidence = PILOT_CONFIDENCE_UNKNOWN;
        result->position_available = false;
        result->relative_to_monitor.valid = false;
        return ESP_ERR_NOT_FOUND;
    }

    /* Re-resolve position in case monitor position changed */
    resolve_best_position(entry);

    /* Copy result */
    *result = entry->position;
    return ESP_OK;
}

esp_err_t pilot_locator_set_operator_location(const char *aircraft_id,
                                              double operator_lat,
                                              double operator_lon)
{
    if (aircraft_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    pilot_tracking_entry_t *entry = find_or_create_entry(aircraft_id);
    if (entry == NULL) {
        return ESP_ERR_NO_MEM;
    }

    entry->operator_lat = operator_lat;
    entry->operator_lon = operator_lon;
    entry->has_operator_location = true;

    /* Immediately resolve — operator location is highest priority */
    resolve_best_position(entry);

    return ESP_OK;
}

esp_err_t pilot_locator_set_home_point(const char *aircraft_id,
                                       double home_lat,
                                       double home_lon)
{
    if (aircraft_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    pilot_tracking_entry_t *entry = find_or_create_entry(aircraft_id);
    if (entry == NULL) {
        return ESP_ERR_NO_MEM;
    }

    entry->home_lat = home_lat;
    entry->home_lon = home_lon;
    entry->has_home_point = true;

    /* Resolve position — home point overrides RSSI but not operator location */
    resolve_best_position(entry);

    return ESP_OK;
}

esp_err_t pilot_locator_add_rssi_reading(const char *aircraft_id,
                                         int16_t rssi_dbm,
                                         const gps_position_t *monitor_pos,
                                         uint64_t timestamp_ms)
{
    if (aircraft_id == NULL || monitor_pos == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Skip readings without valid GPS fix */
    if (!monitor_pos->fix_valid) {
        return ESP_OK;
    }

    pilot_tracking_entry_t *entry = find_or_create_entry(aircraft_id);
    if (entry == NULL) {
        return ESP_ERR_NO_MEM;
    }

    /* Store reading in circular buffer */
    uint8_t idx;
    if (entry->rssi_count < PILOT_LOCATOR_MAX_RSSI_READINGS) {
        idx = entry->rssi_count;
        entry->rssi_count++;
    } else {
        /* Buffer full — overwrite oldest (shift left) */
        memmove(&entry->rssi_readings[0], &entry->rssi_readings[1],
                sizeof(rssi_reading_t) * (PILOT_LOCATOR_MAX_RSSI_READINGS - 1));
        idx = PILOT_LOCATOR_MAX_RSSI_READINGS - 1;
    }

    entry->rssi_readings[idx].rssi_dbm = rssi_dbm;
    entry->rssi_readings[idx].monitor_pos = *monitor_pos;
    entry->rssi_readings[idx].timestamp_ms = timestamp_ms;

    return ESP_OK;
}

void pilot_locator_reset(void)
{
    memset(s_tracking, 0, sizeof(s_tracking));
}
