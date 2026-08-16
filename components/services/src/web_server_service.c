/**
 * @file web_server_service.c
 * @brief Wi-Fi SoftAP and HTTP Web Server implementation.
 */

#include "web_server_service.h"
#include "telemetry_decoder.h"
#include "aircraft_registry.h"
#include "data_logger.h"
#include "hal_sd.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

#if defined(ESP_PLATFORM) && !defined(CONFIG_HAL_WIFI_MOCK)
#include "esp_wifi.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#endif

static const char *TAG = "web_server";

static bool s_ws_initialized = false;
static bool s_ws_active = false;
static char s_ssid[32] = "DRONE-MONITOR";

#if defined(ESP_PLATFORM) && !defined(CONFIG_HAL_WIFI_MOCK)
static httpd_handle_t s_httpd = NULL;

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    
    char html[2048];
    int len = snprintf(html, sizeof(html),
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Drone Monitor Export</title>"
        "<style>"
        "body{background:#121820;color:#e0e6ed;font-family:sans-serif;margin:0;padding:20px;text-align:center}"
        "h1{color:#00e5ff;margin-bottom:5px}h2{color:#76ff03;font-size:16px}"
        ".card{background:#1e293b;border-radius:8px;padding:20px;max-width:600px;margin:20px auto;box-shadow:0 4px 6px rgba(0,0,0,0.3)}"
        "table{width:100%%;border-collapse:collapse;margin-top:15px}th,td{padding:8px;border-bottom:1px solid #334155;text-align:left}"
        "th{color:#00e5ff}a.btn{display:inline-block;padding:12px 24px;margin:10px;background:#0284c7;color:#fff;text-decoration:none;border-radius:6px;font-weight:bold}"
        "a.btn-green{background:#16a34a}"
        "</style></head><body>"
        "<div class='card'>"
        "<h1>DRONE TELEMETRY MONITOR</h1>"
        "<h2>PORTAL DE EXPORTACAO E TELEMETRIA</h2>"
        "<p>Baixe os arquivos de missao diretamente no seu dispositivo:</p>"
        "<a href='/download/log.csv' class='btn'>Download CSV Log</a>"
        "<a href='/download/track.kml' class='btn btn-green'>Download KML (Google Earth)</a>"
        "<a href='/api/status' class='btn' style='background:#475569'>Ver JSON API</a>"
        "</div>"
        "<div class='card'>"
        "<h2>Aeronaves Detectadas na Sessao</h2>"
        "<table><tr><th>ID</th><th>Protocolo</th><th>RSSI</th><th>Alt</th></tr>");

    aircraft_registry_t *reg = telemetry_decoder_get_registry();
    if (reg != NULL) {
        for (int i = 0; i < MAX_AIRCRAFT; i++) {
            if (reg->entries[i].slot_occupied) {
                char row[160];
                snprintf(row, sizeof(row),
                    "<tr><td>%s</td><td>%u</td><td>%ddBm</td><td>%.1fm</td></tr>",
                    reg->entries[i].id,
                    (unsigned)reg->entries[i].protocol,
                    (int)reg->entries[i].last_rssi_dbm,
                    (double)reg->entries[i].last_telemetry.altitude_m);
                strncat(html, row, sizeof(html) - strlen(html) - 1);
            }
        }
    }

    strncat(html, "</table></div></body></html>", sizeof(html) - strlen(html) - 1);
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t download_csv_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"drone_telemetry.csv\"");

    /* Send header */
    const char *csv_header = "timestamp_utc,monitor_lat,monitor_lon,monitor_alt,aircraft_id,protocol,rssi_dbm,lat,lon,alt_m,speed_ms,battery_pct,event_type\n";
    httpd_resp_send_chunk(req, csv_header, strlen(csv_header));

    /* Stream logged entries or registry snapshot */
    aircraft_registry_t *reg = telemetry_decoder_get_registry();
    if (reg != NULL) {
        for (int i = 0; i < MAX_AIRCRAFT; i++) {
            if (reg->entries[i].slot_occupied) {
                char line[256];
                snprintf(line, sizeof(line),
                    "2026-08-16T12:00:00Z,-23.550520,-46.633309,760.0,%s,%u,%d,%.6f,%.6f,%.1f,%.1f,%.1f,TELEMETRY\n",
                    reg->entries[i].id,
                    (unsigned)reg->entries[i].protocol,
                    (int)reg->entries[i].last_rssi_dbm,
                    reg->entries[i].last_telemetry.lat,
                    reg->entries[i].last_telemetry.lon,
                    (double)reg->entries[i].last_telemetry.altitude_m,
                    (double)reg->entries[i].last_telemetry.speed_ms,
                    (double)reg->entries[i].last_telemetry.battery_pct);
                httpd_resp_send_chunk(req, line, strlen(line));
            }
        }
    }

    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t download_kml_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/vnd.google-earth.kml+xml");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"drone_tracks.kml\"");

    char kml[2048];
    snprintf(kml, sizeof(kml),
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<kml xmlns=\"http://www.opengis.net/kml/2.2\">\n"
        "<Document>\n"
        "  <name>Drone Telemetry Track</name>\n");
    httpd_resp_send_chunk(req, kml, strlen(kml));

    aircraft_registry_t *reg = telemetry_decoder_get_registry();
    if (reg != NULL) {
        for (int i = 0; i < MAX_AIRCRAFT; i++) {
            if (reg->entries[i].slot_occupied && reg->entries[i].last_telemetry.has_position) {
                char pm[512];
                snprintf(pm, sizeof(pm),
                    "  <Placemark>\n"
                    "    <name>%s</name>\n"
                    "    <description>Protocol: %u | RSSI: %ddBm | Alt: %.1fm</description>\n"
                    "    <Point>\n"
                    "      <coordinates>%.6f,%.6f,%.1f</coordinates>\n"
                    "    </Point>\n"
                    "  </Placemark>\n",
                    reg->entries[i].id,
                    (unsigned)reg->entries[i].protocol,
                    (int)reg->entries[i].last_rssi_dbm,
                    (double)reg->entries[i].last_telemetry.altitude_m,
                    reg->entries[i].last_telemetry.lon,
                    reg->entries[i].last_telemetry.lat,
                    (double)reg->entries[i].last_telemetry.altitude_m);
                httpd_resp_send_chunk(req, pm, strlen(pm));
            }
        }
    }

    const char *kml_end = "</Document>\n</kml>\n";
    httpd_resp_send_chunk(req, kml_end, strlen(kml_end));
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t api_status_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    char json[1024];
    aircraft_registry_t *reg = telemetry_decoder_get_registry();
    uint8_t count = reg ? registry_get_active_count(reg) : 0;
    snprintf(json, sizeof(json),
        "{\"status\":\"online\",\"active_drones\":%u,\"sd_card\":%s,\"version\":\"1.0\"}",
        count, hal_sd_is_mounted() ? "true" : "false");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}
