/**
 * @file telemetry_decoder.h
 * @brief Telemetry Decoder — central orchestrator for protocol-specific decoding.
 *
 * Receives raw detections (raw_detection_t) and dispatches to the appropriate
 * protocol decoder based on classification results from the Protocol Classifier:
 *   - PROTOCOL_REMOTEID or source WIFI_RID/BLE_RID → remoteid_decode
 *   - PROTOCOL_MAVLINK → mavlink_decode
 *   - PROTOCOL_ELRS or PROTOCOL_CROSSFIRE → elrs_decode
 *   - Others → ERR_DECODE_UNKNOWN_FMT
 *
 * After successful decoding, the decoded telemetry is used to update
 * the Aircraft Registry (find or create entry, update fields).
 *
 * Validates: Requirements 8.1, 8.2, 8.3, 8.5, 8.6
 */

#ifndef TELEMETRY_DECODER_H
#define TELEMETRY_DECODER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "protocol_classifier.h"
#include "aircraft_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Types
 * ======================================================================== */

/**
 * @brief Function pointer type for protocol-specific decoders.
 *
 * Each decoder takes a raw detection and populates a decoded telemetry struct.
 *
 * @param[in]  raw  Pointer to the raw detection data.
 * @param[out] out  Pointer to decoded telemetry output struct.
 * @return ESP_OK on success, or an error code on failure.
 */
typedef esp_err_t (*telemetry_decode_fn)(const raw_detection_t *raw, decoded_telemetry_t *out);

/* ========================================================================
 * API Functions
 * ======================================================================== */

/**
 * @brief Initialize the Telemetry Decoder service.
 *
 * Registers all available protocol decoders (RemoteID, MAVLink, ELRS)
 * and initializes the Protocol Classifier for dispatch decisions.
 * Must be called after classifier_init() and before telemetry_decode().
 *
 * @return ESP_OK on success,
 *         ESP_FAIL if initialization fails.
 */
esp_err_t telemetry_decoder_init(void);

/**
 * @brief Decode a raw detection and update the Aircraft Registry.
 *
 * Processing pipeline:
 *   1. Classify the raw detection using the Protocol Classifier
 *   2. Override classification with source hint (WIFI_RID/BLE_RID → REMOTEID)
 *   3. Dispatch to the appropriate decoder function
 *   4. On successful decode, find or create the aircraft entry in the registry
 *   5. Update the entry with decoded telemetry, RSSI, frequency, timestamp
 *
 * @param[in]  raw  Pointer to raw detection data from the Detection Service queue.
 * @param[out] out  Pointer to decoded telemetry output struct (populated on success).
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_ARG if raw or out is NULL,
 *         ERR_DECODE_UNKNOWN_FMT if no decoder can handle the protocol,
 *         other error codes from the specific decoder on decode failure.
 */
esp_err_t telemetry_decode(const raw_detection_t *raw, decoded_telemetry_t *out);

/**
 * @brief Set the Aircraft Registry instance used by the decoder.
 *
 * If not set, the decoder will still decode packets but will not update
 * any registry. This allows unit testing of decode logic in isolation.
 *
 * @param[in] reg  Pointer to initialized Aircraft Registry, or NULL to disable updates.
 */
void telemetry_decoder_set_registry(aircraft_registry_t *reg);

/**
 * @brief Get the Aircraft Registry instance currently used by the decoder.
 *
 * @return Pointer to the registry, or NULL if not set.
 */
aircraft_registry_t *telemetry_decoder_get_registry(void);

#ifdef __cplusplus
}
#endif

#endif /* TELEMETRY_DECODER_H */
