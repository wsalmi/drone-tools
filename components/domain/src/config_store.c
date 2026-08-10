/**
 * @file config_store.c
 * @brief Configuration Store — JSON parser and validator implementation.
 *
 * Uses cJSON (bundled with ESP-IDF) to parse config.json and validates
 * each field against defined ranges. Invalid or missing fields fall back
 * to safe defaults per-field.
 *
 * Validates: Requirements 7.4, 7.5, 12.2
 */

#include "config_store.h"
#include "error_codes.h"
#include "cJSON.h"

#include <string.h>
#include <math.h>

/* ========================================================================
 * Default Values
 * ======================================================================== */

/* Alert defaults */
#define DEFAULT_SOUND_ENABLED               true
#define DEFAULT_PROXIMITY_THRESHOLD_M       500
#define DEFAULT_PROXIMITY_REPEAT_S          10
#define DEFAULT_OUT_OF_RANGE_TIMEOUT_S      30

/* Spectrum defaults */
#define DEFAULT_CENTER_FREQ_MHZ             915
#define DEFAULT_BANDWIDTH_KHZ               500
#define DEFAULT_GAIN_DB                     20.0f
#define DEFAULT_DETECTION_THRESHOLD_DBM     (-60)

/* Logging defaults */
#define DEFAULT_MAX_FILE_SIZE_MB            10
#define DEFAULT_BUFFER_SIZE_RECORDS         100

/* Scan defaults */
#define DEFAULT_REMOTEID_CYCLE_MS           3000
#define DEFAULT_NRF24_DWELL_TIME_MS         100
#define DEFAULT_LORA_DWELL_TIME_MS          50
#define DEFAULT_MODULE_POLL_INTERVAL_MS     500

/* GPS defaults */
#define DEFAULT_MIN_SATELLITES              4
#define DEFAULT_MAX_HDOP                    5.0f
#define DEFAULT_FIX_TIMEOUT_S              60
#define DEFAULT_DEGRADED_TIMEOUT_S          5

/* ========================================================================
 * Validation Ranges
 * ======================================================================== */

#define SPECTRUM_FREQ_MIN_MHZ       24
#define SPECTRUM_FREQ_MAX_MHZ       1766
#define SPECTRUM_BW_MIN_KHZ         10
#define SPECTRUM_BW_MAX_KHZ         1000
#define SPECTRUM_GAIN_MIN_DB        0.0f
#define SPECTRUM_GAIN_MAX_DB        49.6f

/* ========================================================================
 * Internal Helper Functions
 * ======================================================================== */

/**
 * @brief Safely read an unsigned integer from a cJSON object field.
 * @return true if value was read and is within [min_val, max_val], false otherwise.
 */
static bool read_uint32(const cJSON *obj, const char *key, uint32_t *out,
                        uint32_t min_val, uint32_t max_val)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    double val = item->valuedouble;
    if (val < (double)min_val || val > (double)max_val) {
        return false;
    }
    *out = (uint32_t)val;
    return true;
}

/**
 * @brief Safely read a uint8 from a cJSON object field.
 * @return true if value was read and is within [min_val, max_val], false otherwise.
 */
static bool read_uint8(const cJSON *obj, const char *key, uint8_t *out,
                       uint8_t min_val, uint8_t max_val)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    double val = item->valuedouble;
    if (val < (double)min_val || val > (double)max_val) {
        return false;
    }
    *out = (uint8_t)val;
    return true;
}

/**
 * @brief Safely read a float from a cJSON object field.
 * @return true if value was read and is within [min_val, max_val], false otherwise.
 */
static bool read_float(const cJSON *obj, const char *key, float *out,
                       float min_val, float max_val)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    float val = (float)item->valuedouble;
    if (val < min_val || val > max_val) {
        return false;
    }
    *out = val;
    return true;
}

/**
 * @brief Safely read an int32 from a cJSON object field (no range check).
 * @return true if value was read, false otherwise.
 */
static bool read_int32(const cJSON *obj, const char *key, int32_t *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    *out = (int32_t)item->valuedouble;
    return true;
}

/**
 * @brief Safely read a boolean from a cJSON object field.
 * @return true if value was read, false otherwise.
 */
static bool read_bool(const cJSON *obj, const char *key, bool *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsBool(item)) {
        return false;
    }
    *out = cJSON_IsTrue(item) ? true : false;
    return true;
}

/* ========================================================================
 * Section Parsers
 * ======================================================================== */

static void parse_alert_section(const cJSON *root, config_alert_t *alert)
{
    const cJSON *section = cJSON_GetObjectItemCaseSensitive(root, "alert");
    if (!cJSON_IsObject(section)) {
        return; /* Keep defaults */
    }

    read_bool(section, "sound_enabled", &alert->sound_enabled);
    read_uint32(section, "proximity_threshold_m", &alert->proximity_threshold_m, 1, UINT32_MAX);
    read_uint32(section, "proximity_repeat_interval_s", &alert->proximity_repeat_interval_s, 1, UINT32_MAX);
    read_uint32(section, "out_of_range_timeout_s", &alert->out_of_range_timeout_s, 1, UINT32_MAX);
}

