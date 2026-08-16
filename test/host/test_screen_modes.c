/**
 * @file test_screen_modes.c
 * @brief Unit tests for Modes & Sensors quick toggles screen.
 */

#include "unity.h"
#include "screen_modes.h"
#include "ui_manager.h"
#include "hal_display.h"

void setUp(void)
{
    hal_display_init();
    ui_manager_init();
    screen_modes_init();
}

void tearDown(void)
{
    screen_modes_deinit();
    ui_manager_deinit();
    hal_display_deinit();
}

void test_screen_modes_init_deinit(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, screen_modes_init());
    TEST_ASSERT_EQUAL(ESP_OK, screen_modes_deinit());
}

void test_screen_modes_render(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, screen_modes_render());
}

void test_screen_modes_toggle(void)
{
    bool initial_gps = screen_modes_is_enabled(MODE_ITEM_GPS);
    screen_modes_set_enabled(MODE_ITEM_GPS, !initial_gps);
    TEST_ASSERT_EQUAL(!initial_gps, screen_modes_is_enabled(MODE_ITEM_GPS));

    /* Test key toggle with ENTER */
    TEST_ASSERT_EQUAL(ESP_OK, screen_modes_handle_key(UI_KEY_ENTER));
    TEST_ASSERT_EQUAL(ESP_OK, screen_modes_render());
}

void test_screen_modes_navigation(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, screen_modes_handle_key(UI_KEY_DOWN));
    TEST_ASSERT_EQUAL(ESP_OK, screen_modes_handle_key(UI_KEY_UP));
    TEST_ASSERT_EQUAL(ESP_OK, screen_modes_handle_key(UI_KEY_BACK));
    TEST_ASSERT_EQUAL(UI_SCREEN_MAIN_MENU, ui_manager_get_current_screen());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_screen_modes_init_deinit);
    RUN_TEST(test_screen_modes_render);
    RUN_TEST(test_screen_modes_toggle);
    RUN_TEST(test_screen_modes_navigation);
    return UNITY_END();
}
