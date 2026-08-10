/**
 * @file config_store.h
 * @brief Configuration Store — parser and validator for system config.json.
 *
 * Provides a typed configuration structure with hardcoded safe defaults
 * and JSON parsing/validation from SD card config file. Invalid or missing
 * fields fall back to defaults per-field (not per-file).
 *
 * Validates: Requirements 7.4, 7.5, 12.2
 */

#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Configuration Sections
 * ======================================================================== */

/** @brief Alert configuration parameters */
typedef struct {
    bool sound_enabled;                 /**< Enable audible alerts via buzzer */
    uint32_t proximity_threshold_m;     /**< Distance threshold for proximity alert (meters, > 0) */
    uint32_t proximity_repeat_interval_s; /**< Interval between repeated proximity alerts (seconds, > 0) */
    uint32_t out_of_range_timeout_s;    /**< Time without transmission to mark as out-of-range (seconds, > 0) */
} config_alert_t;

/** @brief Spectrum analyzer configuration parameters */
typedef struct {
    uint32_t default_center_freq_mhz;   /**< Center frequency in MHz [24, 1766] */
    uint32_t default_bandwidth_khz;     /**< Resolution bandwidth in kHz [10, 1000] */
    float default_gain_db;              /**< Gain in dB [0.0, 49.6] */
    int32_t detection_threshold_dbm;    /**< Signal detection threshold in dBm */
} config_spectrum_t;

/** @brief Logging configuration parameters */
typedef struct {
    uint32_t max_file_size_mb;          /**< Maximum log file size before rotation (MB, > 0) */
    uint32_t buffer_size_records;       /**< RAM buffer size when SD unavailable (> 0) */
} config_logging_t;

/** @brief Scan timing configuration parameters */
typedef struct {
    uint32_t remoteid_cycle_ms;         /**< RemoteID scan cycle time (ms, > 0) */
    uint32_t nrf24_dwell_time_ms;       /**< NRF24 channel dwell time (ms, > 0) */
    uint32_t lora_dwell_time_ms;        /**< LoRa channel dwell time (ms, > 0) */
    uint32_t module_poll_interval_ms;   /**< Module presence polling interval (ms, > 0) */
} config_scan_t;

/** @brief GPS configuration parameters */
typedef struct {
    uint8_t min_satellites;             /**< Minimum satellites for valid fix (>= 1) */
    float max_hdop;                     /**< Maximum HDOP for valid fix (> 0) */
    uint32_t fix_timeout_s;             /**< Timeout waiting for initial fix (seconds, > 0) */
    uint32_t degraded_timeout_s;        /**< Timeout before declaring degraded fix (seconds, > 0) */
} config_gps_t;

/** @brief Complete system configuration */
typedef struct {
    config_alert_t alert;
    config_spectrum_t spectrum;
    config_logging_t logging;
    config_scan_t scan;
    config_gps_t gps;
} config_store_t;

/* ========================================================================
 * API Functions
 * ======================================================================== */

/**
 * @brief Get hardcoded safe default configuration.
 *
 * Returns a configuration struct with all fields set to safe operational
 * defaults. This is used as the baseline when no config file is available
 * or when individual fields fail validation.
 *
 * @param[out] config Pointer to configuration struct to fill with defaults.
 */
void config_store_get_defaults(config_store_t *config);

/**
 * @brief Parse and validate JSON configuration string.
 *
 * Parses the provided JSON string (typically read from config.json on SD card)
 * and populates the configuration struct. Individual fields that are missing
 * or fail validation are replaced with defaults (the entire config is NOT
 * rejected for a single bad field).
 *
 * @param[in]  json_str  Null-terminated JSON string to parse.
 * @param[out] out       Pointer to configuration struct to populate.
 *
 * @return 0 on success (all fields valid or substituted with defaults),
 *         ERR_CONFIG_PARSE_FAIL if JSON is fundamentally malformed (output
 *         will contain all defaults in this case).
 */
int config_store_load_from_json(const char *json_str, config_store_t *out);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_STORE_H */
