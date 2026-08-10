/**
 * @file protocol_classifier.h
 * @brief Protocol Classifier Service — classifies raw detections into known protocols.
 *
 * Wraps the protocol_signatures database with confidence-level logic.
 * Confidence is determined by the quality of the match:
 *   - HIGH: header AND frequency both match a signature with a specific range
 *   - LOW:  header matches but signature accepts any frequency (freq range is 0/0)
 *   - Result is PROTOCOL_UNKNOWN when no signature matches at all
 *
 * Validates: Requirements 7.1, 7.2, 7.3, 7.6
 */

#ifndef PROTOCOL_CLASSIFIER_H
#define PROTOCOL_CLASSIFIER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "protocol_signatures.h"
#include "hal_gps.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Types
 * ======================================================================== */

/** @brief Detection source for raw captures */
typedef enum {
    DETECTION_SOURCE_WIFI_RID = 0,
    DETECTION_SOURCE_BLE_RID,
    DETECTION_SOURCE_LORA,
    DETECTION_SOURCE_NRF24,
    DETECTION_SOURCE_SDR
} detection_source_t;

/** @brief Raw detection from any RF source */
typedef struct {
    detection_source_t source;
    uint8_t raw_payload[256];
    uint16_t payload_len;
    int16_t rssi_dbm;
    int8_t snr_db;
    uint32_t frequency_hz;
    uint64_t timestamp_utc_ms;
    gps_position_t monitor_position;
} raw_detection_t;

/** @brief Classification confidence level */
#ifndef CONFIDENCE_LEVEL_DEFINED
#define CONFIDENCE_LEVEL_DEFINED
typedef enum {
    CONFIDENCE_HIGH = 0,    /**< Header AND frequency both match */
    CONFIDENCE_LOW          /**< Header matches but frequency is "any" */
} confidence_level_t;
#endif

/** @brief Classification result for a raw detection */
typedef struct {
    protocol_type_t protocol;           /**< Identified protocol (UNKNOWN if no match) */
    confidence_level_t confidence;      /**< Confidence of the classification */
    uint32_t frequency_hz;              /**< Frequency from the detection */
    char modulation_info[32];           /**< Modulation type from matched signature */
} classification_result_t;

/* ========================================================================
 * API Functions
 * ======================================================================== */

/**
 * @brief Initialize the protocol classifier.
 *
 * Initializes the underlying signatures database. If signatures_file_path
 * is non-NULL and the file exists, attempts to load custom signatures from it.
 * Falls back to embedded defaults on failure or if path is NULL.
 *
 * @param[in] signatures_file_path  Path to CSV signatures file on SD card,
 *                                  or NULL to use embedded defaults only.
 * @return ESP_OK on success, ESP_FAIL on critical init failure.
 */
esp_err_t classifier_init(const char *signatures_file_path);

/**
 * @brief Classify a raw detection against the signature database.
 *
 * Matches the raw payload header bytes and frequency against loaded signatures.
 * Sets result->protocol to PROTOCOL_UNKNOWN if no signature matches.
 *
 * Confidence logic:
 *   - CONFIDENCE_HIGH: signature has a specific frequency range (freq_min > 0 || freq_max > 0)
 *                      and the detection's frequency falls within it
 *   - CONFIDENCE_LOW:  signature frequency range is 0/0 ("any frequency") meaning only
 *                      the header matched without frequency confirmation
 *
 * @param[in]  raw     Raw detection to classify.
 * @param[out] result  Classification result.
 * @return ESP_OK on success (even if protocol is UNKNOWN),
 *         ESP_ERR_INVALID_ARG if raw or result is NULL.
 */
esp_err_t classifier_classify(const raw_detection_t *raw, classification_result_t *result);

/**
 * @brief Get the number of signatures currently loaded.
 *
 * @return Number of signatures in the active table.
 */
uint16_t classifier_get_signature_count(void);

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_CLASSIFIER_H */
