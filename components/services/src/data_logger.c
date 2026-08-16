/**
 * @file data_logger.c
 * @brief Data Logger Service implementation.
 *
 * Implements CSV logging with circular RAM buffer, file rotation,
 * and KML export for the drone telemetry monitor.
 *
 * Validates: Requirements 11.1, 11.2, 11.3, 11.4, 11.5
 */

#include "data_logger.h"
#include "hal_sd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

/* ========================================================================
 * Constants
 * ======================================================================== */

/** @brief Log directory on SD card */
#define LOG_DIR             "/sdcard/logs"

/** @brief KML directory on SD card */
#define KML_DIR             "/sdcard/kml"

/** @brief Session filename prefix */
#define LOG_FILE_PREFIX     "session_"

/** @brief Log file extension */
#define LOG_FILE_EXT        ".csv"

/** @brief File rotation counter max */
#define MAX_ROTATION_FILES  999

/** @brief Consecutive SD write failures (while reportedly mounted) before
 *  the card is treated as unavailable, so records fall back to the RAM
 *  buffer and hal_sd_is_mounted() transitioning back to true later
 *  triggers a proper flush instead of silently losing data forever. */
#define MAX_CONSECUTIVE_SD_WRITE_FAILURES  3

/* ========================================================================
 * Protocol name lookup table
 * ======================================================================== */

static const char *s_protocol_names[] = {
    [PROTOCOL_ELRS]       = "ELRS",
    [PROTOCOL_DJI]        = "DJI",
    [PROTOCOL_WIFI]       = "WIFI",
    [PROTOCOL_MAVLINK]    = "MAVLINK",
    [PROTOCOL_CROSSFIRE]  = "CROSSFIRE",
    [PROTOCOL_FRSKY]      = "FRSKY",
    [PROTOCOL_REMOTEID]   = "REMOTEID",
    [PROTOCOL_UNKNOWN]    = "UNKNOWN"
};

#define PROTOCOL_COUNT (sizeof(s_protocol_names) / sizeof(s_protocol_names[0]))

/* ========================================================================
 * Event type name lookup table
 * ======================================================================== */

static const char *s_event_names[] = {
    [LOG_EVENT_TELEMETRY]    = "TELEMETRY",
    [LOG_EVENT_DETECTION]    = "DETECTION",
    [LOG_EVENT_OUT_OF_RANGE] = "OUT_OF_RANGE",
    [LOG_EVENT_PROTOCOL_ID]  = "PROTOCOL_ID"
};

#define EVENT_COUNT (sizeof(s_event_names) / sizeof(s_event_names[0]))

/* ========================================================================
 * Circular Buffer
 * ======================================================================== */

typedef struct {
    log_record_t records[DATA_LOGGER_BUFFER_SIZE];
    size_t head;        /**< Next write index */
    size_t count;       /**< Number of valid records */
} circular_buffer_t;

/* ========================================================================
 * Module State
 * ======================================================================== */

static bool s_initialized = false;
static bool s_sd_was_available = false;
static uint8_t s_consecutive_write_failures = 0;
static circular_buffer_t s_buffer;

/** Current CSV file handle */
static hal_sd_file_t s_current_file;
static bool s_file_open = false;
static size_t s_current_file_size = 0;

/** Session tracking */
static uint32_t s_session_id = 0;
static uint16_t s_rotation_index = 0;
static char s_current_path[DATA_LOGGER_MAX_PATH_LEN];

/* ========================================================================
 * Internal Helpers
 * ======================================================================== */

/**
 * @brief Generate a filename for the current session and rotation.
 */
static void generate_filename(char *path, size_t path_len)
{
    snprintf(path, path_len, "%s/%s%04lu_%03u%s",
             LOG_DIR, LOG_FILE_PREFIX,
             (unsigned long)s_session_id, s_rotation_index, LOG_FILE_EXT);
}

#include <sys/stat.h>

/**
 * @brief Open a new CSV file for writing (creates with header).
 */
static esp_err_t open_new_file(void)
{
    /* Close existing file if open */
    if (s_file_open) {
        hal_sd_close(&s_current_file);
        s_file_open = false;
    }

    mkdir(LOG_DIR, 0777);
    generate_filename(s_current_path, sizeof(s_current_path));

    esp_err_t err = hal_sd_open(s_current_path, "w", &s_current_file);
    if (err != ESP_OK) {
        return err;
    }

    s_file_open = true;
    s_current_file_size = 0;

    /* Write CSV header */
    const char *header = DATA_LOGGER_CSV_HEADER "\n";
    size_t header_len = strlen(header);
    size_t written = 0;
    err = hal_sd_write(&s_current_file, header, header_len, &written);
    if (err != ESP_OK) {
        hal_sd_close(&s_current_file);
        s_file_open = false;
        return err;
    }
    s_current_file_size += written;

    return ESP_OK;
}

