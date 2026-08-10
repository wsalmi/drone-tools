/**
 * @file esp_timer.h
 * @brief Mock of ESP-IDF esp_timer.h for host-based testing.
 *
 * Provides a controllable time source for tests.
 */

#ifndef ESP_TIMER_MOCK_H
#define ESP_TIMER_MOCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the current time in microseconds since boot.
 * In mock mode, returns a controllable value.
 */
int64_t esp_timer_get_time(void);

/**
 * @brief Set the mock timer value (for test control).
 * @param time_us Time in microseconds to return from esp_timer_get_time.
 */
void mock_esp_timer_set_time(int64_t time_us);

/**
 * @brief Advance mock timer by a given amount.
 * @param advance_us Microseconds to advance.
 */
void mock_esp_timer_advance(int64_t advance_us);

#ifdef __cplusplus
}
#endif

#endif /* ESP_TIMER_MOCK_H */
