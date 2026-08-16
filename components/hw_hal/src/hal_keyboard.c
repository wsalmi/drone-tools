/**
 * @file hal_keyboard.c
 * @brief HAL driver implementation for M5Stack Cardputer ADV keyboard matrix & input.
 */

#include "hal_keyboard.h"
#include "esp_log.h"
#include <string.h>

#if defined(ESP_PLATFORM) && !defined(CONFIG_HAL_KEYBOARD_MOCK)
#include "driver/gpio.h"
#include "esp_timer.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#endif

static const char *TAG = "hal_kb";

/* Cardputer Matrix GPIO definitions */
#define ROW_COUNT 3
#define COL_COUNT 7

#if defined(ESP_PLATFORM) && !defined(CONFIG_HAL_KEYBOARD_MOCK)
static const gpio_num_t s_row_pins[ROW_COUNT] = { GPIO_NUM_8, GPIO_NUM_9, GPIO_NUM_11 };
static const gpio_num_t s_col_pins[COL_COUNT] = { GPIO_NUM_13, GPIO_NUM_15, GPIO_NUM_3, GPIO_NUM_4, GPIO_NUM_5, GPIO_NUM_6, GPIO_NUM_7 };
#endif

static bool s_initialized = false;
static ui_key_t s_injected_key = UI_KEY_NONE;
static uint8_t s_last_key_char = 0;
static uint32_t s_last_press_time_ms = 0;

esp_err_t hal_keyboard_init(void)
{
#if defined(ESP_PLATFORM) && !defined(CONFIG_HAL_KEYBOARD_MOCK)
    /* Configure Row pins as outputs (default HIGH) */
    gpio_config_t io_conf = {0};
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.pin_bit_mask = 0;
    for (int i = 0; i < ROW_COUNT; i++) {
        io_conf.pin_bit_mask |= (1ULL << s_row_pins[i]);
    }
    gpio_config(&io_conf);
    for (int i = 0; i < ROW_COUNT; i++) {
        gpio_set_level(s_row_pins[i], 1);
    }

    /* Configure Column pins as inputs with pull-up */
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pin_bit_mask = 0;
    for (int i = 0; i < COL_COUNT; i++) {
        io_conf.pin_bit_mask |= (1ULL << s_col_pins[i]);
    }
    gpio_config(&io_conf);
#endif

    s_initialized = true;
    s_injected_key = UI_KEY_NONE;
    s_last_key_char = 0;
    s_last_press_time_ms = 0;
    ESP_LOGI(TAG, "Keyboard HAL initialized");
    return ESP_OK;
}

static ui_key_t map_char_to_key(char c)
{
    switch (c) {
        case '1': return UI_KEY_1;
        case '2': return UI_KEY_2;
        case '3': return UI_KEY_3;
        case '4': return UI_KEY_4;
        case '5': return UI_KEY_5;
        case '6': return UI_KEY_6;
        case '7': return UI_KEY_7;
        case 'w': case 'W':
            return UI_KEY_UP;
        case 's': case 'S':
            return UI_KEY_DOWN;
        case 'a': case 'A':
            return UI_KEY_LEFT;
        case 'd': case 'D':
            return UI_KEY_RIGHT;
        case '\n': case '\r': case ' ':
            return UI_KEY_ENTER;
        case '\b': case 127: case 27:
            return UI_KEY_BACK;
        case 'm': case 'M':
            return UI_KEY_MENU;
        case '\t':
            return UI_KEY_TAB;
        default:
            return UI_KEY_NONE;
    }
}

