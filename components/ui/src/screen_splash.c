/**
 * @file screen_splash.c
 * @brief Full-screen 240x135 pixel-art splash renderer.
 *
 * The image data is generated with assets/logo.png by generate_logo.py and
 * stored as RGB565 run-length encoding to keep flash usage compact.
 */

#include "screen_splash.h"
#include "hal_display.h"
#include "splash_art.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stddef.h>

esp_err_t screen_splash_render(void)
{
    uint16_t x = 0;
    uint16_t y = 0;

    for (size_t i = 0; i < SPLASH_ART_RUN_COUNT; ++i) {
        uint32_t remaining = s_splash_art_runs[i].length;
        const uint16_t color = s_splash_art_runs[i].color;

        while (remaining > 0) {
            const uint16_t row_space = SPLASH_ART_WIDTH - x;
            const uint16_t segment = remaining < row_space
                                       ? (uint16_t)remaining : row_space;
            esp_err_t err = hal_display_draw_rect(x, y, segment, 1, color, true);
            if (err != ESP_OK) {
                return err;
            }
            remaining -= segment;
            x += segment;
            if (x == SPLASH_ART_WIDTH) {
                x = 0;
                ++y;
            }
        }
    }

    return y == SPLASH_ART_HEIGHT ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

esp_err_t screen_splash_show(uint32_t duration_ms)
{
    if (duration_ms == 0) {
        duration_ms = SPLASH_DURATION_MS;
    }

    esp_err_t ret = screen_splash_render();
    if (ret == ESP_OK) {
        ret = hal_display_flush();
    }
    if (ret == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
    }
    return ret;
}
