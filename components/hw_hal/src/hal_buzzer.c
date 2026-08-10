/**
 * @file hal_buzzer.c
 * @brief HAL driver for the NS4150B buzzer/amplifier.
 *
 * Implements:
 * - I2S peripheral configuration for tone generation
 * - Asynchronous tone playback with configurable frequency and duration
 * - Timer-based auto-stop for tone duration management
 * - Square wave synthesis at runtime
 */

#include "hal_buzzer.h"
#include "error_codes.h"
#include "esp_log.h"

#include <string.h>
#include <math.h>

#ifndef CONFIG_HAL_BUZZER_MOCK
#include "driver/i2s_std.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

/* ========================================================================
 * Configuration
 * ======================================================================== */

#define BUZZER_TAG              "hal_buzzer"

/* I2S configuration for tone output */
#define BUZZER_I2S_NUM          I2S_NUM_0
#define BUZZER_SAMPLE_RATE      44100
#define BUZZER_PIN_BCLK         -1  /* Internal clock for NS4150B */
#define BUZZER_PIN_WS           -1
#define BUZZER_PIN_DOUT         2   /* Data output to NS4150B */

/* Tone buffer: enough samples for one period at lowest frequency */
#define BUZZER_BUF_SAMPLES      512
#define BUZZER_AMPLITUDE        16000  /* 16-bit amplitude for square wave */

/* ========================================================================
 * Internal state
 * ======================================================================== */

static struct {
    bool initialized;
    bool playing;
    uint32_t current_freq_hz;
    hal_module_state_t module_state;
#ifndef CONFIG_HAL_BUZZER_MOCK
    i2s_chan_handle_t i2s_handle;
    esp_timer_handle_t stop_timer;
    TaskHandle_t playback_task;
#endif
} buzzer_ctx = {
    .initialized = false,
    .playing = false,
    .current_freq_hz = 0,
    .module_state = {
        .status = HAL_STATUS_INACTIVE,
        .last_activity_ms = 0,
        .error_count = 0
    }
};

/* ========================================================================
 * Internal helpers
 * ======================================================================== */

#ifndef CONFIG_HAL_BUZZER_MOCK

/**
 * @brief Timer callback to stop tone after duration expires.
 */
static void buzzer_stop_timer_cb(void *arg)
{
    (void)arg;
    hal_buzzer_stop();
}

/**
 * @brief Playback task that generates and sends square wave samples.
 */
