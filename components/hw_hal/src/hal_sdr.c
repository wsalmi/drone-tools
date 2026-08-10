/**
 * @file hal_sdr.c
 * @brief RTL-SDR V3c HAL implementation using ESP-IDF USB Host OTG driver.
 *
 * Implements the SDR hardware abstraction layer for the RTL-SDR V3c dongle
 * connected via the ESP32-S3 USB Host OTG interface. Provides frequency tuning,
 * sample rate configuration, I/Q data streaming, and FFT-based spectrum analysis.
 */

#include "hal_sdr.h"
#include "error_codes.h"

#include <string.h>
#include <math.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "usb/usb_host.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "hal_sdr";

/* ========================================================================
 * RTL-SDR Register and Command Definitions
 * ======================================================================== */

/** @brief RTL2832U control request type (vendor write) */
#define RTL_USB_REQ_TYPE_WRITE  0x40

/** @brief RTL2832U control request type (vendor read) */
#define RTL_USB_REQ_TYPE_READ   0xC0

/** @brief RTL2832U demod register block index */
#define RTL_BLOCK_DEMOD         0x0100

/** @brief RTL2832U USB register block index */
#define RTL_BLOCK_USB           0x0200

/** @brief RTL2832U system register block index */
#define RTL_BLOCK_SYS           0x0200

/** @brief RTL2832U I2C pass-through register for tuner */
#define RTL_BLOCK_I2C           0x0600

/** @brief Write register command */
#define RTL_CMD_WR_REG          0x10

/** @brief Read register command */
#define RTL_CMD_RD_REG          0x11

/** @brief Bulk transfer endpoint for I/Q data */
#define RTL_BULK_EP_ADDR        0x81

/** @brief Maximum bulk transfer size in bytes */
#define RTL_BULK_TRANSFER_SIZE  (16 * 512)

/** @brief R820T2 tuner I2C address */
#define R820T2_I2C_ADDR         0x34

/* ========================================================================
 * Gain Table (R820T2 tuner — 30 gain steps)
 * ======================================================================== */

/**
 * @brief R820T2 gain values in tenths of dB, indexed 0–29.
 * Maps gain_index to actual hardware gain values.
 */
static const int16_t r820t2_gain_table[30] = {
      0,   9,  14,  27,  37,  77,  87, 125, 144, 157,
    166, 197, 207, 229, 254, 280, 297, 328, 338, 364,
    372, 386, 402, 421, 434, 439, 445, 480, 496, 496
};

/* ========================================================================
 * Module State
 * ======================================================================== */

/** @brief Internal module state */
static struct {
    hal_module_state_t state;
    sdr_config_t config;
    usb_host_client_handle_t client_handle;
    usb_device_handle_t dev_handle;
    usb_transfer_t *xfer_ctrl;
    usb_transfer_t *xfer_bulk;
    SemaphoreHandle_t mutex;
    bool usb_host_installed;
    bool device_opened;
    bool streaming;
} s_sdr = {
    .state = {
        .status = HAL_STATUS_INACTIVE,
        .last_activity_ms = 0,
        .error_count = 0
    },
    .client_handle = NULL,
    .dev_handle = NULL,
    .xfer_ctrl = NULL,
    .xfer_bulk = NULL,
    .mutex = NULL,
    .usb_host_installed = false,
    .device_opened = false,
    .streaming = false
};

/** @brief Static FFT output buffer for spectrum computation */
static float s_fft_power_db[SDR_FFT_SIZE];

/* ========================================================================
 * FFT Implementation (Radix-2 Cooley-Tukey, in-place)
 * ======================================================================== */

/**
 * @brief Bit-reversal permutation for FFT indices.
 */
static uint32_t fft_bit_reverse(uint32_t x, uint32_t log2n)
{
    uint32_t result = 0;
    for (uint32_t i = 0; i < log2n; i++) {
        result = (result << 1) | (x & 1);
        x >>= 1;
    }
    return result;
}

/**
 * @brief Compute radix-2 FFT in-place on complex data.
 *
 * @param real Array of real parts (modified in-place).
 * @param imag Array of imaginary parts (modified in-place).
 * @param n FFT size (must be power of 2).
 */
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
        float w_real = cosf(-2.0f * M_PI / (float)m);
        float w_imag = sinf(-2.0f * M_PI / (float)m);

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
 * USB Host Helper Functions
 * ======================================================================== */

