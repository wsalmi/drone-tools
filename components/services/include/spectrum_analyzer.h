/**
 * @file spectrum_analyzer.h
 * @brief Spectrum Analyzer Service — continuous spectrum analysis with peak detection.
 *
 * Integrates with the HAL SDR module to provide continuous spectrum readings
 * for the UI waterfall display, peak detection above a configurable threshold,
 * protocol frequency marker overlay, and classification of signals outside
 * known drone frequencies as "Não Classificado".
 *
 * Architecture:
 *   - spectrum_analyzer_init() configures SDR parameters from config_store
 *   - spectrum_analyzer_start() begins continuous acquisition loop
 *   - spectrum_analyzer_stop() halts acquisition
 *   - Consumers call spectrum_analyzer_get_spectrum() for latest spectrum data
 *   - Consumers call spectrum_analyzer_get_peaks() for detected signal peaks
 *   - Frequency markers are accessible via spectrum_analyzer_get_markers()
 *
 * Validates: Requirements 12.1, 12.2, 12.3, 12.4
 */

#ifndef SPECTRUM_ANALYZER_H
#define SPECTRUM_ANALYZER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "hal_sdr.h"
#include "config_store.h"
#include "protocol_signatures.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Constants
 * ======================================================================== */

/** @brief Maximum number of detected peaks per spectrum sweep */
#define SPECTRUM_MAX_PEAKS              32

/** @brief Maximum number of frequency markers for known protocols */
#define SPECTRUM_MAX_MARKERS            16

/** @brief Default detection threshold in dBm */
#define SPECTRUM_DEFAULT_THRESHOLD_DBM  (-60)

/** @brief Minimum valid center frequency in MHz */
#define SPECTRUM_FREQ_MIN_MHZ           24

/** @brief Maximum valid center frequency in MHz */
#define SPECTRUM_FREQ_MAX_MHZ           1766

/** @brief Minimum valid bandwidth in kHz */
#define SPECTRUM_BW_MIN_KHZ             10

/** @brief Maximum valid bandwidth in kHz */
#define SPECTRUM_BW_MAX_KHZ             1000

/** @brief Minimum valid gain in dB (x10 for fixed-point) */
#define SPECTRUM_GAIN_MIN_DB            0.0f

/** @brief Maximum valid gain in dB */
#define SPECTRUM_GAIN_MAX_DB            49.6f

/** @brief Label for unclassified signals */
#define SPECTRUM_LABEL_UNCLASSIFIED     "Não Classificado"

/* ========================================================================
 * Data Types
 * ======================================================================== */

/**
 * @brief Spectrum analyzer configuration.
 *
 * Configurable parameters for the spectrum analyzer service.
 * Validated against hardware limits on init/set.
 */
typedef struct {
    uint32_t center_freq_mhz;       /**< Center frequency in MHz [24, 1766] */
    uint32_t bandwidth_khz;         /**< Resolution bandwidth in kHz [10, 1000] */
    float gain_db;                  /**< Gain in dB [0.0, 49.6] */
    int32_t detection_threshold_dbm; /**< Peak detection threshold in dBm */
} spectrum_config_t;

/**
 * @brief Classification of a detected spectrum peak.
 */
typedef enum {
    PEAK_CLASS_ELRS_900 = 0,    /**< ExpressLRS 900 MHz band */
    PEAK_CLASS_ELRS_2400,       /**< ExpressLRS 2.4 GHz band */
    PEAK_CLASS_DJI,             /**< DJI OcuSync/O3 */
    PEAK_CLASS_WIFI,            /**< WiFi 2.4 GHz */
    PEAK_CLASS_MAVLINK,         /**< MAVLink telemetry */
    PEAK_CLASS_CROSSFIRE,       /**< TBS Crossfire */
    PEAK_CLASS_FRSKY,           /**< FrSky */
    PEAK_CLASS_UNCLASSIFIED     /**< Signal outside known drone frequencies */
} peak_classification_t;

