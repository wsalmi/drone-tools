/**
 * @file test_aircraft_registry.c
 * @brief Unit tests for the Aircraft Registry module.
 *
 * Tests cover: initialization, find/create, find, status update timeout,
 * active count, eviction policy, and edge cases.
 */

#include "unity.h"
#include "aircraft_registry.h"
#include <string.h>
#include <stdio.h>

static aircraft_registry_t reg;

void setUp(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, registry_init(&reg));
}

void tearDown(void)
{
    /* Nothing to clean — mock mutex is a static dummy */
}

/* ========================================================================
 * Initialization Tests
 * ======================================================================== */

void test_init_clears_all_entries(void)
{
    /* All slots should be unoccupied */
    for (int i = 0; i < MAX_AIRCRAFT; i++) {
        TEST_ASSERT_FALSE(reg.entries[i].slot_occupied);
    }
    TEST_ASSERT_EQUAL_UINT8(0, reg.count);
    TEST_ASSERT_EQUAL_UINT32(0, reg.total_detected);
    TEST_ASSERT_EQUAL_UINT32(0, reg.error_count);
    TEST_ASSERT_NOT_NULL(reg.mutex);
}

void test_init_null_returns_invalid_arg(void)
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, registry_init(NULL));
}

/* ========================================================================
 * Find or Create Tests
 * ======================================================================== */

void test_find_or_create_new_aircraft(void)
{
    aircraft_entry_t *entry = registry_find_or_create(&reg, "DRONE-001");
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL_STRING("DRONE-001", entry->id);
    TEST_ASSERT_TRUE(entry->slot_occupied);
    TEST_ASSERT_EQUAL(AIRCRAFT_STATUS_ACTIVE, entry->status);
    TEST_ASSERT_EQUAL_UINT8(1, reg.count);
    TEST_ASSERT_EQUAL_UINT32(1, reg.total_detected);
}

void test_find_or_create_returns_existing(void)
{
    aircraft_entry_t *first = registry_find_or_create(&reg, "DRONE-001");
    first->last_rssi_dbm = -55;

    aircraft_entry_t *second = registry_find_or_create(&reg, "DRONE-001");
    TEST_ASSERT_EQUAL_PTR(first, second);
    TEST_ASSERT_EQUAL_INT16(-55, second->last_rssi_dbm);
    TEST_ASSERT_EQUAL_UINT8(1, reg.count);
    TEST_ASSERT_EQUAL_UINT32(1, reg.total_detected);
}

void test_find_or_create_multiple_aircraft(void)
{
    aircraft_entry_t *a = registry_find_or_create(&reg, "DRONE-A");
    aircraft_entry_t *b = registry_find_or_create(&reg, "DRONE-B");
    aircraft_entry_t *c = registry_find_or_create(&reg, "DRONE-C");

    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_NOT_EQUAL(a, b);
    TEST_ASSERT_NOT_EQUAL(b, c);
    TEST_ASSERT_EQUAL_UINT8(3, reg.count);
    TEST_ASSERT_EQUAL_UINT32(3, reg.total_detected);
}

void test_find_or_create_null_id_returns_null(void)
{
    TEST_ASSERT_NULL(registry_find_or_create(&reg, NULL));
    TEST_ASSERT_NULL(registry_find_or_create(&reg, ""));
    TEST_ASSERT_NULL(registry_find_or_create(NULL, "ID"));
}

void test_find_or_create_reactivates_out_of_range(void)
{
    aircraft_entry_t *entry = registry_find_or_create(&reg, "DRONE-001");
    entry->last_seen_utc_ms = 1000;

    /* Mark as out of range */
    registry_update_status(&reg, 32000);
    TEST_ASSERT_EQUAL(AIRCRAFT_STATUS_OUT_OF_RANGE, entry->status);

    /* Re-find should reactivate */
    aircraft_entry_t *reactivated = registry_find_or_create(&reg, "DRONE-001");
    TEST_ASSERT_EQUAL_PTR(entry, reactivated);
    TEST_ASSERT_EQUAL(AIRCRAFT_STATUS_ACTIVE, reactivated->status);
}

/* ========================================================================
 * Find Tests
 * ======================================================================== */

void test_find_existing_aircraft(void)
{
    registry_find_or_create(&reg, "DRONE-XYZ");
    aircraft_entry_t *found = registry_find(&reg, "DRONE-XYZ");
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_STRING("DRONE-XYZ", found->id);
}

