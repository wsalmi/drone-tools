/**
 * @file screen_spectrum.c
 * @brief Spectrum Analyzer screen implementation — waterfall display with protocol markers.
 *
 * Renders spectrum data from the spectrum_analyzer service as a power vs frequency
 * graph with protocol frequency marker overlays. Uses hal_display draw primitives.
 *
 * Validates: Requirements 12.1, 12.4
 */

#include "screen_spectrum.h"
#include "hal_display.h"
#include "spectrum_analyzer.h"
#include "ui_manager.h"
#include <string.h>
#include <stdio.h>

/* ========================================================================
 * Internal State
 * ======================================================================== */

static screen_spectrum_state_t s_state = {0};

/* ========================================================================
 * Helper: Map power dBm value to pixel Y coordinate
 * ======================================================================== */

static uint16_t power_to_y(float power_dbm)
{
    /* Clamp to display range */
    if (power_dbm < (float)SCREEN_SPECTRUM_POWER_MIN_DBM) {
        power_dbm = (float)SCREEN_SPECTRUM_POWER_MIN_DBM;
    }
    if (power_dbm > (float)SCREEN_SPECTRUM_POWER_MAX_DBM) {
        power_dbm = (float)SCREEN_SPECTRUM_POWER_MAX_DBM;
    }

    /* Map range [MIN, MAX] dBm to [graph_bottom, graph_top] pixels */
    float range = (float)(SCREEN_SPECTRUM_POWER_MAX_DBM - SCREEN_SPECTRUM_POWER_MIN_DBM);
    float normalized = (power_dbm - (float)SCREEN_SPECTRUM_POWER_MIN_DBM) / range;

    uint16_t graph_top = SCREEN_SPECTRUM_CONTENT_Y + 2;
    uint16_t graph_bottom = graph_top + SCREEN_SPECTRUM_GRAPH_HEIGHT - 1;

    /* Invert Y axis (higher power = higher on screen = lower Y) */
    uint16_t y = graph_bottom - (uint16_t)(normalized * (float)(SCREEN_SPECTRUM_GRAPH_HEIGHT - 1));
    return y;
}

/* ========================================================================
 * Helper: Map frequency to pixel X coordinate
 * ======================================================================== */

static uint16_t freq_to_x(uint32_t freq_hz, uint32_t freq_start_hz, uint32_t freq_step_hz, uint32_t num_bins)
{
    if (num_bins == 0 || freq_step_hz == 0) {
        return 0;
    }

    uint32_t freq_end_hz = freq_start_hz + (freq_step_hz * num_bins);
    if (freq_hz < freq_start_hz) {
        return 0;
    }
    if (freq_hz > freq_end_hz) {
        return HAL_DISPLAY_WIDTH - 1;
    }

    float fraction = (float)(freq_hz - freq_start_hz) / (float)(freq_end_hz - freq_start_hz);
    return (uint16_t)(fraction * (float)(HAL_DISPLAY_WIDTH - 1));
}

/* ========================================================================
 * Helper: Get color for signal power (heatmap palette)
 * ======================================================================== */

static uint16_t power_to_color(float power_dbm)
{
    float range = (float)(SCREEN_SPECTRUM_POWER_MAX_DBM - SCREEN_SPECTRUM_POWER_MIN_DBM);
    float normalized = (power_dbm - (float)SCREEN_SPECTRUM_POWER_MIN_DBM) / range;

    if (normalized < 0.0f) normalized = 0.0f;
    if (normalized > 1.0f) normalized = 1.0f;

    /* Blue → Cyan → Green → Yellow → Red gradient */
    if (normalized < 0.25f) {
        /* Blue to Cyan */
        uint8_t g = (uint8_t)(normalized * 4.0f * 63.0f);
        return HAL_COLOR_BLUE | ((uint16_t)g << 5);
    } else if (normalized < 0.5f) {
        /* Cyan to Green */
        float t = (normalized - 0.25f) * 4.0f;
        uint8_t b = (uint8_t)((1.0f - t) * 31.0f);
        return HAL_COLOR_GREEN | b;
    } else if (normalized < 0.75f) {
        /* Green to Yellow */
        float t = (normalized - 0.5f) * 4.0f;
        uint8_t r = (uint8_t)(t * 31.0f);
        return HAL_COLOR_GREEN | ((uint16_t)r << 11);
    } else {
        /* Yellow to Red */
        float t = (normalized - 0.75f) * 4.0f;
        uint8_t g = (uint8_t)((1.0f - t) * 63.0f);
        return HAL_COLOR_RED | ((uint16_t)g << 5);
    }
}

