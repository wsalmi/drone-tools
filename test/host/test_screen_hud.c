/**
 * @file test_screen_hud.c
 * @brief Unit tests for Dynamic HUD Dashboard screen.
 */

#include "unity.h"
#include "screen_hud.h"
#include "aircraft_registry.h"
#include "ui_manager.h"
#include "hal_display.h"

void setUp(void)
{
    hal_display_init();
    ui_manager_init();
    screen_hud_init();
}

void tearDown(void)
{
    screen_hud_deinit();
    ui_manager_deinit();
    hal_display_deinit();
}

void test_screen_hud_init_deinit(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, screen_hud_init());
    TEST_ASSERT_EQUAL(ESP_OK, screen_hud_deinit());
}

void test_screen_hud_render_empty(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, screen_hud_render(NULL));
}

void test_screen_hud_render_with_targets(void)
{
    aircraft_registry_t reg;
    TEST_ASSERT_EQUAL(ESP_OK, registry_init(&reg));

    aircraft_entry_t *entry = registry_find_or_create(&reg, "TEST-DRONE-01");
    TEST_ASSERT_NOT_NULL(entry);
    entry->protocol = PROTOCOL_REMOTEID;
    entry->status = AIRCRAFT_STATUS_ACTIVE;
    entry->last_rssi_dbm = -65;
    entry->last_telemetry.lat = -23.550520;
    entry->last_telemetry.lon = -46.633309;
    entry->last_telemetry.altitude_m = 120.0f;
    entry->last_telemetry.speed_ms = 15.0f;
    entry->last_telemetry.battery_pct = 85.0f;
    entry->last_telemetry.has_position = true;
    entry->last_telemetry.has_altitude = true;
    entry->last_telemetry.has_speed = true;
    entry->last_telemetry.has_battery = true;
    entry->relative_pos.distance_m = 450.0f;
    entry->relative_pos.azimuth_deg = 90.0f;
    entry->relative_pos.valid = true;

    TEST_ASSERT_EQUAL(ESP_OK, screen_hud_render(&reg));
}

void test_screen_hud_key_handling(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, screen_hud_handle_key(UI_KEY_UP));
    TEST_ASSERT_EQUAL(ESP_OK, screen_hud_handle_key(UI_KEY_DOWN));
    TEST_ASSERT_EQUAL(ESP_OK, screen_hud_handle_key(UI_KEY_BACK));
    TEST_ASSERT_EQUAL(UI_SCREEN_MAIN_MENU, ui_manager_get_current_screen());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_screen_hud_init_deinit);
    RUN_TEST(test_screen_hud_render_empty);
    RUN_TEST(test_screen_hud_render_with_targets);
    RUN_TEST(test_screen_hud_key_handling);
    return UNITY_END();
}
