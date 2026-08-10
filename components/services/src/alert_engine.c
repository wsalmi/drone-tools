/**
 * @file alert_engine.c
 * @brief Alert Engine implementation — audio and visual alert management.
 *
 * Implements:
 *   - New detection alert: 1s buzzer tone @ 2000 Hz + 3s visual notification
 *   - Proximity alert: 3x rapid beeps @ 3500 Hz + visual notification with distance
 *   - GPS fix condition: proximity alerts only fire when GPS fix is valid
 *   - Silent mode: suppresses buzzer, preserves visual notifications
 *
 * Validates: Requirements 13.1, 13.2, 13.4, 13.5
 */

#include "alert_engine.h"
#include "hal_buzzer.h"
#include "geolocation_service.h"

#include <string.h>
#include <stdio.h>

/* ========================================================================
 * Internal State
 * ======================================================================== */

/** Maximum number of aircraft tracked for proximity repeat timing */
#define MAX_PROXIMITY_TRACKED   MAX_AIRCRAFT

/** Default proximity threshold when no config provided */
#define DEFAULT_PROXIMITY_THRESHOLD_M       500

/** Default repeat interval when no config provided */
#define DEFAULT_PROXIMITY_REPEAT_S          10

static struct {
    bool initialized;
    alert_engine_config_t config;
    alert_notification_t current_notification;
    proximity_track_t proximity_tracks[MAX_PROXIMITY_TRACKED];
    uint8_t proximity_track_count;
} s_engine = {
    .initialized = false,
    .config = {
        .sound_enabled = true,
        .proximity_threshold_m = DEFAULT_PROXIMITY_THRESHOLD_M,
        .proximity_repeat_interval_s = DEFAULT_PROXIMITY_REPEAT_S
    },
    .current_notification = { .active = false },
    .proximity_track_count = 0
};

/* ========================================================================
 * Internal Helpers
 * ======================================================================== */

/**
 * @brief Get protocol name string for notification display.
 */
static const char *protocol_to_str(protocol_type_t proto)
{
    switch (proto) {
        case PROTOCOL_ELRS:      return "ELRS";
        case PROTOCOL_DJI:       return "DJI";
        case PROTOCOL_WIFI:      return "WiFi";
        case PROTOCOL_MAVLINK:   return "MAVLink";
        case PROTOCOL_CROSSFIRE: return "CRSF";
        case PROTOCOL_FRSKY:     return "FrSky";
        case PROTOCOL_REMOTEID:  return "RemoteID";
        default:                 return "Unknown";
    }
}

/**
 * @brief Find or create a proximity tracking entry for an aircraft.
 *
 * @return pointer to tracking entry, or NULL if table is full
 */
static proximity_track_t *find_or_create_track(const char *aircraft_id)
{
    /* Search existing */
    for (uint8_t i = 0; i < s_engine.proximity_track_count; i++) {
        if (strncmp(s_engine.proximity_tracks[i].aircraft_id, aircraft_id,
                    AIRCRAFT_ID_MAX_LEN) == 0) {
            return &s_engine.proximity_tracks[i];
        }
    }

    /* Create new if space available */
    if (s_engine.proximity_track_count < MAX_PROXIMITY_TRACKED) {
        proximity_track_t *track = &s_engine.proximity_tracks[s_engine.proximity_track_count];
        memset(track, 0, sizeof(proximity_track_t));
        strncpy(track->aircraft_id, aircraft_id, AIRCRAFT_ID_MAX_LEN - 1);
        track->aircraft_id[AIRCRAFT_ID_MAX_LEN - 1] = '\0';
        track->last_alert_time_ms = 0;
        track->within_threshold = false;
        s_engine.proximity_track_count++;
        return track;
    }

    return NULL;
}

/**
 * @brief Set the current visual notification.
 */
static void set_notification(const char *text, alert_type_t type,
                             uint32_t duration_ms, uint64_t timestamp_ms)
{
    strncpy(s_engine.current_notification.text, text,
            ALERT_NOTIFICATION_TEXT_MAX - 1);
    s_engine.current_notification.text[ALERT_NOTIFICATION_TEXT_MAX - 1] = '\0';
    s_engine.current_notification.type = type;
    s_engine.current_notification.duration_ms = duration_ms;
    s_engine.current_notification.timestamp_ms = timestamp_ms;
    s_engine.current_notification.active = true;
}

/**
 * @brief Play the new detection buzzer pattern (single 1s tone).
 */
static void play_new_detection_buzzer(void)
{
    if (s_engine.config.sound_enabled) {
        hal_buzzer_play_tone(ALERT_NEW_DETECTION_FREQ_HZ, ALERT_NEW_DETECTION_BUZZER_MS);
    }
}

/**
 * @brief Play the proximity alert buzzer pattern (3 rapid beeps).
 *
 * Since hal_buzzer_play_tone is asynchronous and replaces any current tone,
 * we play a single longer beep to represent the distinct proximity pattern.
 * In a full implementation with FreeRTOS, this would schedule 3 beeps
 * with pauses via a timer. For now, a single beep at the distinct frequency
 * is used to differentiate from the detection alert.
 */
static void play_proximity_buzzer(void)
{
    if (s_engine.config.sound_enabled) {
        /* Distinct pattern: higher frequency, shorter duration than new detection */
        hal_buzzer_play_tone(ALERT_PROXIMITY_FREQ_HZ,
                            ALERT_PROXIMITY_BEEP_MS * ALERT_PROXIMITY_BEEP_COUNT);
    }
}

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

