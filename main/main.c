/**
 * @file main.c
 * @brief Drone Telemetry Monitor — Entry Point (app_main)
 *
 * Full initialization sequence for the drone telemetry monitoring firmware
 * on the M5 Stack Cardputer ADV (ESP32-S3).
 *
 * Initialization order:
 *   1. Hardware buses (SPI, UART, USB OTG)
 *   2. HAL modules: Display, SD Card, GPS, LoRa/NRF24 (via HW Manager), SDR,
 *      WiFi Scanner, BLE Scanner, Buzzer
 *   3. Configuration loading: config.json + signatures.csv from SD Card
 *   4. Domain layer: aircraft_registry, protocol_signatures
 *   5. Services: detection_service, telemetry_decoder, protocol_classifier,
 *      geolocation_service, pilot_locator, data_logger, spectrum_analyzer, alert_engine
 *   6. UI: ui_manager (all screens)
 *   7. Data pipeline + task manager → start tasks
 *
 * Each module initialization is shown on the Display_Interface with a 500ms
 * timeout indicator. Modules that fail to initialize are marked as INACTIVE
 * and the system operates in degraded mode.
 *
 * Validates: Requirements 10.1, 7.4, 7.5
 */

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* HAL headers */
#include "hal_common.h"
#include "hal_display.h"
#include "hal_sd.h"
#include "hal_gps.h"
#include "hal_lora.h"
#include "hal_nrf24.h"
#include "hal_sdr.h"
#include "hal_wifi_scanner.h"
#include "hal_ble_scanner.h"
#include "hal_buzzer.h"
#include "hw_manager.h"

/* Domain headers */
#include "config_store.h"
#include "aircraft_registry.h"
#include "protocol_signatures.h"

/* Service headers */
#include "detection_service.h"
#include "telemetry_decoder.h"
#include "protocol_classifier.h"
#include "geolocation_service.h"
#include "pilot_locator.h"
#include "data_logger.h"
#include "spectrum_analyzer.h"
#include "alert_engine.h"

/* UI header */
#include "ui_manager.h"

/* Main module headers */
#include "task_manager.h"
#include "data_pipeline.h"

static const char *TAG = "main";

/* ========================================================================
 * Configuration Constants
 * ======================================================================== */

/** @brief Maximum time to wait for any module initialization (ms) */
#define INIT_MODULE_TIMEOUT_MS      500

/** @brief GPS UART baud rate (ATGM336H default) */
#define GPS_BAUD_RATE               9600

/** @brief Path to config.json on SD card */
#define CONFIG_JSON_PATH            "/sdcard/config.json"

/** @brief Path to signatures.csv on SD card */
#define SIGNATURES_CSV_PATH         "/sdcard/signatures.csv"

/** @brief Maximum config file size to read (16 KB) */
#define CONFIG_FILE_MAX_SIZE        (16 * 1024)

/** @brief Maximum signatures file size to read (32 KB) */
#define SIGNATURES_FILE_MAX_SIZE    (32 * 1024)

/* ========================================================================
 * Global State
 * ======================================================================== */

/** @brief Global system configuration (loaded from SD or defaults) */
static config_store_t g_config;

/** @brief Global aircraft registry */
static aircraft_registry_t g_registry;

/* ========================================================================
 * Display Helpers for Initialization Status
 * ======================================================================== */

/** @brief Y position tracker for init status messages */
static uint16_t s_init_display_y = 2;

/**
 * @brief Show an initialization status line on the display.
 *
 * Displays module name and status on the display during boot.
 * Each line advances the Y position for the next message.
 *
 * @param module_name  Name of the module being initialized
 * @param status       "OK", "FAIL", "SKIP", etc.
 * @param is_error     true to show in red, false for green/white
 */
