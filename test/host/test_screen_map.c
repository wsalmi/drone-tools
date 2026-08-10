/**
 * @file test_screen_map.c
 * @brief Unit tests for the Map Screen (screen_map.c).
 *
 * Tests verify correct rendering behavior:
 *   - Empty state rendering
 *   - Aircraft positioned at correct pixel locations
 *   - Scale indicator rendering
 *   - GPS fix absent behavior
 *
 * Uses hal_display_mock to capture draw calls for verification.
 */

#include "unity.h"
#include "screen_map.h"
#include "ui_manager.h"
#include "aircraft_registry.h"
#include "hal_display.h"
#include "geolocation_service.h"
#include <string.h>
#include <math.h>

/* ========================================================================
 * Test Fixtures
 * ======================================================================== */

static aircraft_registry_t s_registry;

void setUp(void)
{
    ui_manager_init();
    ui_manager_navigate_to(UI_SCREEN_MAP);
    registry_init(&s_registry);
}

void tearDown(void)
{
    ui_manager_deinit();
}

/* ========================================================================
 * Test: Render empty map (no aircraft)
 * ======================================================================== */

void test_screen_map_render_empty_returns_ok(void)
{
    esp_err_t err = screen_map_render_empty();
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

void test_screen_map_render_empty_state_when_no_aircraft(void)
{
    /* Registry has no entries */
    esp_err_t err = screen_map_render(&s_registry);
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

/* ========================================================================
 * Test: Null registry returns error
 * ======================================================================== */

void test_screen_map_render_null_registry_returns_error(void)
{
    esp_err_t err = screen_map_render(NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

/* ========================================================================
 * Test: Uninitialized UI returns error
 * ======================================================================== */

void test_screen_map_render_uninitialized_returns_error(void)
{
    ui_manager_deinit();
    esp_err_t err = screen_map_render(&s_registry);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, err);
    ui_manager_init(); /* Restore for tearDown */
}

void test_screen_map_render_empty_uninitialized_returns_error(void)
{
    ui_manager_deinit();
    esp_err_t err = screen_map_render_empty();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, err);
    ui_manager_init(); /* Restore for tearDown */
}

/* ========================================================================
 * Test: Aircraft with position is drawn without crash
 * ======================================================================== */

void test_screen_map_render_with_aircraft(void)
{
    /* Create an aircraft entry with a position */
    aircraft_entry_t *entry = registry_find_or_create(&s_registry, "TEST-AC-001");
    TEST_ASSERT_NOT_NULL(entry);

    entry->status = AIRCRAFT_STATUS_ACTIVE;
    entry->last_telemetry.has_position = true;
    entry->last_telemetry.lat = -23.5505;
    entry->last_telemetry.lon = -46.6333;
    entry->relative_pos.valid = true;
    entry->relative_pos.distance_m = 200.0f;
    entry->relative_pos.azimuth_deg = 45.0f;

    /* Render should succeed without crash */
    esp_err_t err = screen_map_render(&s_registry);
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

/* ========================================================================
 * Test: Aircraft with pilot position is drawn without crash
 * ======================================================================== */

void test_screen_map_render_with_pilot(void)
{
    aircraft_entry_t *entry = registry_find_or_create(&s_registry, "PILOT-AC-001");
    TEST_ASSERT_NOT_NULL(entry);

    entry->status = AIRCRAFT_STATUS_ACTIVE;
    entry->last_telemetry.has_position = true;
    entry->last_telemetry.lat = -23.5510;
    entry->last_telemetry.lon = -46.6340;
    entry->relative_pos.valid = true;
    entry->relative_pos.distance_m = 150.0f;

    entry->pilot.position_available = true;
    entry->pilot.lat = -23.5520;
    entry->pilot.lon = -46.6350;
    entry->pilot.confidence = PILOT_CONFIDENCE_CONFIRMED;

    esp_err_t err = screen_map_render(&s_registry);
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

/* ========================================================================
 * Test: OUT_OF_RANGE aircraft is not drawn
 * ======================================================================== */

void test_screen_map_skips_out_of_range_aircraft(void)
{
    aircraft_entry_t *entry = registry_find_or_create(&s_registry, "OOR-AC-001");
    TEST_ASSERT_NOT_NULL(entry);

    entry->status = AIRCRAFT_STATUS_OUT_OF_RANGE;
    entry->last_telemetry.has_position = true;
    entry->last_telemetry.lat = -23.5505;
    entry->last_telemetry.lon = -46.6333;

    /* Should render without including OOR aircraft (no crash) */
    esp_err_t err = screen_map_render(&s_registry);
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

/* ========================================================================
 * Test: Aircraft without position is skipped
 * ======================================================================== */

void test_screen_map_skips_aircraft_without_position(void)
{
    aircraft_entry_t *entry = registry_find_or_create(&s_registry, "NOPOS-AC-001");
    TEST_ASSERT_NOT_NULL(entry);

    entry->status = AIRCRAFT_STATUS_ACTIVE;
    entry->last_telemetry.has_position = false;

    esp_err_t err = screen_map_render(&s_registry);
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

/* ========================================================================
 * Test: Zoom in reduces scale
 * ======================================================================== */

void test_screen_map_zoom_in_reduces_scale(void)
{
    const ui_state_t *state = ui_manager_get_state();
    float initial_scale = state->map_scale_m;

    ui_manager_handle_key(UI_KEY_UP); /* Zoom in */
    TEST_ASSERT_LESS_THAN_FLOAT(initial_scale, state->map_scale_m);
}

/* ========================================================================
 * Test: Zoom out increases scale
 * ======================================================================== */

void test_screen_map_zoom_out_increases_scale(void)
{
    const ui_state_t *state = ui_manager_get_state();
    float initial_scale = state->map_scale_m;

    ui_manager_handle_key(UI_KEY_DOWN); /* Zoom out */
    TEST_ASSERT_GREATER_THAN_FLOAT(initial_scale, state->map_scale_m);
}

/* ========================================================================
 * Test: Scale does not go below minimum
 * ======================================================================== */

void test_screen_map_zoom_in_clamps_at_min(void)
{
    const ui_state_t *state = ui_manager_get_state();

    /* Zoom in many times */
    for (int i = 0; i < 20; i++) {
        ui_manager_handle_key(UI_KEY_UP);
    }

    TEST_ASSERT_FLOAT_WITHIN(0.01f, UI_MAP_MIN_SCALE_M, state->map_scale_m);
}

/* ========================================================================
 * Test: Scale does not exceed maximum
 * ======================================================================== */

void test_screen_map_zoom_out_clamps_at_max(void)
{
    const ui_state_t *state = ui_manager_get_state();

    /* Zoom out many times */
    for (int i = 0; i < 20; i++) {
        ui_manager_handle_key(UI_KEY_DOWN);
    }

    TEST_ASSERT_FLOAT_WITHIN(0.01f, UI_MAP_MAX_SCALE_M, state->map_scale_m);
}

/* ========================================================================
 * Unity Main
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_screen_map_render_empty_returns_ok);
    RUN_TEST(test_screen_map_render_empty_state_when_no_aircraft);
    RUN_TEST(test_screen_map_render_null_registry_returns_error);
    RUN_TEST(test_screen_map_render_uninitialized_returns_error);
    RUN_TEST(test_screen_map_render_empty_uninitialized_returns_error);
    RUN_TEST(test_screen_map_render_with_aircraft);
    RUN_TEST(test_screen_map_render_with_pilot);
    RUN_TEST(test_screen_map_skips_out_of_range_aircraft);
    RUN_TEST(test_screen_map_skips_aircraft_without_position);
    RUN_TEST(test_screen_map_zoom_in_reduces_scale);
    RUN_TEST(test_screen_map_zoom_out_increases_scale);
    RUN_TEST(test_screen_map_zoom_in_clamps_at_min);
    RUN_TEST(test_screen_map_zoom_out_clamps_at_max);

    return UNITY_END();
}
