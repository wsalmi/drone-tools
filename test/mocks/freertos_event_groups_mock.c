/**
 * @file freertos_event_groups_mock.c
 * @brief Mock implementation of FreeRTOS Event Groups for host tests.
 *
 * Provides a simple single-threaded implementation of event groups
 * using a uint32_t bitmask. Suitable for testing pipeline logic
 * without actual RTOS concurrency.
 */

#include "freertos/event_groups.h"
#include <stdlib.h>

/* Internal event group structure */
typedef struct {
    EventBits_t bits;
} mock_event_group_t;

EventGroupHandle_t xEventGroupCreate(void)
{
    mock_event_group_t *eg = (mock_event_group_t *)calloc(1, sizeof(mock_event_group_t));
    if (eg == NULL) {
        return NULL;
    }
    eg->bits = 0;
    return (EventGroupHandle_t)eg;
}

EventBits_t xEventGroupSetBits(EventGroupHandle_t xEventGroup, EventBits_t uxBitsToSet)
{
    if (xEventGroup == NULL) {
        return 0;
    }
    mock_event_group_t *eg = (mock_event_group_t *)xEventGroup;
    EventBits_t prev = eg->bits;
    eg->bits |= uxBitsToSet;
    return prev;
}

EventBits_t xEventGroupWaitBits(EventGroupHandle_t xEventGroup,
                                EventBits_t uxBitsToWaitFor,
                                BaseType_t xClearOnExit,
                                BaseType_t xWaitForAllBits,
                                TickType_t xTicksToWait)
{
    (void)xTicksToWait;

    if (xEventGroup == NULL) {
        return 0;
    }

    mock_event_group_t *eg = (mock_event_group_t *)xEventGroup;
    EventBits_t current = eg->bits;

    bool matched;
    if (xWaitForAllBits == pdTRUE) {
        matched = ((current & uxBitsToWaitFor) == uxBitsToWaitFor);
    } else {
        matched = ((current & uxBitsToWaitFor) != 0);
    }

    if (matched && xClearOnExit == pdTRUE) {
        eg->bits &= ~(current & uxBitsToWaitFor);
    }

    return current;
}

EventBits_t xEventGroupGetBits(EventGroupHandle_t xEventGroup)
{
    if (xEventGroup == NULL) {
        return 0;
    }
    mock_event_group_t *eg = (mock_event_group_t *)xEventGroup;
    return eg->bits;
}

EventBits_t xEventGroupClearBits(EventGroupHandle_t xEventGroup, EventBits_t uxBitsToClear)
{
    if (xEventGroup == NULL) {
        return 0;
    }
    mock_event_group_t *eg = (mock_event_group_t *)xEventGroup;
    EventBits_t prev = eg->bits;
    eg->bits &= ~uxBitsToClear;
    return prev;
}

void vEventGroupDelete(EventGroupHandle_t xEventGroup)
{
    if (xEventGroup != NULL) {
        free(xEventGroup);
    }
}
