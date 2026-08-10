/**
 * @file pilot_locator.h
 * @brief Pilot Locator Service — determines pilot position from multiple sources.
 *
 * Estimates the remote pilot's position using a priority-based source hierarchy:
 *   1. Operator Location from RemoteID (System message) → CONFIRMED
 *   2. Home Point from MAVLink HOME_POSITION → ESTIMATED
 *   3. RSSI triangulation (≥3 readings with ≥10m separation) → ESTIMATED
 *   4. No source available → UNKNOWN (position_available = false)
 *
 * When a higher-confidence source is available, it overrides lower-confidence data.
 * State is maintained per-aircraft using the aircraft_id as key.
 *
 * Note: This header avoids including protocol_classifier.h and geolocation_service.h
 * to prevent type redefinition conflicts with aircraft_registry.h placeholder types.
 * The implementation uses those services internally.
 *
 * Validates: Requirements 6.1, 6.2, 6.3, 6.4, 6.5
 */

#ifndef PILOT_LOCATOR_H
#define PILOT_LOCATOR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "aircraft_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Constants
 * ======================================================================== */

/** Maximum number of tracked aircraft for pilot location */
#define PILOT_LOCATOR_MAX_AIRCRAFT  MAX_AIRCRAFT

/** Maximum number of RSSI readings stored per aircraft for triangulation */
#define PILOT_LOCATOR_MAX_RSSI_READINGS  16

/** Minimum number of RSSI readings required for triangulation */
#define PILOT_LOCATOR_MIN_RSSI_FOR_TRIANGULATION  3

/** Minimum GPS separation in meters between RSSI readings for triangulation */
#define PILOT_LOCATOR_MIN_SEPARATION_M  10.0f

/* ========================================================================
 * Types
 * ======================================================================== */

/*
 * pilot_confidence_t, pilot_position_t, and relative_position_t are
 * already defined in aircraft_registry.h (canonical source for shared types).
 */

/**
 * @brief Source type that provided the pilot position.
 */
typedef enum {
    PILOT_SOURCE_NONE = 0,              /**< No source available */
    PILOT_SOURCE_OPERATOR_LOCATION,     /**< RemoteID Operator Location (Type 4) */
    PILOT_SOURCE_HOME_POINT,            /**< MAVLink HOME_POSITION */
    PILOT_SOURCE_RSSI_TRIANGULATION     /**< RSSI triangulation */
} pilot_source_t;

/**
 * @brief Single RSSI reading for triangulation.
 */
typedef struct {
    int16_t rssi_dbm;           /**< RSSI at the time of reading */
    gps_position_t monitor_pos; /**< Monitor GPS position at time of reading */
    uint64_t timestamp_ms;      /**< UTC timestamp of reading */
} rssi_reading_t;

/**
 * @brief Per-aircraft pilot tracking state.
 */
typedef struct {
    char aircraft_id[AIRCRAFT_ID_MAX_LEN];  /**< Aircraft identifier */
    bool slot_occupied;                      /**< True if this slot is in use */

    /* Current best pilot position */
    pilot_position_t position;
    pilot_source_t source;

    /* Operator Location data (highest priority) */
    double operator_lat;
    double operator_lon;
    bool has_operator_location;

    /* Home Point data (medium priority) */
    double home_lat;
    double home_lon;
    bool has_home_point;

    /* RSSI readings for triangulation (lowest priority) */
    rssi_reading_t rssi_readings[PILOT_LOCATOR_MAX_RSSI_READINGS];
    uint8_t rssi_count;
} pilot_tracking_entry_t;

/* ========================================================================
 * API Functions
 * ======================================================================== */

/**
 * @brief Initialize the Pilot Locator service.
 *
 * Clears all tracking state. Should be called after geo_service_init().
 *
 * @return ESP_OK on success.
 */
esp_err_t pilot_locator_init(void);