/**
 * @brief Client event callback for USB Host library.
 */
static void usb_client_event_cb(const usb_host_client_event_msg_t *event_msg,
                                 void *arg)
{
    switch (event_msg->event) {
        case USB_HOST_CLIENT_EVENT_NEW_DEV:
            ESP_LOGI(TAG, "USB device connected (addr=%d)",
                     event_msg->new_dev.address);
            break;
        case USB_HOST_CLIENT_EVENT_DEV_GONE:
            ESP_LOGW(TAG, "USB device disconnected");
            if (s_sdr.device_opened) {
                s_sdr.state.status = HAL_STATUS_ERROR;
                s_sdr.state.error_count++;
                s_sdr.device_opened = false;
            }
            break;
        default:
            break;
    }
}

/**
 * @brief Send a vendor control transfer to RTL2832U.
 *
 * @param request Request code (RTL_CMD_WR_REG or RTL_CMD_RD_REG).
 * @param value Register value/address.
 * @param index Block index.
 * @param data Data buffer for transfer.
 * @param len Data length.
 * @return ESP_OK on success.
 */
static esp_err_t rtl_ctrl_transfer(uint8_t request, uint16_t value,
                                    uint16_t index, uint8_t *data,
                                    uint16_t len)
{
    if (!s_sdr.device_opened || !s_sdr.xfer_ctrl) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t bm_request_type = (request == RTL_CMD_WR_REG)
                              ? RTL_USB_REQ_TYPE_WRITE
                              : RTL_USB_REQ_TYPE_READ;

    usb_setup_packet_t *setup = (usb_setup_packet_t *)s_sdr.xfer_ctrl->data_buffer;
    setup->bmRequestType = bm_request_type;
    setup->bRequest = request;
    setup->wValue = value;
    setup->wIndex = index;
    setup->wLength = len;

    if (request == RTL_CMD_WR_REG && data && len > 0) {
        memcpy(s_sdr.xfer_ctrl->data_buffer + sizeof(usb_setup_packet_t),
               data, len);
    }

    s_sdr.xfer_ctrl->num_bytes = sizeof(usb_setup_packet_t) + len;

    esp_err_t ret = usb_host_transfer_submit_control(s_sdr.client_handle,
                                                      s_sdr.xfer_ctrl);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Control transfer submit failed: %s", esp_err_to_name(ret));
        s_sdr.state.error_count++;
        return ERR_HAL_SDR_USB_FAIL;
    }

    /* Wait for completion (synchronous) */
    vTaskDelay(pdMS_TO_TICKS(10));

    if (request == RTL_CMD_RD_REG && data && len > 0) {
        memcpy(data, s_sdr.xfer_ctrl->data_buffer + sizeof(usb_setup_packet_t),
               len);
    }

    return ESP_OK;
}

/**
 * @brief Write a 16-bit value to an RTL2832U register.
 */
static esp_err_t rtl_write_reg(uint16_t block, uint16_t addr, uint16_t value)
{
    uint8_t data[2] = { (uint8_t)(value >> 8), (uint8_t)(value & 0xFF) };
    return rtl_ctrl_transfer(RTL_CMD_WR_REG, addr, block | 0x10, data, 2);
}

/**
 * @brief Initialize the RTL2832U demodulator.
 */
static esp_err_t rtl_demod_init(void)
{
    esp_err_t ret;

    /* Reset demod */
    ret = rtl_write_reg(RTL_BLOCK_DEMOD, 0x0001, 0x0014);
    if (ret != ESP_OK) return ret;

    ret = rtl_write_reg(RTL_BLOCK_DEMOD, 0x0001, 0x0010);
    if (ret != ESP_OK) return ret;

    /* Disable zero-IF mode */
    ret = rtl_write_reg(RTL_BLOCK_DEMOD, 0x0006, 0x0000);
    if (ret != ESP_OK) return ret;

    /* Enable I/Q output */
    ret = rtl_write_reg(RTL_BLOCK_DEMOD, 0x0061, 0x0060);
    if (ret != ESP_OK) return ret;

    /* Set FIR coefficients for sampling */
    ret = rtl_write_reg(RTL_BLOCK_DEMOD, 0x000A, 0x0001);
    if (ret != ESP_OK) return ret;

    /* Set IF frequency to 0 (direct sampling off, zero-IF) */
    ret = rtl_write_reg(RTL_BLOCK_DEMOD, 0x0019, 0x0000);
    if (ret != ESP_OK) return ret;

    ret = rtl_write_reg(RTL_BLOCK_DEMOD, 0x001A, 0x0000);
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}

