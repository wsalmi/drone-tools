/**
 * @file event_groups.h
 * @brief Mock of FreeRTOS event_groups.h for host-based testing.
 *
 * Provides stub types and functions for event groups to allow
 * compilation and testing of firmware modules on host.
 */

#ifndef FREERTOS_EVENT_GROUPS_MOCK_H
#define FREERTOS_EVENT_GROUPS_MOCK_H

#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/* EventGroupHandle_t is an opaque pointer */
typedef void *EventGroupHandle_t;

/* EventBits_t matches the FreeRTOS type (typically 24 usable bits) */
typedef uint32_t EventBits_t;

/**
 * @brief Create a new event group.
 * @return Handle to the event group, or NULL on failure.
 */
EventGroupHandle_t xEventGroupCreate(void);

/**
 * @brief Set bits in an event group.
 * @param xEventGroup  Handle to the event group.
 * @param uxBitsToSet  Bits to set.
 * @return The event group value before bits were set.
 */
EventBits_t xEventGroupSetBits(EventGroupHandle_t xEventGroup, EventBits_t uxBitsToSet);

/**
 * @brief Wait for bits to be set in an event group.
 * @param xEventGroup      Handle to the event group.
 * @param uxBitsToWaitFor  Bits to wait for.
 * @param xClearOnExit     If pdTRUE, clear the waited bits on return.
 * @param xWaitForAllBits  If pdTRUE, wait for ALL bits; else any bit.
 * @param xTicksToWait     Maximum ticks to wait.
 * @return The event group value when bits were matched or timeout occurred.
 */
EventBits_t xEventGroupWaitBits(EventGroupHandle_t xEventGroup,
                                EventBits_t uxBitsToWaitFor,
                                BaseType_t xClearOnExit,
                                BaseType_t xWaitForAllBits,
                                TickType_t xTicksToWait);

/**
 * @brief Get the current value of an event group.
 * @param xEventGroup  Handle to the event group.
 * @return Current event bits value.
 */
EventBits_t xEventGroupGetBits(EventGroupHandle_t xEventGroup);

/**
 * @brief Clear bits in an event group.
 * @param xEventGroup    Handle to the event group.
 * @param uxBitsToClear  Bits to clear.
 * @return The event group value before the bits were cleared.
 */
EventBits_t xEventGroupClearBits(EventGroupHandle_t xEventGroup, EventBits_t uxBitsToClear);

/**
 * @brief Delete an event group.
 * @param xEventGroup  Handle to the event group to delete.
 */
void vEventGroupDelete(EventGroupHandle_t xEventGroup);

#ifdef __cplusplus
}
#endif

#endif /* FREERTOS_EVENT_GROUPS_MOCK_H */