static void init_show_status(const char *module_name, const char *status, bool is_error)
{
    char line[40];
    snprintf(line, sizeof(line), "%-14s [%s]", module_name, status);

    uint16_t color = is_error ? HAL_COLOR_RED : HAL_COLOR_GREEN;
    hal_display_draw_text(2, s_init_display_y, line, color, HAL_COLOR_BLACK);
    hal_display_flush();

    s_init_display_y += 10;
    if (s_init_display_y > HAL_DISPLAY_HEIGHT - 10) {
        s_init_display_y = 2; /* Wrap around if we run out of vertical space */
    }
}

/**
 * @brief Show a section header on the display during initialization.
 */
static void init_show_header(const char *text)
{
    hal_display_draw_text(2, s_init_display_y, text, HAL_COLOR_CYAN, HAL_COLOR_BLACK);
    hal_display_flush();
    s_init_display_y += 10;
}

/**
 * @brief Load a file from SD card into a malloc'd buffer.
 *
 * @param path      File path on SD card
 * @param max_size  Maximum size to read
 * @param out_buf   Pointer to receive allocated buffer (caller must free)
 * @param out_len   Pointer to receive actual bytes read
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t load_file_from_sd(const char *path, size_t max_size,
                                   char **out_buf, size_t *out_len)
{
    hal_sd_file_t file;
    esp_err_t err = hal_sd_open(path, "r", &file);
    if (err != ESP_OK) {
        return err;
    }

    char *buf = malloc(max_size + 1);
    if (buf == NULL) {
        hal_sd_close(&file);
        return ESP_ERR_NO_MEM;
    }

    size_t bytes_read = 0;
    err = hal_sd_read(&file, buf, max_size, &bytes_read);
    hal_sd_close(&file);

    if (err != ESP_OK) {
        free(buf);
        return err;
    }

    buf[bytes_read] = '\0';  /* Null-terminate */
    *out_buf = buf;
    *out_len = bytes_read;
    return ESP_OK;
}

/* ========================================================================
 * Initialization Phases
 * ======================================================================== */

/**
 * @brief Phase 1: Initialize hardware buses (SPI, UART, USB).
 *
 * SPI2 (HSPI) — Display
 * SPI3 (VSPI) — LoRa/NRF24/SD Card (shared)
 * UART1 — GPS
 * USB OTG — RTL-SDR
 *
 * Note: ESP-IDF initializes SPI buses lazily when the first device is
 * registered, so explicit bus init is handled within each HAL driver.
 */
static void init_phase_buses(void)
{
    ESP_LOGI(TAG, "=== Phase 1: Hardware Buses ===");
    init_show_header("-- Buses --");

    /* SPI buses are initialized by HAL drivers when they register devices.
     * UART is initialized by hal_gps_init.
     * USB Host is initialized by hal_sdr_init.
     * Log the phase for visibility. */
    ESP_LOGI(TAG, "SPI2 (Display): configured by hal_display_init");
    ESP_LOGI(TAG, "SPI3 (RF+SD): configured by hw_manager/hal_sd_init");
    ESP_LOGI(TAG, "UART1 (GPS): configured by hal_gps_init");
    ESP_LOGI(TAG, "USB OTG (SDR): configured by hal_sdr_init");

    init_show_status("Buses", "READY", false);
}

/**
 * @brief Phase 2: Initialize HAL modules with display feedback.
 *
 * Each module gets up to INIT_MODULE_TIMEOUT_MS to initialize.
 * Failures are non-fatal — the system operates in degraded mode.
 */
