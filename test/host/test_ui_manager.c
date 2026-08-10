/**
 * @file test_ui_manager.c
 * @brief Unit tests for UI Manager — navigation, state, status bar, notifications.
 *
 * Validates: Requirements 9.1, 9.2, 9.5, 9.6, 10.6
 */

#include "unity.h"
#include "ui_manager.h"
#include "hal_display.h"
#include <string.h>

/* ========================================================================
 * External mock control functions
 * ======================================================================== */
extern void mock_hal_display_reset(void);
extern void mock_hal_display_set_initialized(bool init);
extern uint32_t mock_hal_display_get_draw_text_count(void);
extern uint32_t mock_hal_display_get_draw_rect_count(void);

/* ========================================================================
 * Test Setup / Teardown
 * ======================================================================== */

void setUp(void)
{
    mock_hal_display_reset();
    mock_hal_display_set_initialized(true);
    ui_manager_init();
}

void tearDown(void)
{
    ui_manager_deinit();
}

/* ========================================================================
 * Initialization Tests
 * ======================================================================== */

void test_init_sets_default_state(void)
{
    const ui_state_t *state = ui_manager_get_state();
    TEST_ASSERT_NOT_NULL(state);
    TEST_ASSERT_EQUAL(UI_SCREEN_MAIN_MENU, state->current_screen);
    TEST_ASSERT_EQUAL(0, state->scanner_page);
    TEST_ASSERT_EQUAL(0, state->selected_aircraft_idx);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, UI_MAP_DEFAULT_SCALE_M, state->map_scale_m);
    TEST_ASSERT_FALSE(state->notification_visible);
    TEST_ASSERT_EQUAL(0, state->aircraft_count);
    TEST_ASSERT_FALSE(state->gps_fix_valid);
    TEST_ASSERT_FALSE(state->sd_available);
    TEST_ASSERT_TRUE(state->initialized);
}

void test_init_modules_all_inactive(void)
{
    const ui_state_t *state = ui_manager_get_state();
    for (int i = 0; i < UI_MODULE_COUNT; i++) {
        TEST_ASSERT_EQUAL(HAL_STATUS_INACTIVE, state->module_status[i]);
    }
}

/* ========================================================================
 * Navigation Tests
 * ======================================================================== */

void test_navigate_to_scanner(void)
{
    esp_err_t err = ui_manager_navigate_to(UI_SCREEN_SCANNER);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(UI_SCREEN_SCANNER, ui_manager_get_current_screen());
}

void test_navigate_to_invalid_screen(void)
{
    esp_err_t err = ui_manager_navigate_to(UI_SCREEN_COUNT);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
    TEST_ASSERT_EQUAL(UI_SCREEN_MAIN_MENU, ui_manager_get_current_screen());
}

void test_navigate_saves_previous_screen(void)
{
    ui_manager_navigate_to(UI_SCREEN_SCANNER);
    const ui_state_t *state = ui_manager_get_state();
    TEST_ASSERT_EQUAL(UI_SCREEN_MAIN_MENU, state->previous_screen);

    ui_manager_navigate_to(UI_SCREEN_MAP);
    TEST_ASSERT_EQUAL(UI_SCREEN_SCANNER, state->previous_screen);
}

/* ========================================================================
 * Main Menu Key Handling Tests
 * ======================================================================== */

void test_main_menu_down_key_increments_selection(void)
{
    const ui_state_t *state = ui_manager_get_state();
    TEST_ASSERT_EQUAL(0, state->selected_aircraft_idx);

    ui_manager_handle_key(UI_KEY_DOWN);
    TEST_ASSERT_EQUAL(1, state->selected_aircraft_idx);

    ui_manager_handle_key(UI_KEY_DOWN);
    TEST_ASSERT_EQUAL(2, state->selected_aircraft_idx);
}

void test_main_menu_up_key_decrements_selection(void)
{
    /* Move down first */
    ui_manager_handle_key(UI_KEY_DOWN);
    ui_manager_handle_key(UI_KEY_DOWN);

    const ui_state_t *state = ui_manager_get_state();
    TEST_ASSERT_EQUAL(2, state->selected_aircraft_idx);

    ui_manager_handle_key(UI_KEY_UP);
    TEST_ASSERT_EQUAL(1, state->selected_aircraft_idx);
}

void test_main_menu_enter_navigates_to_selected(void)
{
    /* Select item 1 (MAP screen) */
    ui_manager_handle_key(UI_KEY_DOWN);
    ui_manager_handle_key(UI_KEY_ENTER);
    TEST_ASSERT_EQUAL(UI_SCREEN_MAP, ui_manager_get_current_screen());
}

