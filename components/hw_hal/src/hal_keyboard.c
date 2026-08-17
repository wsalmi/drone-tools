/**
 * @file hal_keyboard.c
 * @brief HAL driver implementation for M5Stack Cardputer ADV keyboard matrix & input.
 */

#include "hal_keyboard.h"
#include "esp_log.h"
#include <string.h>

#if defined(ESP_PLATFORM) && !defined(CONFIG_HAL_KEYBOARD_MOCK)
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
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

static const char *map_key_to_name(ui_key_t k)
{
    switch (k) {
        case UI_KEY_UP:    return "UP/W";
        case UI_KEY_DOWN:  return "DOWN/S";
        case UI_KEY_LEFT:  return "LEFT/A";
        case UI_KEY_RIGHT: return "RIGHT/D";
        case UI_KEY_ENTER: return "ENTER/SPACE";
        case UI_KEY_BACK:  return "ESC/DEL";
        case UI_KEY_MENU:  return "MENU/M";
        case UI_KEY_TAB:   return "TAB";
        case UI_KEY_1:     return "1";
        case UI_KEY_2:     return "2";
        case UI_KEY_3:     return "3";
        case UI_KEY_4:     return "4";
        case UI_KEY_5:     return "5";
        case UI_KEY_6:     return "6";
        case UI_KEY_7:     return "7";
        default:           return "OTHER";
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
    /* 1. Check serial console input (STDIN non-blocking) */
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
        { UI_KEY_TAB,   UI_KEY_BACK,  UI_KEY_UP,    UI_KEY_UP,    UI_KEY_NONE,  UI_KEY_NONE,  UI_KEY_NONE },
        /* Row 2 (A0=0,A1=1,A2=0): Fn, Shift, A, S, D, F, G */
        { UI_KEY_NONE,  UI_KEY_NONE,  UI_KEY_LEFT,  UI_KEY_DOWN,  UI_KEY_RIGHT, UI_KEY_ENTER, UI_KEY_NONE },
        /* Row 3 (A0=1,A1=1,A2=0): Ctrl, Opt, Alt, Z, X, C, V */
        { UI_KEY_NONE,  UI_KEY_NONE,  UI_KEY_NONE,  UI_KEY_DOWN,  UI_KEY_DOWN,  UI_KEY_DOWN,  UI_KEY_DOWN },
        /* Row 4 (A0=0,A1=0,A2=1): Space, B, N, M, ,, ., / (Arrows: , is Left, . is Down, / is Right) */
        { UI_KEY_ENTER, UI_KEY_NONE,  UI_KEY_NONE,  UI_KEY_MENU,  UI_KEY_LEFT,  UI_KEY_DOWN,  UI_KEY_RIGHT },
        /* Row 5 (A0=1,A1=0,A2=1): 7, 8, 9, 0, Del, _, = */
        { UI_KEY_7,     UI_KEY_1,     UI_KEY_2,     UI_KEY_3,     UI_KEY_BACK,  UI_KEY_BACK,  UI_KEY_ENTER },
        /* Row 6 (A0=0,A1=1,A2=1): U, I, O, P, [, ], \ */
        { UI_KEY_NONE,  UI_KEY_UP,    UI_KEY_NONE,  UI_KEY_NONE,  UI_KEY_UP,    UI_KEY_DOWN,  UI_KEY_ENTER },
        /* Row 7 (A0=1,A1=1,A2=1): H, J, K, L, ;, ', Enter (Arrows: ; is Up, J is Down, K is Up, L is Right, H is Left) */
        { UI_KEY_LEFT,  UI_KEY_DOWN,  UI_KEY_UP,    UI_KEY_RIGHT, UI_KEY_UP,    UI_KEY_ENTER, UI_KEY_ENTER }
    };

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);

    /* 1. First scan all 8 rows and build the full 8x7 matrix table */
    uint8_t matrix_raw[8][COL_COUNT] = {0};
    uint8_t col_low_count[COL_COUNT] = {0};

    for (int r = 0; r < 8; r++) {
        gpio_set_level(s_row_pins[0], (r & 0x01) ? 1 : 0);
        gpio_set_level(s_row_pins[1], (r & 0x02) ? 1 : 0);
        gpio_set_level(s_row_pins[2], (r & 0x04) ? 1 : 0);
        esp_rom_delay_us(15);

        for (int c = 0; c < COL_COUNT; c++) {
            int lvl = gpio_get_level(s_col_pins[c]);
            matrix_raw[r][c] = (uint8_t)lvl;
            if (lvl == 0) {
                col_low_count[c]++;
            }
        }
    }

    /* Restore all row pins to HIGH */
    gpio_set_level(s_row_pins[0], 1);
    gpio_set_level(s_row_pins[1], 1);
    gpio_set_level(s_row_pins[2], 1);

    /* 2. Identify pressed key with instant edge-detection and typematic repeat */
    static ui_key_t s_active_held_key = UI_KEY_NONE;
    static uint32_t s_key_press_start_ms = 0;
    static uint32_t s_last_repeat_time_ms = 0;

    ui_key_t detected_key = UI_KEY_NONE;
    int detected_row = -1;
    int detected_col = -1;

    for (int c = 0; c < COL_COUNT; c++) {
        /* Reject lines held low across >= 3 rows (e.g. SX1262 BUSY on GPIO 4, GPS UART) */
        if (col_low_count[c] > 0 && col_low_count[c] <= 2) {
            for (int r = 0; r < 8; r++) {
                if (matrix_raw[r][c] == 0) {
                    ui_key_t k = s_matrix_keys[r][c];
                    if (k != UI_KEY_NONE) {
                        detected_key = k;
                        detected_row = r;
                        detected_col = c;
                        break;
                    }
                }
            }
            if (detected_key != UI_KEY_NONE) {
                break;
            }
        }
    }

    if (detected_key != UI_KEY_NONE) {
        if (detected_key != s_active_held_key) {
            /* New key press -> INSTANT trigger */
            s_active_held_key = detected_key;
            s_key_press_start_ms = now;
            s_last_repeat_time_ms = now;
            *key = detected_key;
            ESP_LOGI(TAG, "[KEY PRESS] Key '%s' (ID %d) on Row %d, Col %d",
                     map_key_to_name(detected_key), (int)detected_key, detected_row, detected_col);
            return ESP_OK;
        } else {
            /* Held key -> Typematic repeat (250ms initial delay, then 80ms repeat) */
            if ((now - s_key_press_start_ms >= 250) && (now - s_last_repeat_time_ms >= 80)) {
                s_last_repeat_time_ms = now;
                *key = detected_key;
                return ESP_OK;
            }
        }
    } else {
        /* No key pressed -> reset state immediately */
        s_active_held_key = UI_KEY_NONE;
        s_key_press_start_ms = 0;
        s_last_repeat_time_ms = 0;
    }
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
