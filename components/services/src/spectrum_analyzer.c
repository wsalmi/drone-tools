/**
 * @file spectrum_analyzer.c
 * @brief Spectrum Analyzer Service implementation.
 *
 * Provides continuous spectrum analysis via RTL-SDR HAL integration,
 * peak detection above configurable threshold, protocol frequency marker
 * overlay, and classification of unrecognized signals.
 *
 * Validates: Requirements 12.1, 12.2, 12.3, 12.4
 */

#include "spectrum_analyzer.h"
#include "hal_sdr.h"
#include "config_store.h"
#include "protocol_signatures.h"
#include "error_codes.h"

#include <string.h>
#include <math.h>
#include <stdbool.h>

/* ========================================================================
 * Internal Constants
 * ======================================================================== */

/** @brief Number of predefined frequency markers for known protocols */
#define NUM_KNOWN_MARKERS   6

/** @brief Minimum dB above noise floor for peak candidate */
#define PEAK_MIN_PROMINENCE_DB  3.0f

/* ========================================================================
 * Known Protocol Frequency Bands
 * ======================================================================== */

/**
 * @brief Predefined frequency markers for known drone protocols.
 *
 * These markers are overlaid on the spectrum display to indicate where
 * known protocols operate. Frequencies outside these bands are classified
 * as "Não Classificado" when a peak is detected.
 */
static const frequency_marker_t s_known_markers[NUM_KNOWN_MARKERS] = {
    {
        .label = "ELRS 900",
        .freq_start_hz = 862000000U,
        .freq_end_hz   = 928000000U,
        .protocol      = PROTOCOL_ELRS
    },
    {
        .label = "ELRS 2.4G",
        .freq_start_hz = 2400000000U,
        .freq_end_hz   = 2500000000U,
        .protocol      = PROTOCOL_ELRS
    },
    {
        .label = "DJI",
        .freq_start_hz = 2400000000U,
        .freq_end_hz   = 2500000000U,
        .protocol      = PROTOCOL_DJI
    },
    {
        .label = "WiFi 2.4G",
        .freq_start_hz = 2400000000U,
        .freq_end_hz   = 2500000000U,
        .protocol      = PROTOCOL_WIFI
    },
    {
        .label = "Crossfire",
        .freq_start_hz = 862000000U,
        .freq_end_hz   = 928000000U,
        .protocol      = PROTOCOL_CROSSFIRE
    },
    {
        .label = "FrSky",
        .freq_start_hz = 2400000000U,
        .freq_end_hz   = 2500000000U,
        .protocol      = PROTOCOL_FRSKY
    }
};

/* ========================================================================
 * Internal State
 * ======================================================================== */

/** @brief Service initialization flag */
static bool s_initialized = false;

/** @brief Service running flag */
static bool s_running = false;

/** @brief Current configuration */
static spectrum_config_t s_config;

/** @brief Latest spectrum data (internal buffer) */
static float s_power_buffer[SDR_FFT_SIZE];
static sdr_spectrum_t s_current_spectrum;

/** @brief Detected peaks */
static spectrum_peak_t s_peaks[SPECTRUM_MAX_PEAKS];
static uint8_t s_peak_count = 0;

/** @brief Last update timestamp */
static uint64_t s_last_update_ms = 0;

/* ========================================================================
 * Internal Helper Functions
 * ======================================================================== */

/**
 * @brief Convert gain_db to the nearest RTL-SDR gain index (0–29).
 *
 * The RTL-SDR gain table maps index 0–29 to discrete gain values
 * from 0.0 dB to 49.6 dB.
 */
static uint8_t gain_db_to_index(float gain_db)
{
    /* RTL-SDR gain values in tenths of dB */
    static const int16_t gain_table[] = {
        0, 9, 14, 27, 37, 77, 87, 125, 144, 157,
        166, 197, 207, 229, 254, 280, 297, 328, 338, 364,
        372, 386, 402, 421, 434, 439, 444, 480, 484, 496
    };
    const int num_entries = 30;
    int16_t target = (int16_t)(gain_db * 10.0f);

    /* Find closest entry */
    uint8_t best_idx = 0;
    int16_t best_diff = 32767;
    for (int i = 0; i < num_entries; i++) {
        int16_t diff = target - gain_table[i];
        if (diff < 0) diff = -diff;
        if (diff < best_diff) {
            best_diff = diff;
            best_idx = (uint8_t)i;
        }
    }
    return best_idx;
}

/**
 * @brief Estimate noise floor from spectrum data.
 *
 * Computes the median-approximation of the noise floor using the
 * lower quartile of power values (simple approach for embedded).
 */
