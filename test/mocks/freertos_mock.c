/**
 * @file freertos_mock.c
 * @brief Stub implementations of FreeRTOS primitives for host tests.
 *
 * These stubs provide trivial implementations that allow firmware code
 * using FreeRTOS APIs to compile and link on host. Mutex operations
 * are no-ops (single-threaded test execution).
 */

#include "freertos/FreeRTOS.h"
#include <stdlib.h>

static uint32_t mock_tick_count = 0;

SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    /* Return a non-NULL dummy pointer */
    static int dummy_sem;
    return (SemaphoreHandle_t)&dummy_sem;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t timeout) {
    (void)sem;
    (void)timeout;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t sem) {
    (void)sem;
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t sem) {
    (void)sem;
}

TickType_t xTaskGetTickCount(void) {
    return mock_tick_count++;
}

void vTaskDelay(TickType_t ticks) {
    (void)ticks;
    mock_tick_count += ticks;
}

void vTaskDelete(TaskHandle_t task) {
    (void)task;
}

BaseType_t xTaskCreatePinnedToCore(
    TaskFunction_t pvTaskCode,
    const char *pcName,
    uint32_t usStackDepth,
    void *pvParameters,
    UBaseType_t uxPriority,
    TaskHandle_t *pvCreatedTask,
    int xCoreID)
{
    (void)pvTaskCode;
    (void)pcName;
    (void)usStackDepth;
    (void)pvParameters;
    (void)uxPriority;
    (void)xCoreID;

    /* In host test mode, don't actually create a task —
     * just return success and set a dummy task handle */
    static int dummy_task;
    if (pvCreatedTask != NULL) {
        *pvCreatedTask = (TaskHandle_t)&dummy_task;
    }
    return pdPASS;
}
