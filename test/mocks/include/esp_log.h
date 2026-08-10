/**
 * @file esp_log.h
 * @brief Mock of ESP-IDF esp_log.h for host-based testing.
 *
 * Provides no-op logging macros that match the ESP-IDF API.
 * In test builds, logs are suppressed by default.
 * Set ESP_LOG_MOCK_VERBOSE=1 at compile time to enable output.
 */

#ifndef ESP_LOG_MOCK_H
#define ESP_LOG_MOCK_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ESP_LOG_NONE = 0,
    ESP_LOG_ERROR,
    ESP_LOG_WARN,
    ESP_LOG_INFO,
    ESP_LOG_DEBUG,
    ESP_LOG_VERBOSE
} esp_log_level_t;

#ifdef ESP_LOG_MOCK_VERBOSE
    #define ESP_LOGE(tag, fmt, ...) fprintf(stderr, "E (%s) " fmt "\n", tag, ##__VA_ARGS__)
    #define ESP_LOGW(tag, fmt, ...) fprintf(stderr, "W (%s) " fmt "\n", tag, ##__VA_ARGS__)
    #define ESP_LOGI(tag, fmt, ...) fprintf(stderr, "I (%s) " fmt "\n", tag, ##__VA_ARGS__)
    #define ESP_LOGD(tag, fmt, ...) fprintf(stderr, "D (%s) " fmt "\n", tag, ##__VA_ARGS__)
    #define ESP_LOGV(tag, fmt, ...) fprintf(stderr, "V (%s) " fmt "\n", tag, ##__VA_ARGS__)
#else
    #define ESP_LOGE(tag, fmt, ...) ((void)0)
    #define ESP_LOGW(tag, fmt, ...) ((void)0)
    #define ESP_LOGI(tag, fmt, ...) ((void)0)
    #define ESP_LOGD(tag, fmt, ...) ((void)0)
    #define ESP_LOGV(tag, fmt, ...) ((void)0)
#endif

#define esp_log_level_set(tag, level) ((void)0)

#ifdef __cplusplus
}
#endif

#endif /* ESP_LOG_MOCK_H */
