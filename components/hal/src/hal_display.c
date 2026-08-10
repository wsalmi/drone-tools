/**
 * @file hal_display.c
 * @brief HAL driver for the ST7789V2 display (240×135 px) via SPI2.
 *
 * Implements:
 * - SPI2 (HSPI) bus configuration for display
 * - ST7789V2 initialization sequence
 * - Framebuffer management (RGB565, 240×135)
 * - Drawing primitives: clear, pixel, rect, text
 * - Flush framebuffer to hardware via DMA SPI transfer
 */

#include "hal_display.h"
#include "error_codes.h"
#include "esp_log.h"

#include <string.h>
#include <stdlib.h>

#ifndef CONFIG_HAL_DISPLAY_MOCK
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

/* ========================================================================
 * Configuration
 * ======================================================================== */

#define DISPLAY_TAG             "hal_display"

/* SPI2 (HSPI) pin assignments for Cardputer ADV */
#define DISPLAY_SPI_HOST        SPI2_HOST
#define DISPLAY_PIN_CLK         36
#define DISPLAY_PIN_MOSI        35
#define DISPLAY_PIN_CS          37
#define DISPLAY_PIN_DC          34
#define DISPLAY_PIN_RST         33
#define DISPLAY_PIN_BL          38  /* Backlight */

#define DISPLAY_SPI_FREQ_HZ     40000000  /* 40 MHz */
#define DISPLAY_FB_SIZE         (HAL_DISPLAY_WIDTH * HAL_DISPLAY_HEIGHT * 2)

/* Font: 6x8 pixel monospace (ASCII 32–126) */
#define FONT_WIDTH              6
#define FONT_HEIGHT             8

/* ST7789 commands */
#define ST7789_NOP              0x00
#define ST7789_SWRESET          0x01
#define ST7789_SLPOUT           0x11
#define ST7789_NORON            0x13
#define ST7789_INVON            0x21
#define ST7789_DISPON           0x29
#define ST7789_CASET            0x2A
#define ST7789_RASET            0x2B
#define ST7789_RAMWR            0x2C
#define ST7789_MADCTL           0x36
#define ST7789_COLMOD           0x3A

/* ========================================================================
 * Built-in 6x8 Font Data (ASCII 32–126)
 * Each character is 6 bytes wide, 8 rows high, MSB = top pixel
 * ======================================================================== */