/**
 * @brief Configure the R820T2 tuner frequency via I2C pass-through.
 *
 * @param freq_hz Target frequency in Hz.
 * @return ESP_OK on success.
 */
static esp_err_t r820t2_set_frequency(uint32_t freq_hz)
{
    /*
     * R820T2 PLL programming:
     * The tuner uses a fractional-N PLL with reference clock of 28.8 MHz.
     * VCO freq = freq_hz * divider
     * We program the PLL integer and fractional parts via I2C registers.
     */
    uint32_t ref_clk = 28800000U;
    uint32_t vco_min = 1770000000U;

    /* Determine VCO divider */
    uint8_t div_num = 0;
    uint32_t vco_freq = 0;
    for (div_num = 0; div_num < 5; div_num++) {
        vco_freq = freq_hz * (2U << div_num);
        if (vco_freq >= vco_min) break;
    }

    /* Calculate PLL integer and fractional parts */
    uint32_t n_int = vco_freq / (2 * ref_clk);
    uint32_t n_frac = ((uint64_t)(vco_freq % (2 * ref_clk)) * 65536ULL)
                      / (2 * ref_clk);

    /* Write PLL registers via control transfers (simplified) */
    uint8_t pll_data[4];
    pll_data[0] = (uint8_t)(div_num << 5);
    pll_data[1] = (uint8_t)(n_int & 0xFF);
    pll_data[2] = (uint8_t)((n_frac >> 8) & 0xFF);
    pll_data[3] = (uint8_t)(n_frac & 0xFF);

    return rtl_ctrl_transfer(RTL_CMD_WR_REG, R820T2_I2C_ADDR,
                             RTL_BLOCK_I2C, pll_data, 4);
}

/**
 * @brief Configure the RTL2832U sample rate.
 *
 * @param rate_hz Desired sample rate in Hz.
 * @return ESP_OK on success.
 */
static esp_err_t rtl_set_sample_rate_internal(uint32_t rate_hz)
{
    /*
     * RTL2832U sample rate is derived from 28.8 MHz reference:
     * rate = 28800000 * 2 / (resampling_ratio)
     * resampling_ratio = 28800000 * 2 / rate
     */
    uint32_t rsamp_ratio = (28800000U * 2U) / rate_hz;
    rsamp_ratio &= 0x0FFFFFFFU;  /* 28-bit field */

    esp_err_t ret;
    ret = rtl_write_reg(RTL_BLOCK_DEMOD, 0x009F,
                        (uint16_t)((rsamp_ratio >> 16) & 0xFFFF));
    if (ret != ESP_OK) return ret;

    ret = rtl_write_reg(RTL_BLOCK_DEMOD, 0x00A1,
                        (uint16_t)(rsamp_ratio & 0xFFFF));
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}

/**
 * @brief Configure the R820T2 tuner gain.
 *
 * @param gain_index Index 0–29 into the gain table.
 * @param agc_enabled Whether AGC should be enabled.
 * @return ESP_OK on success.
 */
static esp_err_t r820t2_set_gain(uint8_t gain_index, bool agc_enabled)
{
    uint8_t gain_data[2];

    if (agc_enabled) {
        gain_data[0] = 0x00;  /* AGC mode */
        gain_data[1] = 0x00;
    } else {
        /* Manual gain: map index to register value */
        int16_t gain_tenth_db = r820t2_gain_table[gain_index];
        gain_data[0] = 0x10;  /* Manual gain mode */
        gain_data[1] = (uint8_t)(gain_tenth_db / 10);
    }

    return rtl_ctrl_transfer(RTL_CMD_WR_REG, R820T2_I2C_ADDR,
                             RTL_BLOCK_I2C, gain_data, 2);
}

/* ========================================================================
 * USB Device Enumeration
 * ======================================================================== */

/**
 * @brief Wait for RTL-SDR device to appear on USB bus within timeout.
 *
 * @param timeout_ms Maximum time to wait in milliseconds.
 * @return ESP_OK if device found, ERR_HAL_SDR_NO_DEVICE on timeout.
 */
