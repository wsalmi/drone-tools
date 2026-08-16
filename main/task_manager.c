/**
 * @file task_manager.c
 * @brief Task Manager — FreeRTOS task lifecycle and stack monitoring.
 *
 * Creates and manages Core 1 (APP_CPU) tasks:
 *   - UI Render (priority 7, 8192 bytes)
 *   - Decoder Pipeline (priority 5, 8192 bytes)
 *   - GPS Reader (priority 3, 4096 bytes)
 *   - Logger (priority 2, 4096 bytes)
 *
 * Validates: Requirements 1.5, 9.2, 9.5
 */

#include "task_manager.h"
#include "data_pipeline.h"
#include "detection_service.h"
#include "telemetry_decoder.h"
#include "data_logger.h"
#include "hal_gps.h"
#include "hal_display.h"
#include "ui_manager.h"
#include "aircraft_registry.h"
#include "alert_engine.h"
#include "geolocation_service.h"
#include "screen_menu.h"
#include "screen_scanner.h"
#include "screen_map.h"
#include "screen_hud.h"
#include "screen_modes.h"
#include "screen_spectrum.h"
#include "screen_settings.h"
#include "screen_log.h"
#include "hal_keyboard.h"
#include "simulation_service.h"
#include "web_server_service.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"

#include <string.h>

/* Fallback if CONFIG_ESP_TASK_WDT_TIMEOUT_S not defined by sdkconfig */
#ifndef CONFIG_ESP_TASK_WDT_TIMEOUT_S
#define CONFIG_ESP_TASK_WDT_TIMEOUT_S  10
#endif

static const char *TAG = "task_mgr";

/* ========================================================================
 * Internal State
 * ======================================================================== */

static struct {
    TaskHandle_t ui_task;
    TaskHandle_t decoder_task;
    TaskHandle_t gps_task;
    TaskHandle_t logger_task;
    esp_timer_handle_t stack_monitor_timer;
    bool initialized;
    bool running;
    bool stop_requested;
} s_tm = {0};

/* ========================================================================
 * Task Function Prototypes
 * ======================================================================== */

static void task_ui_render(void *arg);
static void task_decoder_pipeline(void *arg);
static void task_gps_reader(void *arg);
static void task_logger(void *arg);

/* ========================================================================
 * Stack Monitoring
 * ======================================================================== */

/**
 * @brief Periodic stack monitoring callback (runs from esp_timer context).
 *
 * Checks uxTaskGetStackHighWaterMark for each managed task and logs
 * warnings if any task has dangerously low stack headroom.
 */
static void stack_monitor_timer_cb(void *arg)
{
    (void)arg;
    if (!s_tm.running) return;

    struct { TaskHandle_t handle; const char *name; uint32_t size; } tasks[] = {
        { s_tm.ui_task,      "ui_render",  TASK_STACK_UI_RENDER },
        { s_tm.decoder_task, "decoder",    TASK_STACK_DECODER },
        { s_tm.gps_task,     "gps_reader", TASK_STACK_GPS_READER },
        { s_tm.logger_task,  "logger",     TASK_STACK_LOGGER },
    };

    for (int i = 0; i < 4; i++) {
        if (tasks[i].handle == NULL) continue;
        UBaseType_t hwm = uxTaskGetStackHighWaterMark(tasks[i].handle);
        uint32_t free_bytes = (uint32_t)(hwm * sizeof(StackType_t));
        if (free_bytes < TASK_STACK_WARNING_THRESHOLD) {
            ESP_LOGW(TAG, "STACK WARNING: %s has only %lu bytes free (of %lu)",
                     tasks[i].name, (unsigned long)free_bytes, (unsigned long)tasks[i].size);
        }
    }
}

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