esp_err_t hal_keyboard_read(ui_key_t *key)
{
    if (key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *key = UI_KEY_NONE;

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Check injected key first */
    if (s_injected_key != UI_KEY_NONE) {
        *key = s_injected_key;
        s_injected_key = UI_KEY_NONE;
        return ESP_OK;
    }

#if defined(ESP_PLATFORM) && !defined(CONFIG_HAL_KEYBOARD_MOCK)
    /* 1. Check serial console input (non-blocking) */
    char c = 0;
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags != -1) {
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
        if (read(STDIN_FILENO, &c, 1) == 1) {
            ui_key_t mapped = map_char_to_key(c);
            if (mapped != UI_KEY_NONE) {
                *key = mapped;
                return ESP_OK;
            }
        }
    }

    /* 2. Cardputer 8x7 Matrix scan table */
    static const ui_key_t s_matrix_keys[8][7] = {
        /* Row 0 (A0=0,A1=0,A2=0): Esc, 1, 2, 3, 4, 5, 6 */
        { UI_KEY_BACK,  UI_KEY_1,     UI_KEY_2,     UI_KEY_3,     UI_KEY_4,     UI_KEY_5,     UI_KEY_6 },
        /* Row 1 (A0=1,A1=0,A2=0): Tab, Q, W, E, R, T, Y */
        { UI_KEY_TAB,   UI_KEY_NONE,  UI_KEY_UP,    UI_KEY_NONE,  UI_KEY_NONE,  UI_KEY_NONE,  UI_KEY_NONE },
        /* Row 2 (A0=0,A1=1,A2=0): Fn, Shift, A, S, D, F, G */
        { UI_KEY_NONE,  UI_KEY_NONE,  UI_KEY_LEFT,  UI_KEY_DOWN,  UI_KEY_RIGHT, UI_KEY_NONE,  UI_KEY_NONE },
        /* Row 3 (A0=1,A1=1,A2=0): Ctrl, Opt, Alt, Z, X, C, V */
        { UI_KEY_NONE,  UI_KEY_NONE,  UI_KEY_NONE,  UI_KEY_NONE,  UI_KEY_NONE,  UI_KEY_NONE,  UI_KEY_NONE },
        /* Row 4 (A0=0,A1=0,A2=1): Space, B, N, M, ,, ., / */
        { UI_KEY_ENTER, UI_KEY_NONE,  UI_KEY_NONE,  UI_KEY_MENU,  UI_KEY_NONE,  UI_KEY_NONE,  UI_KEY_NONE },
        /* Row 5 (A0=1,A1=0,A2=1): 7, 8, 9, 0, Del, _, = */
        { UI_KEY_7,     UI_KEY_NONE,  UI_KEY_NONE,  UI_KEY_NONE,  UI_KEY_BACK,  UI_KEY_NONE,  UI_KEY_NONE },
        /* Row 6 (A0=0,A1=1,A2=1): U, I, O, P, [, ], \ */
        { UI_KEY_NONE,  UI_KEY_NONE,  UI_KEY_NONE,  UI_KEY_NONE,  UI_KEY_NONE,  UI_KEY_NONE,  UI_KEY_NONE },
        /* Row 7 (A0=1,A1=1,A2=1): H, J, K, L, ;, ', Enter */
        { UI_KEY_NONE,  UI_KEY_NONE,  UI_KEY_NONE,  UI_KEY_NONE,  UI_KEY_NONE,  UI_KEY_NONE,  UI_KEY_ENTER }
    };

    /* Scan matrix rows 0 to 7 */
    for (int r = 0; r < 8; r++) {
        /* Set 3 address lines (8, 9, 11) to select row r */
        gpio_set_level(s_row_pins[0], (r & 0x01) ? 1 : 0);
        gpio_set_level(s_row_pins[1], (r & 0x02) ? 1 : 0);
        gpio_set_level(s_row_pins[2], (r & 0x04) ? 1 : 0);
        esp_rom_delay_us(5);

        for (int c_idx = 0; c_idx < COL_COUNT; c_idx++) {
            if (gpio_get_level(s_col_pins[c_idx]) == 0) {
                ui_key_t k = s_matrix_keys[r][c_idx];
                if (k != UI_KEY_NONE) {
                    /* Verify this pin goes HIGH on an alternate row to reject UART/bus signals */
                    int test_row = (r + 1) % 8;
                    gpio_set_level(s_row_pins[0], (test_row & 0x01) ? 1 : 0);
                    gpio_set_level(s_row_pins[1], (test_row & 0x02) ? 1 : 0);
                    gpio_set_level(s_row_pins[2], (test_row & 0x04) ? 1 : 0);
                    esp_rom_delay_us(5);
                    int test_level = gpio_get_level(s_col_pins[c_idx]);

                    /* Restore row r */
                    gpio_set_level(s_row_pins[0], (r & 0x01) ? 1 : 0);
                    gpio_set_level(s_row_pins[1], (r & 0x02) ? 1 : 0);
                    gpio_set_level(s_row_pins[2], (r & 0x04) ? 1 : 0);
                    esp_rom_delay_us(5);

                    if (test_level == 1) {
                        /* Real physical key press! */
                        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
                        if (now - s_last_press_time_ms > 180) {
                            s_last_press_time_ms = now;
                            *key = k;
                            /* Restore row pins to inactive HIGH before returning */
                            gpio_set_level(s_row_pins[0], 1);
                            gpio_set_level(s_row_pins[1], 1);
                            gpio_set_level(s_row_pins[2], 1);
                            return ESP_OK;
                        }
                    }
                }
            }
        }
    }

    /* Restore all row lines to HIGH (1, 1, 1 = Row 7 / idle) */
    gpio_set_level(s_row_pins[0], 1);
    gpio_set_level(s_row_pins[1], 1);
    gpio_set_level(s_row_pins[2], 1);
#endif

    return ESP_OK;
}

esp_err_t hal_keyboard_inject_key(ui_key_t key)
{
    s_injected_key = key;
    return ESP_OK;
}

esp_err_t hal_keyboard_deinit(void)
{
    s_initialized = false;
    s_injected_key = UI_KEY_NONE;
    return ESP_OK;
}
