/**
 * @file hal_gps.h
 * @brief HAL interface for the ATGM336H GPS module.
 *
 * Provides position data from the ATGM336H GPS receiver via UART.
 * Parses NMEA sentences (GGA, RMC) and exposes a clean position API.
 *
 * Fix validity criteria:
 *   fix_valid = (satellites_used >= 4) AND (hdop < 5.0)
 *
 * When fix is lost, the last valid position is preserved.
 */

#ifndef HAL_GPS_H
#define HAL_GPS_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "hal_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief GPS position data structure.
 */
typedef struct {
    double latitude;            /**< Decimal degrees (-90 to +90) */
    double longitude;           /**< Decimal degrees (-180 to +180) */
    float altitude_m;           /**< Meters above sea level */
    float hdop;                 /**< Horizontal Dilution of Precision */
    uint8_t satellites_used;    /**< Number of satellites in use */
    uint32_t timestamp_utc_ms;  /**< Milliseconds since epoch */
    bool fix_valid;             /**< true if sats >= 4 AND hdop < 5.0 */
} gps_position_t;

/**
 * @brief Initialize the GPS module on UART1.
 *
 * Configures the UART peripheral and starts the background reader task
 * that continuously parses incoming NMEA sentences.
 *
 * @param baud_rate UART baud rate (default: 9600 for ATGM336H)
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already initialized
 */
esp_err_t hal_gps_init(uint32_t baud_rate);

/**
 * @brief Get the current GPS position.
 *
 * Returns the most recent valid position. If fix has been lost,
 * returns the last known valid position with fix_valid = false.
 *
 * @param[out] pos Pointer to position structure to fill
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if pos is NULL,
 *         ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t hal_gps_get_position(gps_position_t *pos);

/**
 * @brief Check if GPS currently has a valid fix.
 *
 * @return true if satellites_used >= 4 AND hdop < 5.0
 */
bool hal_gps_has_fix(void);

/**
 * @brief Get the current operational status of the GPS module.
 *
 * @return Current hal_status_t value
 */
hal_status_t hal_gps_get_status(void);

/**
 * @brief Deinitialize the GPS module and release resources.
 *
 * Stops the background reader task and releases the UART peripheral.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t hal_gps_deinit(void);

/* ========================================================================
 * NMEA Parser (internal, exposed for unit testing)
 * ======================================================================== */

/**
 * @brief Parse a single NMEA sentence and update position data.
 *
 * Supports $GPGGA and $GPRMC sentences. This function is exposed
 * for testability but is normally called internally by the reader task.
 *
 * @param sentence Null-terminated NMEA sentence string
 * @param[in,out] pos Position structure to update
 * @return ESP_OK if sentence was parsed successfully,
 *         ESP_ERR_INVALID_ARG if inputs are invalid,
 *         ESP_ERR_NOT_SUPPORTED if sentence type is not handled
 */
esp_err_t nmea_parse_sentence(const char *sentence, gps_position_t *pos);

/**
 * @brief Validate NMEA checksum.
 *
 * Computes XOR checksum between '$' and '*' and compares against
 * the two hex digits following '*'.
 *
 * @param sentence Null-terminated NMEA sentence (must start with '$')
 * @return true if checksum is valid
 */
bool nmea_validate_checksum(const char *sentence);

/**
 * @brief Evaluate whether fix is valid based on current position data.
 *
 * fix_valid = (satellites_used >= 4) AND (hdop < 5.0)
 *
 * @param pos Pointer to position data
 * @return true if fix criteria are met
 */
bool gps_evaluate_fix(const gps_position_t *pos);

#ifdef __cplusplus
}
#endif

#endif /* HAL_GPS_H */
