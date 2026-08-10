/**
 * @file data_logger.h
 * @brief Data Logger Service — CSV logging, RAM buffer, file rotation, and KML export.
 *
 * Provides persistent logging of aircraft detections and telemetry data
 * to the SD card in CSV format. When the SD card is unavailable, records
 * are stored in a circular RAM buffer (100 entries) and flushed when the
 * card becomes available again.
 *
 * Features:
 *   - CSV format: timestamp_utc,monitor_lat,monitor_lon,monitor_alt,aircraft_id,
 *                 protocol,rssi_dbm,lat,lon,alt_m,speed_ms,battery_pct,event_type
 *   - Circular RAM buffer (100 records) when SD unavailable
 *   - File rotation at 10 MB (new file created when threshold exceeded)
 *   - KML generation with placemarks for aircraft and pilot positions
 *   - New CSV file per session
 *
 * Validates: Requirements 11.1, 11.2, 11.3, 11.4, 11.5
 */

#ifndef DATA_LOGGER_H
#define DATA_LOGGER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "hal_gps.h"
#include "aircraft_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Constants
 * ======================================================================== */

/** @brief Maximum number of records in the circular RAM buffer */
#define DATA_LOGGER_BUFFER_SIZE         100

/** @brief Maximum file size in bytes before rotation (10 MB) */
#define DATA_LOGGER_MAX_FILE_SIZE       (10UL * 1024UL * 1024UL)

/** @brief Maximum length of a single CSV line */
#define DATA_LOGGER_MAX_LINE_LEN        512

/** @brief Maximum filename path length */
#define DATA_LOGGER_MAX_PATH_LEN        128

/** @brief CSV header line */
#define DATA_LOGGER_CSV_HEADER \
    "timestamp_utc,monitor_lat,monitor_lon,monitor_alt,aircraft_id,protocol,rssi_dbm,lat,lon,alt_m,speed_ms,battery_pct,event_type"

/* ========================================================================
 * Data Types
 * ======================================================================== */

/**
 * @brief Event type for log entries.
 */
typedef enum {
    LOG_EVENT_TELEMETRY = 0,    /**< Decoded telemetry update */
    LOG_EVENT_DETECTION,        /**< New aircraft detected */
    LOG_EVENT_OUT_OF_RANGE,     /**< Aircraft went out of range */
    LOG_EVENT_PROTOCOL_ID       /**< Protocol identified */
} log_event_type_t;

/**
 * @brief Single log record containing all fields for a CSV line.
 */
typedef struct {
    /* Timestamp */
    uint64_t timestamp_utc_ms;          /**< UTC timestamp in milliseconds */

    /* Monitor position */
    double monitor_lat;                 /**< Monitor latitude (degrees) */
    double monitor_lon;                 /**< Monitor longitude (degrees) */
    float monitor_alt;                  /**< Monitor altitude (meters) */

    /* Aircraft identification */
    char aircraft_id[AIRCRAFT_ID_MAX_LEN]; /**< Aircraft/UAS identifier */
    protocol_type_t protocol;           /**< Classified protocol */
    int16_t rssi_dbm;                   /**< Signal strength */

    /* Telemetry (optional fields) */
    double lat;                         /**< Aircraft latitude (degrees) */
    double lon;                         /**< Aircraft longitude (degrees) */
    float alt_m;                        /**< Aircraft altitude (meters) */
    float speed_ms;                     /**< Aircraft speed (m/s) */
    float battery_pct;                  /**< Battery percentage (0–100) */

    /* Flags indicating which optional fields are valid */
    bool has_position;
    bool has_altitude;
    bool has_speed;
    bool has_battery;

    /* Event type */
    log_event_type_t event_type;
} log_record_t;

/**
 * @brief KML placemark data for export.
 */
typedef struct {
    double lat;                         /**< Latitude (degrees) */
    double lon;                         /**< Longitude (degrees) */
    float alt_m;                        /**< Altitude (meters) */
    char name[AIRCRAFT_ID_MAX_LEN];     /**< Placemark name/ID */
    bool is_pilot;                      /**< true = pilot position, false = aircraft */
    uint64_t timestamp_utc_ms;          /**< When this position was recorded */
} kml_placemark_t;

/* ========================================================================
 * Public API
 * ======================================================================== */

/**
 * @brief Initialize the Data Logger service.
 *
 * Creates a new session CSV file on the SD card (if available).
 * Initializes the circular RAM buffer. If SD is not available,
 * operates in buffered mode until SD becomes available.
 *
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_STATE if already initialized
 */
esp_err_t data_logger_init(void);