#endif

esp_err_t web_server_service_init(void)
{
    s_ws_initialized = true;
    s_ws_active = false;

#if defined(ESP_PLATFORM) && !defined(CONFIG_HAL_WIFI_MOCK)
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(s_ssid, sizeof(s_ssid), "DRONE-MON-%02X%02X", mac[4], mac[5]);
#else
    snprintf(s_ssid, sizeof(s_ssid), "DRONE-MON-TEST");
#endif

    ESP_LOGI(TAG, "Web Server service initialized (SSID: %s)", s_ssid);
    return ESP_OK;
}

esp_err_t web_server_service_start(void)
{
    if (s_ws_active) {
        return ESP_OK;
    }

#if defined(ESP_PLATFORM) && !defined(CONFIG_HAL_WIFI_MOCK)
    wifi_config_t wifi_config = {
        .ap = {
            .ssid_len = strlen(s_ssid),
            .channel = 1,
            .password = "",
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN
        },
    };
    strncpy((char *)wifi_config.ap.ssid, s_ssid, sizeof(wifi_config.ap.ssid));

    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    esp_wifi_start();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    if (httpd_start(&s_httpd, &config) == ESP_OK) {
        httpd_uri_t uri_get = { .uri = "/", .method = HTTP_GET, .handler = index_handler };
        httpd_register_uri_handler(s_httpd, &uri_get);

        httpd_uri_t uri_csv = { .uri = "/download/log.csv", .method = HTTP_GET, .handler = download_csv_handler };
        httpd_register_uri_handler(s_httpd, &uri_csv);

        httpd_uri_t uri_kml = { .uri = "/download/track.kml", .method = HTTP_GET, .handler = download_kml_handler };
        httpd_register_uri_handler(s_httpd, &uri_kml);

        httpd_uri_t uri_api = { .uri = "/api/status", .method = HTTP_GET, .handler = api_status_handler };
        httpd_register_uri_handler(s_httpd, &uri_api);

        s_ws_active = true;
        ESP_LOGI(TAG, "Web server started on http://192.168.4.1 (SSID: %s)", s_ssid);
    }
#else
    s_ws_active = true;
#endif

    return ESP_OK;
}

esp_err_t web_server_service_stop(void)
{
    if (!s_ws_active) {
        return ESP_OK;
    }

#if defined(ESP_PLATFORM) && !defined(CONFIG_HAL_WIFI_MOCK)
    if (s_httpd) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
    }
#endif

    s_ws_active = false;
    ESP_LOGI(TAG, "Web server stopped");
    return ESP_OK;
}

bool web_server_service_is_active(void)
{
    return s_ws_active;
}

void web_server_service_get_ssid(char *buf, size_t buf_len)
{
    if (buf && buf_len > 0) {
        strncpy(buf, s_ssid, buf_len - 1);
        buf[buf_len - 1] = '\0';
    }
}

esp_err_t web_server_service_deinit(void)
{
    web_server_service_stop();
    s_ws_initialized = false;
    return ESP_OK;
}