static const uint8_t font_6x8[][FONT_WIDTH] = {
    {0x00,0x00,0x00,0x00,0x00,0x00}, /* ' ' (32) */
    {0x00,0x00,0x5F,0x00,0x00,0x00}, /* '!' */
    {0x00,0x07,0x00,0x07,0x00,0x00}, /* '"' */
    {0x14,0x7F,0x14,0x7F,0x14,0x00}, /* '#' */
    {0x24,0x2A,0x7F,0x2A,0x12,0x00}, /* '$' */
    {0x23,0x13,0x08,0x64,0x62,0x00}, /* '%' */
    {0x36,0x49,0x55,0x22,0x50,0x00}, /* '&' */
    {0x00,0x05,0x03,0x00,0x00,0x00}, /* ''' */
    {0x00,0x1C,0x22,0x41,0x00,0x00}, /* '(' */
    {0x00,0x41,0x22,0x1C,0x00,0x00}, /* ')' */
    {0x14,0x08,0x3E,0x08,0x14,0x00}, /* '*' */
    {0x08,0x08,0x3E,0x08,0x08,0x00}, /* '+' */
    {0x00,0x50,0x30,0x00,0x00,0x00}, /* ',' */
    {0x08,0x08,0x08,0x08,0x08,0x00}, /* '-' */
    {0x00,0x60,0x60,0x00,0x00,0x00}, /* '.' */
    {0x20,0x10,0x08,0x04,0x02,0x00}, /* '/' */
    {0x3E,0x51,0x49,0x45,0x3E,0x00}, /* '0' */
    {0x00,0x42,0x7F,0x40,0x00,0x00}, /* '1' */
    {0x42,0x61,0x51,0x49,0x46,0x00}, /* '2' */
    {0x21,0x41,0x45,0x4B,0x31,0x00}, /* '3' */
    {0x18,0x14,0x12,0x7F,0x10,0x00}, /* '4' */
    {0x27,0x45,0x45,0x45,0x39,0x00}, /* '5' */
    {0x3C,0x4A,0x49,0x49,0x30,0x00}, /* '6' */
    {0x01,0x71,0x09,0x05,0x03,0x00}, /* '7' */
    {0x36,0x49,0x49,0x49,0x36,0x00}, /* '8' */
    {0x06,0x49,0x49,0x29,0x1E,0x00}, /* '9' */
    {0x00,0x36,0x36,0x00,0x00,0x00}, /* ':' */
    {0x00,0x56,0x36,0x00,0x00,0x00}, /* ';' */
    {0x08,0x14,0x22,0x41,0x00,0x00}, /* '<' */
    {0x14,0x14,0x14,0x14,0x14,0x00}, /* '=' */
    {0x00,0x41,0x22,0x14,0x08,0x00}, /* '>' */
    {0x02,0x01,0x51,0x09,0x06,0x00}, /* '?' */
    {0x32,0x49,0x79,0x41,0x3E,0x00}, /* '@' */
    {0x7E,0x11,0x11,0x11,0x7E,0x00}, /* 'A' */
    {0x7F,0x49,0x49,0x49,0x36,0x00}, /* 'B' */
    {0x3E,0x41,0x41,0x41,0x22,0x00}, /* 'C' */
    {0x7F,0x41,0x41,0x22,0x1C,0x00}, /* 'D' */
    {0x7F,0x49,0x49,0x49,0x41,0x00}, /* 'E' */
    {0x7F,0x09,0x09,0x09,0x01,0x00}, /* 'F' */
    {0x3E,0x41,0x49,0x49,0x7A,0x00}, /* 'G' */
    {0x7F,0x08,0x08,0x08,0x7F,0x00}, /* 'H' */
    {0x00,0x41,0x7F,0x41,0x00,0x00}, /* 'I' */
    {0x20,0x40,0x41,0x3F,0x01,0x00}, /* 'J' */
    {0x7F,0x08,0x14,0x22,0x41,0x00}, /* 'K' */
    {0x7F,0x40,0x40,0x40,0x40,0x00}, /* 'L' */
    {0x7F,0x02,0x0C,0x02,0x7F,0x00}, /* 'M' */
    {0x7F,0x04,0x08,0x10,0x7F,0x00}, /* 'N' */
    {0x3E,0x41,0x41,0x41,0x3E,0x00}, /* 'O' */
    {0x7F,0x09,0x09,0x09,0x06,0x00}, /* 'P' */
    {0x3E,0x41,0x51,0x21,0x5E,0x00}, /* 'Q' */
    {0x7F,0x09,0x19,0x29,0x46,0x00}, /* 'R' */
    {0x46,0x49,0x49,0x49,0x31,0x00}, /* 'S' */
    {0x01,0x01,0x7F,0x01,0x01,0x00}, /* 'T' */
    {0x3F,0x40,0x40,0x40,0x3F,0x00}, /* 'U' */
    {0x1F,0x20,0x40,0x20,0x1F,0x00}, /* 'V' */
    {0x3F,0x40,0x38,0x40,0x3F,0x00}, /* 'W' */
    {0x63,0x14,0x08,0x14,0x63,0x00}, /* 'X' */
    {0x07,0x08,0x70,0x08,0x07,0x00}, /* 'Y' */
    {0x61,0x51,0x49,0x45,0x43,0x00}, /* 'Z' */
    {0x00,0x7F,0x41,0x41,0x00,0x00}, /* '[' */
    {0x02,0x04,0x08,0x10,0x20,0x00}, /* '\' */
    {0x00,0x41,0x41,0x7F,0x00,0x00}, /* ']' */
    {0x04,0x02,0x01,0x02,0x04,0x00}, /* '^' */
    {0x40,0x40,0x40,0x40,0x40,0x00}, /* '_' */
    {0x00,0x01,0x02,0x04,0x00,0x00}, /* '`' */
    {0x20,0x54,0x54,0x54,0x78,0x00}, /* 'a' */
    {0x7F,0x48,0x44,0x44,0x38,0x00}, /* 'b' */
    {0x38,0x44,0x44,0x44,0x20,0x00}, /* 'c' */
    {0x38,0x44,0x44,0x48,0x7F,0x00}, /* 'd' */
    {0x38,0x54,0x54,0x54,0x18,0x00}, /* 'e' */
    {0x08,0x7E,0x09,0x01,0x02,0x00}, /* 'f' */
    {0x0C,0x52,0x52,0x52,0x3E,0x00}, /* 'g' */
    {0x7F,0x08,0x04,0x04,0x78,0x00}, /* 'h' */
    {0x00,0x44,0x7D,0x40,0x00,0x00}, /* 'i' */
    {0x20,0x40,0x44,0x3D,0x00,0x00}, /* 'j' */
    {0x7F,0x10,0x28,0x44,0x00,0x00}, /* 'k' */
    {0x00,0x41,0x7F,0x40,0x00,0x00}, /* 'l' */
    {0x7C,0x04,0x18,0x04,0x78,0x00}, /* 'm' */
    {0x7C,0x08,0x04,0x04,0x78,0x00}, /* 'n' */
    {0x38,0x44,0x44,0x44,0x38,0x00}, /* 'o' */
    {0x7C,0x14,0x14,0x14,0x08,0x00}, /* 'p' */
    {0x08,0x14,0x14,0x18,0x7C,0x00}, /* 'q' */
    {0x7C,0x08,0x04,0x04,0x08,0x00}, /* 'r' */
    {0x48,0x54,0x54,0x54,0x20,0x00}, /* 's' */
    {0x04,0x3F,0x44,0x40,0x20,0x00}, /* 't' */
    {0x3C,0x40,0x40,0x20,0x7C,0x00}, /* 'u' */
    {0x1C,0x20,0x40,0x20,0x1C,0x00}, /* 'v' */
    {0x3C,0x40,0x30,0x40,0x3C,0x00}, /* 'w' */
    {0x44,0x28,0x10,0x28,0x44,0x00}, /* 'x' */
    {0x0C,0x50,0x50,0x50,0x3C,0x00}, /* 'y' */
    {0x44,0x64,0x54,0x4C,0x44,0x00}, /* 'z' */
    {0x00,0x08,0x36,0x41,0x00,0x00}, /* '{' */
    {0x00,0x00,0x7F,0x00,0x00,0x00}, /* '|' */
    {0x00,0x41,0x36,0x08,0x00,0x00}, /* '}' */
    {0x08,0x08,0x2A,0x1C,0x08,0x00}, /* '~' */
};

