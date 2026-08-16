/**
 * @file hal_keyboard_mock.c
 * @brief Mock implementation of hal_keyboard for host testing.
 */

#include "hal_keyboard.h"
#include <stdbool.h>
#include <stddef.h>

static bool s_mock_initialized = false;
static ui_key_t s_mock_injected_key = UI_KEY_NONE;

esp_err_t hal_keyboard_init(void)
{
    s_mock_initialized = true;
    s_mock_injected_key = UI_KEY_NONE;
    return ESP_OK;
}

esp_err_t hal_keyboard_read(ui_key_t *key)
{
    if (key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_mock_initialized) {
        *key = UI_KEY_NONE;
        return ESP_ERR_INVALID_STATE;
    }

    *key = s_mock_injected_key;
    s_mock_injected_key = UI_KEY_NONE;
    return ESP_OK;
}

esp_err_t hal_keyboard_inject_key(ui_key_t key)
{
    s_mock_injected_key = key;
    return ESP_OK;
}

esp_err_t hal_keyboard_deinit(void)
{
    s_mock_initialized = false;
    s_mock_injected_key = UI_KEY_NONE;
    return ESP_OK;
}
