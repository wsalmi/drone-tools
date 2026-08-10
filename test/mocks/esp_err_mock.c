/**
 * @file esp_err_mock.c
 * @brief Implementation of esp_err.h mock for host tests.
 */

#include "esp_err.h"

const char *esp_err_to_name(esp_err_t code) {
    switch (code) {
        case ESP_OK:                return "ESP_OK";
        case ESP_FAIL:              return "ESP_FAIL";
        case ESP_ERR_NO_MEM:        return "ESP_ERR_NO_MEM";
        case ESP_ERR_INVALID_ARG:   return "ESP_ERR_INVALID_ARG";
        case ESP_ERR_INVALID_STATE: return "ESP_ERR_INVALID_STATE";
        case ESP_ERR_INVALID_SIZE:  return "ESP_ERR_INVALID_SIZE";
        case ESP_ERR_NOT_FOUND:     return "ESP_ERR_NOT_FOUND";
        case ESP_ERR_NOT_SUPPORTED: return "ESP_ERR_NOT_SUPPORTED";
        case ESP_ERR_TIMEOUT:       return "ESP_ERR_TIMEOUT";
        default:                    return "UNKNOWN_ERROR";
    }
}