#define FONT_FIRST_CHAR         32
#define FONT_LAST_CHAR          126

/* ========================================================================
 * Internal state
 * ======================================================================== */

static struct {
    bool initialized;
    uint16_t *framebuffer;      /* RGB565 framebuffer (240×135) */
    hal_module_state_t module_state;
#ifndef CONFIG_HAL_DISPLAY_MOCK
    spi_device_handle_t spi_handle;
#endif
} display_ctx = {
    .initialized = false,
    .framebuffer = NULL,
    .module_state = {
        .status = HAL_STATUS_INACTIVE,
        .last_activity_ms = 0,
        .error_count = 0
    }
};

/* ========================================================================
 * SPI Command/Data Helpers (real hardware only)
 * ======================================================================== */

#ifndef CONFIG_HAL_DISPLAY_MOCK

/**
 * @brief Send a command byte to the ST7789.
 */
static esp_err_t display_send_cmd(uint8_t cmd)
{
    gpio_set_level(DISPLAY_PIN_DC, 0);  /* Command mode */
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    return spi_device_polling_transmit(display_ctx.spi_handle, &t);
}

/**
 * @brief Send data bytes to the ST7789.
 */
static esp_err_t display_send_data(const uint8_t *data, size_t len)
{
    gpio_set_level(DISPLAY_PIN_DC, 1);  /* Data mode */
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    return spi_device_polling_transmit(display_ctx.spi_handle, &t);
}

