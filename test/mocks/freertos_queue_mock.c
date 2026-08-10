/**
 * @file freertos_queue_mock.c
 * @brief Mock implementation of FreeRTOS Queue API for host tests.
 *
 * Provides a simple array-based queue for single-threaded test execution.
 * Memory is dynamically allocated; items are stored in a circular buffer.
 */

#include "freertos/queue.h"
#include <stdlib.h>
#include <string.h>

/* Internal queue structure */
typedef struct {
    uint8_t *buffer;        /* Storage for queue items */
    UBaseType_t item_size;  /* Size of each item in bytes */
    UBaseType_t capacity;   /* Maximum number of items */
    UBaseType_t count;      /* Current number of items */
    UBaseType_t head;       /* Index of next item to read */
    UBaseType_t tail;       /* Index of next item to write */
} mock_queue_t;

QueueHandle_t xQueueCreate(UBaseType_t uxQueueLength, UBaseType_t uxItemSize)
{
    if (uxQueueLength == 0 || uxItemSize == 0) {
        return NULL;
    }

    mock_queue_t *q = (mock_queue_t *)calloc(1, sizeof(mock_queue_t));
    if (q == NULL) {
        return NULL;
    }

    q->buffer = (uint8_t *)calloc(uxQueueLength, uxItemSize);
    if (q->buffer == NULL) {
        free(q);
        return NULL;
    }

    q->item_size = uxItemSize;
    q->capacity = uxQueueLength;
    q->count = 0;
    q->head = 0;
    q->tail = 0;

    return (QueueHandle_t)q;
}

BaseType_t xQueueSend(QueueHandle_t xQueue, const void *pvItemToQueue, TickType_t xTicksToWait)
{
    (void)xTicksToWait;

    if (xQueue == NULL || pvItemToQueue == NULL) {
        return pdFALSE;
    }

    mock_queue_t *q = (mock_queue_t *)xQueue;

    if (q->count >= q->capacity) {
        /* Queue full — drop silently */
        return pdFALSE;
    }

    /* Copy item to tail position */
    memcpy(&q->buffer[q->tail * q->item_size], pvItemToQueue, q->item_size);
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;

    return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t xQueue, void *pvBuffer, TickType_t xTicksToWait)
{
    (void)xTicksToWait;

    if (xQueue == NULL || pvBuffer == NULL) {
        return pdFALSE;
    }

    mock_queue_t *q = (mock_queue_t *)xQueue;

    if (q->count == 0) {
        return pdFALSE;
    }

    /* Copy item from head position */
    memcpy(pvBuffer, &q->buffer[q->head * q->item_size], q->item_size);
    q->head = (q->head + 1) % q->capacity;
    q->count--;

    return pdTRUE;
}

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t xQueue)
{
    if (xQueue == NULL) {
        return 0;
    }

    mock_queue_t *q = (mock_queue_t *)xQueue;
    return q->count;
}

BaseType_t xQueueReset(QueueHandle_t xQueue)
{
    if (xQueue == NULL) {
        return pdFAIL;
    }

    mock_queue_t *q = (mock_queue_t *)xQueue;
    q->count = 0;
    q->head = 0;
    q->tail = 0;

    return pdPASS;
}

void vQueueDelete(QueueHandle_t xQueue)
{
    if (xQueue == NULL) {
        return;
    }

    mock_queue_t *q = (mock_queue_t *)xQueue;
    if (q->buffer != NULL) {
        free(q->buffer);
    }
    free(q);
}