static void init_phase_hal(void)
{
    esp_err_t err;

    ESP_LOGI(TAG, "=== Phase 2: HAL Modules ===");
    init_show_header("-- HAL Init --");

    /* --- Display (must be first — we need it for status output) --- */
    err = hal_display_init();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Display: OK");
        hal_display_clear(HAL_COLOR_BLACK);
        s_init_display_y = 2;
        hal_display_draw_text(2, s_init_display_y, "Drone Telemetry Monitor",
                              HAL_COLOR_WHITE, HAL_COLOR_BLACK);
        s_init_display_y += 12;
        hal_display_draw_text(2, s_init_display_y, "Initializing...",
                              HAL_COLOR_YELLOW, HAL_COLOR_BLACK);
        s_init_display_y += 12;
        hal_display_flush();
    } else {
        ESP_LOGE(TAG, "Display init FAILED: %s", esp_err_to_name(err));
        /* Cannot show status on display, continue with logging only */
    }

    /* --- SD Card --- */
    err = hal_sd_init();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "SD Card: OK");
        init_show_status("SD Card", "OK", false);
    } else {
        ESP_LOGW(TAG, "SD Card: ABSENT/FAIL (%s)", esp_err_to_name(err));
        init_show_status("SD Card", "FAIL", true);
    }

    /* --- GPS (UART1, 9600 baud) --- */
    err = hal_gps_init(GPS_BAUD_RATE);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "GPS: OK (waiting for fix)");
        init_show_status("GPS ATGM336H", "OK", false);
    } else {
        ESP_LOGW(TAG, "GPS init FAILED: %s", esp_err_to_name(err));
        init_show_status("GPS ATGM336H", "FAIL", true);
    }

    /* --- RF Modules via Hardware Manager (LoRa/NRF24 hot-swap) --- */
    hw_manager_config_t hw_cfg;
    hw_manager_get_default_config(&hw_cfg);
    err = hw_manager_init(&hw_cfg);
    if (err == ESP_OK) {
        hw_manager_state_t hw_state = hw_manager_get_state();
        const char *state_str;
        switch (hw_state) {
            case HW_STATE_LORA_ACTIVE:   state_str = "LoRa"; break;
            case HW_STATE_NRF24_ACTIVE:  state_str = "NRF24"; break;
            case HW_STATE_ERROR:         state_str = "ERR"; break;
            default:                     state_str = "INIT"; break;
        }
        ESP_LOGI(TAG, "HW Manager: OK (active: %s)", state_str);
        init_show_status("RF (HW Mgr)", state_str, hw_state == HW_STATE_ERROR);
    } else {
        ESP_LOGW(TAG, "HW Manager init FAILED: %s", esp_err_to_name(err));
        init_show_status("RF (HW Mgr)", "FAIL", true);
    }

    /* --- RTL-SDR (USB Host OTG) --- */
    sdr_config_t sdr_cfg = {
        .center_freq_hz = 915000000U,  /* Default: 915 MHz */
        .sample_rate_hz = 2400000U,    /* 2.4 MHz */
        .gain_index = 14,              /* ~20 dB */
        .agc_enabled = false
    };
    err = hal_sdr_init(&sdr_cfg);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "RTL-SDR: OK");
        init_show_status("RTL-SDR", "OK", false);
    } else {
        ESP_LOGW(TAG, "RTL-SDR: NOT PRESENT (%s)", esp_err_to_name(err));
        init_show_status("RTL-SDR", "SKIP", true);
    }

    /* --- WiFi Scanner --- */
    err = hal_wifi_scanner_init();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "WiFi Scanner: OK");
        init_show_status("WiFi Scanner", "OK", false);
    } else {
        ESP_LOGW(TAG, "WiFi Scanner init FAILED: %s", esp_err_to_name(err));
        init_show_status("WiFi Scanner", "FAIL", true);
    }

    /* --- BLE Scanner --- */
    err = hal_ble_scanner_init();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "BLE Scanner: OK");
        init_show_status("BLE Scanner", "OK", false);
    } else {
        ESP_LOGW(TAG, "BLE Scanner init FAILED: %s", esp_err_to_name(err));
        init_show_status("BLE Scanner", "FAIL", true);
    }

    /* --- Buzzer --- */
    err = hal_buzzer_init();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Buzzer: OK");
        init_show_status("Buzzer", "OK", false);
    } else {
        ESP_LOGW(TAG, "Buzzer init FAILED: %s", esp_err_to_name(err));
        init_show_status("Buzzer", "FAIL", true);
    }
}

/**
 * @brief Phase 3: Load configuration from SD card.
 *
 * Loads config.json and signatures.csv. Falls back to embedded defaults
 * if files are absent or malformed (Requirements 7.4, 7.5).
 */
