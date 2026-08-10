/**
 * @file hal_nrf24.h
 * @brief Hardware Abstraction Layer for the NRF24L01+ module.
 *
 * Provides an interface for:
 * - Initialization and deinitialization of the NRF24L01+ over SPI
 * - Spectrum scanning across 126 channels (2400–2525 MHz)
 * - Listening on a specific channel for incoming packets
 * - Presence detection via SPI poll
 * - Module status reporting
 *
 * The NRF24 shares SPI3 (VSPI) with LoRa and SD Card.
 * NRF24 and LoRa are mutually exclusive on this bus.
 *
 * Validates: Requirements 2.1, 2.2, 2.5, 10.5
 */

#ifndef HAL_NRF24_H
#define HAL_NRF24_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "hal_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * NRF24L01+ Register Definitions
 * ======================================================================== */

/** @brief NRF24 SPI Commands */
#define NRF24_CMD_R_REGISTER        0x00  /**< Read register (OR with register address) */
#define NRF24_CMD_W_REGISTER        0x20  /**< Write register (OR with register address) */
#define NRF24_CMD_R_RX_PAYLOAD      0x61  /**< Read RX payload */
#define NRF24_CMD_W_TX_PAYLOAD      0xA0  /**< Write TX payload */
#define NRF24_CMD_FLUSH_TX          0xE1  /**< Flush TX FIFO */
#define NRF24_CMD_FLUSH_RX          0xE2  /**< Flush RX FIFO */
#define NRF24_CMD_NOP               0xFF  /**< No operation (used to read STATUS) */

/** @brief NRF24 Register Addresses */
#define NRF24_REG_CONFIG            0x00  /**< Configuration register */
#define NRF24_REG_EN_AA             0x01  /**< Enable Auto Acknowledgment */
#define NRF24_REG_EN_RXADDR         0x02  /**< Enable RX addresses */
#define NRF24_REG_SETUP_AW          0x03  /**< Setup of Address Width */
#define NRF24_REG_SETUP_RETR        0x04  /**< Setup of Automatic Retransmission */
#define NRF24_REG_RF_CH             0x05  /**< RF Channel (0–125) */
#define NRF24_REG_RF_SETUP          0x06  /**< RF Setup Register */
#define NRF24_REG_STATUS            0x07  /**< Status Register */
#define NRF24_REG_OBSERVE_TX        0x08  /**< Transmit observe register */
#define NRF24_REG_RPD               0x09  /**< Received Power Detector (Carrier Detect) */
#define NRF24_REG_RX_ADDR_P0        0x0A  /**< RX address pipe 0 */
#define NRF24_REG_TX_ADDR           0x10  /**< TX address */
#define NRF24_REG_RX_PW_P0          0x11  /**< RX payload width pipe 0 */
#define NRF24_REG_FIFO_STATUS       0x17  /**< FIFO Status Register */
#define NRF24_REG_DYNPD             0x1C  /**< Enable dynamic payload length */
#define NRF24_REG_FEATURE           0x1D  /**< Feature register */

/** @brief NRF24 CONFIG register bits */
#define NRF24_CONFIG_MASK_RX_DR     (1 << 6)  /**< Mask RX_DR interrupt */
#define NRF24_CONFIG_MASK_TX_DS     (1 << 5)  /**< Mask TX_DS interrupt */
#define NRF24_CONFIG_MASK_MAX_RT    (1 << 4)  /**< Mask MAX_RT interrupt */
#define NRF24_CONFIG_EN_CRC         (1 << 3)  /**< Enable CRC */
#define NRF24_CONFIG_CRCO           (1 << 2)  /**< CRC encoding (0=1 byte, 1=2 bytes) */
#define NRF24_CONFIG_PWR_UP         (1 << 1)  /**< Power up */
#define NRF24_CONFIG_PRIM_RX        (1 << 0)  /**< RX/TX control (1=PRX, 0=PTX) */

/** @brief NRF24 RF_SETUP register bits */
#define NRF24_RF_SETUP_RF_DR_LOW    (1 << 5)  /**< Set RF Data Rate to 250kbps */
#define NRF24_RF_SETUP_RF_DR_HIGH   (1 << 3)  /**< Set RF Data Rate to 2Mbps */
#define NRF24_RF_SETUP_RF_PWR_MASK  0x06      /**< RF output power mask */

/** @brief NRF24 STATUS register bits */
#define NRF24_STATUS_RX_DR          (1 << 6)  /**< Data Ready RX FIFO interrupt */
#define NRF24_STATUS_TX_DS          (1 << 5)  /**< Data Sent TX FIFO interrupt */
#define NRF24_STATUS_MAX_RT         (1 << 4)  /**< Max retransmits interrupt */
#define NRF24_STATUS_RX_P_NO_MASK   0x0E      /**< Data pipe number for RX FIFO */
#define NRF24_STATUS_TX_FULL        (1 << 0)  /**< TX FIFO full flag */