static float estimate_noise_floor(const float *power_db, uint32_t num_bins)
{
    if (num_bins == 0 || power_db == NULL) {
        return -100.0f;
    }

    /* Simple mean of the lower 50% of values as noise floor estimate */
    float sum = 0.0f;
    float min_val = power_db[0];
    for (uint32_t i = 0; i < num_bins; i++) {
        sum += power_db[i];
        if (power_db[i] < min_val) {
            min_val = power_db[i];
        }
    }
    float mean = sum / (float)num_bins;

    /* Use values below the mean to estimate noise floor */
    float noise_sum = 0.0f;
    uint32_t noise_count = 0;
    for (uint32_t i = 0; i < num_bins; i++) {
        if (power_db[i] <= mean) {
            noise_sum += power_db[i];
            noise_count++;
        }
    }

    if (noise_count == 0) {
        return mean;
    }
    return noise_sum / (float)noise_count;
}

/**
 * @brief Detect peaks in the spectrum above the configured threshold.
 *
 * A peak is defined as a local maximum (value at bin i is greater than
 * bins i-1 and i+1) that exceeds the detection threshold in dBm.
 * Peaks are also required to be at least PEAK_MIN_PROMINENCE_DB above
 * the estimated noise floor.
 */
static void detect_peaks(const sdr_spectrum_t *spectrum, int32_t threshold_dbm,
                         spectrum_peak_t *peaks, uint8_t max_peaks, uint8_t *count)
{
    *count = 0;

    if (spectrum == NULL || spectrum->power_db == NULL || spectrum->num_bins < 3) {
        return;
    }

    float noise_floor = estimate_noise_floor(spectrum->power_db, spectrum->num_bins);
    float threshold_f = (float)threshold_dbm;

    for (uint32_t i = 1; i < spectrum->num_bins - 1 && *count < max_peaks; i++) {
        float val = spectrum->power_db[i];

        /* Check if this is a local maximum */
        if (val > spectrum->power_db[i - 1] && val > spectrum->power_db[i + 1]) {
            /* Check against absolute threshold and noise floor prominence */
            if (val >= threshold_f && val >= (noise_floor + PEAK_MIN_PROMINENCE_DB)) {
                uint32_t peak_freq_hz = spectrum->freq_start_hz + (i * spectrum->freq_step_hz);

                peaks[*count].frequency_hz = peak_freq_hz;
                peaks[*count].power_dbm = val;
                peaks[*count].timestamp_utc_ms = 0; /* Set by caller if needed */

                /* Estimate bandwidth: count bins above threshold around the peak */
                uint32_t bw_bins = 1;
                uint32_t left = i;
                while (left > 0 && spectrum->power_db[left - 1] >= threshold_f) {
                    left--;
                    bw_bins++;
                }
                uint32_t right = i;
                while (right < spectrum->num_bins - 1 && spectrum->power_db[right + 1] >= threshold_f) {
                    right++;
                    bw_bins++;
                }
                peaks[*count].bandwidth_hz = (float)(bw_bins * spectrum->freq_step_hz);

                /* Classify the peak frequency */
                peaks[*count].classification = spectrum_analyzer_classify_frequency(peak_freq_hz);

                (*count)++;
            }
        }
    }
}

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

