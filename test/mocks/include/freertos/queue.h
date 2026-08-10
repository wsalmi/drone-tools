/**
 * @file queue.h
 * @brief Mock of FreeRTOS queue.h for host-based testing.
 *
 * Provides a simple array-based queue implementation for single-threaded
 * test execution. Queue operations work synchronously without actual
 * task blocking.
 */

#ifndef FREERTOS_QUEUE_MOCK_H
#define FREERTOS_QUEUE_MOCK_H

#include "freertos/FreeRTOS.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a new queue.
 *
 * @param uxQueueLength Maximum number of items the queue can hold.
 * @param uxItemSize    Size of each item in bytes.
 * @return Queue handle, or NULL on failure.
 */
QueueHandle_t xQueueCreate(UBaseType_t uxQueueLength, UBaseType_t uxItemSize);

/**
 * @brief Send an item to the back of the queue.
 *
 * @param xQueue     Queue handle.
 * @param pvItemToQueue Pointer to item to copy into the queue.
 * @param xTicksToWait  Ticks to wait (ignored in mock — non-blocking).
 * @return pdTRUE if item was sent, pdFALSE if queue is full.
 */
BaseType_t xQueueSend(QueueHandle_t xQueue, const void *pvItemToQueue, TickType_t xTicksToWait);

/**
 * @brief Receive an item from the front of the queue.
 *
 * @param xQueue      Queue handle.
 * @param pvBuffer    Buffer to copy the received item into.
 * @param xTicksToWait Ticks to wait (ignored in mock — returns immediately).
 * @return pdTRUE if an item was received, pdFALSE if queue is empty.
 */
BaseType_t xQueueReceive(QueueHandle_t xQueue, void *pvBuffer, TickType_t xTicksToWait);

/**
 * @brief Get the number of items currently in a queue.
 *
 * @param xQueue Queue handle.
 * @return Number of items waiting in the queue.
 */
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t xQueue);

/**
 * @brief Reset a queue to its empty state.
 *
 * @param xQueue Queue handle.
 * @return pdPASS always.
 */
BaseType_t xQueueReset(QueueHandle_t xQueue);

/**
 * @brief Delete a queue and free its memory.
 *
 * @param xQueue Queue handle.
 */
void vQueueDelete(QueueHandle_t xQueue);

#ifdef __cplusplus
}
#endif

#endif /* FREERTOS_QUEUE_MOCK_H */