/**
 * @brief Update pilot position from telemetry and raw detection data.
 *
 * Processes incoming telemetry data and adds RSSI readings for triangulation.
 * This is the unified update path that:
 *   - Adds RSSI reading from the detection (if rssi_dbm != 0 and GPS valid)
 *   - Re-resolves the best position based on all available sources
 *
 * For operator location and home point, use the dedicated setters
 * (pilot_locator_set_operator_location, pilot_locator_set_home_point) which
 * are called by the telemetry decoder pipeline when those specific data are decoded.
 *
 * @param[in] aircraft_id     Null-terminated aircraft identifier string.
 * @param[in] telemetry       Decoded telemetry data (may be NULL).
 * @param[in] rssi_dbm        Signal strength in dBm (0 means not available).
 * @param[in] monitor_pos     Monitor GPS position at time of detection (may be NULL).
 * @param[in] timestamp_ms    UTC timestamp of the detection.
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_ARG if aircraft_id is NULL,
 *         ESP_ERR_NO_MEM if no slots available for new aircraft.
 */
esp_err_t pilot_locator_update(const char *aircraft_id,
                               const decoded_telemetry_t *telemetry,
                               int16_t rssi_dbm,
                               const gps_position_t *monitor_pos,
                               uint64_t timestamp_ms);

/**
 * @brief Get the current pilot position estimate for an aircraft.
 *
 * Returns the best available pilot position based on the source hierarchy.
 * If no source is available, result->position_available is set to false
 * and confidence is PILOT_CONFIDENCE_UNKNOWN.
 *
 * @param[in]  aircraft_id  Null-terminated aircraft identifier string.
 * @param[out] result       Pointer to pilot_position_t to populate.
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_ARG if aircraft_id or result is NULL,
 *         ESP_ERR_NOT_FOUND if aircraft_id is not tracked.
 */
esp_err_t pilot_locator_get_position(const char *aircraft_id, pilot_position_t *result);

/**
 * @brief Update pilot position with RemoteID Operator Location.
 *
 * This is the highest-priority source. When available, it overrides all
 * other position estimates for the aircraft.
 *
 * @param[in] aircraft_id  Aircraft identifier.
 * @param[in] operator_lat Operator latitude in decimal degrees.
 * @param[in] operator_lon Operator longitude in decimal degrees.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if aircraft_id is NULL.
 */
esp_err_t pilot_locator_set_operator_location(const char *aircraft_id,
                                              double operator_lat,
                                              double operator_lon);

/**
 * @brief Update pilot position with MAVLink Home Point.
 *
 * This is a medium-priority source. Overrides RSSI triangulation but
 * is overridden by Operator Location.
 *
 * @param[in] aircraft_id  Aircraft identifier.
 * @param[in] home_lat     Home point latitude in decimal degrees.
 * @param[in] home_lon     Home point longitude in decimal degrees.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if aircraft_id is NULL.
 */
esp_err_t pilot_locator_set_home_point(const char *aircraft_id,
                                       double home_lat,
                                       double home_lon);

/**
 * @brief Add an RSSI reading for triangulation.
 *
 * Stores an RSSI reading along with the monitor's GPS position at the
 * time of detection. When ≥3 readings with ≥10m separation exist,
 * triangulation can estimate pilot direction/position.
 *
 * @param[in] aircraft_id  Aircraft identifier.
 * @param[in] rssi_dbm     Signal strength in dBm.
 * @param[in] monitor_pos  Monitor GPS position at time of detection.
 * @param[in] timestamp_ms UTC timestamp of the reading.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if aircraft_id or monitor_pos is NULL.
 */
esp_err_t pilot_locator_add_rssi_reading(const char *aircraft_id,
                                         int16_t rssi_dbm,
                                         const gps_position_t *monitor_pos,
                                         uint64_t timestamp_ms);

/**
 * @brief Reset all tracking state.
 *
 * Clears all tracked aircraft pilot positions.
 */
void pilot_locator_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* PILOT_LOCATOR_H */
