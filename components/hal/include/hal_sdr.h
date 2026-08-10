/**
 * @file hal_sdr.h
 * @brief Hardware Abstraction Layer for RTL-SDR V3c module.
 *
 * Provides an interface for configuring and reading I/Q samples from the
 * RTL-SDR V3c receiver connected via USB Host OTG. Supports frequency tuning
 * (24 MHz – 1766 MHz), sample rate configuration (up to 2.4 MHz), gain control,
 * and FFT-based spectrum computation from raw I/Q data.
 *
 * The USB Host OTG interface operates independently from the SPI bus used
 * by other RF modules (LoRa, NRF24).
 */

#ifndef HAL_SDR_H
#define HAL_SDR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "hal_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Constants
 * ======================================================================== */

/** @brief Minimum supported center frequency in Hz (24 MHz) */
#define SDR_FREQ_MIN_HZ        24000000U

/** @brief Maximum supported center frequency in Hz (1766 MHz) */
#define SDR_FREQ_MAX_HZ        1766000000U

/** @brief Maximum supported sample rate in Hz (2.4 MHz) */
#define SDR_SAMPLE_RATE_MAX_HZ 2400000U

/** @brief Minimum supported sample rate in Hz (225 kHz) */
#define SDR_SAMPLE_RATE_MIN_HZ 225000U

/** @brief Maximum gain index (maps to 0.0–49.6 dB) */
#define SDR_GAIN_INDEX_MAX     29U

/** @brief Default FFT size for spectrum computation */
#define SDR_FFT_SIZE           1024U

/** @brief USB device detection timeout in milliseconds */
#define SDR_USB_DETECT_TIMEOUT_MS 5000U

/** @brief RTL-SDR USB Vendor ID */
#define SDR_USB_VID            0x0BDAU

/** @brief RTL-SDR USB Product ID (RTL2838UHIDIR) */
#define SDR_USB_PID            0x2838U

/* ========================================================================
 * Data Types
 * ======================================================================== */

/**
 * @brief SDR module configuration.
 *
 * Used during initialization and reconfiguration of the RTL-SDR receiver.
 */
typedef struct {
    uint32_t center_freq_hz;    /**< Center frequency: 24 MHz – 1766 MHz */
    uint32_t sample_rate_hz;    /**< Sample rate: 225 kHz – 2.4 MHz */
    uint8_t gain_index;         /**< Gain index: 0–29 (mapped to 0.0–49.6 dB) */
    bool agc_enabled;           /**< Enable automatic gain control */
} sdr_config_t;

/**
 * @brief I/Q sample buffer received from the SDR.
 *
 * Contains interleaved 8-bit signed I/Q samples along with metadata
 * about the capture (center frequency, timestamp).
 */
typedef struct {
    int8_t *iq_samples;         /**< Buffer of interleaved I/Q samples (8-bit signed) */
    uint32_t num_samples;       /**< Number of I/Q sample pairs in the buffer */
    uint32_t center_freq_hz;    /**< Center frequency at time of capture */
    uint32_t timestamp_ms;      /**< Capture timestamp in milliseconds (system tick) */
} sdr_iq_buffer_t;

/**
 * @brief Spectrum analysis result computed from I/Q samples via FFT.
 *
 * Contains power spectral density values in dB for each frequency bin.
 */
typedef struct {
    float *power_db;            /**< Array of power values in dB per frequency bin */
    uint32_t num_bins;          /**< Number of frequency bins (FFT size) */
    uint32_t freq_start_hz;     /**< Start frequency of the spectrum in Hz */
    uint32_t freq_step_hz;      /**< Frequency step between bins in Hz */
} sdr_spectrum_t;

/* ========================================================================
 * Public API
 * ======================================================================== */

/**
 * @brief Initialize the RTL-SDR module via USB Host OTG.
 *
 * Installs the USB Host driver, enumerates connected devices, and configures
 * the RTL-SDR with the given parameters. If no RTL-SDR device is detected
 * within 5 seconds, returns ERR_HAL_SDR_NO_DEVICE.
 *
 * @param config Pointer to SDR configuration (frequency, sample rate, gain).
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_ARG if config is NULL or parameters are out of range.
 * @return ERR_HAL_SDR_NO_DEVICE if device not found within timeout.
 * @return ERR_HAL_SDR_USB_FAIL if USB communication fails.
 */
esp_err_t hal_sdr_init(const sdr_config_t *config);

/**
 * @brief Set the center frequency of the SDR receiver.
 *
 * @param freq_hz Frequency in Hz (24 MHz – 1766 MHz).
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_ARG if frequency is out of range.
 * @return ESP_ERR_INVALID_STATE if module is not initialized.
 * @return ERR_HAL_SDR_USB_FAIL if USB command fails.
 */
esp_err_t hal_sdr_set_frequency(uint32_t freq_hz);

/**
 * @brief Set the sample rate of the SDR receiver.
 *
 * @param rate_hz Sample rate in Hz (225 kHz – 2.4 MHz).
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_ARG if rate is out of range.
 * @return ESP_ERR_INVALID_STATE if module is not initialized.
 * @return ERR_HAL_SDR_USB_FAIL if USB command fails.
 */
esp_err_t hal_sdr_set_sample_rate(uint32_t rate_hz);

/**
 * @brief Read I/Q samples from the SDR into the provided buffer.
 *
 * Performs a bulk USB transfer to read interleaved I/Q samples from the
 * RTL-SDR. The caller must allocate the iq_samples buffer and set num_samples
 * to the desired number of sample pairs to read.
 *
 * @param buffer Pointer to I/Q buffer struct. iq_samples must be pre-allocated
 *               with at least (num_samples * 2) bytes.
 * @param timeout_ms Maximum time to wait for data in milliseconds.
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_ARG if buffer or buffer->iq_samples is NULL.
 * @return ESP_ERR_INVALID_STATE if module is not initialized.
 * @return ESP_ERR_TIMEOUT if no data received within timeout.
 * @return ERR_HAL_SDR_USB_FAIL if USB transfer fails.
 */
esp_err_t hal_sdr_read_iq(sdr_iq_buffer_t *buffer, uint32_t timeout_ms);

/**
 * @brief Compute power spectrum from I/Q samples using FFT.
 *
 * Performs an FFT on the I/Q samples and computes the power spectral density
 * in dB. The spectrum output is allocated internally if power_db is NULL,
 * or uses the caller-provided buffer if power_db is pre-allocated.
 *
 * @param iq Pointer to I/Q buffer with valid samples.
 * @param spectrum Pointer to spectrum struct to receive results.
 *                 If spectrum->power_db is NULL, it will be set to use
 *                 an internal static buffer (valid until next call).
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_ARG if iq or spectrum is NULL, or iq has no samples.
 * @return ESP_ERR_INVALID_STATE if module is not initialized.
 */
esp_err_t hal_sdr_compute_spectrum(const sdr_iq_buffer_t *iq, sdr_spectrum_t *spectrum);

/**
 * @brief Get the current operational status of the SDR module.
 *
 * @return Current hal_status_t value for the SDR module.
 */
hal_status_t hal_sdr_get_status(void);

/**
 * @brief Deinitialize the SDR module and release USB resources.
 *
 * Stops any ongoing transfers, releases the USB device, and uninstalls
 * the USB Host driver.
 *
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_STATE if module was not initialized.
 */
esp_err_t hal_sdr_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_SDR_H */