/**
 * @brief Rotate to a new file (increment rotation index and open new file).
 *
 * When the rotation index for the current session exhausts
 * MAX_ROTATION_FILES, advance to a new session ID instead of wrapping
 * the index back to 0 — wrapping would reuse the same filename
 * (session_XXXX_000.csv) and silently overwrite the first file of the
 * current session.
 */
static esp_err_t rotate_file(void)
{
    s_rotation_index++;
    if (s_rotation_index > MAX_ROTATION_FILES) {
        s_session_id++;
        s_rotation_index = 0;
    }
    return open_new_file();
}

/**
 * @brief Write a single CSV line to the current file, handling rotation.
 */
static esp_err_t write_csv_line(const char *line, size_t line_len)
{
    if (!s_file_open) {
        esp_err_t err = open_new_file();
        if (err != ESP_OK) {
            return err;
        }
    }

    /* Check if rotation is needed BEFORE writing */
    if (s_current_file_size > DATA_LOGGER_MAX_FILE_SIZE) {
        esp_err_t err = rotate_file();
        if (err != ESP_OK) {
            return err;
        }
    }

    size_t written = 0;
    esp_err_t err = hal_sd_write(&s_current_file, line, line_len, &written);
    if (err != ESP_OK) {
        return err;
    }
    s_current_file_size += written;

    return ESP_OK;
}

/**
 * @brief Add a record to the circular RAM buffer.
 */
static void buffer_push(const log_record_t *record)
{
    s_buffer.records[s_buffer.head] = *record;
    s_buffer.head = (s_buffer.head + 1) % DATA_LOGGER_BUFFER_SIZE;
    if (s_buffer.count < DATA_LOGGER_BUFFER_SIZE) {
        s_buffer.count++;
    }
}

/* ========================================================================
 * Public API
 * ======================================================================== */

esp_err_t data_logger_init(void)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Initialize buffer */
    memset(&s_buffer, 0, sizeof(s_buffer));

    /* Generate session ID from current time (or incrementing counter) */
    s_session_id++;
    s_rotation_index = 0;
    s_file_open = false;
    s_current_file_size = 0;

    /* Check if SD is available and open initial file */
    s_consecutive_write_failures = 0;
    s_sd_was_available = hal_sd_is_mounted();
    if (s_sd_was_available) {
        esp_err_t err = open_new_file();
        if (err != ESP_OK) {
            /* SD reported mounted but open failed — operate in buffer mode */
            s_sd_was_available = false;
        }
    }

    s_initialized = true;
    return ESP_OK;
}

esp_err_t data_logger_log(const log_record_t *record)
{
    if (record == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Serialize record to CSV */
    char line[DATA_LOGGER_MAX_LINE_LEN];
    int len = data_logger_record_to_csv(record, line, sizeof(line) - 1);
    if (len < 0) {
        return ESP_FAIL;
    }

    /* Append newline */
    line[len] = '\n';
    line[len + 1] = '\0';
    size_t total_len = (size_t)(len + 1);

    /* Try to write to SD */
    if (hal_sd_is_mounted()) {
        esp_err_t err = write_csv_line(line, total_len);
        if (err == ESP_OK) {
            s_consecutive_write_failures = 0;
            return ESP_OK;
        }

        /* Write failed even though the card reports as mounted (e.g. I/O
         * error without the card being physically removed). After enough
         * consecutive failures, stop trusting hal_sd_is_mounted() and mark
         * the SD as unavailable so data_logger_check_sd() can detect a
         * later true availability transition and flush the buffer instead
         * of silently dropping records once the buffer wraps. */
        if (s_consecutive_write_failures < UINT8_MAX) {
            s_consecutive_write_failures++;
        }
        if (s_consecutive_write_failures >= MAX_CONSECUTIVE_SD_WRITE_FAILURES) {
            s_sd_was_available = false;
            if (s_file_open) {
                hal_sd_close(&s_current_file);
                s_file_open = false;
            }
        }
        /* Fall through to buffer if write fails */
    }

    /* SD unavailable or write failed — store in RAM buffer */
    buffer_push(record);
    return ESP_OK;
}

esp_err_t data_logger_flush_buffer(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!hal_sd_is_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_buffer.count == 0) {
        return ESP_OK;
    }

    /* Determine read starting index:
     * If buffer is full, start from head (oldest item).
     * If not full, start from 0. */
    size_t start;
    if (s_buffer.count == DATA_LOGGER_BUFFER_SIZE) {
        start = s_buffer.head; /* head points to the oldest record */
    } else {
        start = 0;
    }

    /* Write all buffered records in insertion order */
    for (size_t i = 0; i < s_buffer.count; i++) {
        size_t idx = (start + i) % DATA_LOGGER_BUFFER_SIZE;
        const log_record_t *rec = &s_buffer.records[idx];

        char line[DATA_LOGGER_MAX_LINE_LEN];
        int len = data_logger_record_to_csv(rec, line, sizeof(line) - 1);
        if (len < 0) {
            continue;
        }
        line[len] = '\n';
        line[len + 1] = '\0';

        esp_err_t err = write_csv_line(line, (size_t)(len + 1));
        if (err != ESP_OK) {
            return ESP_FAIL;
        }
    }

    /* Clear buffer after successful flush */
    s_buffer.count = 0;
    s_buffer.head = 0;

    return ESP_OK;
}

