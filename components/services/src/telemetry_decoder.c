/**
 * @file telemetry_decoder.c
 * @brief Telemetry Decoder — central orchestrator implementation.
 *
 * Dispatches raw detections to protocol-specific decoders based on
 * classification results and detection source hints. After successful
 * decoding, updates the Aircraft Registry with the decoded telemetry.
 *
 * Dispatch logic:
 *   - Source is WIFI_RID or BLE_RID → remoteid_decode (bypasses classifier)
 *   - Classifier returns PROTOCOL_REMOTEID → remoteid_decode
 *   - Classifier returns PROTOCOL_MAVLINK → mavlink_decode
 *   - Classifier returns PROTOCOL_ELRS or PROTOCOL_CROSSFIRE → elrs_decode
 *   - Others → return ERR_DECODE_UNKNOWN_FMT
 *
 * Registry update on successful decode:
 *   - Find or create aircraft entry by uas_id (or generated ID)
 *   - Update last_telemetry, last_seen_utc_ms, protocol, rssi, frequency
 *   - Transition status to ACTIVE if was OUT_OF_RANGE
 *
 * Validates: Requirements 8.1, 8.2, 8.3, 8.5, 8.6
 */

#include "telemetry_decoder.h"
#include "remoteid_decoder.h"
#include "mavlink_decoder.h"
#include "elrs_decoder.h"
#include "error_codes.h"

#include <string.h>
#include <stdio.h>

/* ========================================================================
 * Internal State
 * ======================================================================== */

static bool s_initialized = false;
static aircraft_registry_t *s_registry = NULL;

/* ========================================================================
 * Internal Decoder Wrappers
 *
 * These wrappers adapt the specific decoder APIs to the unified
 * telemetry_decode_fn signature: (raw_detection_t*, decoded_telemetry_t*) → esp_err_t
 * ======================================================================== */

/**
 * @brief Wrapper for RemoteID decoding, adapting to telemetry_decode_fn.
 *
 * Determines WiFi vs BLE based on the detection source field, then
 * dispatches to the unified remoteid_decode() function.
 */
static esp_err_t decode_remoteid(const raw_detection_t *raw, decoded_telemetry_t *out)
{
    bool is_ble = (raw->source == DETECTION_SOURCE_BLE_RID);

    return remoteid_decode(raw->raw_payload, raw->payload_len, is_ble, out, NULL);
}

/**
 * @brief Wrapper for MAVLink decoding, adapting to telemetry_decode_fn.
 */
static esp_err_t decode_mavlink(const raw_detection_t *raw, decoded_telemetry_t *out)
{
    return mavlink_decode(raw->raw_payload, raw->payload_len, out);
}

/**
 * @brief Wrapper for ELRS/CRSF decoding, adapting to telemetry_decode_fn.
 */
static esp_err_t decode_elrs(const raw_detection_t *raw, decoded_telemetry_t *out)
{
    return elrs_decode(raw->raw_payload, raw->payload_len, out);
}

/* ========================================================================
 * Decoder dispatch table
 * ======================================================================== */

/** Maximum number of registered decoders */
#define MAX_DECODERS 3

typedef struct {
    protocol_type_t protocol;
    telemetry_decode_fn decode_fn;
} decoder_entry_t;

static decoder_entry_t s_decoders[MAX_DECODERS];
static uint8_t s_decoder_count = 0;

/**
 * @brief Register a decoder for a specific protocol.
 */
static void register_decoder(protocol_type_t protocol, telemetry_decode_fn fn)
{
    if (s_decoder_count < MAX_DECODERS) {
        s_decoders[s_decoder_count].protocol = protocol;
        s_decoders[s_decoder_count].decode_fn = fn;
        s_decoder_count++;
    }
}

/**
 * @brief Find the decoder function for a given protocol.
 *
 * @param protocol The classified protocol type.
 * @return Decoder function pointer, or NULL if no decoder registered.
 */
static telemetry_decode_fn find_decoder(protocol_type_t protocol)
{
    for (uint8_t i = 0; i < s_decoder_count; i++) {
        if (s_decoders[i].protocol == protocol) {
            return s_decoders[i].decode_fn;
        }
    }
    return NULL;
}

/* ========================================================================
 * Internal Helper: Generate aircraft ID from raw detection
 * ======================================================================== */