/**
 * @brief Set the display window for pixel data writes.
 */
static esp_err_t display_set_window(uint16_t x0, uint16_t y0,
                                     uint16_t x1, uint16_t y1)
{
    /* Column address set */
    uint8_t col_data[4] = {
        (uint8_t)(x0 >> 8), (uint8_t)(x0 & 0xFF),
        (uint8_t)(x1 >> 8), (uint8_t)(x1 & 0xFF)
    };
    esp_err_t err = display_send_cmd(ST7789_CASET);
    if (err != ESP_OK) return err;
    err = display_send_data(col_data, 4);
    if (err != ESP_OK) return err;

    /* Row address set */
    uint8_t row_data[4] = {
        (uint8_t)(y0 >> 8), (uint8_t)(y0 & 0xFF),
        (uint8_t)(y1 >> 8), (uint8_t)(y1 & 0xFF)
    };
    err = display_send_cmd(ST7789_RASET);
    if (err != ESP_OK) return err;
    err = display_send_data(row_data, 4);
    if (err != ESP_OK) return err;

    /* Memory write */
    return display_send_cmd(ST7789_RAMWR);
}

/**
 * @brief Initialize the ST7789V2 controller with the startup sequence.
 */
static esp_err_t display_hw_init(void)
{
    /* Hardware reset */
    gpio_set_level(DISPLAY_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(DISPLAY_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    esp_err_t err;

    /* Software reset */
    err = display_send_cmd(ST7789_SWRESET);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(150));

    /* Sleep out */
    err = display_send_cmd(ST7789_SLPOUT);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(50));

    /* Color mode: 16-bit RGB565 */
    err = display_send_cmd(ST7789_COLMOD);
    if (err != ESP_OK) return err;
    uint8_t colmod = 0x55;  /* 16-bit/pixel */
    err = display_send_data(&colmod, 1);
    if (err != ESP_OK) return err;

    /* Memory access control (landscape orientation) */
    err = display_send_cmd(ST7789_MADCTL);
    if (err != ESP_OK) return err;
    uint8_t madctl = 0x70;  /* MX + MV for landscape */
    err = display_send_data(&madctl, 1);
    if (err != ESP_OK) return err;

    /* Inversion on (required for correct colors on ST7789) */
    err = display_send_cmd(ST7789_INVON);
    if (err != ESP_OK) return err;

    /* Normal display mode */
    err = display_send_cmd(ST7789_NORON);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Display on */
    err = display_send_cmd(ST7789_DISPON);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Turn on backlight */
    gpio_set_level(DISPLAY_PIN_BL, 1);

    return ESP_OK;
}

#endif /* CONFIG_HAL_DISPLAY_MOCK */

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