esp_err_t task_manager_init(void)
{
    if (s_tm.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_tm, 0, sizeof(s_tm));

    /* Create periodic stack monitoring timer */
    const esp_timer_create_args_t timer_args = {
        .callback = stack_monitor_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "stack_mon"
    };
    esp_err_t err = esp_timer_create(&timer_args, &s_tm.stack_monitor_timer);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to create stack monitor timer: %s", esp_err_to_name(err));
        /* Non-fatal — continue without periodic monitoring */
        s_tm.stack_monitor_timer = NULL;
    }

    /* Initialize or reconfigure Task Watchdog */
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };
    err = esp_task_wdt_reconfigure(&twdt_config);
    if (err != ESP_OK) {
        err = esp_task_wdt_init(&twdt_config);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "TWDT init: %s", esp_err_to_name(err));
        }
    }

    s_tm.initialized = true;

    ESP_LOGI(TAG, "Task Manager initialized");
    return ESP_OK;
}

esp_err_t task_manager_start(void)
{
    if (!s_tm.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_tm.running) {
        return ESP_ERR_INVALID_STATE;
    }

    s_tm.stop_requested = false;

    /* Create UI Render task — highest priority on Core 1 */
    BaseType_t ret = xTaskCreatePinnedToCore(
        task_ui_render, "ui_render",
        TASK_STACK_UI_RENDER, NULL,
        TASK_PRIO_UI_RENDER, &s_tm.ui_task,
        TASK_CORE_APP_CPU);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UI task");
        return ESP_ERR_NO_MEM;
    }

    /* Create Decoder Pipeline task */
    ret = xTaskCreatePinnedToCore(
        task_decoder_pipeline, "decoder",
        TASK_STACK_DECODER, NULL,
        TASK_PRIO_DECODER, &s_tm.decoder_task,
        TASK_CORE_APP_CPU);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Decoder task");
        return ESP_ERR_NO_MEM;
    }

    /* Create GPS Reader task */
    ret = xTaskCreatePinnedToCore(
        task_gps_reader, "gps_reader",
        TASK_STACK_GPS_READER, NULL,
        TASK_PRIO_GPS_READER, &s_tm.gps_task,
        TASK_CORE_APP_CPU);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create GPS task");
        return ESP_ERR_NO_MEM;
    }

    /* Create Logger task */
    ret = xTaskCreatePinnedToCore(
        task_logger, "logger",
        TASK_STACK_LOGGER, NULL,
        TASK_PRIO_LOGGER, &s_tm.logger_task,
        TASK_CORE_APP_CPU);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Logger task");
        return ESP_ERR_NO_MEM;
    }

    s_tm.running = true;

    /* Start periodic stack monitoring (every 10 seconds) */
    if (s_tm.stack_monitor_timer != NULL) {
        esp_err_t err = esp_timer_start_periodic(s_tm.stack_monitor_timer,
                                                  TASK_STACK_MONITOR_INTERVAL_MS * 1000ULL);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to start stack monitor: %s", esp_err_to_name(err));
        }
    }

    ESP_LOGI(TAG, "All Core 1 tasks started");
    return ESP_OK;
}

esp_err_t task_manager_stop(void)
{
    if (!s_tm.initialized || !s_tm.running) {
        return ESP_ERR_INVALID_STATE;
    }

    s_tm.stop_requested = true;

    /* Stop stack monitoring timer */
    if (s_tm.stack_monitor_timer != NULL) {
        esp_timer_stop(s_tm.stack_monitor_timer);
    }

    /* Wait for tasks to terminate (they check stop_requested) */
    vTaskDelay(pdMS_TO_TICKS(500));

    /* Delete tasks if they haven't self-terminated */
    if (s_tm.ui_task) { vTaskDelete(s_tm.ui_task); s_tm.ui_task = NULL; }
    if (s_tm.decoder_task) { vTaskDelete(s_tm.decoder_task); s_tm.decoder_task = NULL; }
    if (s_tm.gps_task) { vTaskDelete(s_tm.gps_task); s_tm.gps_task = NULL; }
    if (s_tm.logger_task) { vTaskDelete(s_tm.logger_task); s_tm.logger_task = NULL; }

    s_tm.running = false;
    ESP_LOGI(TAG, "All Core 1 tasks stopped");
    return ESP_OK;
}