static esp_err_t usb_wait_for_device(uint32_t timeout_ms)
{
    uint32_t elapsed_ms = 0;
    const uint32_t poll_interval_ms = 100;

    while (elapsed_ms < timeout_ms) {
        /* Process USB host events */
        usb_host_client_handle_events(s_sdr.client_handle, poll_interval_ms);

        /* Try to open device at address 1 (first device) */
        esp_err_t ret = usb_host_device_open(s_sdr.client_handle, 1,
                                              &s_sdr.dev_handle);
        if (ret == ESP_OK && s_sdr.dev_handle != NULL) {
            /* Verify VID/PID */
            const usb_device_desc_t *desc;
            ret = usb_host_get_device_descriptor(s_sdr.dev_handle, &desc);
            if (ret == ESP_OK &&
                desc->idVendor == SDR_USB_VID &&
                desc->idProduct == SDR_USB_PID) {
                ESP_LOGI(TAG, "RTL-SDR device found (VID=0x%04X PID=0x%04X)",
                         desc->idVendor, desc->idProduct);
                s_sdr.device_opened = true;
                return ESP_OK;
            }
            /* Wrong device — close and keep looking */
            usb_host_device_close(s_sdr.client_handle, s_sdr.dev_handle);
            s_sdr.dev_handle = NULL;
        }

        elapsed_ms += poll_interval_ms;
    }

    ESP_LOGE(TAG, "RTL-SDR device not found within %lu ms", (unsigned long)timeout_ms);
    return ERR_HAL_SDR_NO_DEVICE;
}

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

esp_err_t hal_sdr_init(const sdr_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Validate configuration parameters */
    if (config->center_freq_hz < SDR_FREQ_MIN_HZ ||
        config->center_freq_hz > SDR_FREQ_MAX_HZ) {
        ESP_LOGE(TAG, "Frequency %lu Hz out of range [%lu, %lu]",
                 (unsigned long)config->center_freq_hz,
                 (unsigned long)SDR_FREQ_MIN_HZ,
                 (unsigned long)SDR_FREQ_MAX_HZ);
        return ESP_ERR_INVALID_ARG;
    }

    if (config->sample_rate_hz < SDR_SAMPLE_RATE_MIN_HZ ||
        config->sample_rate_hz > SDR_SAMPLE_RATE_MAX_HZ) {
        ESP_LOGE(TAG, "Sample rate %lu Hz out of range [%lu, %lu]",
                 (unsigned long)config->sample_rate_hz,
                 (unsigned long)SDR_SAMPLE_RATE_MIN_HZ,
                 (unsigned long)SDR_SAMPLE_RATE_MAX_HZ);
        return ESP_ERR_INVALID_ARG;
    }

    if (config->gain_index > SDR_GAIN_INDEX_MAX) {
        ESP_LOGE(TAG, "Gain index %u exceeds maximum %u",
                 config->gain_index, SDR_GAIN_INDEX_MAX);
        return ESP_ERR_INVALID_ARG;
    }

    s_sdr.state.status = HAL_STATUS_INITIALIZING;
    ESP_LOGI(TAG, "Initializing RTL-SDR (freq=%lu Hz, rate=%lu Hz, gain=%u)",
             (unsigned long)config->center_freq_hz,
             (unsigned long)config->sample_rate_hz,
             config->gain_index);

    /* Create mutex */
    s_sdr.mutex = xSemaphoreCreateMutex();
    if (s_sdr.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        s_sdr.state.status = HAL_STATUS_ERROR;
        return ESP_ERR_NO_MEM;
    }

    /* Install USB Host library */
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    esp_err_t ret = usb_host_install(&host_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "USB Host install failed: %s", esp_err_to_name(ret));
        s_sdr.state.status = HAL_STATUS_ERROR;
        s_sdr.state.error_count++;
        vSemaphoreDelete(s_sdr.mutex);
        s_sdr.mutex = NULL;
        return ERR_HAL_SDR_USB_FAIL;
    }
    s_sdr.usb_host_installed = true;

    /* Register USB client */
    const usb_host_client_config_t client_config = {
        .is_synchronous = true,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = usb_client_event_cb,
            .callback_arg = NULL,
        },
    };
    ret = usb_host_client_register(&client_config, &s_sdr.client_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "USB client register failed: %s", esp_err_to_name(ret));
        s_sdr.state.status = HAL_STATUS_ERROR;
        s_sdr.state.error_count++;
        usb_host_uninstall();
        s_sdr.usb_host_installed = false;
        vSemaphoreDelete(s_sdr.mutex);
        s_sdr.mutex = NULL;
        return ERR_HAL_SDR_USB_FAIL;
    }

    /* Wait for RTL-SDR device on USB bus (5 second timeout) */
    ret = usb_wait_for_device(SDR_USB_DETECT_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RTL-SDR device not detected");
        s_sdr.state.status = HAL_STATUS_ERROR;
        s_sdr.state.error_count++;
        usb_host_client_deregister(s_sdr.client_handle);
        s_sdr.client_handle = NULL;
        usb_host_uninstall();
        s_sdr.usb_host_installed = false;
        vSemaphoreDelete(s_sdr.mutex);
        s_sdr.mutex = NULL;
        return ERR_HAL_SDR_NO_DEVICE;
    }

    /* Allocate USB transfers */
    ret = usb_host_transfer_alloc(sizeof(usb_setup_packet_t) + 64, 0,
                                   &s_sdr.xfer_ctrl);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Control transfer alloc failed");
        goto fail_cleanup;
    }

    ret = usb_host_transfer_alloc(RTL_BULK_TRANSFER_SIZE, 0,
                                   &s_sdr.xfer_bulk);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bulk transfer alloc failed");
        goto fail_cleanup;
    }

    /* Claim the bulk interface */
    ret = usb_host_interface_claim(s_sdr.client_handle, s_sdr.dev_handle, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Interface claim failed: %s", esp_err_to_name(ret));
        goto fail_cleanup;
    }

    /* Initialize RTL2832U demodulator */
    ret = rtl_demod_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Demod init failed");
        goto fail_cleanup;
    }

    /* Configure sample rate */
    ret = rtl_set_sample_rate_internal(config->sample_rate_hz);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Sample rate config failed");
        goto fail_cleanup;
    }

    /* Configure tuner frequency */
    ret = r820t2_set_frequency(config->center_freq_hz);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Frequency config failed");
        goto fail_cleanup;
    }

    /* Configure gain */
    ret = r820t2_set_gain(config->gain_index, config->agc_enabled);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Gain config failed");
        goto fail_cleanup;
    }

    /* Store configuration and mark as active */
    memcpy(&s_sdr.config, config, sizeof(sdr_config_t));
    s_sdr.state.status = HAL_STATUS_ACTIVE;
    s_sdr.state.last_activity_ms = (uint32_t)(esp_timer_get_time() / 1000);
    ESP_LOGI(TAG, "RTL-SDR initialized successfully");
    return ESP_OK;