esp_err_t spectrum_analyzer_validate_config(const spectrum_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (config->center_freq_mhz < SPECTRUM_FREQ_MIN_MHZ ||
        config->center_freq_mhz > SPECTRUM_FREQ_MAX_MHZ) {
        return ESP_ERR_INVALID_ARG;
    }

    if (config->bandwidth_khz < SPECTRUM_BW_MIN_KHZ ||
        config->bandwidth_khz > SPECTRUM_BW_MAX_KHZ) {
        return ESP_ERR_INVALID_ARG;
    }

    if (config->gain_db < SPECTRUM_GAIN_MIN_DB ||
        config->gain_db > SPECTRUM_GAIN_MAX_DB) {
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

esp_err_t spectrum_analyzer_init(const config_spectrum_t *spectrum_cfg)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Load configuration from config_store or use defaults */
    if (spectrum_cfg != NULL) {
        s_config.center_freq_mhz = spectrum_cfg->default_center_freq_mhz;
        s_config.bandwidth_khz = spectrum_cfg->default_bandwidth_khz;
        s_config.gain_db = spectrum_cfg->default_gain_db;
        s_config.detection_threshold_dbm = spectrum_cfg->detection_threshold_dbm;
    } else {
        /* Hardcoded defaults matching config.json design */
        s_config.center_freq_mhz = 915;
        s_config.bandwidth_khz = 500;
        s_config.gain_db = 20.0f;
        s_config.detection_threshold_dbm = SPECTRUM_DEFAULT_THRESHOLD_DBM;
    }

    /* Validate configuration */
    esp_err_t err = spectrum_analyzer_validate_config(&s_config);
    if (err != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Check SDR HAL availability */
    hal_status_t sdr_status = hal_sdr_get_status();
    if (sdr_status == HAL_STATUS_INACTIVE) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Configure SDR with computed parameters */
    sdr_config_t sdr_cfg = {
        .center_freq_hz = s_config.center_freq_mhz * 1000000U,
        .sample_rate_hz = s_config.bandwidth_khz * 1000U,
        .gain_index = gain_db_to_index(s_config.gain_db),
        .agc_enabled = false
    };

    /* Clamp sample rate to valid range */
    if (sdr_cfg.sample_rate_hz < SDR_SAMPLE_RATE_MIN_HZ) {
        sdr_cfg.sample_rate_hz = SDR_SAMPLE_RATE_MIN_HZ;
    }
    if (sdr_cfg.sample_rate_hz > SDR_SAMPLE_RATE_MAX_HZ) {
        sdr_cfg.sample_rate_hz = SDR_SAMPLE_RATE_MAX_HZ;
    }

    err = hal_sdr_init(&sdr_cfg);
    if (err != ESP_OK) {
        return ESP_FAIL;
    }

    /* Initialize internal state */
    memset(s_power_buffer, 0, sizeof(s_power_buffer));
    memset(&s_current_spectrum, 0, sizeof(s_current_spectrum));
    s_current_spectrum.power_db = s_power_buffer;
    s_current_spectrum.num_bins = SDR_FFT_SIZE;
    s_current_spectrum.freq_start_hz = sdr_cfg.center_freq_hz - (sdr_cfg.sample_rate_hz / 2);
    s_current_spectrum.freq_step_hz = sdr_cfg.sample_rate_hz / SDR_FFT_SIZE;

    memset(s_peaks, 0, sizeof(s_peaks));
    s_peak_count = 0;
    s_last_update_ms = 0;
    s_running = false;
    s_initialized = true;

    return ESP_OK;
}

esp_err_t spectrum_analyzer_start(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_running) {
        return ESP_ERR_INVALID_STATE;
    }

    s_running = true;
    return ESP_OK;
}

esp_err_t spectrum_analyzer_stop(void)
{
    if (!s_initialized || !s_running) {
        return ESP_ERR_INVALID_STATE;
    }

    s_running = false;
    return ESP_OK;
}

esp_err_t spectrum_analyzer_deinit(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_running) {
        spectrum_analyzer_stop();
    }

    hal_sdr_deinit();

    s_initialized = false;
    s_running = false;
    s_peak_count = 0;
    s_last_update_ms = 0;

    return ESP_OK;
}

esp_err_t spectrum_analyzer_set_config(const spectrum_config_t *config)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = spectrum_analyzer_validate_config(config);
    if (err != ESP_OK) {
        return err;
    }

    /* Apply new configuration to SDR */
    uint32_t new_center_hz = config->center_freq_mhz * 1000000U;
    uint32_t new_sample_rate = config->bandwidth_khz * 1000U;

    /* Clamp sample rate */
    if (new_sample_rate < SDR_SAMPLE_RATE_MIN_HZ) {
        new_sample_rate = SDR_SAMPLE_RATE_MIN_HZ;
    }
    if (new_sample_rate > SDR_SAMPLE_RATE_MAX_HZ) {
        new_sample_rate = SDR_SAMPLE_RATE_MAX_HZ;
    }

    err = hal_sdr_set_frequency(new_center_hz);
    if (err != ESP_OK) {
        return ESP_FAIL;
    }

    err = hal_sdr_set_sample_rate(new_sample_rate);
    if (err != ESP_OK) {
        return ESP_FAIL;
    }

    /* Update stored configuration */
    s_config = *config;

    /* Update spectrum metadata */
    s_current_spectrum.freq_start_hz = new_center_hz - (new_sample_rate / 2);
    s_current_spectrum.freq_step_hz = new_sample_rate / SDR_FFT_SIZE;

    return ESP_OK;
}

esp_err_t spectrum_analyzer_get_config(spectrum_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    *config = s_config;
    return ESP_OK;
}

