/**
 * @file hal_gps.c
 * @brief HAL driver for the ATGM336H GPS module via UART1.
 *
 * Implements:
 * - UART configuration and read task (FreeRTOS)
 * - NMEA sentence parsing (GGA, RMC)
 * - Fix validation (satellites >= 4 AND hdop < 5.0)
 * - Last valid position preservation on fix loss
 * - No-fix timeout reporting (60s after init)
 */

#include "hal_gps.h"
#include "error_codes.h"
#include "esp_log.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifndef CONFIG_HAL_GPS_MOCK
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#else
/* Mock mode: no UART, no FreeRTOS task management needed */
#define UART_NUM_1 1
#endif

/* ========================================================================
 * Configuration
 * ======================================================================== */

#define GPS_TAG                 "hal_gps"
#define GPS_UART_NUM            UART_NUM_1
#define GPS_UART_BUF_SIZE       1024
#define GPS_NMEA_MAX_LEN        128
#define GPS_READER_TASK_STACK   4096
#define GPS_READER_TASK_PRIO    3
#define GPS_NO_FIX_TIMEOUT_MS   60000   /* 60 seconds */
#define GPS_UPDATE_INTERVAL_MS  1000    /* 1 second */

/* Fix validity thresholds */
#define GPS_MIN_SATELLITES      4
#define GPS_MAX_HDOP            5.0f

/* ========================================================================
 * Internal state
 * ======================================================================== */

static struct {
    bool initialized;
    gps_position_t current_position;
    gps_position_t last_valid_position;
    bool has_ever_had_fix;
    hal_module_state_t module_state;
    uint32_t init_timestamp_ms;
    bool no_fix_reported;
#ifndef CONFIG_HAL_GPS_MOCK
    TaskHandle_t reader_task_handle;
    SemaphoreHandle_t data_mutex;
#endif
} gps_ctx = {
    .initialized = false,
    .has_ever_had_fix = false,
    .no_fix_reported = false,
    .module_state = {
        .status = HAL_STATUS_INACTIVE,
        .last_activity_ms = 0,
        .error_count = 0
    }
};

/* ========================================================================
 * NMEA Parser Implementation
 * ======================================================================== */

bool nmea_validate_checksum(const char *sentence)
{
    if (sentence == NULL || sentence[0] != '$') {
        return false;
    }

    const char *p = sentence + 1; /* Skip '$' */
    uint8_t checksum = 0;

    /* XOR all characters between '$' and '*' */
    while (*p != '\0' && *p != '*') {
        checksum ^= (uint8_t)*p;
        p++;
    }

    /* Must have '*' followed by two hex digits */
    if (*p != '*') {
        return false;
    }
    p++; /* Skip '*' */

    if (p[0] == '\0' || p[1] == '\0') {
        return false;
    }

    /* Parse expected checksum from hex */
    char hex_str[3] = { p[0], p[1], '\0' };
    uint8_t expected = (uint8_t)strtoul(hex_str, NULL, 16);

    return (checksum == expected);
}

/**
 * @brief Convert NMEA coordinate (DDMM.MMMM) to decimal degrees.
 *
 * @param nmea_coord NMEA coordinate string (e.g., "4807.038")
 * @param direction 'N'/'S' for latitude, 'E'/'W' for longitude
 * @param[out] degrees Output in decimal degrees
 * @return true on success
 */
static bool nmea_coord_to_degrees(const char *nmea_coord, char direction, double *degrees)
{
    if (nmea_coord == NULL || nmea_coord[0] == '\0' || degrees == NULL) {
        return false;
    }

    double raw = strtod(nmea_coord, NULL);
    if (raw == 0.0 && nmea_coord[0] != '0') {
        return false;
    }

    /* NMEA format: DDDMM.MMMM -> degrees = DDD + MM.MMMM/60 */
    int deg_part = (int)(raw / 100.0);
    double min_part = raw - (deg_part * 100.0);
    *degrees = deg_part + (min_part / 60.0);

    /* Apply hemisphere sign */
    if (direction == 'S' || direction == 'W') {
        *degrees = -(*degrees);
    }

    return true;
}

/**
 * @brief Split a comma-separated NMEA field string into tokens.
 *
 * Modifies the input buffer in-place. Handles empty fields (consecutive commas).
 *
 * @param sentence Input buffer (will be modified)
 * @param fields Output array of field pointers
 * @param max_fields Maximum number of fields to extract
 * @return Number of fields extracted
 */
static int nmea_split_fields(char *sentence, char **fields, int max_fields)
{
    int count = 0;
    char *p = sentence;

    while (count < max_fields) {
        fields[count++] = p;
        p = strchr(p, ',');
        if (p == NULL) {
            break;
        }
        *p = '\0';
        p++;
    }

    return count;
}

