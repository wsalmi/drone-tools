/**
 * @file alert_engine.h
 * @brief Alert Engine — manages audio and visual notifications for detections and proximity.
 *
 * The Alert Engine handles two types of alerts:
 *   1. New detection alert: 1s buzzer tone + 3s visual notification (aircraft_id + protocol)
 *   2. Proximity alert: distinct buzzer pattern + visual notification with distance,
 *      repeating every 10s while aircraft is within threshold
 *
 * Proximity alerts are conditioned on:
 *   - GPS fix being valid (monitor position known)
 *   - Alert configuration enabled
 *
 * Silent mode suppresses buzzer output but preserves visual notifications.
 *
 * Validates: Requirements 13.1, 13.2, 13.4, 13.5
 */

#ifndef ALERT_ENGINE_H
#define ALERT_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "config_store.h"
#include "aircraft_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Constants
 * ======================================================================== */

/** @brief Duration of new detection buzzer tone in milliseconds */
#define ALERT_NEW_DETECTION_BUZZER_MS       1000

/** @brief Duration of new detection visual notification in milliseconds */
#define ALERT_NEW_DETECTION_VISUAL_MS       3000

/** @brief Frequency for new detection buzzer tone in Hz */
#define ALERT_NEW_DETECTION_FREQ_HZ         2000

/** @brief Frequency for proximity alert buzzer tone in Hz (distinct pattern) */
#define ALERT_PROXIMITY_FREQ_HZ             3500

/** @brief Duration of each proximity buzzer beep in milliseconds */
#define ALERT_PROXIMITY_BEEP_MS             200

/** @brief Number of rapid beeps for proximity alert pattern */
#define ALERT_PROXIMITY_BEEP_COUNT          3

/** @brief Maximum notification text length */
#define ALERT_NOTIFICATION_TEXT_MAX          64

/* ========================================================================
 * Types
 * ======================================================================== */

/** @brief Alert type enumeration */
typedef enum {
    ALERT_TYPE_NEW_DETECTION = 0,   /**< New aircraft first detected */
    ALERT_TYPE_PROXIMITY            /**< Aircraft within proximity threshold */
} alert_type_t;

/** @brief Visual notification data passed to UI layer */
typedef struct {
    char text[ALERT_NOTIFICATION_TEXT_MAX];  /**< Notification text to display */
    alert_type_t type;                       /**< Alert type for styling */
    uint32_t duration_ms;                    /**< Display duration in ms */
    uint64_t timestamp_ms;                   /**< When the notification was created */
    bool active;                             /**< Whether notification is currently active */
} alert_notification_t;

/** @brief Proximity tracking state per aircraft (internal) */
typedef struct {
    char aircraft_id[AIRCRAFT_ID_MAX_LEN];   /**< Aircraft being tracked */
    uint64_t last_alert_time_ms;             /**< Last time proximity alert fired */
    bool within_threshold;                   /**< Currently inside proximity radius */
} proximity_track_t;

/** @brief Alert Engine configuration (runtime) */
typedef struct {
    bool sound_enabled;                      /**< false = silent mode */
    uint32_t proximity_threshold_m;          /**< Distance in meters for proximity alert */
    uint32_t proximity_repeat_interval_s;    /**< Repeat interval in seconds */
} alert_engine_config_t;

/* ========================================================================
 * API Functions
 * ======================================================================== */

/**
 * @brief Initialize the Alert Engine.
 *
 * Loads alert configuration from config_store and prepares internal state.
 * Must be called after hal_buzzer_init() and geo_service_init().
 *
 * @param[in] config  Alert configuration from config_store. If NULL, uses defaults.
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_STATE if dependencies not initialized
 */
esp_err_t alert_engine_init(const config_alert_t *config);

/**
 * @brief Trigger a new detection alert.
 *
 * Called when a previously unknown aircraft is first detected.
 * Emits a 1-second buzzer tone (unless silent mode) and produces
 * a visual notification lasting 3 seconds with the aircraft ID and protocol.
 *
 * @param[in] aircraft_id  Null-terminated aircraft identifier string
 * @param[in] protocol     Detected protocol type
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_ARG if aircraft_id is NULL
 */
esp_err_t alert_engine_new_detection(const char *aircraft_id, protocol_type_t protocol);

/**
 * @brief Evaluate proximity alert for an aircraft.
 *
 * Checks if the given aircraft is within the proximity threshold and
 * whether enough time has elapsed since the last proximity alert for
 * this aircraft. Produces alert only if:
 *   - GPS fix is valid
 *   - Distance is below threshold
 *   - Repeat interval has elapsed since last alert for this aircraft
 *
 * @param[in] aircraft_id   Null-terminated aircraft identifier
 * @param[in] distance_m    Current distance to aircraft in meters
 * @param[in] current_time_ms  Current time in milliseconds (UTC or monotonic)
 * @return ESP_OK if alert was evaluated (regardless of whether it fired),
 *         ESP_ERR_INVALID_ARG if aircraft_id is NULL,
 *         ESP_ERR_INVALID_STATE if engine not initialized
 */
esp_err_t alert_engine_check_proximity(const char *aircraft_id,
                                       float distance_m,
                                       uint64_t current_time_ms);

/**
 * @brief Set silent mode (suppress buzzer, keep visual notifications).
 *
 * @param[in] silent  true to enable silent mode, false to restore sound
 */
void alert_engine_set_silent(bool silent);

/**
 * @brief Check if the alert engine is currently in silent mode.
 *
 * @return true if silent mode is active
 */
bool alert_engine_is_silent(void);

/**
 * @brief Get the current visual notification (if any).
 *
 * Returns the most recent active notification. The notification becomes
 * inactive after its duration_ms has elapsed from timestamp_ms.
 *
 * @param[out] notification  Pointer to receive notification data
 * @return ESP_OK if an active notification exists,
 *         ESP_ERR_NOT_FOUND if no notification is active,
 *         ESP_ERR_INVALID_ARG if notification is NULL
 */
esp_err_t alert_engine_get_notification(alert_notification_t *notification);

/**
 * @brief Update the alert engine configuration at runtime.
 *
 * @param[in] config  New alert configuration to apply
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_ARG if config is NULL
 */
esp_err_t alert_engine_update_config(const config_alert_t *config);

/**
 * @brief Clear proximity tracking state for an aircraft.
 *
 * Call this when an aircraft goes out of range to clean up
 * its proximity tracking entry.
 *
 * @param[in] aircraft_id  Null-terminated aircraft identifier
 */
void alert_engine_clear_proximity(const char *aircraft_id);

/**
 * @brief Deinitialize the Alert Engine and release resources.
 *
 * @return ESP_OK on success
 */
esp_err_t alert_engine_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* ALERT_ENGINE_H */
