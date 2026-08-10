/**
 * @file geolocation_service.c
 * @brief Geolocation Service implementation.
 *
 * Provides geographic calculations using the Haversine formula for
 * great-circle distance and atan2-based forward azimuth computation.
 *
 * Earth radius constant: 6,371,000 meters (mean radius).
 */

#include "geolocation_service.h"
#include "hal_gps.h"

#include <math.h>
#include <string.h>

/* ========================================================================
 * Constants
 * ======================================================================== */

/** Mean Earth radius in meters */
#define EARTH_RADIUS_M 6371000.0

/** Conversion factor: degrees to radians */
#define DEG_TO_RAD (M_PI / 180.0)

/** Conversion factor: radians to degrees */
#define RAD_TO_DEG (180.0 / M_PI)

/* ========================================================================
 * Module state
 * ======================================================================== */

static bool s_initialized = false;
static gps_position_t s_monitor_position;

/* ========================================================================
 * Public API
 * ======================================================================== */

esp_err_t geo_service_init(void)
{
    /* Verify GPS HAL is available */
    hal_status_t gps_status = hal_gps_get_status();
    if (gps_status == HAL_STATUS_INACTIVE) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Cache initial position from GPS */
    memset(&s_monitor_position, 0, sizeof(s_monitor_position));
    hal_gps_get_position(&s_monitor_position);

    s_initialized = true;
    return ESP_OK;
}

esp_err_t geo_calculate_relative(const gps_position_t *from,
                                 double to_lat, double to_lon,
                                 relative_position_t *result)
{
    if (from == NULL || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* If the source position has no valid fix, mark result invalid */
    if (!from->fix_valid) {
        result->distance_m = 0.0f;
        result->azimuth_deg = 0.0f;
        result->valid = false;
        return ESP_OK;
    }

    /* Convert coordinates to radians */
    double lat1 = from->latitude * DEG_TO_RAD;
    double lon1 = from->longitude * DEG_TO_RAD;
    double lat2 = to_lat * DEG_TO_RAD;
    double lon2 = to_lon * DEG_TO_RAD;

    /* ---- Haversine formula for distance ---- */
    double dlat = lat2 - lat1;
    double dlon = lon2 - lon1;

    double sin_dlat_2 = sin(dlat / 2.0);
    double sin_dlon_2 = sin(dlon / 2.0);

    double a = sin_dlat_2 * sin_dlat_2 +
               cos(lat1) * cos(lat2) * sin_dlon_2 * sin_dlon_2;

    /* Clamp 'a' to [0, 1] to guard against floating-point rounding */
    if (a < 0.0) a = 0.0;
    if (a > 1.0) a = 1.0;

    double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
    double distance = EARTH_RADIUS_M * c;

    /* ---- Forward azimuth (initial bearing) ---- */
    double y = sin(dlon) * cos(lat2);
    double x = cos(lat1) * sin(lat2) - sin(lat1) * cos(lat2) * cos(dlon);
    double bearing_rad = atan2(y, x);

    /* Normalize bearing to [0, 360) degrees */
    double bearing_deg = bearing_rad * RAD_TO_DEG;
    if (bearing_deg < 0.0) {
        bearing_deg += 360.0;
    }
    /* Guard against floating-point producing exactly 360.0 */
    if (bearing_deg >= 360.0) {
        bearing_deg -= 360.0;
    }

    result->distance_m = (float)distance;
    result->azimuth_deg = (float)bearing_deg;
    result->valid = true;

    return ESP_OK;
}

const gps_position_t* geo_get_monitor_position(void)
{
    if (!s_initialized) {
        return NULL;
    }

    /* Refresh position from GPS HAL */
    hal_gps_get_position(&s_monitor_position);
    return &s_monitor_position;
}

bool geo_has_valid_fix(void)
{
    return hal_gps_has_fix();
}