/** @brief NRF24 Data Rate values for nrf24_config_t.data_rate */
#define NRF24_DATA_RATE_1MBPS       0
#define NRF24_DATA_RATE_2MBPS       1
#define NRF24_DATA_RATE_250KBPS     2

/** @brief Maximum payload size for NRF24L01+ */
#define NRF24_MAX_PAYLOAD_LEN       32

/** @brief Number of channels available for scanning */
#define NRF24_NUM_CHANNELS          126

/** @brief Timeout for init presence check (ms) */
#define NRF24_INIT_TIMEOUT_MS       3000

/** @brief Default dwell time per channel during spectrum scan (us) */
#define NRF24_DEFAULT_DWELL_TIME_US 200

/* ========================================================================
 * Data Types
 * ======================================================================== */

/**
 * @brief Configuration for the NRF24L01+ module.
 */
typedef struct {
    uint8_t channel;            /**< RF channel 0–125 (frequency = 2400 + channel MHz) */
    uint8_t data_rate;          /**< Data rate: 0=1Mbps, 1=2Mbps, 2=250kbps */
    uint8_t address_width;      /**< Address width: 3–5 bytes */
} nrf24_config_t;

/**
 * @brief Received packet from NRF24L01+.
 */
typedef struct {
    uint8_t channel;            /**< Channel the packet was received on */
    uint8_t rssi_level;         /**< 0 or 1 (from RPD/CD register, limited resolution) */
    uint8_t *payload;           /**< Pointer to payload buffer (caller-allocated) */
    uint8_t payload_len;        /**< Length of payload data received */
    uint32_t timestamp_ms;      /**< Timestamp of reception (ms since boot) */
} nrf24_packet_t;

/**
 * @brief Spectrum scan result across all 126 channels.
 */
typedef struct {
    uint8_t channel_energy[NRF24_NUM_CHANNELS]; /**< Energy level per channel (0–255) */
    uint32_t scan_duration_ms;                  /**< Total time taken for the scan */
} nrf24_spectrum_t;

/* ========================================================================
 * Public API
 * ======================================================================== */

/**
 * @brief Initialize the NRF24L01+ module.
 *
 * Configures SPI communication and sets up the module in receive mode.
 * If the module does not respond within NRF24_INIT_TIMEOUT_MS (3s),
 * returns ERR_HAL_NRF_TIMEOUT.
 *
 * @param config Pointer to configuration parameters.
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_ARG if config is NULL or parameters out of range.
 * @return ERR_HAL_NRF_TIMEOUT if module does not respond within 3 seconds.
 * @return ERR_HAL_NRF_SPI_FAIL if SPI communication fails.
 */
esp_err_t hal_nrf24_init(const nrf24_config_t *config);

/**
 * @brief Perform a full spectrum scan across 126 channels.
 *
 * Scans channels 0–125 (2400–2525 MHz, 1 MHz spacing) using the
 * RPD (Received Power Detector) register to determine channel energy.
 * Multiple samples are taken per channel for better accuracy.
 *
 * @param result Pointer to spectrum result structure (caller-allocated).
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_ARG if result is NULL.
 * @return ESP_ERR_INVALID_STATE if module is not initialized.
 * @return ERR_HAL_NRF_SPI_FAIL if SPI communication fails during scan.
 */
esp_err_t hal_nrf24_scan_spectrum(nrf24_spectrum_t *result);

/**
 * @brief Listen on a specific channel for incoming packets.
 *
 * Tunes to the specified channel and waits for a packet until timeout.
 * The caller must provide a pre-allocated payload buffer in packet->payload
 * with at least NRF24_MAX_PAYLOAD_LEN bytes.
 *
 * @param channel RF channel number (0–125).
 * @param packet Pointer to packet structure with pre-allocated payload buffer.
 * @param timeout_ms Maximum time to wait for a packet (milliseconds).
 * @return ESP_OK if a packet was received.
 * @return ESP_ERR_TIMEOUT if no packet received within timeout.
 * @return ESP_ERR_INVALID_ARG if channel > 125 or packet is NULL.
 * @return ESP_ERR_INVALID_STATE if module is not initialized.
 */
esp_err_t hal_nrf24_listen_channel(uint8_t channel, nrf24_packet_t *packet, uint32_t timeout_ms);

/**
 * @brief Check if the NRF24L01+ module is physically present.
 *
 * Performs a quick SPI poll to verify the module responds. This is used
 * for hot-swap detection (requirement 10.5: poll every 500ms).
 *
 * @return true if module responds to SPI communication.
 * @return false if module does not respond.
 */
bool hal_nrf24_is_present(void);

/**
 * @brief Get the current operational status of the NRF24 module.
 *
 * @return Current hal_status_t value (INACTIVE, ACTIVE, ERROR, INITIALIZING).
 */
hal_status_t hal_nrf24_get_status(void);

/**
 * @brief Deinitialize the NRF24L01+ module and release resources.
 *
 * Powers down the module and frees SPI bus resources, allowing
 * the LoRa module to use the shared bus.
 *
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_STATE if module is not initialized.
 */
esp_err_t hal_nrf24_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_NRF24_H */