fail_cleanup:
    s_sdr.state.status = HAL_STATUS_ERROR;
    s_sdr.state.error_count++;
    if (s_sdr.xfer_bulk) {
        usb_host_transfer_free(s_sdr.xfer_bulk);
        s_sdr.xfer_bulk = NULL;
    }
    if (s_sdr.xfer_ctrl) {
        usb_host_transfer_free(s_sdr.xfer_ctrl);
        s_sdr.xfer_ctrl = NULL;
    }
    if (s_sdr.device_opened) {
        usb_host_device_close(s_sdr.client_handle, s_sdr.dev_handle);
        s_sdr.device_opened = false;
        s_sdr.dev_handle = NULL;
    }
    usb_host_client_deregister(s_sdr.client_handle);
    s_sdr.client_handle = NULL;
    usb_host_uninstall();
    s_sdr.usb_host_installed = false;
    vSemaphoreDelete(s_sdr.mutex);
    s_sdr.mutex = NULL;
    return ERR_HAL_SDR_USB_FAIL;
}

esp_err_t hal_sdr_set_frequency(uint32_t freq_hz)
{
    if (s_sdr.state.status != HAL_STATUS_ACTIVE) {
        return ESP_ERR_INVALID_STATE;
    }

    if (freq_hz < SDR_FREQ_MIN_HZ || freq_hz > SDR_FREQ_MAX_HZ) {
        ESP_LOGE(TAG, "Frequency %lu Hz out of range", (unsigned long)freq_hz);
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_sdr.mutex, portMAX_DELAY);

    esp_err_t ret = r820t2_set_frequency(freq_hz);
    if (ret == ESP_OK) {
        s_sdr.config.center_freq_hz = freq_hz;
        s_sdr.state.last_activity_ms = (uint32_t)(esp_timer_get_time() / 1000);
        ESP_LOGD(TAG, "Frequency set to %lu Hz", (unsigned long)freq_hz);
    } else {
        s_sdr.state.error_count++;
        ESP_LOGE(TAG, "Failed to set frequency");
    }

    xSemaphoreGive(s_sdr.mutex);
    return ret;
}