void test_find_nonexistent_returns_null(void)
{
    registry_find_or_create(&reg, "DRONE-001");
    TEST_ASSERT_NULL(registry_find(&reg, "DRONE-999"));
}

void test_find_null_args_returns_null(void)
{
    TEST_ASSERT_NULL(registry_find(NULL, "ID"));
    TEST_ASSERT_NULL(registry_find(&reg, NULL));
    TEST_ASSERT_NULL(registry_find(&reg, ""));
}

/* ========================================================================
 * Status Update Tests
 * ======================================================================== */

void test_update_status_marks_out_of_range_after_30s(void)
{
    aircraft_entry_t *entry = registry_find_or_create(&reg, "DRONE-001");
    entry->last_seen_utc_ms = 10000;

    /* At 40000 ms: elapsed = 30000 ms — exactly at boundary, NOT yet out of range */
    registry_update_status(&reg, 40000);
    TEST_ASSERT_EQUAL(AIRCRAFT_STATUS_ACTIVE, entry->status);

    /* At 40001 ms: elapsed = 30001 ms — now out of range */
    registry_update_status(&reg, 40001);
    TEST_ASSERT_EQUAL(AIRCRAFT_STATUS_OUT_OF_RANGE, entry->status);
}

void test_update_status_does_not_affect_recent_aircraft(void)
{
    aircraft_entry_t *entry = registry_find_or_create(&reg, "DRONE-001");
    entry->last_seen_utc_ms = 50000;

    registry_update_status(&reg, 60000);
    TEST_ASSERT_EQUAL(AIRCRAFT_STATUS_ACTIVE, entry->status);
}

void test_update_status_null_reg_does_not_crash(void)
{
    /* Should not crash */
    registry_update_status(NULL, 100000);
}

void test_update_status_only_transitions_active_to_oor(void)
{
    aircraft_entry_t *entry = registry_find_or_create(&reg, "DRONE-001");
    entry->last_seen_utc_ms = 1000;

    /* Transition to OUT_OF_RANGE */
    registry_update_status(&reg, 32000);
    TEST_ASSERT_EQUAL(AIRCRAFT_STATUS_OUT_OF_RANGE, entry->status);

    /* Calling again should not change anything (already OOR) */
    registry_update_status(&reg, 99000);
    TEST_ASSERT_EQUAL(AIRCRAFT_STATUS_OUT_OF_RANGE, entry->status);
}

/* ========================================================================
 * Active Count Tests
 * ======================================================================== */

void test_active_count_empty_registry(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, registry_get_active_count(&reg));
}

void test_active_count_all_active(void)
{
    registry_find_or_create(&reg, "A");
    registry_find_or_create(&reg, "B");
    registry_find_or_create(&reg, "C");
    TEST_ASSERT_EQUAL_UINT8(3, registry_get_active_count(&reg));
}

void test_active_count_mixed_status(void)
{
    aircraft_entry_t *a = registry_find_or_create(&reg, "A");
    aircraft_entry_t *b = registry_find_or_create(&reg, "B");
    aircraft_entry_t *c = registry_find_or_create(&reg, "C");

    a->last_seen_utc_ms = 1000;
    b->last_seen_utc_ms = 30000; /* Recent relative to check time 50000 */
    c->last_seen_utc_ms = 1000;

    /* At time 50000: A elapsed=49000 (>30s OOR), B elapsed=20000 (<30s ACTIVE), C elapsed=49000 (>30s OOR) */
    registry_update_status(&reg, 50000);

    TEST_ASSERT_EQUAL(AIRCRAFT_STATUS_OUT_OF_RANGE, a->status);
    TEST_ASSERT_EQUAL(AIRCRAFT_STATUS_ACTIVE, b->status);
    TEST_ASSERT_EQUAL(AIRCRAFT_STATUS_OUT_OF_RANGE, c->status);
    TEST_ASSERT_EQUAL_UINT8(1, registry_get_active_count(&reg));
}

void test_active_count_null_returns_zero(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, registry_get_active_count(NULL));
}

/* ========================================================================
 * Eviction Policy Tests
 * ======================================================================== */