/**
 * @brief Generate a fallback aircraft ID when the decoder does not provide one.
 *
 * Creates an ID from the protocol name and a hash of the raw payload.
 * Format: "<PROTO>-<8-char hex hash>"
 *
 * @param[in]  protocol  The classified protocol type.
 * @param[in]  raw       The raw detection data (used for hash).
 * @param[out] id_buf    Buffer to write the generated ID (AIRCRAFT_ID_MAX_LEN).
 */
static void generate_aircraft_id(protocol_type_t protocol, const raw_detection_t *raw,
                                  char *id_buf)
{
    /* Simple hash from payload bytes for uniqueness */
    uint32_t hash = 0x811C9DC5u; /* FNV-1a offset basis */
    for (uint16_t i = 0; i < raw->payload_len && i < 64; i++) {
        hash ^= raw->raw_payload[i];
        hash *= 0x01000193u; /* FNV-1a prime */
    }

    const char *proto_prefix;
    switch (protocol) {
    case PROTOCOL_MAVLINK:   proto_prefix = "MAV"; break;
    case PROTOCOL_ELRS:      proto_prefix = "ELRS"; break;
    case PROTOCOL_CROSSFIRE: proto_prefix = "CRSF"; break;
    case PROTOCOL_DJI:       proto_prefix = "DJI"; break;
    case PROTOCOL_FRSKY:     proto_prefix = "FRSK"; break;
    default:                 proto_prefix = "UNK"; break;
    }

    snprintf(id_buf, AIRCRAFT_ID_MAX_LEN, "%s-%08X", proto_prefix, (unsigned int)hash);
}

/* ========================================================================
 * Internal Helper: Determine effective protocol from source + classifier
 * ======================================================================== */

/**
 * @brief Determine the effective protocol for dispatch.
 *
 * Source-based override takes priority for RemoteID sources (WiFi/BLE RID),
 * since these are known to be RemoteID regardless of what the classifier
 * says about their payload.
 *
 * For all other sources, uses the classifier result.
 *
 * @param[in] raw     Raw detection with source info.
 * @param[in] classified  Classifier result (may be overridden).
 * @return The effective protocol type for decoder dispatch.
 */
static protocol_type_t determine_protocol(const raw_detection_t *raw,
                                           const classification_result_t *classified)
{
    /* Source-based override: WiFi/BLE RID sources are always RemoteID */
    if (raw->source == DETECTION_SOURCE_WIFI_RID ||
        raw->source == DETECTION_SOURCE_BLE_RID) {
        return PROTOCOL_REMOTEID;
    }

    return classified->protocol;
}

/* ========================================================================
 * Internal Helper: Update Aircraft Registry
 * ======================================================================== */

/**
 * @brief Update the Aircraft Registry with decoded telemetry.
 *
 * Finds or creates an aircraft entry and populates it with decoded data.
 * If the decoded telemetry has a uas_id, uses that as the key.
 * Otherwise generates an ID from the protocol and payload hash.
 *
 * @param[in] raw        Raw detection data (for RSSI, frequency, timestamp).
 * @param[in] telemetry  Decoded telemetry data.
 * @param[in] protocol   Classified protocol type.
 * @param[in] confidence Classification confidence level.
 */