/**
 * @brief Parse $GPGGA sentence (Global Positioning System Fix Data).
 *
 * Fields: $GPGGA,time,lat,N/S,lon,E/W,quality,numSats,hdop,alt,M,geoidSep,M,dgpsAge,dgpsRefId*cs
 *
 * @param fields Array of NMEA field pointers
 * @param num_fields Number of fields
 * @param[in,out] pos Position to update
 * @return ESP_OK on success
 */
static esp_err_t nmea_parse_gga(char **fields, int num_fields, gps_position_t *pos)
{
    if (num_fields < 10) {
        return ESP_ERR_INVALID_SIZE;
    }

    /* Field 6: Fix quality (0=invalid, 1=GPS, 2=DGPS, etc.) */
    int fix_quality = atoi(fields[6]);
    if (fix_quality == 0) {
        /* No fix - update satellites and hdop but don't update position */
        pos->satellites_used = 0;
        pos->hdop = 99.0f;
        pos->fix_valid = false;
        return ESP_OK;
    }

    /* Field 7: Number of satellites */
    pos->satellites_used = (uint8_t)atoi(fields[7]);

    /* Field 8: HDOP */
    if (fields[8][0] != '\0') {
        pos->hdop = (float)strtod(fields[8], NULL);
    }

    /* Fields 2-5: Latitude and Longitude */
    double lat, lon;
    if (fields[2][0] != '\0' && fields[3][0] != '\0') {
        if (nmea_coord_to_degrees(fields[2], fields[3][0], &lat)) {
            pos->latitude = lat;
        }
    }
    if (fields[4][0] != '\0' && fields[5][0] != '\0') {
        if (nmea_coord_to_degrees(fields[4], fields[5][0], &lon)) {
            pos->longitude = lon;
        }
    }

    /* Field 9: Altitude above sea level */
    if (fields[9][0] != '\0') {
        pos->altitude_m = (float)strtod(fields[9], NULL);
    }

    /* Field 1: UTC Time (HHMMSS.sss) */
    if (fields[1][0] != '\0') {
        double utc_time = strtod(fields[1], NULL);
        int hours = (int)(utc_time / 10000.0);
        int minutes = (int)((utc_time - hours * 10000.0) / 100.0);
        double seconds = utc_time - hours * 10000.0 - minutes * 100.0;
        pos->timestamp_utc_ms = (uint32_t)((hours * 3600 + minutes * 60) * 1000 +
                                            (uint32_t)(seconds * 1000.0));
    }

    /* Evaluate fix validity */
    pos->fix_valid = gps_evaluate_fix(pos);

    return ESP_OK;
}

/**
 * @brief Parse $GPRMC sentence (Recommended Minimum Navigation Information).
 *
 * Fields: $GPRMC,time,status,lat,N/S,lon,E/W,speed,course,date,magVar,E/W,mode*cs
 *
 * Used primarily for date/time and as a secondary position source.
 *
 * @param fields Array of NMEA field pointers
 * @param num_fields Number of fields
 * @param[in,out] pos Position to update
 * @return ESP_OK on success
 */
static esp_err_t nmea_parse_rmc(char **fields, int num_fields, gps_position_t *pos)
{
    if (num_fields < 10) {
        return ESP_ERR_INVALID_SIZE;
    }

    /* Field 2: Status (A=valid, V=void) */
    if (fields[2][0] != 'A') {
        /* Data not valid - don't update position */
        return ESP_OK;
    }

    /* Fields 3-6: Latitude and Longitude */
    double lat, lon;
    if (fields[3][0] != '\0' && fields[4][0] != '\0') {
        if (nmea_coord_to_degrees(fields[3], fields[4][0], &lat)) {
            pos->latitude = lat;
        }
    }
    if (fields[5][0] != '\0' && fields[6][0] != '\0') {
        if (nmea_coord_to_degrees(fields[5], fields[6][0], &lon)) {
            pos->longitude = lon;
        }
    }

    /* Field 1: UTC Time (HHMMSS.sss) */
    if (fields[1][0] != '\0') {
        double utc_time = strtod(fields[1], NULL);
        int hours = (int)(utc_time / 10000.0);
        int minutes = (int)((utc_time - hours * 10000.0) / 100.0);
        double seconds = utc_time - hours * 10000.0 - minutes * 100.0;
        pos->timestamp_utc_ms = (uint32_t)((hours * 3600 + minutes * 60) * 1000 +
                                            (uint32_t)(seconds * 1000.0));
    }

    return ESP_OK;
}