void test_main_menu_enter_navigates_to_scanner(void)
{
    /* Item 0 = Scanner */
    ui_manager_handle_key(UI_KEY_ENTER);
    TEST_ASSERT_EQUAL(UI_SCREEN_SCANNER, ui_manager_get_current_screen());
}

void test_main_menu_up_wraps_to_bottom(void)
{
    /* At index 0, pressing up should wrap to last item */
    ui_manager_handle_key(UI_KEY_UP);
    const ui_state_t *state = ui_manager_get_state();
    /* Should wrap to UI_SCREEN_COUNT - 2 (excluding MAIN_MENU itself) */
    TEST_ASSERT_EQUAL(UI_SCREEN_COUNT - 2, state->selected_aircraft_idx);
}

/* ========================================================================
 * Scanner Key Handling Tests
 * ======================================================================== */

void test_scanner_back_returns_to_menu(void)
{
    ui_manager_navigate_to(UI_SCREEN_SCANNER);
    ui_manager_handle_key(UI_KEY_BACK);
    TEST_ASSERT_EQUAL(UI_SCREEN_MAIN_MENU, ui_manager_get_current_screen());
}

void test_scanner_page_right_increments(void)
{
    ui_manager_navigate_to(UI_SCREEN_SCANNER);
    const ui_state_t *state = ui_manager_get_state();
    TEST_ASSERT_EQUAL(0, state->scanner_page);

    ui_manager_handle_key(UI_KEY_RIGHT);
    TEST_ASSERT_EQUAL(1, state->scanner_page);
}

void test_scanner_page_left_does_not_underflow(void)
{
    ui_manager_navigate_to(UI_SCREEN_SCANNER);
    ui_manager_handle_key(UI_KEY_LEFT);
    const ui_state_t *state = ui_manager_get_state();
    TEST_ASSERT_EQUAL(0, state->scanner_page);
}

void test_scanner_selection_bounded(void)
{
    ui_manager_navigate_to(UI_SCREEN_SCANNER);
    const ui_state_t *state = ui_manager_get_state();

    /* Move to last item */
    for (int i = 0; i < UI_PAGE_SIZE; i++) {
        ui_manager_handle_key(UI_KEY_DOWN);
    }
    /* Should not exceed UI_PAGE_SIZE - 1 */
    TEST_ASSERT_EQUAL(UI_PAGE_SIZE - 1, state->selected_aircraft_idx);
}

/* ========================================================================
 * Map Key Handling Tests
 * ======================================================================== */

void test_map_zoom_in(void)
{
    ui_manager_navigate_to(UI_SCREEN_MAP);
    const ui_state_t *state = ui_manager_get_state();
    float initial = state->map_scale_m;

    ui_manager_handle_key(UI_KEY_UP);
    TEST_ASSERT_TRUE(state->map_scale_m < initial);
}

void test_map_zoom_out(void)
{
    ui_manager_navigate_to(UI_SCREEN_MAP);
    const ui_state_t *state = ui_manager_get_state();
    float initial = state->map_scale_m;

    ui_manager_handle_key(UI_KEY_DOWN);
    TEST_ASSERT_TRUE(state->map_scale_m > initial);
}

void test_map_zoom_in_clamps_to_min(void)
{
    ui_manager_navigate_to(UI_SCREEN_MAP);
    const ui_state_t *state = ui_manager_get_state();

    /* Zoom in many times */
    for (int i = 0; i < 20; i++) {
        ui_manager_handle_key(UI_KEY_UP);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.01f, UI_MAP_MIN_SCALE_M, state->map_scale_m);
}

void test_map_zoom_out_clamps_to_max(void)
{
    ui_manager_navigate_to(UI_SCREEN_MAP);
    const ui_state_t *state = ui_manager_get_state();

    /* Zoom out many times */
    for (int i = 0; i < 20; i++) {
        ui_manager_handle_key(UI_KEY_DOWN);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.01f, UI_MAP_MAX_SCALE_M, state->map_scale_m);
}

/* ========================================================================
 * Notification Tests
 * ======================================================================== */

void test_show_notification(void)
{
    esp_err_t err = ui_manager_show_notification("Nova aeronave!", 0);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    const ui_state_t *state = ui_manager_get_state();
    TEST_ASSERT_TRUE(state->notification_visible);
    TEST_ASSERT_EQUAL_STRING("Nova aeronave!", state->notification_text);
}

