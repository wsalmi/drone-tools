/**
 * @file hal_display_mock.c
 * @brief Mock implementation of hal_display for host testing.
 *
 * Provides stub functions that track call counts and parameters
 * without actual hardware interaction.
 */

#include "hal_display.h"
#include <string.h>
#include <stdbool.h>

/* Internal mock state */
static bool s_initialized = false;
static uint32_t s_draw_text_count = 0;
static uint32_t s_draw_rect_count = 0;
static uint32_t s_flush_count = 0;
static uint32_t s_clear_count = 0;

/* ========================================================================
 * Mock Control API
 * ======================================================================== */

void mock_hal_display_reset(void)
{
    s_initialized = false;
    s_draw_text_count = 0;
    s_draw_rect_count = 0;
    s_flush_count = 0;
    s_clear_count = 0;
}

void mock_hal_display_set_initialized(bool init)
{
    s_initialized = init;
}

uint32_t mock_hal_display_get_draw_text_count(void)
{
    return s_draw_text_count;
}

uint32_t mock_hal_display_get_draw_rect_count(void)
{
    return s_draw_rect_count;
}

uint32_t mock_hal_display_get_flush_count(void)
{
    return s_flush_count;
}

/* ========================================================================
 * HAL Display API (mock implementations)
 * ======================================================================== */

esp_err_t hal_display_init(void)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    s_initialized = true;
    return ESP_OK;
}

esp_err_t hal_display_clear(uint16_t color)
{
    (void)color;
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    s_clear_count++;
    return ESP_OK;
}

esp_err_t hal_display_draw_text(uint16_t x, uint16_t y, const char *text,
                                 uint16_t color, uint16_t bg_color)
{
    (void)x;
    (void)y;
    (void)color;
    (void)bg_color;
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_draw_text_count++;
    return ESP_OK;
}

esp_err_t hal_display_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                 uint16_t color, bool filled)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)color;
    (void)filled;
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    s_draw_rect_count++;
    return ESP_OK;
}

esp_err_t hal_display_draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    (void)x;
    (void)y;
    (void)color;
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

esp_err_t hal_display_flush(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    s_flush_count++;
    return ESP_OK;
}

hal_status_t hal_display_get_status(void)
{
    return s_initialized ? HAL_STATUS_ACTIVE : HAL_STATUS_INACTIVE;
}

esp_err_t hal_display_deinit(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    s_initialized = false;
    return ESP_OK;
}