static void update_registry(const raw_detection_t *raw,
                            const decoded_telemetry_t *telemetry,
                            protocol_type_t protocol,
                            confidence_level_t confidence)
{
    if (s_registry == NULL) {
        return;
    }

    /* Determine the aircraft ID */
    char aircraft_id[AIRCRAFT_ID_MAX_LEN];
    if (telemetry->uas_id[0] != '\0') {
        /* Use the UAS ID from the decoded telemetry */
        strncpy(aircraft_id, telemetry->uas_id, AIRCRAFT_ID_MAX_LEN - 1);
        aircraft_id[AIRCRAFT_ID_MAX_LEN - 1] = '\0';
    } else {
        /* Generate an ID from protocol + payload hash */
        generate_aircraft_id(protocol, raw, aircraft_id);
    }

    /* Find or create the aircraft entry */
    aircraft_entry_t *entry = registry_find_or_create(s_registry, aircraft_id);
    if (entry == NULL) {
        /* Registry is full with all ACTIVE entries — cannot add */
        return;
    }

    /* Update the entry with new telemetry data */
    memcpy(&entry->last_telemetry, telemetry, sizeof(decoded_telemetry_t));
    entry->last_seen_utc_ms = raw->timestamp_utc_ms;
    entry->protocol = protocol;
    entry->protocol_confidence = confidence;
    entry->last_rssi_dbm = raw->rssi_dbm;
    entry->last_frequency_hz = raw->frequency_hz;

    /* Transition status to ACTIVE (handles re-activation from OUT_OF_RANGE) */
    entry->status = AIRCRAFT_STATUS_ACTIVE;

    /* Set first_seen if this is a newly created entry */
    if (entry->first_seen_utc_ms == 0) {
        entry->first_seen_utc_ms = raw->timestamp_utc_ms;
    }

    /* Add RSSI sample to history for triangulation */
    if (entry->rssi_history_count < TELEMETRY_HISTORY_LEN) {
        uint8_t idx = entry->rssi_history_count;
        entry->rssi_history[idx].rssi_dbm = raw->rssi_dbm;
        entry->rssi_history[idx].monitor_pos = raw->monitor_position;
        entry->rssi_history[idx].timestamp_ms = raw->timestamp_utc_ms;
        entry->rssi_history_count++;
    } else {
        /* Ring buffer: overwrite oldest entry */
        /* Shift all entries left by one, then write to last position */
        for (uint8_t i = 0; i < TELEMETRY_HISTORY_LEN - 1; i++) {
            entry->rssi_history[i] = entry->rssi_history[i + 1];
        }
        uint8_t last = TELEMETRY_HISTORY_LEN - 1;
        entry->rssi_history[last].rssi_dbm = raw->rssi_dbm;
        entry->rssi_history[last].monitor_pos = raw->monitor_position;
        entry->rssi_history[last].timestamp_ms = raw->timestamp_utc_ms;
    }
}

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

esp_err_t telemetry_decoder_init(void)
{
    /* Reset decoder table */
    s_decoder_count = 0;
    memset(s_decoders, 0, sizeof(s_decoders));

    /* Register available decoders */
    register_decoder(PROTOCOL_REMOTEID, decode_remoteid);
    register_decoder(PROTOCOL_MAVLINK, decode_mavlink);
    register_decoder(PROTOCOL_ELRS, decode_elrs);

    s_initialized = true;
    return ESP_OK;
}

esp_err_t telemetry_decode(const raw_detection_t *raw, decoded_telemetry_t *out)
{
    if (raw == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Initialize output */
    memset(out, 0, sizeof(decoded_telemetry_t));

    /* Step 1: Classify the protocol using the Protocol Classifier */
    classification_result_t classification;
    memset(&classification, 0, sizeof(classification));
    classification.protocol = PROTOCOL_UNKNOWN;

    esp_err_t cls_err = classifier_classify(raw, &classification);
    if (cls_err != ESP_OK && cls_err != ESP_ERR_INVALID_ARG) {
        /* Non-critical: classification failure is not fatal.
         * We can still attempt source-based override. */
        classification.protocol = PROTOCOL_UNKNOWN;
        classification.confidence = CONFIDENCE_LOW;
    }

    /* Step 2: Determine effective protocol (source override or classifier) */
    protocol_type_t effective_protocol = determine_protocol(raw, &classification);

    /* Step 3: Map CROSSFIRE → ELRS decoder (same CRSF protocol) */
    protocol_type_t dispatch_protocol = effective_protocol;
    if (dispatch_protocol == PROTOCOL_CROSSFIRE) {
        dispatch_protocol = PROTOCOL_ELRS;
    }

    /* Step 4: Find the decoder for this protocol */
    telemetry_decode_fn decoder = find_decoder(dispatch_protocol);
    if (decoder == NULL) {
        /* No decoder available for this protocol */
        return ERR_DECODE_UNKNOWN_FMT;
    }

    /* Step 5: Dispatch to the protocol-specific decoder */
    esp_err_t decode_err = decoder(raw, out);
    if (decode_err != ESP_OK) {
        return decode_err;
    }

    /* Step 6: Update the Aircraft Registry with decoded data */
    confidence_level_t confidence = classification.confidence;
    /* Source-based RID detection is always HIGH confidence */
    if (raw->source == DETECTION_SOURCE_WIFI_RID ||
        raw->source == DETECTION_SOURCE_BLE_RID) {
        confidence = CONFIDENCE_HIGH;
    }

    update_registry(raw, out, effective_protocol, confidence);

    return ESP_OK;
}

void telemetry_decoder_set_registry(aircraft_registry_t *reg)
{
    s_registry = reg;
}

aircraft_registry_t *telemetry_decoder_get_registry(void)
{
    return s_registry;
}
