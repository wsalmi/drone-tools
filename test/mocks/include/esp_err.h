/**
 * @file esp_err.h
 * @brief Mock of ESP-IDF esp_err.h for host-based testing.
 *
 * Provides the esp_err_t type and standard error codes used throughout
 * the firmware without requiring the ESP-IDF toolchain.
 */

#ifndef ESP_ERR_MOCK_H
#define ESP_ERR_MOCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t esp_err_t;

#define ESP_OK          0
#define ESP_FAIL        (-1)
#define ESP_ERR_NO_MEM          0x101
#define ESP_ERR_INVALID_ARG     0x102
#define ESP_ERR_INVALID_STATE   0x103
#define ESP_ERR_INVALID_SIZE    0x104
#define ESP_ERR_NOT_FOUND       0x105
#define ESP_ERR_NOT_SUPPORTED   0x106
#define ESP_ERR_TIMEOUT         0x107

/**
 * @brief Convert esp_err_t to a string representation.
 */
const char *esp_err_to_name(esp_err_t code);

#ifdef __cplusplus
}
#endif

#endif /* ESP_ERR_MOCK_H */