/* ========================================================================
 * Helper: Render frequency markers
 * ======================================================================== */

static void render_markers(uint32_t freq_start_hz, uint32_t freq_step_hz, uint32_t num_bins)
{
    frequency_marker_t markers[SPECTRUM_MAX_MARKERS];
    uint8_t marker_count = 0;

    esp_err_t err = spectrum_analyzer_get_markers(markers, SPECTRUM_MAX_MARKERS, &marker_count);
    if (err != ESP_OK || marker_count == 0) {
        return;
    }

    uint16_t graph_top = SCREEN_SPECTRUM_CONTENT_Y + 2;

    for (uint8_t i = 0; i < marker_count; i++) {
        uint16_t x_start = freq_to_x(markers[i].freq_start_hz, freq_start_hz, freq_step_hz, num_bins);
        uint16_t x_end = freq_to_x(markers[i].freq_end_hz, freq_start_hz, freq_step_hz, num_bins);

        /* Draw dashed vertical lines at band boundaries */
        for (uint16_t y = graph_top; y < graph_top + SCREEN_SPECTRUM_GRAPH_HEIGHT; y += 4) {
            hal_display_draw_pixel(x_start, y, HAL_COLOR_CYAN);
            if (x_end != x_start) {
                hal_display_draw_pixel(x_end, y, HAL_COLOR_CYAN);
            }
        }

        /* Draw label at the top of the band */
        if (markers[i].label != NULL) {
            uint16_t label_x = (x_start + x_end) / 2;
            /* Shift label left so it doesn't overflow right edge */
            if (label_x > HAL_DISPLAY_WIDTH - 30) {
                label_x = HAL_DISPLAY_WIDTH - 30;
            }
            hal_display_draw_text(label_x, graph_top, markers[i].label,
                                  HAL_COLOR_CYAN, HAL_COLOR_BLACK);
        }
    }
}

/* ========================================================================
 * Public API
 * ======================================================================== */

esp_err_t screen_spectrum_init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_state.initialized = true;
    s_state.waterfall_row_idx = 0;
    return ESP_OK;
}