static void parse_spectrum_section(const cJSON *root, config_spectrum_t *spectrum)
{
    const cJSON *section = cJSON_GetObjectItemCaseSensitive(root, "spectrum");
    if (!cJSON_IsObject(section)) {
        return;
    }

    read_uint32(section, "default_center_freq_mhz", &spectrum->default_center_freq_mhz,
                SPECTRUM_FREQ_MIN_MHZ, SPECTRUM_FREQ_MAX_MHZ);
    read_uint32(section, "default_bandwidth_khz", &spectrum->default_bandwidth_khz,
                SPECTRUM_BW_MIN_KHZ, SPECTRUM_BW_MAX_KHZ);
    read_float(section, "default_gain_db", &spectrum->default_gain_db,
               SPECTRUM_GAIN_MIN_DB, SPECTRUM_GAIN_MAX_DB);
    read_int32(section, "detection_threshold_dbm", &spectrum->detection_threshold_dbm);
}

static void parse_logging_section(const cJSON *root, config_logging_t *logging)
{
    const cJSON *section = cJSON_GetObjectItemCaseSensitive(root, "logging");
    if (!cJSON_IsObject(section)) {
        return;
    }

    read_uint32(section, "max_file_size_mb", &logging->max_file_size_mb, 1, UINT32_MAX);
    read_uint32(section, "buffer_size_records", &logging->buffer_size_records, 1, UINT32_MAX);
}

static void parse_scan_section(const cJSON *root, config_scan_t *scan)
{
    const cJSON *section = cJSON_GetObjectItemCaseSensitive(root, "scan");
    if (!cJSON_IsObject(section)) {
        return;
    }

    read_uint32(section, "remoteid_cycle_ms", &scan->remoteid_cycle_ms, 1, UINT32_MAX);
    read_uint32(section, "nrf24_dwell_time_ms", &scan->nrf24_dwell_time_ms, 1, UINT32_MAX);
    read_uint32(section, "lora_dwell_time_ms", &scan->lora_dwell_time_ms, 1, UINT32_MAX);
    read_uint32(section, "module_poll_interval_ms", &scan->module_poll_interval_ms, 1, UINT32_MAX);
}

static void parse_gps_section(const cJSON *root, config_gps_t *gps)
{
    const cJSON *section = cJSON_GetObjectItemCaseSensitive(root, "gps");
    if (!cJSON_IsObject(section)) {
        return;
    }

    read_uint8(section, "min_satellites", &gps->min_satellites, 1, 255);
    read_float(section, "max_hdop", &gps->max_hdop, 0.01f, 100.0f);
    read_uint32(section, "fix_timeout_s", &gps->fix_timeout_s, 1, UINT32_MAX);
    read_uint32(section, "degraded_timeout_s", &gps->degraded_timeout_s, 1, UINT32_MAX);
}

/* ========================================================================
 * Public API
 * ======================================================================== */

void config_store_get_defaults(config_store_t *config)
{
    if (config == NULL) {
        return;
    }

    /* Alert defaults */
    config->alert.sound_enabled = DEFAULT_SOUND_ENABLED;
    config->alert.proximity_threshold_m = DEFAULT_PROXIMITY_THRESHOLD_M;
    config->alert.proximity_repeat_interval_s = DEFAULT_PROXIMITY_REPEAT_S;
    config->alert.out_of_range_timeout_s = DEFAULT_OUT_OF_RANGE_TIMEOUT_S;

    /* Spectrum defaults */
    config->spectrum.default_center_freq_mhz = DEFAULT_CENTER_FREQ_MHZ;
    config->spectrum.default_bandwidth_khz = DEFAULT_BANDWIDTH_KHZ;
    config->spectrum.default_gain_db = DEFAULT_GAIN_DB;
    config->spectrum.detection_threshold_dbm = DEFAULT_DETECTION_THRESHOLD_DBM;

    /* Logging defaults */
    config->logging.max_file_size_mb = DEFAULT_MAX_FILE_SIZE_MB;
    config->logging.buffer_size_records = DEFAULT_BUFFER_SIZE_RECORDS;

    /* Scan defaults */
    config->scan.remoteid_cycle_ms = DEFAULT_REMOTEID_CYCLE_MS;
    config->scan.nrf24_dwell_time_ms = DEFAULT_NRF24_DWELL_TIME_MS;
    config->scan.lora_dwell_time_ms = DEFAULT_LORA_DWELL_TIME_MS;
    config->scan.module_poll_interval_ms = DEFAULT_MODULE_POLL_INTERVAL_MS;

    /* GPS defaults */
    config->gps.min_satellites = DEFAULT_MIN_SATELLITES;
    config->gps.max_hdop = DEFAULT_MAX_HDOP;
    config->gps.fix_timeout_s = DEFAULT_FIX_TIMEOUT_S;
    config->gps.degraded_timeout_s = DEFAULT_DEGRADED_TIMEOUT_S;
}

int config_store_load_from_json(const char *json_str, config_store_t *out)
{
    if (out == NULL) {
        return ERR_CONFIG_PARSE_FAIL;
    }

    /* Start with defaults — any missing/invalid field keeps its default */
    config_store_get_defaults(out);

    if (json_str == NULL || json_str[0] == '\0') {
        return ERR_CONFIG_PARSE_FAIL;
    }

    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) {
        return ERR_CONFIG_PARSE_FAIL;
    }

    /* Parse each section — missing sections keep defaults */
    parse_alert_section(root, &out->alert);
    parse_spectrum_section(root, &out->spectrum);
    parse_logging_section(root, &out->logging);
    parse_scan_section(root, &out->scan);
    parse_gps_section(root, &out->gps);

    cJSON_Delete(root);
    return 0;
}
