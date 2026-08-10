/**
 * @file geolocation_service.h
 * @brief Geolocation Service — manages monitor position and computes relative geometry.
 *
 * Wraps the HAL GPS module and provides relative position calculations
 * (distance and azimuth) between the monitor and aircraft/pilot positions
 * using the Haversine formula for distance and atan2-based forward azimuth.
 *
 * Properties guaranteed:
 *   - distance(A, B) == distance(B, A)  (symmetry)
 *   - distance(A, A) == 0               (identity)
 *   - azimuth ∈ [0, 360)
 *   - result.valid = false when GPS fix is unavailable
 */

#ifndef GEOLOCATION_SERVICE_H
#define GEOLOCATION_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "hal_gps.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Relative position between two geographic points.
 */
#ifndef RELATIVE_POSITION_T_DEFINED
#define RELATIVE_POSITION_T_DEFINED
typedef struct {
    float distance_m;   /**< Great-circle distance in meters (≥ 0) */
    float azimuth_deg;  /**< Forward azimuth in degrees [0, 360) */
    bool valid;         /**< true if both positions have valid fixes */
} relative_position_t;
#endif

/**
 * @brief Initialize the Geolocation Service.
 *
 * Must be called after hal_gps_init(). Verifies that the GPS HAL is
 * available and caches the initial monitor position.
 *
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_STATE if GPS HAL is not initialized
 */
esp_err_t geo_service_init(void);

/**
 * @brief Calculate relative position from one point to another.
 *
 * Uses the Haversine formula (Earth radius = 6371000 m) for distance
 * and the forward azimuth formula (atan2-based) for bearing.
 *
 * If @p from->fix_valid is false, the result is marked invalid.
 *
 * Guarantees:
 *   - Symmetry: distance(A,B) == distance(B,A)
 *   - Identity: distance(A,A) == 0
 *   - Azimuth normalized to [0, 360)
 *
 * @param[in]  from    Source position (typically the monitor)
 * @param[in]  to_lat  Destination latitude in decimal degrees
 * @param[in]  to_lon  Destination longitude in decimal degrees
 * @param[out] result  Computed relative position
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_ARG if from or result is NULL
 */
esp_err_t geo_calculate_relative(const gps_position_t *from,
                                 double to_lat, double to_lon,
                                 relative_position_t *result);

/**
 * @brief Get the current monitor (device) position.
 *
 * Returns a pointer to the internally cached position which is updated
 * from the GPS HAL. The pointer remains valid for the lifetime of the
 * service.
 *
 * @return Pointer to the current monitor position, or NULL if service
 *         is not initialized
 */
const gps_position_t* geo_get_monitor_position(void);

/**
 * @brief Check whether the monitor GPS currently has a valid fix.
 *
 * Delegates to the HAL GPS fix check.
 *
 * @return true if the GPS has a valid fix (sats ≥ 4 AND hdop < 5.0)
 */
bool geo_has_valid_fix(void);

#ifdef __cplusplus
}
#endif

#endif /* GEOLOCATION_SERVICE_H */
