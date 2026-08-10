/**
 * @file FreeRTOS.h
 * @brief Mock of FreeRTOS types for host-based testing.
 *
 * Provides stub types for FreeRTOS primitives (semaphores, queues, tasks)
 * to allow compilation of firmware modules on host without the RTOS.
 */

#ifndef FREERTOS_MOCK_H
#define FREERTOS_MOCK_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Basic FreeRTOS types */
typedef uint32_t TickType_t;
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
typedef void *SemaphoreHandle_t;
typedef int32_t BaseType_t;
typedef uint32_t UBaseType_t;
typedef void (*TaskFunction_t)(void *);

#define pdTRUE      1
#define pdFALSE     0
#define pdPASS      pdTRUE
#define pdFAIL      pdFALSE

#define portMAX_DELAY   0xFFFFFFFF
#define portTICK_PERIOD_MS 1

/* Tick-to-ms conversion macro */
#define pdMS_TO_TICKS(xTimeInMs) ((TickType_t)(xTimeInMs) / portTICK_PERIOD_MS)

/* Semaphore stubs */
SemaphoreHandle_t xSemaphoreCreateMutex(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t timeout);
BaseType_t xSemaphoreGive(SemaphoreHandle_t sem);
void vSemaphoreDelete(SemaphoreHandle_t sem);

/* Task stubs */
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t ticks);
void vTaskDelete(TaskHandle_t task);

BaseType_t xTaskCreatePinnedToCore(
    TaskFunction_t pvTaskCode,
    const char *pcName,
    uint32_t usStackDepth,
    void *pvParameters,
    UBaseType_t uxPriority,
    TaskHandle_t *pvCreatedTask,
    int xCoreID
);

#ifdef __cplusplus
}
#endif

#endif /* FREERTOS_MOCK_H */