static void init_phase_config(void)
{
    ESP_LOGI(TAG, "=== Phase 3: Configuration ===");
    init_show_header("-- Config --");

    /* Load config.json */
    config_store_get_defaults(&g_config);

    if (hal_sd_is_mounted()) {
        char *json_buf = NULL;
        size_t json_len = 0;
        esp_err_t err = load_file_from_sd(CONFIG_JSON_PATH, CONFIG_FILE_MAX_SIZE,
                                          &json_buf, &json_len);
        if (err == ESP_OK && json_buf != NULL) {
            int parse_result = config_store_load_from_json(json_buf, &g_config);
            if (parse_result == 0) {
                ESP_LOGI(TAG, "config.json: loaded (%zu bytes)", json_len);
                init_show_status("config.json", "OK", false);
            } else {
                ESP_LOGW(TAG, "config.json: malformed, using defaults");
                init_show_status("config.json", "DFLT", true);
                config_store_get_defaults(&g_config);
            }
            free(json_buf);
        } else {
            ESP_LOGW(TAG, "config.json: not found, using defaults");
            init_show_status("config.json", "DFLT", true);
        }

        /* Load signatures.csv */
        char *csv_buf = NULL;
        size_t csv_len = 0;
        err = load_file_from_sd(SIGNATURES_CSV_PATH, SIGNATURES_FILE_MAX_SIZE,
                                &csv_buf, &csv_len);
        if (err == ESP_OK && csv_buf != NULL) {
            int sig_result = signatures_load_csv(csv_buf, csv_len);
            if (sig_result == 0) {
                ESP_LOGI(TAG, "signatures.csv: loaded (%zu bytes, %u sigs)",
                         csv_len, signatures_get_count());
                init_show_status("signatures.csv", "OK", false);
            } else {
                ESP_LOGW(TAG, "signatures.csv: malformed, using defaults");
                init_show_status("signatures.csv", "DFLT", true);
                signatures_load_defaults();
            }
            free(csv_buf);
        } else {
            ESP_LOGW(TAG, "signatures.csv: not found, using defaults");
            init_show_status("signatures.csv", "DFLT", true);
            signatures_load_defaults();
        }
    } else {
        ESP_LOGW(TAG, "SD not mounted — using all default configuration");
        init_show_status("config.json", "NOSD", true);
        init_show_status("signatures.csv", "NOSD", true);
        signatures_load_defaults();
    }
}

/**
 * @brief Phase 4: Initialize domain layer.
 *
 * Sets up the Aircraft Registry and Protocol Signatures database.
 */
static void init_phase_domain(void)
{
    ESP_LOGI(TAG, "=== Phase 4: Domain Layer ===");
    init_show_header("-- Domain --");

    /* Aircraft Registry */
    esp_err_t err = registry_init(&g_registry);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Aircraft Registry: OK (max %d slots)", MAX_AIRCRAFT);
        init_show_status("Registry", "OK", false);
    } else {
        ESP_LOGE(TAG, "Aircraft Registry FAILED: %s", esp_err_to_name(err));
        init_show_status("Registry", "FAIL", true);
    }

    /* Protocol Signatures (already loaded in config phase, just init the module) */
    int sig_err = signatures_init();
    if (sig_err == 0) {
        ESP_LOGI(TAG, "Protocol Signatures: OK (%u loaded)", signatures_get_count());
        init_show_status("Signatures DB", "OK", false);
    } else {
        ESP_LOGW(TAG, "Protocol Signatures init issue (defaults active)");
        init_show_status("Signatures DB", "DFLT", true);
    }
}

/**
 * @brief Phase 5: Initialize application services.
 */