esp_err_t nmea_parse_sentence(const char *sentence, gps_position_t *pos)
{
    if (sentence == NULL || pos == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Validate checksum */
    if (!nmea_validate_checksum(sentence)) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Copy sentence for parsing (nmea_split_fields modifies buffer) */
    char buf[GPS_NMEA_MAX_LEN];
    size_t len = strlen(sentence);
    if (len >= GPS_NMEA_MAX_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(buf, sentence, len + 1);

    /* Strip checksum portion (everything from '*' onward) */
    char *star = strchr(buf, '*');
    if (star != NULL) {
        *star = '\0';
    }

    /* Split into fields */
    char *fields[20];
    int num_fields = nmea_split_fields(buf, fields, 20);
    if (num_fields < 2) {
        return ESP_ERR_INVALID_SIZE;
    }

    /* Identify sentence type (field 0 is the talker+type, e.g. "$GPGGA") */
    const char *type = fields[0];

    /* Support both GP and GN talker IDs */
    if (strcmp(type, "$GPGGA") == 0 || strcmp(type, "$GNGGA") == 0) {
        return nmea_parse_gga(fields, num_fields, pos);
    } else if (strcmp(type, "$GPRMC") == 0 || strcmp(type, "$GNRMC") == 0) {
        return nmea_parse_rmc(fields, num_fields, pos);
    }

    return ESP_ERR_NOT_SUPPORTED;
}

bool gps_evaluate_fix(const gps_position_t *pos)
{
    if (pos == NULL) {
        return false;
    }
    return (pos->satellites_used >= GPS_MIN_SATELLITES) && (pos->hdop < GPS_MAX_HDOP);
}

/* ========================================================================
 * UART Reader Task (real hardware only)
 * ======================================================================== */

#ifndef CONFIG_HAL_GPS_MOCK

/**
 * @brief Background task that reads UART and parses NMEA sentences.
 */
static void gps_reader_task(void *arg)
{
    (void)arg;
    char line_buf[GPS_NMEA_MAX_LEN];
    int line_pos = 0;
    uint8_t uart_buf[128];

    while (1) {
        int bytes_read = uart_read_bytes(GPS_UART_NUM, uart_buf, sizeof(uart_buf),
                                          pdMS_TO_TICKS(GPS_UPDATE_INTERVAL_MS));

        if (bytes_read > 0) {
            for (int i = 0; i < bytes_read; i++) {
                char c = (char)uart_buf[i];

                if (c == '$') {
                    /* Start of new sentence */
                    line_pos = 0;
                    line_buf[line_pos++] = c;
                } else if (c == '\r' || c == '\n') {
                    /* End of sentence */
                    if (line_pos > 0) {
                        line_buf[line_pos] = '\0';

                        /* Parse the sentence */
                        gps_position_t temp_pos;
                        if (xSemaphoreTake(gps_ctx.data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                            memcpy(&temp_pos, &gps_ctx.current_position, sizeof(gps_position_t));

                            esp_err_t parse_result = nmea_parse_sentence(line_buf, &temp_pos);
                            if (parse_result == ESP_OK) {
                                /* Update current position */
                                memcpy(&gps_ctx.current_position, &temp_pos, sizeof(gps_position_t));
                                gps_ctx.module_state.last_activity_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

                                /* Preserve last valid position */
                                if (temp_pos.fix_valid) {
                                    memcpy(&gps_ctx.last_valid_position, &temp_pos, sizeof(gps_position_t));
                                    gps_ctx.has_ever_had_fix = true;
                                    gps_ctx.module_state.status = HAL_STATUS_ACTIVE;
                                } else if (gps_ctx.has_ever_had_fix) {
                                    /* Fix lost - preserve last valid position coordinates
                                     * but mark current as invalid */
                                    gps_ctx.current_position.latitude = gps_ctx.last_valid_position.latitude;
                                    gps_ctx.current_position.longitude = gps_ctx.last_valid_position.longitude;
                                    gps_ctx.current_position.altitude_m = gps_ctx.last_valid_position.altitude_m;
                                    gps_ctx.current_position.fix_valid = false;
                                }
                            } else if (parse_result != ESP_ERR_NOT_SUPPORTED) {
                                gps_ctx.module_state.error_count++;
                            }

                            xSemaphoreGive(gps_ctx.data_mutex);
                        }

                        line_pos = 0;
                    }
                } else if (line_pos < GPS_NMEA_MAX_LEN - 1) {
                    line_buf[line_pos++] = c;
                } else {
                    /* Buffer overflow - discard */
                    line_pos = 0;
                }
            }
        }

        /* Check no-fix timeout */
        if (!gps_ctx.has_ever_had_fix && !gps_ctx.no_fix_reported) {
            uint32_t elapsed = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - gps_ctx.init_timestamp_ms;
            if (elapsed >= GPS_NO_FIX_TIMEOUT_MS) {
                ESP_LOGW(GPS_TAG, "No GPS fix after %d ms", GPS_NO_FIX_TIMEOUT_MS);
                gps_ctx.no_fix_reported = true;
                /* Continue operating - just report the timeout */
            }
        }
    }
}

#endif /* CONFIG_HAL_GPS_MOCK */

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

esp_err_t hal_gps_init(uint32_t baud_rate)
{
    if (gps_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Initialize position to zero */
    memset(&gps_ctx.current_position, 0, sizeof(gps_position_t));
    memset(&gps_ctx.last_valid_position, 0, sizeof(gps_position_t));
    gps_ctx.current_position.hdop = 99.0f; /* No fix initially */
    gps_ctx.has_ever_had_fix = false;
    gps_ctx.no_fix_reported = false;

    gps_ctx.module_state.status = HAL_STATUS_INITIALIZING;
    gps_ctx.module_state.error_count = 0;

#ifndef CONFIG_HAL_GPS_MOCK
    /* Configure UART */
    uart_config_t uart_config = {
        .baud_rate = (int)baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(GPS_UART_NUM, GPS_UART_BUF_SIZE, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(GPS_TAG, "Failed to install UART driver: %d", err);
        gps_ctx.module_state.status = HAL_STATUS_ERROR;
        return err;
    }

    err = uart_param_config(GPS_UART_NUM, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(GPS_TAG, "Failed to configure UART: %d", err);
        uart_driver_delete(GPS_UART_NUM);
        gps_ctx.module_state.status = HAL_STATUS_ERROR;
        return err;
    }

    /* Create data mutex */
    gps_ctx.data_mutex = xSemaphoreCreateMutex();
    if (gps_ctx.data_mutex == NULL) {
        uart_driver_delete(GPS_UART_NUM);
        gps_ctx.module_state.status = HAL_STATUS_ERROR;
        return ESP_ERR_NO_MEM;
    }

    /* Record init time for no-fix timeout */
    gps_ctx.init_timestamp_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    /* Start reader task on Core 1 (APP_CPU) per design */
    BaseType_t ret = xTaskCreatePinnedToCore(
        gps_reader_task,
        "gps_reader",
        GPS_READER_TASK_STACK,
        NULL,
        GPS_READER_TASK_PRIO,
        &gps_ctx.reader_task_handle,
        1  /* Core 1 */
    );

    if (ret != pdPASS) {
        ESP_LOGE(GPS_TAG, "Failed to create GPS reader task");
        vSemaphoreDelete(gps_ctx.data_mutex);
        uart_driver_delete(GPS_UART_NUM);
        gps_ctx.module_state.status = HAL_STATUS_ERROR;
        return ESP_ERR_NO_MEM;
    }
#else
    gps_ctx.init_timestamp_ms = 0;
#endif

    gps_ctx.initialized = true;
    gps_ctx.module_state.status = HAL_STATUS_ACTIVE;

    ESP_LOGI(GPS_TAG, "GPS initialized at %lu baud", (unsigned long)baud_rate);
    return ESP_OK;
}

esp_err_t hal_gps_get_position(gps_position_t *pos)
{
    if (pos == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!gps_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

#ifndef CONFIG_HAL_GPS_MOCK
    if (xSemaphoreTake(gps_ctx.data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(pos, &gps_ctx.current_position, sizeof(gps_position_t));
        xSemaphoreGive(gps_ctx.data_mutex);
    } else {
        return ESP_ERR_TIMEOUT;
    }
#else
    memcpy(pos, &gps_ctx.current_position, sizeof(gps_position_t));
#endif

    return ESP_OK;
}

bool hal_gps_has_fix(void)
{
    if (!gps_ctx.initialized) {
        return false;
    }
    return gps_ctx.current_position.fix_valid;
}

hal_status_t hal_gps_get_status(void)
{
    return gps_ctx.module_state.status;
}

esp_err_t hal_gps_deinit(void)
{
    if (!gps_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

#ifndef CONFIG_HAL_GPS_MOCK
    /* Stop reader task */
    if (gps_ctx.reader_task_handle != NULL) {
        vTaskDelete(gps_ctx.reader_task_handle);
        gps_ctx.reader_task_handle = NULL;
    }

    /* Cleanup mutex */
    if (gps_ctx.data_mutex != NULL) {
        vSemaphoreDelete(gps_ctx.data_mutex);
        gps_ctx.data_mutex = NULL;
    }

    /* Release UART */
    uart_driver_delete(GPS_UART_NUM);
#endif

    gps_ctx.initialized = false;
    gps_ctx.module_state.status = HAL_STATUS_INACTIVE;

    ESP_LOGI(GPS_TAG, "GPS deinitialized");
    return ESP_OK;
}
