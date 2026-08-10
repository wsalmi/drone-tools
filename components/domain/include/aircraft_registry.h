/**
 * @file aircraft_registry.h
 * @brief Aircraft Registry — tracks detected aircraft in fixed-size slots.
 *
 * Manages up to MAX_AIRCRAFT entries with thread-safe access via FreeRTOS
 * mutex. Implements timeout-based status transitions (ACTIVE → OUT_OF_RANGE)
 * and slot eviction policy (oldest OUT_OF_RANGE entry replaced when full).
 *
 * Validates: Requirements 1.1, 1.2, 8.6, 13.3, 13.6
 */

#ifndef AIRCRAFT_REGISTRY_H
#define AIRCRAFT_REGISTRY_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "hal_gps.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Constants
 * ======================================================================== */

#define MAX_AIRCRAFT            32
#define AIRCRAFT_ID_MAX_LEN     21
#define TELEMETRY_HISTORY_LEN   16

/** Timeout in milliseconds before marking aircraft as OUT_OF_RANGE */
#define AIRCRAFT_TIMEOUT_MS     30000

/* ========================================================================
 * Forward-declared / Placeholder Types
 * ======================================================================== */

/**
 * @brief Protocol type enumeration.
 * Canonical definition is in protocol_signatures.h. This is a compatible
 * duplicate for when aircraft_registry.h is used without protocol_signatures.h.
 */
#ifndef PROTOCOL_TYPE_DEFINED
#define PROTOCOL_TYPE_DEFINED
typedef enum {
    PROTOCOL_ELRS = 0,
    PROTOCOL_DJI,
    PROTOCOL_WIFI,
    PROTOCOL_MAVLINK,
    PROTOCOL_CROSSFIRE,
    PROTOCOL_FRSKY,
    PROTOCOL_REMOTEID,
    PROTOCOL_UNKNOWN
} protocol_type_t;
#endif

/**
 * @brief Confidence level for protocol classification.
 * Canonical definition is in protocol_classifier.h. This is a compatible
 * duplicate for when aircraft_registry.h is used without protocol_classifier.h.
 */
#ifndef CONFIDENCE_LEVEL_DEFINED
#define CONFIDENCE_LEVEL_DEFINED
typedef enum {
    CONFIDENCE_HIGH = 0,
    CONFIDENCE_LOW
} confidence_level_t;
#endif

/**
 * @brief Decoded telemetry data from a drone.
 * Placeholder — full definition in telemetry_decoder.h (not yet implemented).
 */
typedef struct {
    char uas_id[AIRCRAFT_ID_MAX_LEN];
    double lat;
    double lon;
    float altitude_m;
    float speed_ms;
    float heading_deg;
    float battery_pct;
    float battery_voltage;
    uint8_t flight_mode;
    uint8_t link_quality_pct;
    int16_t rssi_dbm;
    bool has_position;
    bool has_altitude;
    bool has_speed;
    bool has_battery;
    bool has_flight_mode;
} decoded_telemetry_t;

/**
 * @brief Relative position from monitor to aircraft.
 * Placeholder — full definition in geolocation_service.h.
 */
#ifndef RELATIVE_POSITION_T_DEFINED
#define RELATIVE_POSITION_T_DEFINED
typedef struct {
    float distance_m;
    float azimuth_deg;
    bool valid;
} relative_position_t;
#endif

/**
 * @brief Pilot position estimation result.
 * Placeholder — full definition in pilot_locator.h.
 */
typedef enum {
    PILOT_CONFIDENCE_CONFIRMED = 0,
    PILOT_CONFIDENCE_ESTIMATED,
    PILOT_CONFIDENCE_UNKNOWN
} pilot_confidence_t;

typedef struct {
    double lat;
    double lon;
    pilot_confidence_t confidence;
    relative_position_t relative_to_monitor;
    bool position_available;
} pilot_position_t;

/* ========================================================================
 * Aircraft Status
 * ======================================================================== */

