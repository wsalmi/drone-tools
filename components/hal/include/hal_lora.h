/**
 * @file hal_lora.h
 * @brief HAL interface for LoRa SX1262 module (M5Stack Cap LoRa).
 *
 * Provides high-level operations for the Semtech SX1262 LoRa transceiver
 * connected via SPI3 (VSPI) bus. Supports passive monitoring of LoRa signals
 * in the 862–928 MHz frequency range.
 *
 * The module implements:
 * - Initialization with retry logic (3 attempts, 2s interval)
 * - Frequency configuration within the 862–928 MHz range
 * - Continuous receive mode for passive monitoring
 * - Packet reception with RSSI/SNR metadata
 * - Hot-swap support (can be deactivated/reactivated at runtime)
 *
 * @note This module shares SPI3 bus with NRF24 and SD Card.
 *       Mutual exclusion with NRF24 is managed by the Hardware Manager.
 */

#ifndef HAL_LORA_H
#define HAL_LORA_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "hal_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Configuration Constants
 * ======================================================================== */

/** @brief Minimum supported frequency in Hz (862 MHz) */
#define HAL_LORA_FREQ_MIN_HZ        862000000U

/** @brief Maximum supported frequency in Hz (928 MHz) */
#define HAL_LORA_FREQ_MAX_HZ        928000000U

/** @brief Maximum payload size in bytes */
#define HAL_LORA_MAX_PAYLOAD_LEN     255

/** @brief Number of retry attempts during initialization */
#define HAL_LORA_INIT_RETRY_COUNT    3

/** @brief Interval between retry attempts in milliseconds */
#define HAL_LORA_INIT_RETRY_INTERVAL_MS  2000

/* ========================================================================
 * Data Types
 * ======================================================================== */

/**
 * @brief LoRa module configuration parameters.
 *
 * Used to configure the SX1262 for reception in a specific LoRa mode.
 * The tx_power_dbm field is included for completeness but is not used
 * in this passive monitoring application.
 */
typedef struct {
    uint32_t frequency_hz;      /**< Center frequency (862–928 MHz) */
    uint8_t spreading_factor;   /**< Spreading Factor SF6–SF12 */
    uint32_t bandwidth_hz;      /**< Bandwidth: 125000, 250000, or 500000 Hz */
    uint8_t coding_rate;        /**< Coding rate: 5 (4/5) to 8 (4/8) */
    int8_t tx_power_dbm;        /**< TX power -9 to +22 dBm (not used in RX) */
} lora_config_t;

/**
 * @brief Received LoRa packet with metadata.
 *
 * Contains the received payload along with signal quality indicators
 * and reception context (frequency, timestamp).
 */
typedef struct {
    uint8_t *payload;           /**< Pointer to payload buffer (caller-allocated) */
    uint16_t payload_len;       /**< Length of received payload in bytes */
    int16_t rssi_dbm;           /**< Received Signal Strength Indicator in dBm */
    int8_t snr_db;              /**< Signal-to-Noise Ratio in dB */
    uint32_t frequency_hz;      /**< Frequency at which packet was received */
    uint32_t timestamp_ms;      /**< Reception timestamp (esp_timer, ms since boot) */
} lora_packet_t;

/* ========================================================================
 * Public API
 * ======================================================================== */

/**
 * @brief Initialize the LoRa SX1262 module.
 *
 * Configures the SPI bus, resets the SX1262, verifies communication,
 * and applies the provided configuration. Implements retry logic:
 * up to 3 attempts with 2-second intervals between failures.
 *
 * After 3 consecutive failures, the module enters ERROR state and
 * LoRa monitoring is disabled until a manual reset or re-init.
 *
 * @param config Pointer to configuration parameters. Must not be NULL.
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if config is NULL or parameters out of range
 * @return ERR_HAL_LORA_TIMEOUT if module did not respond after all retries
 * @return ERR_HAL_LORA_SPI_FAIL if SPI communication failed after all retries
 */
esp_err_t hal_lora_init(const lora_config_t *config);

/**
 * @brief Set the center frequency for reception.
 *
 * Changes the SX1262 operating frequency. Must be called after init.
 * Frequency must be within the 862–928 MHz range.
 *
 * @param freq_hz Frequency in Hz (862000000–928000000)
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if frequency is out of range
 * @return ESP_ERR_INVALID_STATE if module is not initialized
 * @return ERR_HAL_LORA_SPI_FAIL on SPI communication error
 */
esp_err_t hal_lora_set_frequency(uint32_t freq_hz);

/**
 * @brief Start continuous receive mode.
 *
 * Places the SX1262 in continuous RX mode to passively monitor for
 * LoRa packets at the currently configured frequency and modulation.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if module is not initialized
 * @return ERR_HAL_LORA_SPI_FAIL on SPI communication error
 */
esp_err_t hal_lora_start_rx(void);

/**
 * @brief Receive a LoRa packet with timeout.
 *
 * Waits for a packet to be received, up to the specified timeout.
 * The caller must provide a valid lora_packet_t with an allocated
 * payload buffer of at least HAL_LORA_MAX_PAYLOAD_LEN bytes.
 *
 * @param packet Pointer to packet structure (payload buffer must be pre-allocated)
 * @param timeout_ms Maximum time to wait in milliseconds (0 = non-blocking check)
 * @return ESP_OK on successful packet reception
 * @return ESP_ERR_TIMEOUT if no packet received within timeout
 * @return ESP_ERR_INVALID_ARG if packet is NULL or payload buffer is NULL
 * @return ESP_ERR_INVALID_STATE if module is not initialized or not in RX mode
 */
esp_err_t hal_lora_get_packet(lora_packet_t *packet, uint32_t timeout_ms);

/**
 * @brief Get the current operational status of the LoRa module.
 *
 * @return Current hal_status_t value (INACTIVE, ACTIVE, ERROR, INITIALIZING)
 */
hal_status_t hal_lora_get_status(void);

/**
 * @brief Deinitialize the LoRa module and release resources.
 *
 * Puts the SX1262 in sleep mode, releases the SPI device handle,
 * and resets internal state. The module can be re-initialized later
 * via hal_lora_init() for hot-swap support.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if module is not initialized
 */
esp_err_t hal_lora_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_LORA_H */
