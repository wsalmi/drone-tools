/**
 * @file test_screen_scanner.c
 * @brief Unit tests for screen_scanner module.
 *
 * Tests pagination logic, rendering, empty state, and helper functions.
 * Validates: Requirements 9.3, 9.6, 9.7
 */

#include "unity.h"
#include "screen_scanner.h"
#include "ui_manager.h"
#include "aircraft_registry.h"
#include "hal_display.h"

#include <string.h>
#include <stdio.h>

/* External mock control (from hal_display_mock.c) */
extern void mock_hal_display_reset(void);
extern void mock_hal_display_set_initialized(bool init);
extern uint32_t mock_hal_display_get_draw_text_count(void);
extern uint32_t mock_hal_display_get_draw_rect_count(void);

/* ========================================================================
 * Test Fixtures
 * ======================================================================== */

static aircraft_registry_t s_registry;

void setUp(void)
{
    mock_hal_display_reset();
    mock_hal_display_set_initialized(true);
    ui_manager_init();
    registry_init(&s_registry);
}

void tearDown(void)
{
    ui_manager_deinit();
}

/* ========================================================================
 * Helper: populate registry with N active aircraft
 * ======================================================================== */

static void populate_registry(uint8_t count)
{
    for (uint8_t i = 0; i < count && i < MAX_AIRCRAFT; i++) {
        char id[AIRCRAFT_ID_MAX_LEN];
        snprintf(id, sizeof(id), "AIRCRAFT-%02u", i);

        aircraft_entry_t *entry = registry_find_or_create(&s_registry, id);
        TEST_ASSERT_NOT_NULL(entry);

        entry->protocol = (protocol_type_t)(i % PROTOCOL_UNKNOWN);
        entry->last_rssi_dbm = -50 - (int16_t)i;
        entry->last_seen_utc_ms = 1000;  /* Recent enough to be ACTIVE */
        entry->status = AIRCRAFT_STATUS_ACTIVE;

        entry->relative_pos.valid = true;
        entry->relative_pos.distance_m = 100.0f + (float)(i * 50);
        entry->relative_pos.azimuth_deg = (float)(i * 30 % 360);
    }
}

/* ========================================================================
 * Tests: screen_scanner_get_total_pages
 * ======================================================================== */

void test_total_pages_zero_aircraft(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, screen_scanner_get_total_pages(0));
}

void test_total_pages_one_aircraft(void)
{
    TEST_ASSERT_EQUAL_UINT8(1, screen_scanner_get_total_pages(1));
}

void test_total_pages_exactly_five(void)
{
    TEST_ASSERT_EQUAL_UINT8(1, screen_scanner_get_total_pages(5));
}

void test_total_pages_six_aircraft(void)
{
    TEST_ASSERT_EQUAL_UINT8(2, screen_scanner_get_total_pages(6));
}

void test_total_pages_ten_aircraft(void)
{
    TEST_ASSERT_EQUAL_UINT8(2, screen_scanner_get_total_pages(10));
}

void test_total_pages_eleven_aircraft(void)
{
    TEST_ASSERT_EQUAL_UINT8(3, screen_scanner_get_total_pages(11));
}

void test_total_pages_max_aircraft(void)
{
    TEST_ASSERT_EQUAL_UINT8(7, screen_scanner_get_total_pages(32));
}

/* ========================================================================
 * Tests: screen_scanner_protocol_name
 * ======================================================================== */

void test_protocol_name_elrs(void)
{
    TEST_ASSERT_EQUAL_STRING("ELRS", screen_scanner_protocol_name(PROTOCOL_ELRS));
}

void test_protocol_name_dji(void)
{
    TEST_ASSERT_EQUAL_STRING("DJI", screen_scanner_protocol_name(PROTOCOL_DJI));
}

void test_protocol_name_mavlink(void)
{
    TEST_ASSERT_EQUAL_STRING("MAVLink", screen_scanner_protocol_name(PROTOCOL_MAVLINK));
}

void test_protocol_name_unknown(void)
{
    TEST_ASSERT_EQUAL_STRING("???", screen_scanner_protocol_name(PROTOCOL_UNKNOWN));
}

void test_protocol_name_remoteid(void)
{
    TEST_ASSERT_EQUAL_STRING("RID", screen_scanner_protocol_name(PROTOCOL_REMOTEID));
}

/* ========================================================================
 * Tests: screen_scanner_format_distance
 * ======================================================================== */

void test_format_distance_meters(void)
{
    char buf[12];
    screen_scanner_format_distance(buf, sizeof(buf), 450.0f);
    TEST_ASSERT_EQUAL_STRING("450m", buf);
}

void test_format_distance_exactly_1000m(void)
{
    char buf[12];
    screen_scanner_format_distance(buf, sizeof(buf), 1000.0f);
    TEST_ASSERT_EQUAL_STRING("1000m", buf);
}