void test_eviction_replaces_oldest_out_of_range(void)
{
    /* Fill all 32 slots */
    for (int i = 0; i < MAX_AIRCRAFT; i++) {
        char id[16];
        snprintf(id, sizeof(id), "DRONE-%02d", i);
        aircraft_entry_t *e = registry_find_or_create(&reg, id);
        TEST_ASSERT_NOT_NULL(e);
        e->last_seen_utc_ms = (uint64_t)(1000 + i * 100);
    }
    TEST_ASSERT_EQUAL_UINT8(MAX_AIRCRAFT, reg.count);

    /* Mark some as OUT_OF_RANGE — DRONE-00 (last_seen=1000) is oldest */
    reg.entries[0].status = AIRCRAFT_STATUS_OUT_OF_RANGE;  /* last_seen=1000 */
    reg.entries[1].status = AIRCRAFT_STATUS_OUT_OF_RANGE;  /* last_seen=1100 */
    reg.entries[2].status = AIRCRAFT_STATUS_OUT_OF_RANGE;  /* last_seen=1200 */

    /* Try to add a new aircraft — should evict DRONE-00 (oldest OOR) */
    aircraft_entry_t *new_entry = registry_find_or_create(&reg, "NEWCOMER");
    TEST_ASSERT_NOT_NULL(new_entry);
    TEST_ASSERT_EQUAL_STRING("NEWCOMER", new_entry->id);
    TEST_ASSERT_EQUAL(AIRCRAFT_STATUS_ACTIVE, new_entry->status);
    TEST_ASSERT_EQUAL_UINT8(MAX_AIRCRAFT, reg.count);

    /* DRONE-00 should no longer be findable */
    TEST_ASSERT_NULL(registry_find(&reg, "DRONE-00"));
}

void test_full_registry_all_active_returns_null(void)
{
    /* Fill all 32 slots with ACTIVE entries */
    for (int i = 0; i < MAX_AIRCRAFT; i++) {
        char id[16];
        snprintf(id, sizeof(id), "ACTIVE-%02d", i);
        aircraft_entry_t *e = registry_find_or_create(&reg, id);
        TEST_ASSERT_NOT_NULL(e);
        e->last_seen_utc_ms = 50000; /* Recent — stays active */
    }

    /* All are ACTIVE, so no eviction possible */
    aircraft_entry_t *result = registry_find_or_create(&reg, "OVERFLOW");
    TEST_ASSERT_NULL(result);
}

/* ========================================================================
 * ID Truncation Test
 * ======================================================================== */

void test_long_id_is_truncated(void)
{
    /* 25-char ID should be truncated to 20 chars */
    const char *long_id = "ABCDEFGHIJKLMNOPQRSTUVWXY";
    aircraft_entry_t *entry = registry_find_or_create(&reg, long_id);
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL_UINT(AIRCRAFT_ID_MAX_LEN - 1, strlen(entry->id));
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* Initialization */
    RUN_TEST(test_init_clears_all_entries);
    RUN_TEST(test_init_null_returns_invalid_arg);

    /* Find or Create */
    RUN_TEST(test_find_or_create_new_aircraft);
    RUN_TEST(test_find_or_create_returns_existing);
    RUN_TEST(test_find_or_create_multiple_aircraft);
    RUN_TEST(test_find_or_create_null_id_returns_null);
    RUN_TEST(test_find_or_create_reactivates_out_of_range);

    /* Find */
    RUN_TEST(test_find_existing_aircraft);
    RUN_TEST(test_find_nonexistent_returns_null);
    RUN_TEST(test_find_null_args_returns_null);

    /* Status Update */
    RUN_TEST(test_update_status_marks_out_of_range_after_30s);
    RUN_TEST(test_update_status_does_not_affect_recent_aircraft);
    RUN_TEST(test_update_status_null_reg_does_not_crash);
    RUN_TEST(test_update_status_only_transitions_active_to_oor);

    /* Active Count */
    RUN_TEST(test_active_count_empty_registry);
    RUN_TEST(test_active_count_all_active);
    RUN_TEST(test_active_count_mixed_status);
    RUN_TEST(test_active_count_null_returns_zero);

    /* Eviction */
    RUN_TEST(test_eviction_replaces_oldest_out_of_range);
    RUN_TEST(test_full_registry_all_active_returns_null);

    /* Edge Cases */
    RUN_TEST(test_long_id_is_truncated);

    return UNITY_END();
}