/**
 * @brief Detected signal peak in the spectrum.
 */
typedef struct {
    uint32_t frequency_hz;          /**< Center frequency of the peak in Hz */
    float power_dbm;                /**< Peak power in dBm */
    float bandwidth_hz;             /**< Estimated bandwidth of the peak in Hz */
    peak_classification_t classification; /**< Protocol classification */
    uint64_t timestamp_utc_ms;      /**< Detection timestamp */
} spectrum_peak_t;

/**
 * @brief Frequency marker for a known protocol band.
 *
 * Used for overlaying protocol indicators on the spectrum display.
 */
typedef struct {
    const char *label;              /**< Protocol label (e.g., "ELRS 900") */
    uint32_t freq_start_hz;         /**< Start frequency of the band in Hz */
    uint32_t freq_end_hz;           /**< End frequency of the band in Hz */
    protocol_type_t protocol;       /**< Associated protocol type */
} frequency_marker_t;

/**
 * @brief Current spectrum analyzer state snapshot.
 *
 * Contains the most recent spectrum data and detected peaks.
 */
typedef struct {
    sdr_spectrum_t spectrum;            /**< Latest computed spectrum data */
    spectrum_peak_t peaks[SPECTRUM_MAX_PEAKS]; /**< Detected peaks above threshold */
    uint8_t peak_count;                 /**< Number of valid peaks in array */
    spectrum_config_t config;           /**< Current active configuration */
    bool running;                       /**< True if acquisition is active */
    uint64_t last_update_ms;            /**< Timestamp of last spectrum update */
} spectrum_state_t;

/* ========================================================================
 * API Functions
 * ======================================================================== */

/**
 * @brief Validate spectrum analyzer configuration parameters.
 *
 * Checks that all configuration values are within acceptable ranges:
 *   - center_freq_mhz: [24, 1766]
 *   - bandwidth_khz:   [10, 1000]
 *   - gain_db:         [0.0, 49.6]
 *
 * @param[in] config Configuration to validate.
 * @return ESP_OK if all parameters are valid.
 * @return ESP_ERR_INVALID_ARG if config is NULL or any parameter is out of range.
 */
esp_err_t spectrum_analyzer_validate_config(const spectrum_config_t *config);

/**
 * @brief Initialize the Spectrum Analyzer Service.
 *
 * Loads configuration from the provided config_store spectrum section,
 * validates parameters, initializes the SDR HAL with computed settings,
 * and populates frequency markers for known protocols.
 *
 * Must be called after HAL SDR is available (hal_sdr_get_status() != INACTIVE).
 *
 * @param[in] spectrum_cfg Spectrum configuration from config store.
 *                         If NULL, uses hardcoded defaults.
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_ARG if configuration parameters are out of range.
 * @return ESP_ERR_INVALID_STATE if SDR HAL is not available.
 * @return ESP_FAIL on SDR initialization failure.
 */
esp_err_t spectrum_analyzer_init(const config_spectrum_t *spectrum_cfg);

/**
 * @brief Start continuous spectrum acquisition.
 *
 * Begins the acquisition loop: read I/Q → compute FFT → detect peaks → classify.
 * The latest spectrum data and peaks are available via get functions.
 *
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_STATE if not initialized or already running.
 */
esp_err_t spectrum_analyzer_start(void);

/**
 * @brief Stop spectrum acquisition.
 *
 * Halts the continuous acquisition loop. The last spectrum data remains
 * accessible until deinit.
 *
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_STATE if not running.
 */
esp_err_t spectrum_analyzer_stop(void);

/**
 * @brief Deinitialize the Spectrum Analyzer Service.
 *
 * Stops acquisition if running and releases all resources.
 *
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_STATE if not initialized.
 */
esp_err_t spectrum_analyzer_deinit(void);

