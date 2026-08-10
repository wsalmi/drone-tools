/**
 * @file aircraft_registry.c
 * @brief Aircraft Registry implementation.
 *
 * Thread-safe registry for tracking up to MAX_AIRCRAFT (32) detected drones.
 * Uses FreeRTOS mutex for concurrent access protection.
 *
 * Eviction policy: when registry is full and a new aircraft is detected,
 * the oldest OUT_OF_RANGE entry is replaced. If all entries are ACTIVE,
 * the new aircraft cannot be registered (returns NULL).
 *
 * Validates: Requirements 1.1, 1.2, 8.6, 13.3, 13.6
 */

#include "aircraft_registry.h"
#include <string.h>

/* ========================================================================
 * Internal Helpers
 * ======================================================================== */

/**
 * @brief Find an empty (unoccupied) slot in the registry.
 * @return Index of empty slot, or -1 if none available.
 */
static int find_empty_slot(const aircraft_registry_t *reg)
{
    for (int i = 0; i < MAX_AIRCRAFT; i++) {
        if (!reg->entries[i].slot_occupied) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Find the oldest OUT_OF_RANGE entry for eviction.
 *
 * "Oldest" is determined by last_seen_utc_ms — the entry that was
 * last seen the longest time ago is evicted first.
 *
 * @return Index of the eviction candidate, or -1 if no OUT_OF_RANGE entry.
 */
static int find_eviction_candidate(const aircraft_registry_t *reg)
{
    int candidate = -1;
    uint64_t oldest_time = UINT64_MAX;

    for (int i = 0; i < MAX_AIRCRAFT; i++) {
        if (reg->entries[i].slot_occupied &&
            reg->entries[i].status == AIRCRAFT_STATUS_OUT_OF_RANGE) {
            if (reg->entries[i].last_seen_utc_ms < oldest_time) {
                oldest_time = reg->entries[i].last_seen_utc_ms;
                candidate = i;
            }
        }
    }
    return candidate;
}

/**
 * @brief Clear a slot and initialize it for a new aircraft.
 */
static void init_slot(aircraft_entry_t *entry, const char *id)
{
    memset(entry, 0, sizeof(aircraft_entry_t));
    strncpy(entry->id, id, AIRCRAFT_ID_MAX_LEN - 1);
    entry->id[AIRCRAFT_ID_MAX_LEN - 1] = '\0';
    entry->slot_occupied = true;
    entry->status = AIRCRAFT_STATUS_ACTIVE;
    entry->protocol = PROTOCOL_UNKNOWN;
    entry->protocol_confidence = CONFIDENCE_LOW;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

esp_err_t registry_init(aircraft_registry_t *reg)
{
    if (reg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(reg->entries, 0, sizeof(reg->entries));
    reg->count = 0;
    reg->total_detected = 0;
    reg->error_count = 0;

    reg->mutex = xSemaphoreCreateMutex();
    if (reg->mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

aircraft_entry_t *registry_find_or_create(aircraft_registry_t *reg, const char *id)
{
    if (reg == NULL || id == NULL || id[0] == '\0') {
        return NULL;
    }

    if (xSemaphoreTake(reg->mutex, portMAX_DELAY) != pdTRUE) {
        return NULL;
    }

    /* Search for existing entry */
    for (int i = 0; i < MAX_AIRCRAFT; i++) {
        if (reg->entries[i].slot_occupied &&
            strncmp(reg->entries[i].id, id, AIRCRAFT_ID_MAX_LEN) == 0) {
            /* Found existing — reactivate if OUT_OF_RANGE */
            if (reg->entries[i].status == AIRCRAFT_STATUS_OUT_OF_RANGE) {
                reg->entries[i].status = AIRCRAFT_STATUS_ACTIVE;
            }
            xSemaphoreGive(reg->mutex);
            return &reg->entries[i];
        }
    }

    /* Not found — try to allocate a new slot */
    int slot = find_empty_slot(reg);

    if (slot < 0) {
        /* No empty slots — try eviction of oldest OUT_OF_RANGE */
        slot = find_eviction_candidate(reg);
        if (slot < 0) {
            /* All slots are ACTIVE, cannot allocate */
            xSemaphoreGive(reg->mutex);
            return NULL;
        }
        /* Evicting: decrement count since we're replacing an occupied slot */
        reg->count--;
    }

    /* Initialize the new slot */
    init_slot(&reg->entries[slot], id);
    reg->count++;
    reg->total_detected++;

    xSemaphoreGive(reg->mutex);
    return &reg->entries[slot];
}

aircraft_entry_t *registry_find(aircraft_registry_t *reg, const char *id)
{
    if (reg == NULL || id == NULL || id[0] == '\0') {
        return NULL;
    }

    if (xSemaphoreTake(reg->mutex, portMAX_DELAY) != pdTRUE) {
        return NULL;
    }

    for (int i = 0; i < MAX_AIRCRAFT; i++) {
        if (reg->entries[i].slot_occupied &&
            strncmp(reg->entries[i].id, id, AIRCRAFT_ID_MAX_LEN) == 0) {
            xSemaphoreGive(reg->mutex);
            return &reg->entries[i];
        }
    }

    xSemaphoreGive(reg->mutex);
    return NULL;
}

void registry_update_status(aircraft_registry_t *reg, uint64_t current_time_ms)
{
    if (reg == NULL) {
        return;
    }

    if (xSemaphoreTake(reg->mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    for (int i = 0; i < MAX_AIRCRAFT; i++) {
        if (reg->entries[i].slot_occupied &&
            reg->entries[i].status == AIRCRAFT_STATUS_ACTIVE) {
            uint64_t elapsed = current_time_ms - reg->entries[i].last_seen_utc_ms;
            if (elapsed > AIRCRAFT_TIMEOUT_MS) {
                reg->entries[i].status = AIRCRAFT_STATUS_OUT_OF_RANGE;
            }
        }
    }

    xSemaphoreGive(reg->mutex);
}

uint8_t registry_get_active_count(const aircraft_registry_t *reg)
{
    if (reg == NULL) {
        return 0;
    }

    /* Cast away const for mutex — semantically this is a read operation */
    aircraft_registry_t *mutable_reg = (aircraft_registry_t *)reg;

    if (xSemaphoreTake(mutable_reg->mutex, portMAX_DELAY) != pdTRUE) {
        return 0;
    }

    uint8_t active = 0;
    for (int i = 0; i < MAX_AIRCRAFT; i++) {
        if (reg->entries[i].slot_occupied &&
            reg->entries[i].status == AIRCRAFT_STATUS_ACTIVE) {
            active++;
        }
    }

    xSemaphoreGive(mutable_reg->mutex);
    return active;
}