void test_show_notification_null_text_fails(void)
{
    esp_err_t err = ui_manager_show_notification(NULL, 0);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

void test_notification_expires(void)
{
    ui_manager_show_notification("Teste", 0);

    /* First update: resolves the absolute expire time */
    ui_manager_update_notifications(10000);
    const ui_state_t *state = ui_manager_get_state();
    TEST_ASSERT_TRUE(state->notification_visible);

    /* After duration has passed */
    ui_manager_update_notifications(10000 + UI_NOTIFICATION_DURATION_MS + 1);
    TEST_ASSERT_FALSE(state->notification_visible);
}

void test_dismiss_notification(void)
{
    ui_manager_show_notification("Dismissable", 0);
    const ui_state_t *state = ui_manager_get_state();
    TEST_ASSERT_TRUE(state->notification_visible);

    ui_manager_dismiss_notification();
    TEST_ASSERT_FALSE(state->notification_visible);
    TEST_ASSERT_EQUAL_STRING("", state->notification_text);
}

void test_notification_replaced(void)
{
    ui_manager_show_notification("First", 0);
    ui_manager_show_notification("Second", 0);

    const ui_state_t *state = ui_manager_get_state();
    TEST_ASSERT_TRUE(state->notification_visible);
    TEST_ASSERT_EQUAL_STRING("Second", state->notification_text);
}

void test_notification_custom_duration(void)
{
    ui_manager_show_notification("Custom", 5000);

    /* First update: resolves expire time */
    ui_manager_update_notifications(1000);
    const ui_state_t *state = ui_manager_get_state();
    TEST_ASSERT_TRUE(state->notification_visible);

    /* After 5s */
    ui_manager_update_notifications(1000 + 5001);
    TEST_ASSERT_FALSE(state->notification_visible);
}

/* ========================================================================
 * Status Bar State Tests
 * ======================================================================== */

void test_update_module_status(void)
{
    ui_manager_update_module_status(
        HAL_STATUS_ACTIVE,
        HAL_STATUS_INACTIVE,
        HAL_STATUS_ERROR,
        HAL_STATUS_ACTIVE,
        HAL_STATUS_ACTIVE
    );

    const ui_state_t *state = ui_manager_get_state();
    TEST_ASSERT_EQUAL(HAL_STATUS_ACTIVE, state->module_status[UI_MODULE_IDX_LORA]);
    TEST_ASSERT_EQUAL(HAL_STATUS_INACTIVE, state->module_status[UI_MODULE_IDX_NRF24]);
    TEST_ASSERT_EQUAL(HAL_STATUS_ERROR, state->module_status[UI_MODULE_IDX_SDR]);
    TEST_ASSERT_EQUAL(HAL_STATUS_ACTIVE, state->module_status[UI_MODULE_IDX_GPS]);
    TEST_ASSERT_EQUAL(HAL_STATUS_ACTIVE, state->module_status[UI_MODULE_IDX_SD]);
    TEST_ASSERT_TRUE(state->gps_fix_valid);
    TEST_ASSERT_TRUE(state->sd_available);
}

void test_update_aircraft_count(void)
{
    ui_manager_update_aircraft_count(7);
    const ui_state_t *state = ui_manager_get_state();
    TEST_ASSERT_EQUAL(7, state->aircraft_count);
}

void test_update_gps_fix(void)
{
    ui_manager_update_gps_fix(true);
    const ui_state_t *state = ui_manager_get_state();
    TEST_ASSERT_TRUE(state->gps_fix_valid);

    ui_manager_update_gps_fix(false);
    TEST_ASSERT_FALSE(state->gps_fix_valid);
}

void test_update_sd_status(void)
{
    ui_manager_update_sd_status(true);
    const ui_state_t *state = ui_manager_get_state();
    TEST_ASSERT_TRUE(state->sd_available);

    ui_manager_update_sd_status(false);
    TEST_ASSERT_FALSE(state->sd_available);
}

/* ========================================================================
 * Render Tests (validate calls to display API)
 * ======================================================================== */

void test_render_status_bar_calls_display(void)
{
    ui_manager_update_module_status(
        HAL_STATUS_ACTIVE, HAL_STATUS_INACTIVE,
        HAL_STATUS_ERROR, HAL_STATUS_ACTIVE, HAL_STATUS_ACTIVE
    );
    ui_manager_update_aircraft_count(3);

    esp_err_t err = ui_manager_render_status_bar();
    TEST_ASSERT_EQUAL(ESP_OK, err);

    /* Should have drawn: 1 rect (clear bar) + 5 module labels + AC count + GPS + SD = many draw_text calls */
    TEST_ASSERT_TRUE(mock_hal_display_get_draw_text_count() > 0);
    TEST_ASSERT_TRUE(mock_hal_display_get_draw_rect_count() > 0);
}

void test_render_notification_when_visible(void)
{
    ui_manager_show_notification("Alerta!", 0);

    mock_hal_display_reset();
    mock_hal_display_set_initialized(true);

    esp_err_t err = ui_manager_render_notification();
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(mock_hal_display_get_draw_rect_count() > 0);
    TEST_ASSERT_TRUE(mock_hal_display_get_draw_text_count() > 0);
}

void test_render_notification_when_hidden(void)
{
    /* No notification shown */
    mock_hal_display_reset();
    mock_hal_display_set_initialized(true);

    esp_err_t err = ui_manager_render_notification();
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(0, mock_hal_display_get_draw_rect_count());
    TEST_ASSERT_EQUAL(0, mock_hal_display_get_draw_text_count());
}

/* ========================================================================
 * Error Handling Tests
 * ======================================================================== */

void test_handle_key_before_init_fails(void)
{
    ui_manager_deinit();
    esp_err_t err = ui_manager_handle_key(UI_KEY_DOWN);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, err);
    /* Re-init for teardown */
    ui_manager_init();
}