esp_err_t screen_spectrum_render(void)
{
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Clear content area */
    hal_display_draw_rect(0, SCREEN_SPECTRUM_CONTENT_Y,
                          HAL_DISPLAY_WIDTH,
                          HAL_DISPLAY_HEIGHT - SCREEN_SPECTRUM_CONTENT_Y,
                          HAL_COLOR_BLACK, true);

    /* Get spectrum data from spectrum_analyzer service */
    spectrum_state_t spec_state;
    esp_err_t err = spectrum_analyzer_get_state(&spec_state);

    if (err != ESP_OK || !spec_state.running) {
        /* SDR not available or not running — show error message */
        hal_display_draw_text(20, SCREEN_SPECTRUM_CONTENT_Y + 40,
                              "SDR nao disponivel",
                              HAL_COLOR_RED, HAL_COLOR_BLACK);
        hal_display_draw_text(20, SCREEN_SPECTRUM_CONTENT_Y + 56,
                              "Conecte RTL-SDR",
                              HAL_COLOR_WHITE, HAL_COLOR_BLACK);
        return ESP_OK;
    }

    sdr_spectrum_t *spectrum = &spec_state.spectrum;

    /* Draw power vs frequency graph */
    if (spectrum->num_bins > 0 && spectrum->power_db != NULL) {
        uint16_t graph_top = SCREEN_SPECTRUM_CONTENT_Y + 2;
        uint16_t graph_bottom = graph_top + SCREEN_SPECTRUM_GRAPH_HEIGHT - 1;
        uint16_t prev_x = 0;
        uint16_t prev_y = graph_bottom;

        /* Scale bins to screen width */
        float bins_per_pixel = (float)spectrum->num_bins / (float)HAL_DISPLAY_WIDTH;

        for (uint16_t px = 0; px < HAL_DISPLAY_WIDTH; px++) {
            /* Determine which bin this pixel corresponds to */
            uint32_t bin_idx = (uint32_t)((float)px * bins_per_pixel);
            if (bin_idx >= spectrum->num_bins) {
                bin_idx = spectrum->num_bins - 1;
            }

            float power = spectrum->power_db[bin_idx];
            uint16_t y = power_to_y(power);

            /* Draw line from previous point to current */
            if (px > 0) {
                /* Simple vertical line connection for adjacent pixels */
                uint16_t y_min = (y < prev_y) ? y : prev_y;
                uint16_t y_max = (y > prev_y) ? y : prev_y;
                for (uint16_t dy = y_min; dy <= y_max; dy++) {
                    hal_display_draw_pixel(px, dy, HAL_COLOR_GREEN);
                }
            } else {
                hal_display_draw_pixel(px, y, HAL_COLOR_GREEN);
            }

            prev_x = px;
            prev_y = y;
        }

        /* Draw reference lines */
        /* -60 dBm threshold line (dashed) */
        uint16_t threshold_y = power_to_y((float)spec_state.config.detection_threshold_dbm);
        for (uint16_t px = 0; px < HAL_DISPLAY_WIDTH; px += 6) {
            hal_display_draw_pixel(px, threshold_y, HAL_COLOR_YELLOW);
            if (px + 1 < HAL_DISPLAY_WIDTH) {
                hal_display_draw_pixel(px + 1, threshold_y, HAL_COLOR_YELLOW);
            }
        }

        /* Draw frequency markers for known protocols */
        render_markers(spectrum->freq_start_hz, spectrum->freq_step_hz, spectrum->num_bins);

        /* Draw detected peak indicators */
        for (uint8_t i = 0; i < spec_state.peak_count; i++) {
            uint16_t peak_x = freq_to_x(spec_state.peaks[i].frequency_hz,
                                         spectrum->freq_start_hz,
                                         spectrum->freq_step_hz,
                                         spectrum->num_bins);
            uint16_t peak_y = power_to_y(spec_state.peaks[i].power_dbm);

            /* Draw small triangle marker above peak */
            hal_display_draw_pixel(peak_x, peak_y - 2, HAL_COLOR_RED);
            hal_display_draw_pixel(peak_x - 1, peak_y - 1, HAL_COLOR_RED);
            hal_display_draw_pixel(peak_x + 1, peak_y - 1, HAL_COLOR_RED);
        }
    }

    /* Draw info bar at bottom */
    uint16_t info_y = HAL_DISPLAY_HEIGHT - SCREEN_SPECTRUM_INFO_HEIGHT;
    hal_display_draw_rect(0, info_y, HAL_DISPLAY_WIDTH, SCREEN_SPECTRUM_INFO_HEIGHT,
                          HAL_COLOR_BLACK, true);

    char info_buf[48];
    snprintf(info_buf, sizeof(info_buf), "F:%luMHz BW:%lukHz G:%.1fdB",
             (unsigned long)spec_state.config.center_freq_mhz,
             (unsigned long)spec_state.config.bandwidth_khz,
             (double)spec_state.config.gain_db);
    hal_display_draw_text(2, info_y + 4, info_buf, HAL_COLOR_WHITE, HAL_COLOR_BLACK);

    /* Peak count */
    char peak_buf[16];
    snprintf(peak_buf, sizeof(peak_buf), "Pk:%u", spec_state.peak_count);
    hal_display_draw_text(200, info_y + 4, peak_buf, HAL_COLOR_YELLOW, HAL_COLOR_BLACK);

    (void)prev_x; /* suppress unused warning */
    return ESP_OK;
}

esp_err_t screen_spectrum_handle_key(uint8_t key)
{
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Spectrum screen uses UP/DOWN for gain adjustments */
    spectrum_config_t config;
    esp_err_t err = spectrum_analyzer_get_config(&config);
    if (err != ESP_OK) {
        return ESP_OK; /* No spectrum available, ignore keys */
    }

    switch ((ui_key_t)key) {
        case UI_KEY_UP:
            /* Increase gain by 1 dB */
            if (config.gain_db < SPECTRUM_GAIN_MAX_DB) {
                config.gain_db += 1.0f;
                if (config.gain_db > SPECTRUM_GAIN_MAX_DB) {
                    config.gain_db = SPECTRUM_GAIN_MAX_DB;
                }
                spectrum_analyzer_set_config(&config);
            }
            break;

        case UI_KEY_DOWN:
            /* Decrease gain by 1 dB */
            if (config.gain_db > SPECTRUM_GAIN_MIN_DB) {
                config.gain_db -= 1.0f;
                if (config.gain_db < SPECTRUM_GAIN_MIN_DB) {
                    config.gain_db = SPECTRUM_GAIN_MIN_DB;
                }
                spectrum_analyzer_set_config(&config);
            }
            break;

        default:
            break;
    }

    return ESP_OK;
}

esp_err_t screen_spectrum_deinit(void)
{
    s_state.initialized = false;
    return ESP_OK;
}