esp_err_t spectrum_analyzer_get_spectrum(sdr_spectrum_t *spectrum)
{
    if (spectrum == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_last_update_ms == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Copy spectrum metadata */
    spectrum->num_bins = s_current_spectrum.num_bins;
    spectrum->freq_start_hz = s_current_spectrum.freq_start_hz;
    spectrum->freq_step_hz = s_current_spectrum.freq_step_hz;

    /* Note: caller must provide power_db buffer or handle the pointer */
    if (spectrum->power_db != NULL && s_current_spectrum.power_db != NULL) {
        memcpy(spectrum->power_db, s_current_spectrum.power_db,
               s_current_spectrum.num_bins * sizeof(float));
    } else {
        spectrum->power_db = s_current_spectrum.power_db;
    }

    return ESP_OK;
}

esp_err_t spectrum_analyzer_get_peaks(spectrum_peak_t *peaks, uint8_t max_peaks, uint8_t *count)
{
    if (peaks == NULL || count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t copy_count = s_peak_count;
    if (copy_count > max_peaks) {
        copy_count = max_peaks;
    }

    memcpy(peaks, s_peaks, copy_count * sizeof(spectrum_peak_t));
    *count = copy_count;

    return ESP_OK;
}

esp_err_t spectrum_analyzer_get_markers(frequency_marker_t *markers, uint8_t max_markers, uint8_t *count)
{
    if (markers == NULL || count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t copy_count = NUM_KNOWN_MARKERS;
    if (copy_count > max_markers) {
        copy_count = max_markers;
    }

    memcpy(markers, s_known_markers, copy_count * sizeof(frequency_marker_t));
    *count = copy_count;

    return ESP_OK;
}

peak_classification_t spectrum_analyzer_classify_frequency(uint32_t frequency_hz)
{
    /* ELRS 900 MHz band: 862–928 MHz */
    if (frequency_hz >= 862000000U && frequency_hz <= 928000000U) {
        return PEAK_CLASS_ELRS_900;
    }

    /* ELRS 2.4 GHz band: 2400–2500 MHz */
    if (frequency_hz >= 2400000000U && frequency_hz <= 2500000000U) {
        return PEAK_CLASS_ELRS_2400;
    }

    /* DJI band overlaps with 2.4 GHz — but differentiation requires header analysis.
     * For frequency-only classification, 2.4 GHz signals that aren't otherwise
     * identified default to the generic 2.4 GHz class. The classifier_classify()
     * in protocol_classifier.h does the full header+frequency analysis.
     *
     * For the spectrum analyzer's frequency-only classification:
     * - 862–928 MHz → ELRS 900 (also Crossfire, but ELRS is dominant)
     * - 2400–2500 MHz → ELRS 2.4G (catch-all for 2.4 GHz drone protocols)
     * - Everything else → UNCLASSIFIED
     *
     * The more specific classifications (DJI, WiFi, FrSky, Crossfire) require
     * packet header analysis via the protocol_classifier service.
     */

    /* All other frequencies are outside known drone bands */
    return PEAK_CLASS_UNCLASSIFIED;
}

esp_err_t spectrum_analyzer_get_state(spectrum_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    state->spectrum = s_current_spectrum;
    memcpy(state->peaks, s_peaks, sizeof(s_peaks));
    state->peak_count = s_peak_count;
    state->config = s_config;
    state->running = s_running;
    state->last_update_ms = s_last_update_ms;

    return ESP_OK;
}

esp_err_t spectrum_analyzer_process_sweep(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_running) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Step 1: Read I/Q samples from SDR */
    int8_t iq_buffer[SDR_FFT_SIZE * 2];
    sdr_iq_buffer_t iq = {
        .iq_samples = iq_buffer,
        .num_samples = SDR_FFT_SIZE,
        .center_freq_hz = s_config.center_freq_mhz * 1000000U,
        .timestamp_ms = 0
    };

    esp_err_t err = hal_sdr_read_iq(&iq, 100);
    if (err != ESP_OK) {
        return err;
    }

    /* Step 2: Compute spectrum via FFT */
    sdr_spectrum_t computed_spectrum = {
        .power_db = s_power_buffer,
        .num_bins = SDR_FFT_SIZE,
        .freq_start_hz = 0,
        .freq_step_hz = 0
    };

    err = hal_sdr_compute_spectrum(&iq, &computed_spectrum);
    if (err != ESP_OK) {
        return err;
    }

    /* Update internal spectrum state */
    s_current_spectrum.num_bins = computed_spectrum.num_bins;
    s_current_spectrum.freq_start_hz = computed_spectrum.freq_start_hz;
    s_current_spectrum.freq_step_hz = computed_spectrum.freq_step_hz;

    /* Step 3: Detect peaks above threshold */
    detect_peaks(&s_current_spectrum, s_config.detection_threshold_dbm,
                 s_peaks, SPECTRUM_MAX_PEAKS, &s_peak_count);

    /* Update timestamp */
    s_last_update_ms = iq.timestamp_ms;

    return ESP_OK;
}