esp_err_t hal_display_init(void)
{
    if (display_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    display_ctx.module_state.status = HAL_STATUS_INITIALIZING;
    display_ctx.module_state.error_count = 0;

    /* Allocate framebuffer */
    display_ctx.framebuffer = (uint16_t *)malloc(DISPLAY_FB_SIZE);
    if (display_ctx.framebuffer == NULL) {
        ESP_LOGE(DISPLAY_TAG, "Failed to allocate framebuffer (%d bytes)",
                 DISPLAY_FB_SIZE);
        display_ctx.module_state.status = HAL_STATUS_ERROR;
        return ESP_ERR_NO_MEM;
    }

    /* Clear framebuffer to black */
    memset(display_ctx.framebuffer, 0, DISPLAY_FB_SIZE);

#ifndef CONFIG_HAL_DISPLAY_MOCK
    /* Configure GPIO for DC, RST, BL */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << DISPLAY_PIN_DC) |
                        (1ULL << DISPLAY_PIN_RST) |
                        (1ULL << DISPLAY_PIN_BL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    /* Configure SPI bus */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = DISPLAY_PIN_MOSI,
        .miso_io_num = -1,  /* Display is write-only */
        .sclk_io_num = DISPLAY_PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DISPLAY_FB_SIZE,
    };

    esp_err_t err = spi_bus_initialize(DISPLAY_SPI_HOST, &bus_cfg,
                                        SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(DISPLAY_TAG, "SPI bus init failed: %d", err);
        free(display_ctx.framebuffer);
        display_ctx.framebuffer = NULL;
        display_ctx.module_state.status = HAL_STATUS_ERROR;
        return err;
    }

    /* Add display device to SPI bus */
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = DISPLAY_SPI_FREQ_HZ,
        .mode = 0,
        .spics_io_num = DISPLAY_PIN_CS,
        .queue_size = 1,
    };

    err = spi_bus_add_device(DISPLAY_SPI_HOST, &dev_cfg,
                              &display_ctx.spi_handle);
    if (err != ESP_OK) {
        ESP_LOGE(DISPLAY_TAG, "SPI device add failed: %d", err);
        spi_bus_free(DISPLAY_SPI_HOST);
        free(display_ctx.framebuffer);
        display_ctx.framebuffer = NULL;
        display_ctx.module_state.status = HAL_STATUS_ERROR;
        return err;
    }

    /* Initialize ST7789V2 controller */
    err = display_hw_init();
    if (err != ESP_OK) {
        ESP_LOGE(DISPLAY_TAG, "Display HW init failed: %d", err);
        spi_bus_remove_device(display_ctx.spi_handle);
        spi_bus_free(DISPLAY_SPI_HOST);
        free(display_ctx.framebuffer);
        display_ctx.framebuffer = NULL;
        display_ctx.module_state.status = HAL_STATUS_ERROR;
        return err;
    }
#endif /* CONFIG_HAL_DISPLAY_MOCK */

    display_ctx.initialized = true;
    display_ctx.module_state.status = HAL_STATUS_ACTIVE;

    ESP_LOGI(DISPLAY_TAG, "Display initialized (%dx%d, RGB565)",
             HAL_DISPLAY_WIDTH, HAL_DISPLAY_HEIGHT);
    return ESP_OK;
}