esp_err_t hal_sdr_set_sample_rate(uint32_t rate_hz)
{
    if (s_sdr.state.status != HAL_STATUS_ACTIVE) {
        return ESP_ERR_INVALID_STATE;
    }

    if (rate_hz < SDR_SAMPLE_RATE_MIN_HZ || rate_hz > SDR_SAMPLE_RATE_MAX_HZ) {
        ESP_LOGE(TAG, "Sample rate %lu Hz out of range", (unsigned long)rate_hz);
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_sdr.mutex, portMAX_DELAY);

    esp_err_t ret = rtl_set_sample_rate_internal(rate_hz);
    if (ret == ESP_OK) {
        s_sdr.config.sample_rate_hz = rate_hz;
        s_sdr.state.last_activity_ms = (uint32_t)(esp_timer_get_time() / 1000);
        ESP_LOGD(TAG, "Sample rate set to %lu Hz", (unsigned long)rate_hz);
    } else {
        s_sdr.state.error_count++;
        ESP_LOGE(TAG, "Failed to set sample rate");
    }

    xSemaphoreGive(s_sdr.mutex);
    return ret;
}

esp_err_t hal_sdr_read_iq(sdr_iq_buffer_t *buffer, uint32_t timeout_ms)
{
    if (buffer == NULL || buffer->iq_samples == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_sdr.state.status != HAL_STATUS_ACTIVE) {
        return ESP_ERR_INVALID_STATE;
    }

    if (buffer->num_samples == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_sdr.mutex, portMAX_DELAY);

    /* Total bytes needed: num_samples * 2 (I + Q interleaved) */
    uint32_t bytes_needed = buffer->num_samples * 2;
    uint32_t bytes_read = 0;
    uint32_t elapsed_ms = 0;
    const uint32_t chunk_size = RTL_BULK_TRANSFER_SIZE;
    esp_err_t ret = ESP_OK;

    while (bytes_read < bytes_needed && elapsed_ms < timeout_ms) {
        uint32_t to_read = bytes_needed - bytes_read;
        if (to_read > chunk_size) {
            to_read = chunk_size;
        }

        /* Submit bulk transfer */
        s_sdr.xfer_bulk->device_handle = s_sdr.dev_handle;
        s_sdr.xfer_bulk->bEndpointAddress = RTL_BULK_EP_ADDR;
        s_sdr.xfer_bulk->num_bytes = to_read;

        ret = usb_host_transfer_submit(s_sdr.xfer_bulk);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Bulk transfer submit failed: %s",
                     esp_err_to_name(ret));
            s_sdr.state.error_count++;
            ret = ERR_HAL_SDR_USB_FAIL;
            break;
        }

        /* Wait for transfer completion */
        vTaskDelay(pdMS_TO_TICKS(5));
        elapsed_ms += 5;

        uint32_t actual_len = s_sdr.xfer_bulk->actual_num_bytes;
        if (actual_len == 0) {
            /* No data available yet, retry */
            vTaskDelay(pdMS_TO_TICKS(10));
            elapsed_ms += 10;
            continue;
        }

        /* Copy data to user buffer, converting from unsigned to signed */
        uint32_t copy_len = actual_len;
        if (bytes_read + copy_len > bytes_needed) {
            copy_len = bytes_needed - bytes_read;
        }

        uint8_t *src = s_sdr.xfer_bulk->data_buffer;
        int8_t *dst = buffer->iq_samples + bytes_read;
        for (uint32_t i = 0; i < copy_len; i++) {
            /* RTL-SDR outputs unsigned 8-bit; convert to signed */
            dst[i] = (int8_t)(src[i] - 128);
        }

        bytes_read += copy_len;
    }

    if (ret == ESP_OK && bytes_read < bytes_needed) {
        ret = ESP_ERR_TIMEOUT;
    }

    if (ret == ESP_OK) {
        buffer->center_freq_hz = s_sdr.config.center_freq_hz;
        buffer->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);
        s_sdr.state.last_activity_ms = buffer->timestamp_ms;
    }

    xSemaphoreGive(s_sdr.mutex);
    return ret;
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

    if (s_sdr.state.status != HAL_STATUS_ACTIVE) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Determine FFT size (use min of available samples and SDR_FFT_SIZE) */
    uint32_t fft_size = SDR_FFT_SIZE;
    if (iq->num_samples < fft_size) {
        /* Round down to nearest power of 2 */
        fft_size = 1;
        while (fft_size * 2 <= iq->num_samples) {
            fft_size *= 2;
        }
    }

    /* Allocate working arrays on stack for FFT (max 1024 * 4 * 2 = 8 KB) */
    float real[SDR_FFT_SIZE];
    float imag[SDR_FFT_SIZE];

    /* Convert I/Q samples to float and apply Hanning window */
    for (uint32_t i = 0; i < fft_size; i++) {
        /* Hanning window coefficient */
        float window = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (fft_size - 1)));

        /* I/Q samples are interleaved: I[0], Q[0], I[1], Q[1], ... */
        real[i] = (float)iq->iq_samples[i * 2] * window;
        imag[i] = (float)iq->iq_samples[i * 2 + 1] * window;
    }

    /* Compute FFT */
    fft_compute(real, imag, fft_size);

    /* Compute power spectral density in dB with FFT shift
     * (negative frequencies first, then positive) */
    float *power_out = (spectrum->power_db != NULL)
                       ? spectrum->power_db
                       : s_fft_power_db;

    uint32_t half = fft_size / 2;
    for (uint32_t i = 0; i < fft_size; i++) {
        /* FFT shift: reorder so DC is in center */
        uint32_t src_idx = (i + half) % fft_size;

        float mag_sq = real[src_idx] * real[src_idx] +
                       imag[src_idx] * imag[src_idx];

        /* Power in dB: 10 * log10(mag^2 / N^2) */
        if (mag_sq < 1e-10f) {
            mag_sq = 1e-10f;  /* Floor to avoid log(0) */
        }
        power_out[i] = 10.0f * log10f(mag_sq / (float)(fft_size * fft_size));
    }

    /* Fill spectrum metadata */
    if (spectrum->power_db == NULL) {
        spectrum->power_db = s_fft_power_db;
    }
    spectrum->num_bins = fft_size;
    spectrum->freq_step_hz = s_sdr.config.sample_rate_hz / fft_size;
    spectrum->freq_start_hz = iq->center_freq_hz -
                              (s_sdr.config.sample_rate_hz / 2);

    return ESP_OK;
}