static void init_phase_services(void)
{
    esp_err_t err;

    ESP_LOGI(TAG, "=== Phase 5: Services ===");
    init_show_header("-- Services --");

    /* Geolocation Service (depends on GPS HAL) */
    err = geo_service_init();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Geolocation: OK");
        init_show_status("Geolocation", "OK", false);
    } else {
        ESP_LOGW(TAG, "Geolocation: %s (GPS may not be ready)", esp_err_to_name(err));
        init_show_status("Geolocation", "SKIP", true);
    }

    /* Protocol Classifier (depends on signatures DB) */
    err = classifier_init(hal_sd_is_mounted() ? SIGNATURES_CSV_PATH : NULL);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Classifier: OK (%u sigs)", classifier_get_signature_count());
        init_show_status("Classifier", "OK", false);
    } else {
        ESP_LOGW(TAG, "Classifier: %s", esp_err_to_name(err));
        init_show_status("Classifier", "FAIL", true);
    }

    /* Telemetry Decoder (depends on classifier + registry) */
    err = telemetry_decoder_init();
    if (err == ESP_OK) {
        telemetry_decoder_set_registry(&g_registry);
        ESP_LOGI(TAG, "Telemetry Decoder: OK");
        init_show_status("TelDecoder", "OK", false);
    } else {
        ESP_LOGW(TAG, "Telemetry Decoder: %s", esp_err_to_name(err));
        init_show_status("TelDecoder", "FAIL", true);
    }

    /* Detection Service (depends on HAL modules) */
    err = detection_service_init();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Detection Service: OK");
        init_show_status("Detection", "OK", false);
    } else {
        ESP_LOGW(TAG, "Detection Service: %s", esp_err_to_name(err));
        init_show_status("Detection", "FAIL", true);
    }

    /* Pilot Locator (depends on geolocation) */
    err = pilot_locator_init();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Pilot Locator: OK");
        init_show_status("Pilot Locator", "OK", false);
    } else {
        ESP_LOGW(TAG, "Pilot Locator: %s", esp_err_to_name(err));
        init_show_status("Pilot Locator", "FAIL", true);
    }

    /* Data Logger (depends on SD card HAL) */
    err = data_logger_init();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Data Logger: OK");
        init_show_status("Data Logger", "OK", false);
    } else {
        ESP_LOGW(TAG, "Data Logger: %s", esp_err_to_name(err));
        init_show_status("Data Logger", "FAIL", true);
    }

    /* Spectrum Analyzer (depends on SDR HAL + config) */
    if (hal_sdr_get_status() != HAL_STATUS_INACTIVE) {
        err = spectrum_analyzer_init(&g_config.spectrum);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Spectrum Analyzer: OK");
            init_show_status("Spectrum", "OK", false);
        } else {
            ESP_LOGW(TAG, "Spectrum Analyzer: %s", esp_err_to_name(err));
            init_show_status("Spectrum", "FAIL", true);
        }
    } else {
        ESP_LOGI(TAG, "Spectrum Analyzer: SKIP (no SDR)");
        init_show_status("Spectrum", "NOSD", true);
    }

    /* Alert Engine (depends on buzzer + geolocation + config) */
    err = alert_engine_init(&g_config.alert);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Alert Engine: OK");
        init_show_status("Alert Engine", "OK", false);
    } else {
        ESP_LOGW(TAG, "Alert Engine: %s", esp_err_to_name(err));
        init_show_status("Alert Engine", "FAIL", true);
    }
}

/**
 * @brief Phase 6: Initialize UI manager and screen modules.
 */
static void init_phase_ui(void)
{
    ESP_LOGI(TAG, "=== Phase 6: UI ===");
    init_show_header("-- UI --");

    esp_err_t err = ui_manager_init();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "UI Manager: OK");
        init_show_status("UI Manager", "OK", false);

        /* Set initial module status based on current HAL state */
        ui_manager_update_module_status(
            hal_lora_get_status(),
            hal_nrf24_get_status(),
            hal_sdr_get_status(),
            hal_gps_get_status(),
            hal_sd_is_mounted() ? HAL_STATUS_ACTIVE : HAL_STATUS_INACTIVE);

        ui_manager_update_gps_fix(hal_gps_has_fix());
        ui_manager_update_sd_status(hal_sd_is_mounted());
        ui_manager_update_aircraft_count(0);
    } else {
        ESP_LOGE(TAG, "UI Manager FAILED: %s", esp_err_to_name(err));
        init_show_status("UI Manager", "FAIL", true);
    }
}