esp_err_t hal_display_clear(uint16_t color)
{
    if (!display_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    uint16_t *fb = display_ctx.framebuffer;
    size_t pixel_count = HAL_DISPLAY_WIDTH * HAL_DISPLAY_HEIGHT;

    for (size_t i = 0; i < pixel_count; i++) {
        fb[i] = color;
    }

    return ESP_OK;
}

esp_err_t hal_display_draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (!display_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Silently ignore out-of-bounds pixels */
    if (x >= HAL_DISPLAY_WIDTH || y >= HAL_DISPLAY_HEIGHT) {
        return ESP_OK;
    }

    display_ctx.framebuffer[y * HAL_DISPLAY_WIDTH + x] = color;
    return ESP_OK;
}

esp_err_t hal_display_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                 uint16_t color, bool filled)
{
    if (!display_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (w == 0 || h == 0) {
        return ESP_OK;
    }

    /* Clip to display bounds */
    uint16_t x_end = x + w;
    uint16_t y_end = y + h;
    if (x_end > HAL_DISPLAY_WIDTH) x_end = HAL_DISPLAY_WIDTH;
    if (y_end > HAL_DISPLAY_HEIGHT) y_end = HAL_DISPLAY_HEIGHT;
    if (x >= HAL_DISPLAY_WIDTH || y >= HAL_DISPLAY_HEIGHT) {
        return ESP_OK;
    }

    if (filled) {
        for (uint16_t row = y; row < y_end; row++) {
            for (uint16_t col = x; col < x_end; col++) {
                display_ctx.framebuffer[row * HAL_DISPLAY_WIDTH + col] = color;
            }
        }
    } else {
        /* Top and bottom horizontal lines */
        for (uint16_t col = x; col < x_end; col++) {
            display_ctx.framebuffer[y * HAL_DISPLAY_WIDTH + col] = color;
            if (y_end - 1 < HAL_DISPLAY_HEIGHT) {
                display_ctx.framebuffer[(y_end - 1) * HAL_DISPLAY_WIDTH + col] = color;
            }
        }
        /* Left and right vertical lines */
        for (uint16_t row = y; row < y_end; row++) {
            display_ctx.framebuffer[row * HAL_DISPLAY_WIDTH + x] = color;
            if (x_end - 1 < HAL_DISPLAY_WIDTH) {
                display_ctx.framebuffer[row * HAL_DISPLAY_WIDTH + (x_end - 1)] = color;
            }
        }
    }

    return ESP_OK;
}

esp_err_t hal_display_draw_text(uint16_t x, uint16_t y, const char *text,
                                 uint16_t color, uint16_t bg_color)
{
    if (!display_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t cursor_x = x;
    uint16_t cursor_y = y;

    while (*text != '\0') {
        char c = *text++;

        /* Only render printable ASCII characters */
        if (c < FONT_FIRST_CHAR || c > FONT_LAST_CHAR) {
            c = '?';  /* Replace unsupported chars with '?' */
        }

        /* Check if character fits on screen */
        if (cursor_x + FONT_WIDTH > HAL_DISPLAY_WIDTH) {
            break;  /* Stop at right edge */
        }
        if (cursor_y + FONT_HEIGHT > HAL_DISPLAY_HEIGHT) {
            break;  /* Stop at bottom edge */
        }

        /* Draw character from font data */
        const uint8_t *glyph = font_6x8[c - FONT_FIRST_CHAR];
        for (uint8_t col = 0; col < FONT_WIDTH; col++) {
            uint8_t column_data = glyph[col];
            for (uint8_t row = 0; row < FONT_HEIGHT; row++) {
                uint16_t px = cursor_x + col;
                uint16_t py = cursor_y + row;
                if (px < HAL_DISPLAY_WIDTH && py < HAL_DISPLAY_HEIGHT) {
                    uint16_t pixel_color = (column_data & (1 << row))
                                            ? color : bg_color;
                    display_ctx.framebuffer[py * HAL_DISPLAY_WIDTH + px] =
                        pixel_color;
                }
            }
        }

        cursor_x += FONT_WIDTH;
    }

    return ESP_OK;
}

esp_err_t hal_display_flush(void)
{
    if (!display_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

#ifndef CONFIG_HAL_DISPLAY_MOCK
    /* Set window to full screen */
    esp_err_t err = display_set_window(0, 0,
                                        HAL_DISPLAY_WIDTH - 1,
                                        HAL_DISPLAY_HEIGHT - 1);
    if (err != ESP_OK) {
        display_ctx.module_state.error_count++;
        return err;
    }

    /* Send framebuffer data */
    gpio_set_level(DISPLAY_PIN_DC, 1);
    spi_transaction_t t = {
        .length = DISPLAY_FB_SIZE * 8,
        .tx_buffer = display_ctx.framebuffer,
    };
    err = spi_device_polling_transmit(display_ctx.spi_handle, &t);
    if (err != ESP_OK) {
        display_ctx.module_state.error_count++;
        return err;
    }
#endif

    return ESP_OK;
}

hal_status_t hal_display_get_status(void)
{
    return display_ctx.module_state.status;
}

esp_err_t hal_display_deinit(void)
{
    if (!display_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

#ifndef CONFIG_HAL_DISPLAY_MOCK
    /* Turn off backlight */
    gpio_set_level(DISPLAY_PIN_BL, 0);

    /* Release SPI device and bus */
    spi_bus_remove_device(display_ctx.spi_handle);
    spi_bus_free(DISPLAY_SPI_HOST);
#endif

    /* Free framebuffer */
    if (display_ctx.framebuffer != NULL) {
        free(display_ctx.framebuffer);
        display_ctx.framebuffer = NULL;
    }

    display_ctx.initialized = false;
    display_ctx.module_state.status = HAL_STATUS_INACTIVE;

    ESP_LOGI(DISPLAY_TAG, "Display deinitialized");
    return ESP_OK;
}
