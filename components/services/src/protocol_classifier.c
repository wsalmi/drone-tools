/**
 * @file protocol_classifier.c
 * @brief Protocol Classifier Service — implementation.
 *
 * Classifies raw RF detections into known drone communication protocols
 * using the protocol_signatures database. Determines confidence level
 * based on how specific the match is (header + frequency vs header only).
 *
 * Classification logic per signature:
 *   1. Apply header_mask to first header_len bytes of packet payload
 *   2. Compare masked payload against header_pattern
 *   3. If header matches AND frequency is in range → CONFIDENCE_HIGH
 *   4. If header matches but frequency is outside range
 *      (or signature freq is 0/0 meaning "any") → CONFIDENCE_LOW
 *   5. If no signature matches at all → PROTOCOL_UNKNOWN
 *
 * The classifier prioritizes HIGH confidence matches over LOW ones.
 * If multiple signatures match, the first HIGH-confidence match wins.
 * If no HIGH-confidence match exists, the first LOW-confidence match is used.
 *
 * Validates: Requirements 7.1, 7.2, 7.3, 7.6
 */

#include "protocol_classifier.h"
#include "protocol_signatures.h"

#include <string.h>

/* ========================================================================
 * Internal State
 * ======================================================================== */

static bool s_initialized = false;

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

esp_err_t classifier_init(const char *signatures_file_path)
{
    /* Initialize the signatures database (loads embedded defaults) */
    int ret = signatures_init();
    if (ret != 0) {
        return ESP_FAIL;
    }

    /* If a custom signatures file path is provided, attempt to load it.
     * On failure, signatures_init() already loaded defaults so we continue. */
    if (signatures_file_path != NULL) {
        /* In a full implementation, we would read the file from SD here:
         *   1. Open the file at signatures_file_path
         *   2. Read contents into a buffer
         *   3. Call signatures_load_csv(buffer, length)
         *
         * For now, the file loading is handled externally or by the
         * system init sequence that reads SD and calls signatures_load_csv().
         * The path parameter is reserved for future direct file loading.
         */
        (void)signatures_file_path;
    }

    s_initialized = true;
    return ESP_OK;
}

esp_err_t classifier_classify(const raw_detection_t *raw, classification_result_t *result)
{
    if (raw == NULL || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Initialize result to UNKNOWN state */
    memset(result, 0, sizeof(classification_result_t));
    result->protocol = PROTOCOL_UNKNOWN;
    result->confidence = CONFIDENCE_LOW;
    result->frequency_hz = raw->frequency_hz;

    /* Need at least 1 byte of payload to classify */
    if (raw->payload_len == 0) {
        return ESP_OK;
    }

    uint16_t sig_count = 0;
    const protocol_signature_t *table = signatures_get_table(&sig_count);
    if (table == NULL || sig_count == 0) {
        return ESP_OK;
    }

    uint8_t input_len = (uint8_t)(raw->payload_len > 255 ? 255 : raw->payload_len);

    /* Track the first header-only (LOW confidence) match as a fallback */
    const protocol_signature_t *low_match = NULL;

    for (uint16_t i = 0; i < sig_count; i++) {
        const protocol_signature_t *sig = &table[i];

        /* Check that we have enough input bytes to match this signature */
        if (input_len < sig->header_len) {
            continue;
        }

        /* Apply mask and compare header bytes */
        bool header_match = true;
        for (uint8_t b = 0; b < sig->header_len; b++) {
            uint8_t masked_input = raw->raw_payload[b] & sig->header_mask[b];
            uint8_t masked_pattern = sig->header_pattern[b] & sig->header_mask[b];
            if (masked_input != masked_pattern) {
                header_match = false;
                break;
            }
        }

        if (!header_match) {
            continue;
        }

        /* Header matched — now check frequency to determine confidence */
        if (sig->freq_min_hz == 0 && sig->freq_max_hz == 0) {
            /* Signature accepts any frequency: header-only match → LOW confidence */
            if (low_match == NULL) {
                low_match = sig;
            }
        } else if (raw->frequency_hz >= sig->freq_min_hz &&
                   raw->frequency_hz <= sig->freq_max_hz) {
            /* Header matches AND frequency is within range → HIGH confidence */
            result->protocol = sig->protocol;
            result->confidence = CONFIDENCE_HIGH;
            strncpy(result->modulation_info, sig->modulation,
                    sizeof(result->modulation_info) - 1);
            result->modulation_info[sizeof(result->modulation_info) - 1] = '\0';
            return ESP_OK;
        } else {
            /* Header matches but frequency is outside this signature's range → LOW confidence */
            if (low_match == NULL) {
                low_match = sig;
            }
        }
    }

    /* No HIGH confidence match found. Use the first LOW confidence match if available. */
    if (low_match != NULL) {
        result->protocol = low_match->protocol;
        result->confidence = CONFIDENCE_LOW;
        strncpy(result->modulation_info, low_match->modulation,
                sizeof(result->modulation_info) - 1);
        result->modulation_info[sizeof(result->modulation_info) - 1] = '\0';
    }
    /* Otherwise result stays PROTOCOL_UNKNOWN with CONFIDENCE_LOW */

    return ESP_OK;
}

uint16_t classifier_get_signature_count(void)
{
    return signatures_get_count();
}