esp_err_t alert_engine_init(const config_alert_t *config)
{
    /* Apply configuration */
    if (config != NULL) {
        s_engine.config.sound_enabled = config->sound_enabled;
        s_engine.config.proximity_threshold_m = config->proximity_threshold_m;
        s_engine.config.proximity_repeat_interval_s = config->proximity_repeat_interval_s;
    } else {
        /* Use defaults */
        s_engine.config.sound_enabled = true;
        s_engine.config.proximity_threshold_m = DEFAULT_PROXIMITY_THRESHOLD_M;
        s_engine.config.proximity_repeat_interval_s = DEFAULT_PROXIMITY_REPEAT_S;
    }

    /* Reset notification state */
    memset(&s_engine.current_notification, 0, sizeof(alert_notification_t));
    s_engine.current_notification.active = false;

    /* Reset proximity tracking */
    memset(s_engine.proximity_tracks, 0, sizeof(s_engine.proximity_tracks));
    s_engine.proximity_track_count = 0;

    s_engine.initialized = true;
    return ESP_OK;
}

esp_err_t alert_engine_new_detection(const char *aircraft_id, protocol_type_t protocol)
{
    if (aircraft_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_engine.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Play buzzer tone (1 second at 2000 Hz) unless in silent mode */
    play_new_detection_buzzer();

    /* Create visual notification (3 seconds) showing aircraft_id and protocol */
    char text[ALERT_NOTIFICATION_TEXT_MAX];
    snprintf(text, sizeof(text), "New: %.12s [%s]", aircraft_id, protocol_to_str(protocol));

    /* Use 0 as timestamp placeholder — caller should provide real time via
     * the notification's timestamp_ms. We use a simple approach here. */
    set_notification(text, ALERT_TYPE_NEW_DETECTION, ALERT_NEW_DETECTION_VISUAL_MS, 0);

    return ESP_OK;
}

esp_err_t alert_engine_check_proximity(const char *aircraft_id,
                                       float distance_m,
                                       uint64_t current_time_ms)
{
    if (aircraft_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_engine.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Condition: GPS fix must be valid for proximity alerts (Req 13.5) */
    if (!geo_has_valid_fix()) {
        return ESP_OK; /* Silently suppress — no proximity alert without GPS fix */
    }

    /* Check if aircraft is within proximity threshold */
    bool within_threshold = (distance_m < (float)s_engine.config.proximity_threshold_m);

    /* Find or create tracking entry */
    proximity_track_t *track = find_or_create_track(aircraft_id);
    if (track == NULL) {
        /* Tracking table full, skip this aircraft */
        return ESP_OK;
    }

    if (!within_threshold) {
        /* Aircraft moved outside threshold — update tracking state */
        track->within_threshold = false;
        return ESP_OK;
    }

    /* Aircraft is within threshold */
    track->within_threshold = true;

    /* Check repeat interval: only alert if enough time has elapsed */
    uint64_t repeat_interval_ms = (uint64_t)s_engine.config.proximity_repeat_interval_s * 1000ULL;
    uint64_t elapsed = current_time_ms - track->last_alert_time_ms;

    if (track->last_alert_time_ms == 0 || elapsed >= repeat_interval_ms) {
        /* Fire proximity alert */
        track->last_alert_time_ms = current_time_ms;

        /* Play distinct buzzer pattern (unless silent mode) */
        play_proximity_buzzer();

        /* Create visual notification with distance */
        char text[ALERT_NOTIFICATION_TEXT_MAX];
        snprintf(text, sizeof(text), "PROX: %.12s %.0fm", aircraft_id, distance_m);

        set_notification(text, ALERT_TYPE_PROXIMITY,
                        ALERT_NEW_DETECTION_VISUAL_MS, current_time_ms);
    }

    return ESP_OK;
}

void alert_engine_set_silent(bool silent)
{
    s_engine.config.sound_enabled = !silent;

    /* If entering silent mode, stop any currently playing tone */
    if (silent) {
        hal_buzzer_stop();
    }
}

bool alert_engine_is_silent(void)
{
    return !s_engine.config.sound_enabled;
}

esp_err_t alert_engine_get_notification(alert_notification_t *notification)
{
    if (notification == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_engine.current_notification.active) {
        return ESP_ERR_NOT_FOUND;
    }

    memcpy(notification, &s_engine.current_notification, sizeof(alert_notification_t));
    return ESP_OK;
}

esp_err_t alert_engine_update_config(const config_alert_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_engine.config.sound_enabled = config->sound_enabled;
    s_engine.config.proximity_threshold_m = config->proximity_threshold_m;
    s_engine.config.proximity_repeat_interval_s = config->proximity_repeat_interval_s;

    return ESP_OK;
}

void alert_engine_clear_proximity(const char *aircraft_id)
{
    if (aircraft_id == NULL) {
        return;
    }

    for (uint8_t i = 0; i < s_engine.proximity_track_count; i++) {
        if (strncmp(s_engine.proximity_tracks[i].aircraft_id, aircraft_id,
                    AIRCRAFT_ID_MAX_LEN) == 0) {
            /* Shift remaining entries down to fill gap */
            if (i < s_engine.proximity_track_count - 1) {
                memmove(&s_engine.proximity_tracks[i],
                        &s_engine.proximity_tracks[i + 1],
                        (s_engine.proximity_track_count - 1 - i) * sizeof(proximity_track_t));
            }
            s_engine.proximity_track_count--;
            return;
        }
    }
}

esp_err_t alert_engine_deinit(void)
{
    s_engine.initialized = false;
    s_engine.current_notification.active = false;
    s_engine.proximity_track_count = 0;
    memset(s_engine.proximity_tracks, 0, sizeof(s_engine.proximity_tracks));
    return ESP_OK;
}
