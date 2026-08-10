/**
 * @file esp_timer_mock.c
 * @brief Mock implementation of esp_timer for host tests.
 */

#include "esp_timer.h"

static int64_t mock_time_us = 0;

int64_t esp_timer_get_time(void) {
    return mock_time_us;
}

void mock_esp_timer_set_time(int64_t time_us) {
    mock_time_us = time_us;
}

void mock_esp_timer_advance(int64_t advance_us) {
    mock_time_us += advance_us;
}
