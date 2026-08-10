/**
 * @file protocol_signatures.c
 * @brief Protocol Signatures Database — implementation.
 *
 * Implements CSV parsing and header+frequency matching for protocol
 * classification. Falls back to embedded defaults when CSV is unavailable.
 *
 * Validates: Requirements 7.1, 7.2, 7.4, 7.5
 */

#include "protocol_signatures.h"
#include "error_codes.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

/* ========================================================================
 * Internal State
 * ======================================================================== */

static protocol_signature_t s_signatures[SIGNATURES_MAX_COUNT];
static uint16_t s_signature_count = 0;
static bool s_initialized = false;

/* ========================================================================
 * Default Embedded Signature Table
 * ======================================================================== */

static const protocol_signature_t DEFAULT_SIGNATURES[] = {
    /* ELRS 868/915 MHz band */
    {
        .protocol = PROTOCOL_ELRS,
        .header_pattern = {0x00},
        .header_mask = {0xFC},
        .header_len = 1,
        .freq_min_hz = 862000000,
        .freq_max_hz = 928000000,
        .modulation = "LORA"
    },
    /* ELRS 2.4 GHz band */
    {
        .protocol = PROTOCOL_ELRS,
        .header_pattern = {0x00},
        .header_mask = {0xFC},
        .header_len = 1,
        .freq_min_hz = 2400000000UL,
        .freq_max_hz = 2500000000UL,
        .modulation = "LORA"
    },
    /* MAVLink v1 (STX = 0xFE) — any frequency */
    {
        .protocol = PROTOCOL_MAVLINK,
        .header_pattern = {0xFE},
        .header_mask = {0xFF},
        .header_len = 1,
        .freq_min_hz = 0,
        .freq_max_hz = 0,
        .modulation = "ANY"
    },
    /* MAVLink v2 (STX = 0xFD) — any frequency */
    {
        .protocol = PROTOCOL_MAVLINK,
        .header_pattern = {0xFD},
        .header_mask = {0xFF},
        .header_len = 1,
        .freq_min_hz = 0,
        .freq_max_hz = 0,
        .modulation = "ANY"
    },
    /* DJI 2.4 GHz */
    {
        .protocol = PROTOCOL_DJI,
        .header_pattern = {0x55},
        .header_mask = {0xFF},
        .header_len = 1,
        .freq_min_hz = 2400000000UL,
        .freq_max_hz = 2500000000UL,
        .modulation = "OFDM"
    },
    /* Crossfire 868/915 MHz band */
    {
        .protocol = PROTOCOL_CROSSFIRE,
        .header_pattern = {0xC8},
        .header_mask = {0xFF},
        .header_len = 1,
        .freq_min_hz = 862000000,
        .freq_max_hz = 928000000,
        .modulation = "LORA"
    },
    /* FrSky 2.4 GHz */
    {
        .protocol = PROTOCOL_FRSKY,
        .header_pattern = {0x7E},
        .header_mask = {0xFF},
        .header_len = 1,
        .freq_min_hz = 2400000000UL,
        .freq_max_hz = 2500000000UL,
        .modulation = "FHSS"
    },
};

#define DEFAULT_SIGNATURE_COUNT \
    (sizeof(DEFAULT_SIGNATURES) / sizeof(DEFAULT_SIGNATURES[0]))

/* ========================================================================
 * Internal Helpers — Hex Parsing
 * ======================================================================== */

/**
 * @brief Convert a single hex character to its nibble value.
 * @return 0–15 on success, -1 on invalid character.
 */
static int hex_char_to_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/**
 * @brief Parse a hex string into a byte array.
 *
 * @param[in]  hex_str   Hex string (e.g., "FE" or "C8FF").
 * @param[in]  hex_len   Length of hex string (must be even).
 * @param[out] out       Output byte array.
 * @param[in]  out_max   Maximum bytes to write.
 * @param[out] out_len   Number of bytes written.
 *
 * @return true on success, false on invalid hex.
 */