hal_status_t hal_sdr_get_status(void)
{
    return s_sdr.state.status;
}

esp_err_t hal_sdr_deinit(void)
{
    if (s_sdr.state.status == HAL_STATUS_INACTIVE) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Deinitializing RTL-SDR");

    if (s_sdr.mutex) {
        xSemaphoreTake(s_sdr.mutex, portMAX_DELAY);
    }

    s_sdr.streaming = false;

    /* Release USB interface */
    if (s_sdr.device_opened && s_sdr.client_handle && s_sdr.dev_handle) {
        usb_host_interface_release(s_sdr.client_handle, s_sdr.dev_handle, 0);
    }

    /* Free transfers */
    if (s_sdr.xfer_bulk) {
        usb_host_transfer_free(s_sdr.xfer_bulk);
        s_sdr.xfer_bulk = NULL;
    }
    if (s_sdr.xfer_ctrl) {
        usb_host_transfer_free(s_sdr.xfer_ctrl);
        s_sdr.xfer_ctrl = NULL;
    }

    /* Close device */
    if (s_sdr.device_opened && s_sdr.client_handle && s_sdr.dev_handle) {
        usb_host_device_close(s_sdr.client_handle, s_sdr.dev_handle);
        s_sdr.device_opened = false;
        s_sdr.dev_handle = NULL;
    }

    /* Deregister client */
    if (s_sdr.client_handle) {
        usb_host_client_deregister(s_sdr.client_handle);
        s_sdr.client_handle = NULL;
    }

    /* Uninstall USB host */
    if (s_sdr.usb_host_installed) {
        usb_host_uninstall();
        s_sdr.usb_host_installed = false;
    }

    s_sdr.state.status = HAL_STATUS_INACTIVE;
    s_sdr.state.last_activity_ms = 0;

    if (s_sdr.mutex) {
        xSemaphoreGive(s_sdr.mutex);
        vSemaphoreDelete(s_sdr.mutex);
        s_sdr.mutex = NULL;
    }

    ESP_LOGI(TAG, "RTL-SDR deinitialized");
    return ESP_OK;
}
