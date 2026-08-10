/**
 * @file protocol_signatures.h
 * @brief Protocol Signatures Database — pattern matching for drone protocol classification.
 *
 * Provides a signature-based protocol identification system. Signatures define
 * header byte patterns (with masks) and frequency ranges that uniquely identify
 * drone communication protocols. The module supports loading custom signatures
 * from a CSV file on SD card, falling back to an embedded default table.
 *
 * Validates: Requirements 7.1, 7.2, 7.4, 7.5
 */

#ifndef PROTOCOL_SIGNATURES_H
#define PROTOCOL_SIGNATURES_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Constants
 * ======================================================================== */

/** @brief Maximum number of signatures supported */
#define SIGNATURES_MAX_COUNT        100

/** @brief Maximum header pattern length in bytes */
#define SIGNATURE_HEADER_MAX_LEN    16

/** @brief Maximum modulation string length (including null terminator) */
#define SIGNATURE_MODULATION_MAX    16

/* ========================================================================
 * Types
 * ======================================================================== */

/** @brief Supported drone communication protocol types */
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
 * @brief Protocol signature entry.
 *
 * A signature defines a pattern to match against received packet headers,
 * optionally constrained to a frequency range. The match logic applies
 * header_mask to both the pattern and the input before comparison.
 *
 * freq_min_hz == 0 && freq_max_hz == 0 means "any frequency" (match regardless).
 */
typedef struct {
    protocol_type_t protocol;                       /**< Protocol this signature identifies */
    uint8_t header_pattern[SIGNATURE_HEADER_MAX_LEN]; /**< Expected header bytes (after masking) */
    uint8_t header_mask[SIGNATURE_HEADER_MAX_LEN];    /**< Bit mask applied before comparison */
    uint8_t header_len;                             /**< Number of header bytes to compare (1–16) */
    uint32_t freq_min_hz;                           /**< Minimum frequency in Hz (0 = any) */
    uint32_t freq_max_hz;                           /**< Maximum frequency in Hz (0 = any) */
    char modulation[SIGNATURE_MODULATION_MAX];       /**< Modulation type string (e.g., "LORA", "OFDM") */
} protocol_signature_t;

/* ========================================================================
 * API Functions
 * ======================================================================== */

/**
 * @brief Initialize the signatures database.
 *
 * Attempts to load signatures from the SD card file (signatures.csv).
 * If the file is absent or malformed, falls back to the embedded default table.
 *
 * @return 0 on success (defaults loaded count as success).
 */
int signatures_init(void);

/**
 * @brief Parse and load signatures from a CSV string.
 *
 * Parses the provided CSV data and replaces the current signature table.
 * The CSV must have a header row and follow the format:
 *   protocol,header_hex,header_mask_hex,header_len,freq_min_mhz,freq_max_mhz,modulation
 *
 * If the CSV is empty or entirely malformed, returns an error and the
 * signature table is reset to embedded defaults.
 *
 * @param[in] csv_data  Pointer to CSV string data.
 * @param[in] len       Length of the CSV data in bytes.
 *
 * @return 0 on success (at least one valid signature parsed),
 *         ERR_CONFIG_PARSE_FAIL if CSV is fundamentally malformed.
 */
int signatures_load_csv(const char *csv_data, size_t len);

/**
 * @brief Get a pointer to the current signature table.
 *
 * @param[out] count  Pointer to receive the number of signatures in the table.
 *
 * @return Pointer to the first element of the signature array (read-only).
 *         Returns NULL if count is NULL.
 */
const protocol_signature_t* signatures_get_table(uint16_t *count);

/**
 * @brief Find the first matching signature for a given packet header and frequency.
 *
 * For each signature in the table:
 *   1. Apply header_mask to both the signature pattern and the input header
 *   2. Compare the masked bytes (up to signature's header_len)
 *   3. If header matches, check frequency: (freq_min == 0 && freq_max == 0) means
 *      "any frequency", otherwise frequency_hz must be in [freq_min_hz, freq_max_hz]
 *
 * Returns the first matching signature, or NULL if no match found.
 *
 * @param[in] header        Pointer to the packet header bytes.
 * @param[in] header_len    Length of the provided header (bytes available for matching).
 * @param[in] frequency_hz  Frequency at which the packet was received (Hz).
 *
 * @return Pointer to the matching signature entry, or NULL if no match.
 */
const protocol_signature_t* signatures_find_match(const uint8_t *header,
                                                   uint8_t header_len,
                                                   uint32_t frequency_hz);

/**
 * @brief Serialize the current signature table to CSV format.
 *
 * Writes the signature table as a CSV string (with header row) into the
 * provided buffer.
 *
 * @param[out] buf       Output buffer for the CSV string.
 * @param[in]  buf_size  Size of the output buffer in bytes.
 * @param[out] written   Number of bytes written (excluding null terminator).
 *
 * @return 0 on success,
 *         ESP_ERR_INVALID_ARG if buf is NULL or buf_size is 0,
 *         ESP_ERR_NO_MEM if buffer is too small (partial output written).
 */
int signatures_serialize_csv(char *buf, size_t buf_size, size_t *written);

/**
 * @brief Get the number of signatures currently loaded.
 *
 * @return Number of signatures in the active table.
 */
uint16_t signatures_get_count(void);

/**
 * @brief Reset the signature table to embedded defaults.
 *
 * Useful for testing or when SD card data is determined invalid.
 */
void signatures_load_defaults(void);

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_SIGNATURES_H */