static bool parse_hex_bytes(const char *hex_str, size_t hex_len,
                            uint8_t *out, size_t out_max, uint8_t *out_len)
{
    if (hex_len == 0 || (hex_len % 2) != 0) {
        *out_len = 0;
        return hex_len == 0; /* empty is OK (means "no mask") */
    }

    size_t byte_count = hex_len / 2;
    if (byte_count > out_max) {
        *out_len = 0;
        return false;
    }

    for (size_t i = 0; i < byte_count; i++) {
        int hi = hex_char_to_nibble(hex_str[i * 2]);
        int lo = hex_char_to_nibble(hex_str[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            *out_len = 0;
            return false;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    *out_len = (uint8_t)byte_count;
    return true;
}

/* ========================================================================
 * Internal Helpers — Protocol Name Mapping
 * ======================================================================== */

/**
 * @brief Map protocol name string to protocol_type_t enum.
 */
static protocol_type_t protocol_from_name(const char *name)
{
    if (strcasecmp(name, "ELRS") == 0) return PROTOCOL_ELRS;
    if (strcasecmp(name, "DJI") == 0) return PROTOCOL_DJI;
    if (strcasecmp(name, "WIFI") == 0) return PROTOCOL_WIFI;
    if (strcasecmp(name, "MAVLINK") == 0) return PROTOCOL_MAVLINK;
    if (strcasecmp(name, "CROSSFIRE") == 0) return PROTOCOL_CROSSFIRE;
    if (strcasecmp(name, "FRSKY") == 0) return PROTOCOL_FRSKY;
    if (strcasecmp(name, "REMOTEID") == 0) return PROTOCOL_REMOTEID;
    return PROTOCOL_UNKNOWN;
}

/**
 * @brief Map protocol_type_t enum to protocol name string.
 */
static const char* protocol_to_name(protocol_type_t proto)
{
    switch (proto) {
        case PROTOCOL_ELRS:      return "ELRS";
        case PROTOCOL_DJI:       return "DJI";
        case PROTOCOL_WIFI:      return "WIFI";
        case PROTOCOL_MAVLINK:   return "MAVLINK";
        case PROTOCOL_CROSSFIRE: return "CROSSFIRE";
        case PROTOCOL_FRSKY:     return "FRSKY";
        case PROTOCOL_REMOTEID:  return "REMOTEID";
        default:                 return "UNKNOWN";
    }
}

/* ========================================================================
 * Internal Helpers — CSV Line Parsing
 * ======================================================================== */

/**
 * @brief Extract the next CSV field from a line, advancing the pointer.
 *
 * Writes the field content into 'field_buf' (null-terminated).
 * Handles fields separated by commas and terminated by '\n', '\r', or '\0'.
 *
 * @return true if a field was extracted, false if end of line/data.
 */
static bool csv_next_field(const char **pos, char *field_buf, size_t field_max)
{
    const char *p = *pos;
    if (p == NULL || *p == '\0' || *p == '\n' || *p == '\r') {
        field_buf[0] = '\0';
        return false;
    }

    size_t i = 0;
    while (*p != ',' && *p != '\n' && *p != '\r' && *p != '\0') {
        if (i < field_max - 1) {
            field_buf[i++] = *p;
        }
        p++;
    }
    field_buf[i] = '\0';

    /* Skip the comma delimiter (if present) */
    if (*p == ',') {
        p++;
    }
    *pos = p;
    return true;
}

/**
 * @brief Advance pointer to the start of the next line.
 */
static void csv_skip_to_next_line(const char **pos)
{
    const char *p = *pos;
    while (*p != '\0' && *p != '\n') {
        p++;
    }
    if (*p == '\n') {
        p++;
    }
    *pos = p;
}

/**
 * @brief Trim leading and trailing whitespace from a string in place.
 */
static void trim_whitespace(char *str)
{
    /* Trim leading */
    char *start = str;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }

    /* Trim trailing */
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[--len] = '\0';
    }
}

/**
 * @brief Parse a single CSV data line into a protocol_signature_t.
 *
 * Expected format: protocol,header_hex,header_mask_hex,header_len,freq_min_mhz,freq_max_mhz,modulation
 *
 * @return true if parsing succeeded, false on any error.
 */
static bool parse_csv_line(const char *line, protocol_signature_t *sig)
{
    char field[64];
    const char *pos = line;

    memset(sig, 0, sizeof(*sig));

    /* Field 1: protocol name */
    if (!csv_next_field(&pos, field, sizeof(field))) return false;
    trim_whitespace(field);
    if (field[0] == '\0') return false;
    sig->protocol = protocol_from_name(field);
    if (sig->protocol == PROTOCOL_UNKNOWN && strcasecmp(field, "UNKNOWN") != 0) {
        return false; /* Unknown protocol name is invalid unless explicitly "UNKNOWN" */
    }

    /* Field 2: header_hex */
    if (!csv_next_field(&pos, field, sizeof(field))) return false;
    trim_whitespace(field);
    if (field[0] == '\0') return false; /* header_hex is required */
    uint8_t parsed_len = 0;
    if (!parse_hex_bytes(field, strlen(field), sig->header_pattern,
                         SIGNATURE_HEADER_MAX_LEN, &parsed_len)) {
        return false;
    }
    if (parsed_len == 0) return false;

    /* Field 3: header_mask_hex (optional — empty means all 0xFF) */
    if (!csv_next_field(&pos, field, sizeof(field))) return false;
    trim_whitespace(field);
    uint8_t mask_len = 0;
    if (field[0] == '\0') {
        /* No mask specified — default to 0xFF for all header bytes */
        memset(sig->header_mask, 0xFF, parsed_len);
        mask_len = parsed_len;
    } else {
        if (!parse_hex_bytes(field, strlen(field), sig->header_mask,
                             SIGNATURE_HEADER_MAX_LEN, &mask_len)) {
            return false;
        }
        /* If mask is shorter than header, pad with 0xFF */
        for (uint8_t i = mask_len; i < parsed_len; i++) {
            sig->header_mask[i] = 0xFF;
        }
    }

    /* Field 4: header_len */
    if (!csv_next_field(&pos, field, sizeof(field))) return false;
    trim_whitespace(field);
    unsigned long hlen = strtoul(field, NULL, 10);
    if (hlen == 0 || hlen > SIGNATURE_HEADER_MAX_LEN) return false;
    sig->header_len = (uint8_t)hlen;

    /* Validate that parsed header bytes >= declared header_len */
    if (parsed_len < sig->header_len) return false;

    /* Field 5: freq_min_mhz */
    if (!csv_next_field(&pos, field, sizeof(field))) return false;
    trim_whitespace(field);
    unsigned long freq_min = strtoul(field, NULL, 10);
    sig->freq_min_hz = (uint32_t)(freq_min * 1000000UL);

    /* Field 6: freq_max_mhz */
    if (!csv_next_field(&pos, field, sizeof(field))) return false;
    trim_whitespace(field);
    unsigned long freq_max = strtoul(field, NULL, 10);
    sig->freq_max_hz = (uint32_t)(freq_max * 1000000UL);

    /* Field 7: modulation */
    if (!csv_next_field(&pos, field, sizeof(field))) return false;
    trim_whitespace(field);
    if (field[0] != '\0') {
        strncpy(sig->modulation, field, SIGNATURE_MODULATION_MAX - 1);
        sig->modulation[SIGNATURE_MODULATION_MAX - 1] = '\0';
    }

    return true;
}

/* ========================================================================
 * Internal Helpers — Header Line Detection
 * ======================================================================== */

/**
 * @brief Check if a CSV line is the header row.
 */
static bool is_header_line(const char *line)
{
    /* Simple heuristic: header contains "protocol" keyword */
    const char *p = line;
    char field[64];
    if (!csv_next_field(&p, field, sizeof(field))) return false;
    trim_whitespace(field);

    /* Case-insensitive check for "protocol" */
    return (strcasecmp(field, "protocol") == 0);
}

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

void signatures_load_defaults(void)
{
    s_signature_count = (uint16_t)DEFAULT_SIGNATURE_COUNT;
    memcpy(s_signatures, DEFAULT_SIGNATURES,
           DEFAULT_SIGNATURE_COUNT * sizeof(protocol_signature_t));
}

int signatures_init(void)
{
    signatures_load_defaults();
    s_initialized = true;
    return 0;
}

int signatures_load_csv(const char *csv_data, size_t len)
{
    if (csv_data == NULL || len == 0) {
        signatures_load_defaults();
        return ERR_CONFIG_PARSE_FAIL;
    }

    /* Temporary storage while parsing — only commit if at least one valid entry */
    protocol_signature_t temp_table[SIGNATURES_MAX_COUNT];
    uint16_t temp_count = 0;

    const char *pos = csv_data;
    const char *end = csv_data + len;
    bool header_skipped = false;

    while (pos < end && *pos != '\0') {
        /* Skip empty lines */
        while (pos < end && (*pos == '\n' || *pos == '\r')) {
            pos++;
        }
        if (pos >= end || *pos == '\0') break;

        /* Check if this is the header row */
        if (!header_skipped && is_header_line(pos)) {
            header_skipped = true;
            csv_skip_to_next_line(&pos);
            continue;
        }

        /* Try to parse the line */
        if (temp_count < SIGNATURES_MAX_COUNT) {
            protocol_signature_t sig;
            if (parse_csv_line(pos, &sig)) {
                temp_table[temp_count++] = sig;
            }
            /* Invalid lines are silently skipped */
        }

        csv_skip_to_next_line(&pos);
    }

    if (temp_count == 0) {
        /* No valid signatures parsed — fall back to defaults */
        signatures_load_defaults();
        return ERR_CONFIG_PARSE_FAIL;
    }

    /* Commit parsed table */
    s_signature_count = temp_count;
    memcpy(s_signatures, temp_table, temp_count * sizeof(protocol_signature_t));

    return 0;
}

const protocol_signature_t* signatures_get_table(uint16_t *count)
{
    if (count == NULL) {
        return NULL;
    }
    *count = s_signature_count;
    return s_signatures;
}

const protocol_signature_t* signatures_find_match(const uint8_t *header,
                                                   uint8_t header_len,
                                                   uint32_t frequency_hz)
{
    if (header == NULL || header_len == 0) {
        return NULL;
    }

    for (uint16_t i = 0; i < s_signature_count; i++) {
        const protocol_signature_t *sig = &s_signatures[i];

        /* Check that we have enough input bytes to match */
        if (header_len < sig->header_len) {
            continue;
        }

        /* Apply mask and compare header bytes */
        bool header_match = true;
        for (uint8_t b = 0; b < sig->header_len; b++) {
            uint8_t masked_input = header[b] & sig->header_mask[b];
            uint8_t masked_pattern = sig->header_pattern[b] & sig->header_mask[b];
            if (masked_input != masked_pattern) {
                header_match = false;
                break;
            }
        }

        if (!header_match) {
            continue;
        }

        /* Check frequency range */
        if (sig->freq_min_hz == 0 && sig->freq_max_hz == 0) {
            /* "Any frequency" — match regardless */
            return sig;
        }

        if (frequency_hz >= sig->freq_min_hz && frequency_hz <= sig->freq_max_hz) {
            return sig;
        }
    }

    return NULL;
}

int signatures_serialize_csv(char *buf, size_t buf_size, size_t *written)
{
    if (buf == NULL || buf_size == 0) {
        return -1; /* ESP_ERR_INVALID_ARG equivalent */
    }

    if (written != NULL) {
        *written = 0;
    }

    size_t offset = 0;

    /* Write header row */
    int n = snprintf(buf + offset, buf_size - offset,
                     "protocol,header_hex,header_mask_hex,header_len,freq_min_mhz,freq_max_mhz,modulation\n");
    if (n < 0 || (size_t)n >= buf_size - offset) {
        if (written != NULL) *written = offset;
        return -2; /* ESP_ERR_NO_MEM equivalent */
    }
    offset += (size_t)n;

    /* Write each signature */
    for (uint16_t i = 0; i < s_signature_count; i++) {
        const protocol_signature_t *sig = &s_signatures[i];

        /* Build header_hex string */
        char header_hex[SIGNATURE_HEADER_MAX_LEN * 2 + 1];
        for (uint8_t b = 0; b < sig->header_len; b++) {
            snprintf(header_hex + b * 2, 3, "%02X", sig->header_pattern[b]);
        }
        header_hex[sig->header_len * 2] = '\0';

        /* Build header_mask_hex string */
        char mask_hex[SIGNATURE_HEADER_MAX_LEN * 2 + 1];
        bool all_ff = true;
        for (uint8_t b = 0; b < sig->header_len; b++) {
            if (sig->header_mask[b] != 0xFF) {
                all_ff = false;
                break;
            }
        }
        if (all_ff) {
            /* Omit mask if all bytes are 0xFF (convention from design doc for MAVLink) */
            mask_hex[0] = '\0';
        } else {
            for (uint8_t b = 0; b < sig->header_len; b++) {
                snprintf(mask_hex + b * 2, 3, "%02X", sig->header_mask[b]);
            }
            mask_hex[sig->header_len * 2] = '\0';
        }

        /* Convert frequencies from Hz back to MHz */
        uint32_t freq_min_mhz = sig->freq_min_hz / 1000000;
        uint32_t freq_max_mhz = sig->freq_max_hz / 1000000;

        n = snprintf(buf + offset, buf_size - offset,
                     "%s,%s,%s,%u,%u,%u,%s\n",
                     protocol_to_name(sig->protocol),
                     header_hex,
                     mask_hex,
                     (unsigned)sig->header_len,
                     (unsigned)freq_min_mhz,
                     (unsigned)freq_max_mhz,
                     sig->modulation);

        if (n < 0 || (size_t)n >= buf_size - offset) {
            if (written != NULL) *written = offset;
            return -2; /* Buffer too small */
        }
        offset += (size_t)n;
    }

    buf[offset] = '\0';
    if (written != NULL) {
        *written = offset;
    }

    return 0;
}

uint16_t signatures_get_count(void)
{
    return s_signature_count;
}
