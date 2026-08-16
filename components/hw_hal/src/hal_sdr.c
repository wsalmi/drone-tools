/**
 * @file hal_sdr.c
 * @brief RTL-SDR HAL stub implementation (bypassed on Cardputer to protect USB Serial/JTAG console).
 */

#include "hal_sdr.h"
#include "error_codes.h"
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "hal_sdr";

static struct {
    hal_module_state_t state;
    sdr_config_t config;
} s_sdr = {
    .state = {
        .status = HAL_STATUS_INACTIVE,
        .last_activity_ms = 0,
        .error_count = 0
    }
};

/** @brief Static FFT output buffer for spectrum computation */
static float s_fft_power_db[SDR_FFT_SIZE];

/* ========================================================================
 * FFT Implementation (Radix-2 Cooley-Tukey, in-place)
 * ======================================================================== */

static uint32_t fft_bit_reverse(uint32_t x, uint32_t log2n)
{
    uint32_t result = 0;
    for (uint32_t i = 0; i < log2n; i++) {
        result = (result << 1) | (x & 1);
        x >>= 1;
    }
    return result;
}

static void fft_compute(float *real, float *imag, uint32_t n)
{
    uint32_t log2n = 0;
    uint32_t temp = n;
    while (temp > 1) {
        log2n++;
        temp >>= 1;
    }

    /* Bit-reversal permutation */
    for (uint32_t i = 0; i < n; i++) {
        uint32_t j = fft_bit_reverse(i, log2n);
        if (j > i) {
            float tr = real[i];
            float ti = imag[i];
            real[i] = real[j];
            imag[i] = imag[j];
            real[j] = tr;
            imag[j] = ti;
        }
    }

    /* Butterfly stages */
    for (uint32_t s = 1; s <= log2n; s++) {
        uint32_t m = 1U << s;
        uint32_t half_m = m >> 1;
        float w_real = cosf(-2.0f * (float)M_PI / (float)m);
        float w_imag = sinf(-2.0f * (float)M_PI / (float)m);

        for (uint32_t k = 0; k < n; k += m) {
            float wn_real = 1.0f;
            float wn_imag = 0.0f;

            for (uint32_t j = 0; j < half_m; j++) {
                uint32_t idx_even = k + j;
                uint32_t idx_odd = k + j + half_m;

                /* Complex multiply: twiddle * odd element */
                float t_real = wn_real * real[idx_odd] - wn_imag * imag[idx_odd];
                float t_imag = wn_real * imag[idx_odd] + wn_imag * real[idx_odd];

                /* Butterfly */
                real[idx_odd] = real[idx_even] - t_real;
                imag[idx_odd] = imag[idx_even] - t_imag;
                real[idx_even] = real[idx_even] + t_real;
                imag[idx_even] = imag[idx_even] + t_imag;

                /* Advance twiddle factor */
                float new_wn_real = wn_real * w_real - wn_imag * w_imag;
                float new_wn_imag = wn_real * w_imag + wn_imag * w_real;
                wn_real = new_wn_real;
                wn_imag = new_wn_imag;
            }
        }
    }
}

/* ========================================================================
 * Public API
 * ======================================================================== */

esp_err_t hal_sdr_init(const sdr_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_sdr.state.status = HAL_STATUS_INACTIVE;
    ESP_LOGI(TAG, "RTL-SDR USB Host disabled (preserving USB Serial / JTAG console)");
    return ESP_ERR_NOT_FOUND;
}

esp_err_t hal_sdr_set_frequency(uint32_t freq_hz)
{
    (void)freq_hz;
    return ESP_ERR_INVALID_STATE;
}

esp_err_t hal_sdr_set_sample_rate(uint32_t rate_hz)
{
    (void)rate_hz;
    return ESP_ERR_INVALID_STATE;
}

esp_err_t hal_sdr_read_iq(sdr_iq_buffer_t *buffer, uint32_t timeout_ms)
{
    (void)buffer;
    (void)timeout_ms;
    return ESP_ERR_INVALID_STATE;
}

esp_err_t hal_sdr_compute_spectrum(const sdr_iq_buffer_t *iq,
                                   sdr_spectrum_t *spectrum)
{
    if (iq == NULL || spectrum == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (iq->iq_samples == NULL || iq->num_samples == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Determine FFT size (use min of available samples and SDR_FFT_SIZE) */
    uint32_t fft_size = SDR_FFT_SIZE;
    if (iq->num_samples < fft_size) {
        fft_size = 1;
        while (fft_size * 2 <= iq->num_samples) {
            fft_size *= 2;
        }
    }

    float real[SDR_FFT_SIZE];
    float imag[SDR_FFT_SIZE];

    /* Convert I/Q samples to float and apply Hanning window */
    for (uint32_t i = 0; i < fft_size; i++) {
        float window = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * (float)i / (float)(fft_size - 1)));
        real[i] = (float)iq->iq_samples[i * 2] * window;
        imag[i] = (float)iq->iq_samples[i * 2 + 1] * window;
    }

    /* Compute FFT */
    fft_compute(real, imag, fft_size);

    float *power_out = (spectrum->power_db != NULL) ? spectrum->power_db : s_fft_power_db;

    uint32_t half = fft_size / 2;
    for (uint32_t i = 0; i < fft_size; i++) {
        uint32_t src_idx = (i + half) % fft_size;
        float mag_sq = real[src_idx] * real[src_idx] + imag[src_idx] * imag[src_idx];
        if (mag_sq < 1e-10f) {
            mag_sq = 1e-10f;
        }
        power_out[i] = 10.0f * log10f(mag_sq / (float)(fft_size * fft_size));
    }

    if (spectrum->power_db == NULL) {
        spectrum->power_db = s_fft_power_db;
    }
    spectrum->num_bins = fft_size;
    spectrum->freq_step_hz = (s_sdr.config.sample_rate_hz > 0) ? (s_sdr.config.sample_rate_hz / fft_size) : 1000;
    spectrum->freq_start_hz = iq->center_freq_hz - (s_sdr.config.sample_rate_hz / 2);

    return ESP_OK;
}

hal_status_t hal_sdr_get_status(void)
{
    return s_sdr.state.status;
}

esp_err_t hal_sdr_deinit(void)
{
    s_sdr.state.status = HAL_STATUS_INACTIVE;
    s_sdr.state.last_activity_ms = 0;
    return ESP_OK;
}