void test_format_distance_over_1000m(void)
{
    char buf[12];
    screen_scanner_format_distance(buf, sizeof(buf), 1500.0f);
    TEST_ASSERT_EQUAL_STRING("1.5km", buf);
}

void test_format_distance_zero(void)
{
    char buf[12];
    screen_scanner_format_distance(buf, sizeof(buf), 0.0f);
    TEST_ASSERT_EQUAL_STRING("0m", buf);
}

void test_format_distance_negative(void)
{
    char buf[12];
    screen_scanner_format_distance(buf, sizeof(buf), -1.0f);
    TEST_ASSERT_EQUAL_STRING("N/D", buf);
}

void test_format_distance_null_buf(void)
{
    /* Should not crash */
    screen_scanner_format_distance(NULL, 0, 100.0f);
}

/* ========================================================================
 * Tests: screen_scanner_render — empty state
 * ======================================================================== */

void test_render_empty_registry_shows_message(void)
{
    esp_err_t ret = screen_scanner_render(&s_registry);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    /* Should have drawn the empty message text */
    uint32_t text_count = mock_hal_display_get_draw_text_count();
    TEST_ASSERT_GREATER_THAN(0, text_count);
}

void test_render_null_registry_returns_error(void)
{
    esp_err_t ret = screen_scanner_render(NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
}

/* ========================================================================
 * Tests: screen_scanner_render — with aircraft
 * ======================================================================== */

void test_render_with_3_aircraft(void)
{
    populate_registry(3);

    esp_err_t ret = screen_scanner_render(&s_registry);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    /* Should draw at least: 1 rect (clear content) + 3 row rects + text */
    uint32_t rect_count = mock_hal_display_get_draw_rect_count();
    TEST_ASSERT_GREATER_OR_EQUAL(4, rect_count);
}

void test_render_with_7_aircraft_page_0(void)
{
    populate_registry(7);

    /* Page 0 should show 5 items */
    esp_err_t ret = screen_scanner_render(&s_registry);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    /* 1 clear rect + 5 row rects = at least 6 */
    uint32_t rect_count = mock_hal_display_get_draw_rect_count();
    TEST_ASSERT_GREATER_OR_EQUAL(6, rect_count);
}

void test_render_with_7_aircraft_page_1(void)
{
    populate_registry(7);

    /* Navigate to page 1 */
    ui_manager_handle_key(UI_KEY_RIGHT);

    esp_err_t ret = screen_scanner_render(&s_registry);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    /* Page 1 should show 2 items: 1 clear rect + 2 row rects */
    uint32_t rect_count = mock_hal_display_get_draw_rect_count();
    TEST_ASSERT_GREATER_OR_EQUAL(3, rect_count);
}

void test_render_page_clamped_when_exceeds_max(void)
{
    populate_registry(3);

    /* Navigate forward multiple pages beyond valid range */
    ui_manager_handle_key(UI_KEY_RIGHT);
    ui_manager_handle_key(UI_KEY_RIGHT);
    ui_manager_handle_key(UI_KEY_RIGHT);

    /* Should still render OK (page clamped) */
    esp_err_t ret = screen_scanner_render(&s_registry);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* Pagination */
    RUN_TEST(test_total_pages_zero_aircraft);
    RUN_TEST(test_total_pages_one_aircraft);
    RUN_TEST(test_total_pages_exactly_five);
    RUN_TEST(test_total_pages_six_aircraft);
    RUN_TEST(test_total_pages_ten_aircraft);
    RUN_TEST(test_total_pages_eleven_aircraft);
    RUN_TEST(test_total_pages_max_aircraft);

    /* Protocol name */
    RUN_TEST(test_protocol_name_elrs);
    RUN_TEST(test_protocol_name_dji);
    RUN_TEST(test_protocol_name_mavlink);
    RUN_TEST(test_protocol_name_unknown);
    RUN_TEST(test_protocol_name_remoteid);

    /* Distance formatting */
    RUN_TEST(test_format_distance_meters);
    RUN_TEST(test_format_distance_exactly_1000m);
    RUN_TEST(test_format_distance_over_1000m);
    RUN_TEST(test_format_distance_zero);
    RUN_TEST(test_format_distance_negative);
    RUN_TEST(test_format_distance_null_buf);

    /* Render - empty */
    RUN_TEST(test_render_empty_registry_shows_message);
    RUN_TEST(test_render_null_registry_returns_error);

    /* Render - with aircraft */
    RUN_TEST(test_render_with_3_aircraft);
    RUN_TEST(test_render_with_7_aircraft_page_0);
    RUN_TEST(test_render_with_7_aircraft_page_1);
    RUN_TEST(test_render_page_clamped_when_exceeds_max);

    return UNITY_END();
}