static void buzzer_playback_task(void *arg)
{
    (void)arg;
    int16_t samples[BUZZER_BUF_SAMPLES];
    size_t bytes_written = 0;

    while (buzzer_ctx.playing) {
        uint32_t freq = buzzer_ctx.current_freq_hz;
        if (freq == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        /* Generate square wave samples */
        uint32_t samples_per_period = BUZZER_SAMPLE_RATE / freq;
        if (samples_per_period == 0) samples_per_period = 1;
        uint32_t half_period = samples_per_period / 2;

        for (uint32_t i = 0; i < BUZZER_BUF_SAMPLES; i++) {
            uint32_t pos_in_period = i % samples_per_period;
            samples[i] = (pos_in_period < half_period)
                         ? BUZZER_AMPLITUDE : -BUZZER_AMPLITUDE;
        }

        /* Write to I2S */
        i2s_channel_write(buzzer_ctx.i2s_handle, samples,
                          BUZZER_BUF_SAMPLES * sizeof(int16_t),
                          &bytes_written, portMAX_DELAY);
    }

    vTaskDelete(NULL);
}

#endif /* CONFIG_HAL_BUZZER_MOCK */

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

esp_err_t hal_buzzer_init(void)
{
    if (buzzer_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    buzzer_ctx.module_state.status = HAL_STATUS_INITIALIZING;
    buzzer_ctx.module_state.error_count = 0;

#ifndef CONFIG_HAL_BUZZER_MOCK
    /* Configure I2S channel */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(
        BUZZER_I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 4;
    chan_cfg.dma_frame_num = BUZZER_BUF_SAMPLES;

    esp_err_t err = i2s_new_channel(&chan_cfg, &buzzer_ctx.i2s_handle, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(BUZZER_TAG, "I2S channel creation failed: %d", err);
        buzzer_ctx.module_state.status = HAL_STATUS_ERROR;
        return err;
    }

    /* Configure I2S standard mode */
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(BUZZER_SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)BUZZER_PIN_BCLK,
            .ws = (gpio_num_t)BUZZER_PIN_WS,
            .dout = (gpio_num_t)BUZZER_PIN_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    err = i2s_channel_init_std_mode(buzzer_ctx.i2s_handle, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(BUZZER_TAG, "I2S std mode init failed: %d", err);
        i2s_del_channel(buzzer_ctx.i2s_handle);
        buzzer_ctx.module_state.status = HAL_STATUS_ERROR;
        return err;
    }

    /* Create stop timer */
    esp_timer_create_args_t timer_args = {
        .callback = buzzer_stop_timer_cb,
        .arg = NULL,
        .name = "buzzer_stop",
    };
    err = esp_timer_create(&timer_args, &buzzer_ctx.stop_timer);
    if (err != ESP_OK) {
        ESP_LOGE(BUZZER_TAG, "Timer creation failed: %d", err);
        i2s_del_channel(buzzer_ctx.i2s_handle);
        buzzer_ctx.module_state.status = HAL_STATUS_ERROR;
        return err;
    }
#endif /* CONFIG_HAL_BUZZER_MOCK */

    buzzer_ctx.initialized = true;
    buzzer_ctx.playing = false;
    buzzer_ctx.module_state.status = HAL_STATUS_ACTIVE;

    ESP_LOGI(BUZZER_TAG, "Buzzer initialized (NS4150B via I2S)");
    return ESP_OK;
}

esp_err_t hal_buzzer_play_tone(uint32_t frequency_hz, uint32_t duration_ms)
{
    if (!buzzer_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Validate parameters */
    if (frequency_hz < HAL_BUZZER_FREQ_MIN ||
        frequency_hz > HAL_BUZZER_FREQ_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    if (duration_ms == 0 || duration_ms > HAL_BUZZER_DURATION_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Stop any currently playing tone */
    if (buzzer_ctx.playing) {
        hal_buzzer_stop();
    }

    buzzer_ctx.current_freq_hz = frequency_hz;

#ifndef CONFIG_HAL_BUZZER_MOCK
    /* Enable I2S channel */
    esp_err_t err = i2s_channel_enable(buzzer_ctx.i2s_handle);
    if (err != ESP_OK) {
        buzzer_ctx.module_state.error_count++;
        return err;
    }

    buzzer_ctx.playing = true;

    /* Start playback task */
    BaseType_t ret = xTaskCreate(
        buzzer_playback_task,
        "buzzer_play",
        4096,
        NULL,
        5,
        &buzzer_ctx.playback_task
    );
    if (ret != pdPASS) {
        i2s_channel_disable(buzzer_ctx.i2s_handle);
        buzzer_ctx.playing = false;
        return ESP_ERR_NO_MEM;
    }

    /* Start auto-stop timer */
    esp_timer_start_once(buzzer_ctx.stop_timer,
                          (uint64_t)duration_ms * 1000);
#else
    buzzer_ctx.playing = true;
#endif /* CONFIG_HAL_BUZZER_MOCK */

    ESP_LOGD(BUZZER_TAG, "Playing tone: %lu Hz, %lu ms",
             (unsigned long)frequency_hz, (unsigned long)duration_ms);
    return ESP_OK;
}

esp_err_t hal_buzzer_stop(void)
{
    if (!buzzer_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!buzzer_ctx.playing) {
        return ESP_OK;  /* Already stopped */
    }

    buzzer_ctx.playing = false;
    buzzer_ctx.current_freq_hz = 0;

#ifndef CONFIG_HAL_BUZZER_MOCK
    /* Stop the timer if it's running */
    esp_timer_stop(buzzer_ctx.stop_timer);

    /* Wait for playback task to finish */
    vTaskDelay(pdMS_TO_TICKS(20));

    /* Disable I2S channel */
    i2s_channel_disable(buzzer_ctx.i2s_handle);
#endif

    return ESP_OK;
}

hal_status_t hal_buzzer_get_status(void)
{
    return buzzer_ctx.module_state.status;
}

esp_err_t hal_buzzer_deinit(void)
{
    if (!buzzer_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Stop any playing tone */
    if (buzzer_ctx.playing) {
        hal_buzzer_stop();
    }

#ifndef CONFIG_HAL_BUZZER_MOCK
    /* Delete timer */
    esp_timer_delete(buzzer_ctx.stop_timer);
    buzzer_ctx.stop_timer = NULL;

    /* Delete I2S channel */
    i2s_del_channel(buzzer_ctx.i2s_handle);
    buzzer_ctx.i2s_handle = NULL;
#endif

    buzzer_ctx.initialized = false;
    buzzer_ctx.module_state.status = HAL_STATUS_INACTIVE;

    ESP_LOGI(BUZZER_TAG, "Buzzer deinitialized");
    return ESP_OK;
}
