/**
 * @file hal_display.h
 * @brief HAL interface for the ST7789V2 display (240×135 px).
 *
 * Provides drawing primitives for the 1.14" TFT display connected via SPI2 (HSPI).
 * Uses RGB565 color format (16-bit). Drawing operations write to an internal
 * framebuffer; call hal_display_flush() to send the buffer to the display.
 *
 * Display characteristics:
 *   - Resolution: 240 × 135 pixels
 *   - Color depth: 16-bit RGB565
 *   - Interface: SPI2 (HSPI), separate from RF SPI3 bus
 *   - Controller: ST7789V2
 */

#ifndef HAL_DISPLAY_H
#define HAL_DISPLAY_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "hal_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Display Constants
 * ======================================================================== */

/** @brief Display width in pixels */
#define HAL_DISPLAY_WIDTH       240

/** @brief Display height in pixels */
#define HAL_DISPLAY_HEIGHT      135

/* Common RGB565 color definitions */
#define HAL_COLOR_BLACK         0x0000
#define HAL_COLOR_WHITE         0xFFFF
#define HAL_COLOR_RED           0xF800
#define HAL_COLOR_GREEN         0x07E0
#define HAL_COLOR_BLUE          0x001F
#define HAL_COLOR_YELLOW        0xFFE0
#define HAL_COLOR_CYAN          0x07FF
#define HAL_COLOR_MAGENTA       0xF81F

/* ========================================================================
 * Public API
 * ======================================================================== */

/**
 * @brief Initialize the display hardware and allocate the framebuffer.
 *
 * Configures SPI2 bus, resets the ST7789V2 controller, sets up
 * orientation (landscape), and clears the screen to black.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already initialized,
 *         ESP_ERR_NO_MEM if framebuffer allocation fails
 */
esp_err_t hal_display_init(void);

/**
 * @brief Clear the entire framebuffer with a solid color.
 *
 * @param color RGB565 color value to fill
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t hal_display_clear(uint16_t color);

/**
 * @brief Draw a text string at the specified position.
 *
 * Uses a built-in 6×8 pixel monospace font. Characters are clipped
 * at the display boundaries.
 *
 * @param x       X coordinate of the top-left corner of the first character
 * @param y       Y coordinate of the top-left corner of the first character
 * @param text    Null-terminated ASCII string to draw
 * @param color   Foreground text color (RGB565)
 * @param bg_color Background color behind text (RGB565)
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if text is NULL,
 *         ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t hal_display_draw_text(uint16_t x, uint16_t y, const char *text,
                                 uint16_t color, uint16_t bg_color);

/**
 * @brief Draw a rectangle (filled or outline only).
 *
 * Coordinates are clipped to display boundaries.
 *
 * @param x      X coordinate of the top-left corner
 * @param y      Y coordinate of the top-left corner
 * @param w      Width in pixels
 * @param h      Height in pixels
 * @param color  Rectangle color (RGB565)
 * @param filled true for filled rectangle, false for outline only
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t hal_display_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                 uint16_t color, bool filled);

/**
 * @brief Draw a single pixel at the specified position.
 *
 * If coordinates are outside display bounds, the call is silently ignored.
 *
 * @param x     X coordinate
 * @param y     Y coordinate
 * @param color Pixel color (RGB565)
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t hal_display_draw_pixel(uint16_t x, uint16_t y, uint16_t color);

/**
 * @brief Flush the framebuffer contents to the physical display.
 *
 * Transfers the entire framebuffer over SPI to the ST7789V2 controller.
 * Should be called after all drawing operations for a frame are complete.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t hal_display_flush(void);

/**
 * @brief Get the current operational status of the display module.
 *
 * @return Current hal_status_t value
 */
hal_status_t hal_display_get_status(void);

/**
 * @brief Deinitialize the display and release resources.
 *
 * Frees the framebuffer and releases the SPI bus.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t hal_display_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_DISPLAY_H */