typedef enum {
    AIRCRAFT_STATUS_ACTIVE = 0,
    AIRCRAFT_STATUS_OUT_OF_RANGE    /**< >30s without transmission */
} aircraft_status_t;

/* ========================================================================
 * Aircraft Entry
 * ======================================================================== */

typedef struct {
    /* Identification */
    char id[AIRCRAFT_ID_MAX_LEN];
    protocol_type_t protocol;
    confidence_level_t protocol_confidence;
    aircraft_status_t status;

    /* Most recent telemetry */
    decoded_telemetry_t last_telemetry;
    uint64_t last_seen_utc_ms;
    uint64_t first_seen_utc_ms;

    /* Relative position to monitor */
    relative_position_t relative_pos;

    /* Pilot */
    pilot_position_t pilot;

    /* Signal */
    int16_t last_rssi_dbm;
    uint32_t last_frequency_hz;

    /* RSSI history for triangulation */
    struct {
        int16_t rssi_dbm;
        gps_position_t monitor_pos;
        uint64_t timestamp_ms;
    } rssi_history[TELEMETRY_HISTORY_LEN];
    uint8_t rssi_history_count;

    /* Control */
    bool slot_occupied;
} aircraft_entry_t;

/* ========================================================================
 * Aircraft Registry
 * ======================================================================== */

typedef struct {
    aircraft_entry_t entries[MAX_AIRCRAFT];
    uint8_t count;                  /**< Number of occupied slots */
    uint32_t total_detected;        /**< Total unique aircraft since init */
    uint32_t error_count;           /**< Discarded packets */
    SemaphoreHandle_t mutex;
} aircraft_registry_t;

/* ========================================================================
 * API Functions
 * ======================================================================== */

/**
 * @brief Initialize the aircraft registry.
 *
 * Clears all entries and creates the mutex. Must be called before any
 * other registry function.
 *
 * @param[in,out] reg Pointer to registry struct to initialize.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if reg is NULL,
 *         ESP_ERR_NO_MEM if mutex creation fails.
 */
esp_err_t registry_init(aircraft_registry_t *reg);

/**
 * @brief Find an existing entry or create a new one for the given aircraft ID.
 *
 * If an entry with the specified ID exists, returns it. Otherwise allocates
 * a new slot. If no empty slot is available, evicts the oldest entry with
 * status OUT_OF_RANGE. If all slots are ACTIVE, returns NULL.
 *
 * The returned pointer is valid only while the caller holds the mutex.
 * The registry mutex is NOT held on return — caller must lock externally
 * if thread safety is required for the returned pointer.
 *
 * @param[in,out] reg Pointer to initialized registry.
 * @param[in]     id  Null-terminated aircraft ID string (max 20 chars).
 * @return Pointer to the entry, or NULL if registry is full with all ACTIVE.
 */
aircraft_entry_t *registry_find_or_create(aircraft_registry_t *reg, const char *id);

/**
 * @brief Find an existing entry by aircraft ID.
 *
 * @param[in] reg Pointer to initialized registry.
 * @param[in] id  Null-terminated aircraft ID string.
 * @return Pointer to the entry if found, NULL otherwise.
 */
aircraft_entry_t *registry_find(aircraft_registry_t *reg, const char *id);

/**
 * @brief Update status of all aircraft based on timeout.
 *
 * For each occupied entry, if (current_time_ms - last_seen_utc_ms) > 30000,
 * transitions the entry to AIRCRAFT_STATUS_OUT_OF_RANGE.
 *
 * @param[in,out] reg             Pointer to initialized registry.
 * @param[in]     current_time_ms Current UTC time in milliseconds.
 */
void registry_update_status(aircraft_registry_t *reg, uint64_t current_time_ms);

/**
 * @brief Get the number of aircraft with ACTIVE status.
 *
 * @param[in] reg Pointer to initialized registry.
 * @return Number of entries with status AIRCRAFT_STATUS_ACTIVE.
 */
uint8_t registry_get_active_count(const aircraft_registry_t *reg);

#ifdef __cplusplus
}
#endif

#endif /* AIRCRAFT_REGISTRY_H */
