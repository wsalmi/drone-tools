/**
 * @file hal_gps_mock.c
 * @brief Mock implementation of HAL GPS ATGM336H for host tests.
 */

#include "hal_gps.h"
#include "hal_common.h"
#include <stdbool.h>
#include <string.h>

/* Mock state */
static hal_module_state_t mock_gps_state = {
    .status = HAL_STATUS_INACTIVE,
    .last_activity_ms = 0,
    .error_count = 0
};

static bool mock_gps_has_fix_val = false;
static bool mock_gps_initialized = false;

/* GPS position mock data */
static gps_position_t mock_position = {
    .latitude = 0.0,
    .longitude = 0.0,
    .altitude_m = 0.0f,
    .hdop = 99.0f,
    .satellites_used = 0,
    .timestamp_utc_ms = 0,
    .fix_valid = false
};

/* Mock control functions */
void mock_hal_gps_reset(void) {
    mock_gps_state.status = HAL_STATUS_INACTIVE;
    mock_gps_state.last_activity_ms = 0;
    mock_gps_state.error_count = 0;
    mock_gps_has_fix_val = false;
    mock_gps_initialized = false;
    memset(&mock_position, 0, sizeof(mock_position));
    mock_position.hdop = 99.0f;
}

void mock_hal_gps_set_fix(bool has_fix, double lat, double lon, float alt,
                           uint8_t sats, float hdop) {
    mock_gps_has_fix_val = has_fix;
    mock_position.latitude = lat;
    mock_position.longitude = lon;
    mock_position.altitude_m = alt;
    mock_position.satellites_used = sats;
    mock_position.hdop = hdop;
    mock_position.fix_valid = has_fix;
    if (has_fix) {
        mock_gps_state.status = HAL_STATUS_ACTIVE;
    }
}

/* HAL interface stubs */
esp_err_t hal_gps_init(uint32_t baud_rate) {
    (void)baud_rate;
    mock_gps_initialized = true;
    mock_gps_state.status = HAL_STATUS_ACTIVE;
    return ESP_OK;
}

esp_err_t hal_gps_get_position(gps_position_t *pos) {
    if (pos == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!mock_gps_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    memcpy(pos, &mock_position, sizeof(gps_position_t));
    return ESP_OK;
}

esp_err_t hal_gps_deinit(void) {
    mock_gps_initialized = false;
    mock_gps_state.status = HAL_STATUS_INACTIVE;
    return ESP_OK;
}

bool hal_gps_has_fix(void) {
    return mock_gps_has_fix_val;
}

hal_status_t hal_gps_get_status(void) {
    return mock_gps_state.status;
}