void test_navigate_before_init_fails(void)
{
    ui_manager_deinit();
    esp_err_t err = ui_manager_navigate_to(UI_SCREEN_SCANNER);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, err);
    ui_manager_init();
}

void test_none_key_is_noop(void)
{
    const ui_state_t *state = ui_manager_get_state();
    ui_screen_t before = state->current_screen;
    ui_manager_handle_key(UI_KEY_NONE);
    TEST_ASSERT_EQUAL(before, state->current_screen);
}

/* ========================================================================
 * Generic Screen Navigation Tests
 * ======================================================================== */

void test_generic_screen_left_right_navigation(void)
{
    ui_manager_navigate_to(UI_SCREEN_SPECTRUM);
    TEST_ASSERT_EQUAL(UI_SCREEN_SPECTRUM, ui_manager_get_current_screen());

    ui_manager_handle_key(UI_KEY_RIGHT);
    TEST_ASSERT_EQUAL(UI_SCREEN_SETTINGS, ui_manager_get_current_screen());

    ui_manager_handle_key(UI_KEY_LEFT);
    TEST_ASSERT_EQUAL(UI_SCREEN_SPECTRUM, ui_manager_get_current_screen());
}

/* ========================================================================
 * Test Runner
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* Initialization */
    RUN_TEST(test_init_sets_default_state);
    RUN_TEST(test_init_modules_all_inactive);

    /* Navigation */
    RUN_TEST(test_navigate_to_scanner);
    RUN_TEST(test_navigate_to_invalid_screen);
    RUN_TEST(test_navigate_saves_previous_screen);

    /* Main Menu Keys */
    RUN_TEST(test_main_menu_down_key_increments_selection);
    RUN_TEST(test_main_menu_up_key_decrements_selection);
    RUN_TEST(test_main_menu_enter_navigates_to_selected);
    RUN_TEST(test_main_menu_enter_navigates_to_scanner);
    RUN_TEST(test_main_menu_up_wraps_to_bottom);

    /* Scanner Keys */
    RUN_TEST(test_scanner_back_returns_to_menu);
    RUN_TEST(test_scanner_page_right_increments);
    RUN_TEST(test_scanner_page_left_does_not_underflow);
    RUN_TEST(test_scanner_selection_bounded);

    /* Map Keys */
    RUN_TEST(test_map_zoom_in);
    RUN_TEST(test_map_zoom_out);
    RUN_TEST(test_map_zoom_in_clamps_to_min);
    RUN_TEST(test_map_zoom_out_clamps_to_max);

    /* Notifications */
    RUN_TEST(test_show_notification);
    RUN_TEST(test_show_notification_null_text_fails);
    RUN_TEST(test_notification_expires);
    RUN_TEST(test_dismiss_notification);
    RUN_TEST(test_notification_replaced);
    RUN_TEST(test_notification_custom_duration);

    /* Status Bar State */
    RUN_TEST(test_update_module_status);
    RUN_TEST(test_update_aircraft_count);
    RUN_TEST(test_update_gps_fix);
    RUN_TEST(test_update_sd_status);

    /* Render */
    RUN_TEST(test_render_status_bar_calls_display);
    RUN_TEST(test_render_notification_when_visible);
    RUN_TEST(test_render_notification_when_hidden);

    /* Error Handling */
    RUN_TEST(test_handle_key_before_init_fails);
    RUN_TEST(test_navigate_before_init_fails);
    RUN_TEST(test_none_key_is_noop);

    /* Generic Navigation */
    RUN_TEST(test_generic_screen_left_right_navigation);

    return UNITY_END();
}