/**
 * @brief Phase 7: Initialize data pipeline and start FreeRTOS tasks.
 */
static void init_phase_tasks(void)
{
    esp_err_t err;

    ESP_LOGI(TAG, "=== Phase 7: Tasks ===");
    init_show_header("-- Tasks --");

    /* Data Pipeline (inter-task queues, events, shared state) */
    err = data_pipeline_init();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Data Pipeline: OK");
        init_show_status("Pipeline", "OK", false);
    } else {
        ESP_LOGE(TAG, "Data Pipeline FAILED: %s", esp_err_to_name(err));
        init_show_status("Pipeline", "FAIL", true);
        return;  /* Cannot proceed without pipeline */
    }

    /* Task Manager initialization */
    err = task_manager_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Task Manager init FAILED: %s", esp_err_to_name(err));
        init_show_status("Task Mgr", "FAIL", true);
        return;
    }

    /* Start Detection Service (creates Core 0 detection tasks) */
    err = detection_service_start();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Detection tasks started (Core 0)");
        init_show_status("Detect Tasks", "OK", false);
    } else {
        ESP_LOGW(TAG, "Detection start: %s", esp_err_to_name(err));
        init_show_status("Detect Tasks", "FAIL", true);
    }

    /* Start Core 1 tasks via Task Manager */
    err = task_manager_start();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Core 1 tasks started (UI, Decoder, GPS, Logger)");
        init_show_status("App Tasks", "OK", false);
    } else {
        ESP_LOGE(TAG, "Task Manager start FAILED: %s", esp_err_to_name(err));
        init_show_status("App Tasks", "FAIL", true);
    }
}

/* ========================================================================
 * Entry Point
 * ======================================================================== */

void app_main(void)
{
    ESP_LOGI(TAG, "================================================");
    ESP_LOGI(TAG, " Drone Telemetry Monitor v1.0");
    ESP_LOGI(TAG, " Platform: M5 Stack Cardputer ADV (ESP32-S3)");
    ESP_LOGI(TAG, "================================================");

    uint32_t start_time_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

    /* Execute initialization phases in order */
    init_phase_buses();
    init_phase_hal();
    init_phase_config();
    init_phase_domain();
    init_phase_services();
    init_phase_ui();
    init_phase_tasks();

    /* Calculate total initialization time */
    uint32_t elapsed_ms = (uint32_t)(esp_timer_get_time() / 1000ULL) - start_time_ms;

    ESP_LOGI(TAG, "================================================");
    ESP_LOGI(TAG, " Initialization complete in %lu ms", (unsigned long)elapsed_ms);
    ESP_LOGI(TAG, " Active modules: LoRa=%d NRF24=%d SDR=%d GPS=%d SD=%d",
             hal_lora_get_status() == HAL_STATUS_ACTIVE,
             hal_nrf24_get_status() == HAL_STATUS_ACTIVE,
             hal_sdr_get_status() == HAL_STATUS_ACTIVE,
             hal_gps_get_status() == HAL_STATUS_ACTIVE,
             hal_sd_is_mounted());
    ESP_LOGI(TAG, " Signatures loaded: %u", signatures_get_count());
    ESP_LOGI(TAG, " Aircraft registry: %d/%d slots",
             registry_get_active_count(&g_registry), MAX_AIRCRAFT);
    ESP_LOGI(TAG, "================================================");

    /* Show completion on display */
    char complete_msg[40];
    snprintf(complete_msg, sizeof(complete_msg), "Ready! (%lums)", (unsigned long)elapsed_ms);
    init_show_status(complete_msg, "", false);

    /* Brief display of init complete before UI takes over */
    vTaskDelay(pdMS_TO_TICKS(1000));

    /* Navigate to main menu — UI task will handle rendering from here */
    ui_manager_navigate_to(UI_SCREEN_MAIN_MENU);

    /* app_main returns — FreeRTOS tasks continue running */
    ESP_LOGI(TAG, "app_main() returning, system running via FreeRTOS tasks");
}