esp_err_t task_manager_deinit(void)
{
    if (!s_tm.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_tm.running) {
        task_manager_stop();
    }

    /* Delete stack monitor timer */
    if (s_tm.stack_monitor_timer != NULL) {
        esp_timer_delete(s_tm.stack_monitor_timer);
        s_tm.stack_monitor_timer = NULL;
    }

    s_tm.initialized = false;
    ESP_LOGI(TAG, "Task Manager deinitialized");
    return ESP_OK;
}

esp_err_t task_manager_get_stats(task_manager_stats_t *stats)
{
    if (stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_tm.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(stats, 0, sizeof(*stats));

    if (s_tm.running) {
        stats->tasks[TASK_IDX_UI_RENDER].task_name = "ui_render";
        stats->tasks[TASK_IDX_UI_RENDER].stack_size = TASK_STACK_UI_RENDER;
        stats->tasks[TASK_IDX_UI_RENDER].core_id = TASK_CORE_APP_CPU;
        stats->tasks[TASK_IDX_UI_RENDER].priority = TASK_PRIO_UI_RENDER;
        stats->tasks[TASK_IDX_UI_RENDER].running = true;
        if (s_tm.ui_task) {
            stats->tasks[TASK_IDX_UI_RENDER].stack_high_water_mark =
                uxTaskGetStackHighWaterMark(s_tm.ui_task) * sizeof(StackType_t);
        }

        stats->tasks[TASK_IDX_DECODER].task_name = "decoder";
        stats->tasks[TASK_IDX_DECODER].stack_size = TASK_STACK_DECODER;
        stats->tasks[TASK_IDX_DECODER].core_id = TASK_CORE_APP_CPU;
        stats->tasks[TASK_IDX_DECODER].priority = TASK_PRIO_DECODER;
        stats->tasks[TASK_IDX_DECODER].running = true;
        if (s_tm.decoder_task) {
            stats->tasks[TASK_IDX_DECODER].stack_high_water_mark =
                uxTaskGetStackHighWaterMark(s_tm.decoder_task) * sizeof(StackType_t);
        }

        stats->tasks[TASK_IDX_GPS_READER].task_name = "gps_reader";
        stats->tasks[TASK_IDX_GPS_READER].stack_size = TASK_STACK_GPS_READER;
        stats->tasks[TASK_IDX_GPS_READER].core_id = TASK_CORE_APP_CPU;
        stats->tasks[TASK_IDX_GPS_READER].priority = TASK_PRIO_GPS_READER;
        stats->tasks[TASK_IDX_GPS_READER].running = true;
        if (s_tm.gps_task) {
            stats->tasks[TASK_IDX_GPS_READER].stack_high_water_mark =
                uxTaskGetStackHighWaterMark(s_tm.gps_task) * sizeof(StackType_t);
        }

        stats->tasks[TASK_IDX_LOGGER].task_name = "logger";
        stats->tasks[TASK_IDX_LOGGER].stack_size = TASK_STACK_LOGGER;
        stats->tasks[TASK_IDX_LOGGER].core_id = TASK_CORE_APP_CPU;
        stats->tasks[TASK_IDX_LOGGER].priority = TASK_PRIO_LOGGER;
        stats->tasks[TASK_IDX_LOGGER].running = true;
        if (s_tm.logger_task) {
            stats->tasks[TASK_IDX_LOGGER].stack_high_water_mark =
                uxTaskGetStackHighWaterMark(s_tm.logger_task) * sizeof(StackType_t);
        }
    }

    stats->last_monitor_time_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    return ESP_OK;
}

esp_err_t task_manager_check_stack_usage(void)
{
    if (!s_tm.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    task_manager_stats_t stats;
    task_manager_get_stats(&stats);

    for (int i = 0; i < TASK_MANAGER_CORE1_COUNT; i++) {
        if (stats.tasks[i].running &&
            stats.tasks[i].stack_high_water_mark < TASK_STACK_WARNING_THRESHOLD) {
            ESP_LOGW(TAG, "Stack warning: %s has only %lu bytes free",
                     stats.tasks[i].task_name,
                     (unsigned long)stats.tasks[i].stack_high_water_mark);
        }
    }

    return ESP_OK;
}

bool task_manager_is_running(void)
{
    return s_tm.running;
}

/* ========================================================================
 * Task Implementations
 * ======================================================================== */

/**
 * @brief UI Render task — refreshes the display at ~20 fps.
 */
static void task_ui_render(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "[Core 1] UI Render task started (prio=%d, stack=%d, Free Heap=%u bytes)",
             TASK_PRIO_UI_RENDER, TASK_STACK_UI_RENDER, (unsigned)esp_get_free_heap_size());

    /* Register with Task Watchdog */
    esp_task_wdt_add(NULL);

    static uint32_t s_last_sim_tick_ms = 0;
    static uint32_t s_frame_count = 0;
    static ui_screen_t s_last_logged_screen = UI_SCREEN_COUNT;

    while (!s_tm.stop_requested) {
        esp_task_wdt_reset();
        s_frame_count++;

        /* Poll Keyboard Input */
        ui_key_t key = UI_KEY_NONE;
        if (hal_keyboard_read(&key) == ESP_OK && key != UI_KEY_NONE) {
            ESP_LOGI(TAG, "[KEY] Received key event: %d on screen %d", (int)key, (int)ui_manager_get_current_screen());
            ui_manager_handle_key(key);
            switch (ui_manager_get_current_screen()) {
                case UI_SCREEN_MAIN_MENU: screen_menu_handle_key(key); break;
                case UI_SCREEN_MODES:     screen_modes_handle_key(key); break;
                case UI_SCREEN_SETTINGS:  screen_settings_handle_key(key); break;
                case UI_SCREEN_HUD:       screen_hud_handle_key(key); break;
                default: break;
            }
        }

        /* Wait for UI events or timeout for periodic refresh (~20fps) */
        uint32_t set_bits = 0;
        data_pipeline_wait_ui_events(PIPELINE_EVT_ALL, 50, &set_bits);

        /* Update simulation engine if enabled */
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        if (screen_modes_is_enabled(MODE_ITEM_SIMULATION)) {
            simulation_service_set_enabled(true);
            if (now_ms - s_last_sim_tick_ms >= 500) {
                s_last_sim_tick_ms = now_ms;
                simulation_service_tick();
            }
        } else {
            simulation_service_set_enabled(false);
        }

        /* Manage Web Server lifecycle */
        if (screen_modes_is_enabled(MODE_ITEM_WEBSERVER)) {
            if (!web_server_service_is_active()) {
                web_server_service_start();
            }
        } else {
            if (web_server_service_is_active()) {
                web_server_service_stop();
            }
        }

        /* Update notification system */
        ui_manager_update_notifications(now_ms);

        /* Update module and aircraft counts in status bar */
        aircraft_registry_t *reg = telemetry_decoder_get_registry();
        if (reg != NULL) {
            ui_manager_update_aircraft_count(registry_get_active_count(reg));
        }

        /* Log screen transition */
        ui_screen_t curr_screen = ui_manager_get_current_screen();
        if (curr_screen != s_last_logged_screen) {
            ESP_LOGI(TAG, "[UI] Screen changed -> %d (frame=%lu, Free Heap=%u bytes)",
                     (int)curr_screen, (unsigned long)s_frame_count, (unsigned)esp_get_free_heap_size());
            s_last_logged_screen = curr_screen;
        }

        if (s_frame_count == 1) {
            ESP_LOGI(TAG, "[UI] Rendering first frame successfully on display...");
        }

        /* Render status bar */
        ui_manager_render_status_bar();

        /* Render current screen content */
        switch (curr_screen) {
            case UI_SCREEN_MAIN_MENU:
                screen_menu_render();
                break;
            case UI_SCREEN_SCANNER:
                screen_scanner_render(reg);
                break;
            case UI_SCREEN_MAP:
                if (reg) {
                    screen_map_render(reg);
                } else {
                    screen_map_render_empty();
                }
                break;
            case UI_SCREEN_HUD:
                screen_hud_render(reg);
                break;
            case UI_SCREEN_MODES:
                screen_modes_render();
                break;
            case UI_SCREEN_SPECTRUM:
                screen_spectrum_render();
                break;
            case UI_SCREEN_SETTINGS:
                screen_settings_render();
                break;
            case UI_SCREEN_LOG:
                screen_log_render();
                break;
            default:
                break;
        }

        /* Render notification overlay (on top of screen content) */
        ui_manager_render_notification();
        hal_display_flush();
    }

    esp_task_wdt_delete(NULL);
    ESP_LOGI(TAG, "[Core 1] UI Render task exiting");
    vTaskDelete(NULL);
}

/**
 * @brief Decoder Pipeline task — receives raw detections, decodes, updates registry.
 */
static void task_decoder_pipeline(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Decoder Pipeline task started");

    /* Register with Task Watchdog */
    esp_task_wdt_add(NULL);

    raw_detection_t detection;
    decoded_telemetry_t telemetry;

    while (!s_tm.stop_requested) {
        esp_task_wdt_reset();

        /* Receive raw detection from the Detection Service queue */
        esp_err_t err = detection_service_receive(&detection, 100);
        if (err == ESP_ERR_TIMEOUT) {
            continue;
        }
        if (err != ESP_OK) {
            continue;
        }

        /* Decode the detection and update registry */
        memset(&telemetry, 0, sizeof(telemetry));
        err = telemetry_decode(&detection, &telemetry);
        if (err == ESP_OK) {
            /* Notify UI of telemetry update */
            data_pipeline_notify_ui(PIPELINE_EVT_TELEMETRY_UPDATED);

            /* Enqueue log record */
            log_record_t record = {0};
            record.timestamp_utc_ms = detection.timestamp_utc_ms;
            record.monitor_lat = detection.monitor_position.latitude;
            record.monitor_lon = detection.monitor_position.longitude;
            record.monitor_alt = detection.monitor_position.altitude_m;
            strncpy(record.aircraft_id, telemetry.uas_id, AIRCRAFT_ID_MAX_LEN - 1);
            record.rssi_dbm = detection.rssi_dbm;
            record.lat = telemetry.lat;
            record.lon = telemetry.lon;
            record.alt_m = telemetry.altitude_m;
            record.speed_ms = telemetry.speed_ms;
            record.battery_pct = telemetry.battery_pct;
            record.has_position = telemetry.has_position;
            record.has_altitude = telemetry.has_altitude;
            record.has_speed = telemetry.has_speed;
            record.has_battery = telemetry.has_battery;
            record.event_type = LOG_EVENT_TELEMETRY;
            data_pipeline_enqueue_log(&record);
        }
    }

    esp_task_wdt_delete(NULL);
    ESP_LOGI(TAG, "Decoder Pipeline task exiting");
    vTaskDelete(NULL);
}

/**
 * @brief GPS Reader task — periodically reads GPS position and updates shared state.
 */
static void task_gps_reader(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "GPS Reader task started");

    /* Register with Task Watchdog */
    esp_task_wdt_add(NULL);

    gps_position_t pos;

    while (!s_tm.stop_requested) {
        esp_task_wdt_reset();

        esp_err_t err = hal_gps_get_position(&pos);
        if (err == ESP_OK) {
            data_pipeline_update_gps(&pos);
            data_pipeline_notify_ui(PIPELINE_EVT_GPS_UPDATED);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));  /* 1 Hz GPS update rate */
    }

    esp_task_wdt_delete(NULL);
    ESP_LOGI(TAG, "GPS Reader task exiting");
    vTaskDelete(NULL);
}

/**
 * @brief Logger task — drains log queue and writes to SD card.
 */
static void task_logger(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Logger task started");

    /* Register with Task Watchdog */
    esp_task_wdt_add(NULL);

    log_record_t record;

    while (!s_tm.stop_requested) {
        esp_task_wdt_reset();

        esp_err_t err = data_pipeline_dequeue_log(&record, 500);
        if (err == ESP_OK) {
            data_logger_log(&record);
        }

        /* Periodically check SD availability for buffer flush */
        data_logger_check_sd();
    }

    esp_task_wdt_delete(NULL);
    ESP_LOGI(TAG, "Logger task exiting");
    vTaskDelete(NULL);
}
