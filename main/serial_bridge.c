/**
 * @file serial_bridge.c
 * @brief USB Serial/JTAG NDJSON bridge. USB is never used as an RF host.
 *
 * Browser contract (one JSON object per line):
 * - device -> {"type":"hello"|"state"|"aircraft"|"ack", ...}
 * - browser -> {"action":"sync"|"shortcut","key":1..7}
 *              {"action":"simulation","enabled":true,"scenario":0..2}
 */

#include "serial_bridge.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"

#include "ui_manager.h"
#include "screen_modes.h"
#include "simulation_service.h"
#include "telemetry_decoder.h"
#include "aircraft_registry.h"
#include "hal_wifi_scanner.h"
#include "hal_ble_scanner.h"
#include "hal_lora.h"
#include "hal_gps.h"
#include "hal_sd.h"

static const char *TAG = "serial_bridge";
static bool s_ready;
static TaskHandle_t s_task;

static void write_line(const char *line)
{
    if (line == NULL || !s_ready) return;
    usb_serial_jtag_write_bytes((const uint8_t *)line, strlen(line), pdMS_TO_TICKS(50));
    usb_serial_jtag_write_bytes((const uint8_t *)"\n", 1, pdMS_TO_TICKS(50));
}

static int parse_integer(const char *json, const char *field, int fallback)
{
    const char *pos = strstr(json, field);
    return pos == NULL ? fallback : atoi(pos + strlen(field));
}

static void publish_snapshot(void)
{
    aircraft_registry_t *registry = telemetry_decoder_get_registry();
    char line[256];
    uint8_t active = registry == NULL ? 0 : registry_get_active_count(registry);
    snprintf(line, sizeof(line),
             "{\"type\":\"state\",\"active\":%u,\"wifi\":%d,\"ble\":%d,\"sx1262\":%d,\"gps\":%d,\"sd\":%d,\"simulation\":%s,\"scenario\":%d,\"tick\":%lu}",
             active, hal_wifi_scanner_get_status() == HAL_STATUS_ACTIVE,
             hal_ble_scanner_get_status() == HAL_STATUS_ACTIVE,
             hal_lora_get_status() == HAL_STATUS_ACTIVE, hal_gps_has_fix(), hal_sd_is_mounted(),
             simulation_service_is_enabled() ? "true" : "false",
             (int)simulation_service_get_scenario(), (unsigned long)simulation_service_get_tick_count());
    write_line(line);
    if (registry == NULL) return;
    for (uint8_t i = 0; i < MAX_AIRCRAFT; ++i) {
        const aircraft_entry_t *entry = &registry->entries[i];
        if (!entry->slot_occupied || entry->status != AIRCRAFT_STATUS_ACTIVE) continue;
        snprintf(line, sizeof(line),
                 "{\"type\":\"aircraft\",\"id\":\"%s\",\"protocol\":%d,\"lat\":%.6f,\"lon\":%.6f,\"altitude\":%.1f,\"speed\":%.1f,\"rssi\":%d,\"distance\":%.1f,\"azimuth\":%.1f}",
                 entry->id, (int)entry->protocol, entry->last_telemetry.lat, entry->last_telemetry.lon,
                 (double)entry->last_telemetry.altitude_m, (double)entry->last_telemetry.speed_ms,
                 entry->last_rssi_dbm, (double)entry->relative_pos.distance_m,
                 (double)entry->relative_pos.azimuth_deg);
        write_line(line);
    }
}

static void handle_command(char *json)
{
    if (strstr(json, "\"action\":\"sync\"")) {
        publish_snapshot();
        return;
    }
    if (strstr(json, "\"action\":\"shortcut\"")) {
        int key = parse_integer(json, "\"key\":", 0);
        if (key >= 1 && key <= 7) ui_manager_handle_key((ui_key_t)(UI_KEY_1 + key - 1));
    } else if (strstr(json, "\"action\":\"simulation\"")) {
        bool enabled = strstr(json, "\"enabled\":true") != NULL;
        int scenario = parse_integer(json, "\"scenario\":", -1);
        if (scenario >= 0 && scenario < SIM_SCENARIO_COUNT) {
            simulation_service_set_scenario((simulation_scenario_t)scenario);
        }
        simulation_service_reset();
        screen_modes_set_enabled(MODE_ITEM_SIMULATION, enabled);
        simulation_service_set_enabled(enabled);
    }
    write_line("{\"type\":\"ack\",\"ok\":true}");
}

static void serial_bridge_task(void *arg)
{
    (void)arg;
    char command[192] = {0};
    size_t used = 0;
    uint32_t elapsed = 0;
    write_line("{\"type\":\"hello\",\"protocol\":\"drone-telemetry-serial/v1\",\"transport\":\"usb-serial-jtag\"}");
    while (true) {
        uint8_t input[48];
        int read = usb_serial_jtag_read_bytes(input, sizeof(input), pdMS_TO_TICKS(50));
        for (int i = 0; i < read; ++i) {
            if (input[i] == '\n' || input[i] == '\r') {
                if (used > 0) {
                    command[used] = '\0';
                    handle_command(command);
                    used = 0;
                }
            } else if (used < sizeof(command) - 1) command[used++] = (char)input[i];
            else used = 0;
        }
        elapsed += 50;
        if (elapsed >= 1000) {
            publish_snapshot();
            elapsed = 0;
        }
    }
}

esp_err_t serial_bridge_init(void)
{
    if (s_ready) return ESP_ERR_INVALID_STATE;
    const usb_serial_jtag_driver_config_t config = {.tx_buffer_size = 1024, .rx_buffer_size = 512};
    esp_err_t err = usb_serial_jtag_driver_install(&config);
    if (err != ESP_OK) return err;
    if (xTaskCreatePinnedToCore(serial_bridge_task, "serial_bridge", 4096, NULL, 3, &s_task, 0) != pdPASS) {
        usb_serial_jtag_driver_uninstall();
        return ESP_ERR_NO_MEM;
    }
    s_ready = true;
    ESP_LOGI(TAG, "NDJSON bridge active on USB Serial/JTAG");
    return ESP_OK;
}

bool serial_bridge_is_ready(void)
{
    return s_ready;
}