esp_err_t data_logger_check_sd(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    bool sd_available = hal_sd_is_mounted();

    /* Detect transition from unavailable to available. This also covers
     * the case where data_logger_log() forced s_sd_was_available to false
     * after repeated write failures while hal_sd_is_mounted() kept
     * reporting true — once writes start succeeding again this branch
     * flushes the buffer and resets the failure counter. */
    if (sd_available && !s_sd_was_available) {
        /* SD just became available — flush buffer */
        if (!s_file_open) {
            esp_err_t err = open_new_file();
            if (err != ESP_OK) {
                return ESP_FAIL;
            }
        }
        esp_err_t err = data_logger_flush_buffer();
        if (err == ESP_OK) {
            s_sd_was_available = true;
            s_consecutive_write_failures = 0;
        }
        return err;
    }

    s_sd_was_available = sd_available;
    return ESP_OK;
}

esp_err_t data_logger_generate_kml(const char *output_path,
                                   const kml_placemark_t *placemarks,
                                   size_t count)
{
    if (output_path == NULL || (placemarks == NULL && count > 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!hal_sd_is_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }

    hal_sd_file_t kml_file;
    esp_err_t err = hal_sd_open(output_path, "w", &kml_file);
    if (err != ESP_OK) {
        return err;
    }

    /* KML header */
    const char *kml_header =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<kml xmlns=\"http://www.opengis.net/kml/2.2\">\n"
        "<Document>\n"
        "  <name>Drone Telemetry Session</name>\n"
        "  <Style id=\"aircraft\">\n"
        "    <IconStyle><color>ff0000ff</color></IconStyle>\n"
        "  </Style>\n"
        "  <Style id=\"pilot\">\n"
        "    <IconStyle><color>ff00ff00</color></IconStyle>\n"
        "  </Style>\n";

    hal_sd_write(&kml_file, kml_header, strlen(kml_header), NULL);

    /* Write placemarks */
    for (size_t i = 0; i < count; i++) {
        const kml_placemark_t *pm = &placemarks[i];
        char placemark_buf[512];

        char ts_buf[32];
        data_logger_format_timestamp(pm->timestamp_utc_ms, ts_buf, sizeof(ts_buf));

        int pm_len = snprintf(placemark_buf, sizeof(placemark_buf),
            "  <Placemark>\n"
            "    <name>%s</name>\n"
            "    <styleUrl>#%s</styleUrl>\n"
            "    <TimeStamp><when>%s</when></TimeStamp>\n"
            "    <Point>\n"
            "      <coordinates>%.7f,%.7f,%.1f</coordinates>\n"
            "    </Point>\n"
            "  </Placemark>\n",
            pm->name,
            pm->is_pilot ? "pilot" : "aircraft",
            ts_buf,
            pm->lon, pm->lat, (double)pm->alt_m);

        if (pm_len > 0 && (size_t)pm_len < sizeof(placemark_buf)) {
            hal_sd_write(&kml_file, placemark_buf, (size_t)pm_len, NULL);
        }
    }

    /* KML footer */
    const char *kml_footer =
        "</Document>\n"
        "</kml>\n";
    hal_sd_write(&kml_file, kml_footer, strlen(kml_footer), NULL);

    hal_sd_close(&kml_file);
    return ESP_OK;
}

size_t data_logger_get_buffer_count(void)
{
    return s_buffer.count;
}

size_t data_logger_get_current_file_size(void)
{
    return s_current_file_size;
}

esp_err_t data_logger_deinit(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Try to flush remaining buffer */
    if (hal_sd_is_mounted() && s_buffer.count > 0) {
        data_logger_flush_buffer();
    }

    /* Close current file */
    if (s_file_open) {
        hal_sd_close(&s_current_file);
        s_file_open = false;
    }

    s_initialized = false;
    return ESP_OK;
}

/* ========================================================================
 * Serialization Helpers
 * ======================================================================== */

int data_logger_record_to_csv(const log_record_t *record, char *buf, size_t buf_len)
{
    if (record == NULL || buf == NULL || buf_len == 0) {
        return -1;
    }

    /* Format timestamp */
    char ts_buf[32];
    if (data_logger_format_timestamp(record->timestamp_utc_ms, ts_buf, sizeof(ts_buf)) < 0) {
        return -1;
    }

    /* Protocol and event strings */
    const char *protocol_str = data_logger_protocol_to_str(record->protocol);
    const char *event_str = data_logger_event_to_str(record->event_type);

    /* Build optional telemetry fields */
    char lat_str[20] = "";
    char lon_str[20] = "";
    char alt_str[16] = "";
    char speed_str[16] = "";
    char batt_str[16] = "";

    if (record->has_position) {
        snprintf(lat_str, sizeof(lat_str), "%.7f", record->lat);
        snprintf(lon_str, sizeof(lon_str), "%.7f", record->lon);
    }
    if (record->has_altitude) {
        snprintf(alt_str, sizeof(alt_str), "%.1f", (double)record->alt_m);
    }
    if (record->has_speed) {
        snprintf(speed_str, sizeof(speed_str), "%.1f", (double)record->speed_ms);
    }
    if (record->has_battery) {
        snprintf(batt_str, sizeof(batt_str), "%.1f", (double)record->battery_pct);
    }

    /* Compose the full CSV line */
    int written = snprintf(buf, buf_len,
        "%s,%.7f,%.7f,%.1f,%s,%s,%d,%s,%s,%s,%s,%s,%s",
        ts_buf,
        record->monitor_lat,
        record->monitor_lon,
        (double)record->monitor_alt,
        record->aircraft_id,
        protocol_str,
        (int)record->rssi_dbm,
        lat_str,
        lon_str,
        alt_str,
        speed_str,
        batt_str,
        event_str);

    if (written < 0 || (size_t)written >= buf_len) {
        return -1;
    }

    return written;
}

esp_err_t data_logger_csv_to_record(const char *csv_line, log_record_t *record)
{
    if (csv_line == NULL || record == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(record, 0, sizeof(log_record_t));

    /* Parse the CSV fields — we expect 13 comma-separated fields */
    /* Format: timestamp,mon_lat,mon_lon,mon_alt,aircraft_id,protocol,rssi,lat,lon,alt,speed,batt,event */

    /* Working copy for parsing */
    char line[DATA_LOGGER_MAX_LINE_LEN];
    size_t line_len = strlen(csv_line);
    if (line_len >= sizeof(line)) {
        return ESP_FAIL;
    }
    strncpy(line, csv_line, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';

    /* Strip trailing newline/CR */
    while (line_len > 0 && (line[line_len - 1] == '\n' || line[line_len - 1] == '\r')) {
        line[--line_len] = '\0';
    }

    /* Tokenize by comma — need exactly 13 fields */
    char *fields[13];
    int field_count = 0;
    char *ptr = line;

    for (int i = 0; i < 13; i++) {
        fields[i] = ptr;
        /* Find next comma or end of string */
        char *comma = strchr(ptr, ',');
        if (comma != NULL) {
            *comma = '\0';
            ptr = comma + 1;
        } else if (i < 12) {
            /* Not enough fields */
            return ESP_FAIL;
        }
        field_count++;
    }

    if (field_count != 13) {
        return ESP_FAIL;
    }

    /* Field 0: timestamp (ISO 8601) — parse back to ms */
    {
        struct tm tm_val;
        memset(&tm_val, 0, sizeof(tm_val));
        int ms = 0;

        /* Parse "YYYY-MM-DDTHH:MM:SS.mmmZ" */
        int parsed = sscanf(fields[0], "%d-%d-%dT%d:%d:%d.%dZ",
                            &tm_val.tm_year, &tm_val.tm_mon, &tm_val.tm_mday,
                            &tm_val.tm_hour, &tm_val.tm_min, &tm_val.tm_sec, &ms);
        if (parsed < 6) {
            return ESP_FAIL;
        }
        tm_val.tm_year -= 1900;
        tm_val.tm_mon -= 1;
        tm_val.tm_isdst = -1;

        /* mktime interprets as local time; on ESP-IDF timezone is UTC by default
         * so mktime gives the correct epoch for UTC-parsed timestamps. */
        time_t epoch = mktime(&tm_val);
        record->timestamp_utc_ms = (uint64_t)epoch * 1000 + (uint64_t)ms;
    }

    /* Field 1: monitor_lat */
    record->monitor_lat = strtod(fields[1], NULL);

    /* Field 2: monitor_lon */
    record->monitor_lon = strtod(fields[2], NULL);

    /* Field 3: monitor_alt */
    record->monitor_alt = (float)strtod(fields[3], NULL);

    /* Field 4: aircraft_id */
    strncpy(record->aircraft_id, fields[4], AIRCRAFT_ID_MAX_LEN - 1);
    record->aircraft_id[AIRCRAFT_ID_MAX_LEN - 1] = '\0';

    /* Field 5: protocol */
    if (data_logger_str_to_protocol(fields[5], &record->protocol) != ESP_OK) {
        record->protocol = PROTOCOL_UNKNOWN;
    }

    /* Field 6: rssi_dbm */
    record->rssi_dbm = (int16_t)atoi(fields[6]);

    /* Field 7: lat (optional) */
    if (fields[7][0] != '\0') {
        record->lat = strtod(fields[7], NULL);
        record->has_position = true;
    }

    /* Field 8: lon (optional) */
    if (fields[8][0] != '\0') {
        record->lon = strtod(fields[8], NULL);
        /* has_position set by lat field already */
    }

    /* Field 9: alt_m (optional) */
    if (fields[9][0] != '\0') {
        record->alt_m = (float)strtod(fields[9], NULL);
        record->has_altitude = true;
    }

    /* Field 10: speed_ms (optional) */
    if (fields[10][0] != '\0') {
        record->speed_ms = (float)strtod(fields[10], NULL);
        record->has_speed = true;
    }

    /* Field 11: battery_pct (optional) */
    if (fields[11][0] != '\0') {
        record->battery_pct = (float)strtod(fields[11], NULL);
        record->has_battery = true;
    }

    /* Field 12: event_type */
    if (data_logger_str_to_event(fields[12], &record->event_type) != ESP_OK) {
        record->event_type = LOG_EVENT_TELEMETRY;
    }

    return ESP_OK;
}

int data_logger_format_timestamp(uint64_t timestamp_ms, char *buf, size_t buf_len)
{
    if (buf == NULL || buf_len < 25) {
        return -1;
    }

    time_t seconds = (time_t)(timestamp_ms / 1000);
    uint32_t millis = (uint32_t)(timestamp_ms % 1000);

    struct tm tm_val;
#ifdef _WIN32
    gmtime_s(&tm_val, &seconds);
#else
    gmtime_r(&seconds, &tm_val);
#endif

    int written = snprintf(buf, buf_len,
        "%04d-%02d-%02dT%02d:%02d:%02d.%03uZ",
        tm_val.tm_year + 1900,
        tm_val.tm_mon + 1,
        tm_val.tm_mday,
        tm_val.tm_hour,
        tm_val.tm_min,
        tm_val.tm_sec,
        (unsigned)millis);

    if (written < 0 || (size_t)written >= buf_len) {
        return -1;
    }

    return written;
}

const char *data_logger_protocol_to_str(protocol_type_t protocol)
{
    if ((size_t)protocol < PROTOCOL_COUNT) {
        return s_protocol_names[protocol];
    }
    return "UNKNOWN";
}

esp_err_t data_logger_str_to_protocol(const char *str, protocol_type_t *protocol)
{
    if (str == NULL || protocol == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < PROTOCOL_COUNT; i++) {
        if (strcmp(str, s_protocol_names[i]) == 0) {
            *protocol = (protocol_type_t)i;
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

const char *data_logger_event_to_str(log_event_type_t event)
{
    if ((size_t)event < EVENT_COUNT) {
        return s_event_names[event];
    }
    return "TELEMETRY";
}

esp_err_t data_logger_str_to_event(const char *str, log_event_type_t *event)
{
    if (str == NULL || event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < EVENT_COUNT; i++) {
        if (strcmp(str, s_event_names[i]) == 0) {
            *event = (log_event_type_t)i;
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}