/**
 * @brief Log a record.
 *
 * Writes the record to the current CSV file on SD. If the SD is
 * unavailable, the record is stored in the circular RAM buffer.
 * If the current file exceeds 10 MB, a new file is created (rotation).
 *
 * @param[in] record  Pointer to the log record to write.
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_ARG if record is NULL,
 *         ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t data_logger_log(const log_record_t *record);

/**
 * @brief Flush the RAM buffer to SD card.
 *
 * Called when the SD card becomes available after being absent.
 * Writes all buffered records to the CSV file in insertion order.
 *
 * @return ESP_OK on success (or if buffer is empty),
 *         ESP_ERR_INVALID_STATE if SD is still unavailable,
 *         ESP_FAIL on write error
 */
esp_err_t data_logger_flush_buffer(void);

/**
 * @brief Check and handle SD card availability changes.
 *
 * Should be called periodically. If SD has just become available,
 * automatically flushes the RAM buffer.
 *
 * @return ESP_OK if no action needed or flush was successful,
 *         ESP_FAIL on flush error
 */
esp_err_t data_logger_check_sd(void);

/**
 * @brief Generate a KML file from an array of placemarks.
 *
 * Writes a valid KML document containing one Placemark per entry
 * in the placemarks array. Aircraft and pilot positions use
 * different icon styles.
 *
 * @param[in] output_path  File path for the KML output (on SD card).
 * @param[in] placemarks   Array of placemark data.
 * @param[in] count        Number of placemarks in the array.
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_ARG if arguments are invalid,
 *         ESP_ERR_INVALID_STATE if SD unavailable,
 *         ESP_FAIL on write error
 */
esp_err_t data_logger_generate_kml(const char *output_path,
                                   const kml_placemark_t *placemarks,
                                   size_t count);

/**
 * @brief Get the number of records currently in the RAM buffer.
 *
 * @return Number of records in the buffer (0 to DATA_LOGGER_BUFFER_SIZE).
 */
size_t data_logger_get_buffer_count(void);

/**
 * @brief Get the current CSV file size in bytes.
 *
 * @return Current file size, or 0 if no file is open.
 */
size_t data_logger_get_current_file_size(void);

/**
 * @brief Deinitialize the Data Logger service.
 *
 * Flushes any pending data, closes the current CSV file,
 * and frees resources.
 *
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t data_logger_deinit(void);

/* ========================================================================
 * Serialization Helpers (exposed for unit testing)
 * ======================================================================== */

/**
 * @brief Serialize a log record to a CSV line string.
 *
 * Writes the CSV representation of the record into the output buffer.
 * Missing optional fields are written as empty values between commas.
 *
 * @param[in]  record  Log record to serialize.
 * @param[out] buf     Output buffer for the CSV line.
 * @param[in]  buf_len Size of the output buffer.
 * @return Number of characters written (excluding null terminator),
 *         or -1 if buffer is too small.
 */
int data_logger_record_to_csv(const log_record_t *record, char *buf, size_t buf_len);

/**
 * @brief Parse a CSV line back into a log record.
 *
 * @param[in]  csv_line  Null-terminated CSV line string.
 * @param[out] record    Parsed log record.
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_ARG if arguments are invalid,
 *         ESP_FAIL if parsing fails
 */
esp_err_t data_logger_csv_to_record(const char *csv_line, log_record_t *record);

/**
 * @brief Format a UTC timestamp in ISO 8601 format.
 *
 * Produces a string like "2024-03-15T10:30:45.123Z".
 *
 * @param[in]  timestamp_ms  UTC timestamp in milliseconds since epoch.
 * @param[out] buf           Output buffer (must be >= 25 bytes).
 * @param[in]  buf_len       Size of output buffer.
 * @return Number of characters written, or -1 on error.
 */
int data_logger_format_timestamp(uint64_t timestamp_ms, char *buf, size_t buf_len);

/**
 * @brief Get protocol name string from enum value.
 *
 * @param[in] protocol  Protocol type enum value.
 * @return Static string with the protocol name (e.g., "ELRS", "MAVLINK").
 */
const char *data_logger_protocol_to_str(protocol_type_t protocol);

/**
 * @brief Parse a protocol name string to enum value.
 *
 * @param[in]  str       Protocol name string.
 * @param[out] protocol  Parsed protocol enum value.
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if string doesn't match.
 */
esp_err_t data_logger_str_to_protocol(const char *str, protocol_type_t *protocol);

/**
 * @brief Get event type name string from enum value.
 *
 * @param[in] event  Event type enum value.
 * @return Static string with the event name (e.g., "TELEMETRY", "DETECTION").
 */
const char *data_logger_event_to_str(log_event_type_t event);

/**
 * @brief Parse an event type name string to enum value.
 *
 * @param[in]  str    Event type string.
 * @param[out] event  Parsed event type enum value.
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if string doesn't match.
 */
esp_err_t data_logger_str_to_event(const char *str, log_event_type_t *event);

#ifdef __cplusplus
}
#endif

#endif /* DATA_LOGGER_H */