/**
 * @brief Update spectrum configuration at runtime.
 *
 * Validates the new configuration and applies it to the SDR.
 * Acquisition is briefly paused during reconfiguration.
 *
 * @param[in] config New spectrum configuration.
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_ARG if config is NULL or parameters out of range.
 * @return ESP_ERR_INVALID_STATE if service is not initialized.
 */
esp_err_t spectrum_analyzer_set_config(const spectrum_config_t *config);

/**
 * @brief Get the current spectrum configuration.
 *
 * @param[out] config Pointer to receive the current configuration.
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_ARG if config is NULL.
 * @return ESP_ERR_INVALID_STATE if service is not initialized.
 */
esp_err_t spectrum_analyzer_get_config(spectrum_config_t *config);

/**
 * @brief Get the latest spectrum data.
 *
 * Copies the most recent spectrum data (power vs frequency) into the
 * provided structure. Thread-safe (uses internal mutex).
 *
 * @param[out] spectrum Pointer to spectrum struct to fill.
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_ARG if spectrum is NULL.
 * @return ESP_ERR_INVALID_STATE if service is not initialized or no data available.
 */
esp_err_t spectrum_analyzer_get_spectrum(sdr_spectrum_t *spectrum);

/**
 * @brief Get detected peaks above the threshold.
 *
 * Returns peaks detected in the most recent spectrum sweep that exceed
 * the configured detection threshold.
 *
 * @param[out] peaks      Array to fill with detected peaks.
 * @param[in]  max_peaks  Maximum number of peaks to return.
 * @param[out] count      Pointer to receive actual number of peaks returned.
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_ARG if peaks or count is NULL.
 * @return ESP_ERR_INVALID_STATE if service is not initialized.
 */
esp_err_t spectrum_analyzer_get_peaks(spectrum_peak_t *peaks, uint8_t max_peaks, uint8_t *count);

/**
 * @brief Get frequency markers for known protocol bands.
 *
 * Returns the set of frequency markers used for overlay on the spectrum
 * display, indicating where known protocols operate.
 *
 * @param[out] markers     Array to fill with frequency markers.
 * @param[in]  max_markers Maximum number of markers to return.
 * @param[out] count       Pointer to receive actual number of markers returned.
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_ARG if markers or count is NULL.
 * @return ESP_ERR_INVALID_STATE if service is not initialized.
 */
esp_err_t spectrum_analyzer_get_markers(frequency_marker_t *markers, uint8_t max_markers, uint8_t *count);

/**
 * @brief Classify a frequency against known protocol bands.
 *
 * Determines if a given frequency falls within a known drone protocol band.
 * If no match is found, returns PEAK_CLASS_UNCLASSIFIED.
 *
 * @param[in] frequency_hz Frequency to classify in Hz.
 * @return Classification result.
 */
peak_classification_t spectrum_analyzer_classify_frequency(uint32_t frequency_hz);

/**
 * @brief Get the current analyzer state snapshot.
 *
 * Returns a complete snapshot of the spectrum analyzer state including
 * spectrum data, peaks, configuration, and operational status.
 *
 * @param[out] state Pointer to state struct to fill.
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_ARG if state is NULL.
 * @return ESP_ERR_INVALID_STATE if service is not initialized.
 */
esp_err_t spectrum_analyzer_get_state(spectrum_state_t *state);

/**
 * @brief Process one spectrum acquisition sweep.
 *
 * Performs a single cycle of the acquisition loop:
 *   1. Read I/Q samples from the SDR HAL
 *   2. Compute power spectrum via FFT
 *   3. Detect peaks above threshold
 *   4. Classify detected peaks
 *
 * This function is typically called by the FreeRTOS task loop but can
 * be invoked directly for testing or single-shot acquisition.
 *
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_STATE if not initialized or not running.
 * @return ESP_ERR_TIMEOUT if SDR read times out.
 * @return ESP_FAIL on SDR communication failure.
 */
esp_err_t spectrum_analyzer_process_sweep(void);

#ifdef __cplusplus
}
#endif

#endif /* SPECTRUM_ANALYZER_H */
