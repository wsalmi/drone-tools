/**
 * @file detection_service.c
 * @brief Wi-Fi, BLE and SX1262 passive detection service.
 *
 * Deliberately excludes USB host receivers, RTL-SDR and NRF24. USB remains
 * available to the firmware updater and console Serial/JTAG interface.
 */

#include "detection_service.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"

#include "hal_wifi_scanner.h"
#include "hal_ble_scanner.h"
#include "hal_lora.h"
#include "hal_gps.h"

static const char *TAG = "detection_svc";

#define WIFI_BLE_CYCLE_MS 3000U
#define WIFI_PHASE_MS     1500U
#define BLE_PHASE_MS      1500U
#define LORA_RX_TIMEOUT_MS 50U
#define LORA_DWELL_TIME_MS 50U

typedef struct {
    bool initialized;
    bool started;
    QueueHandle_t detection_queue;
    bool source_available[DETECTION_SOURCE_COUNT];
    TaskHandle_t task_wifi_ble;
    TaskHandle_t task_lora;
    volatile bool stop_requested;
} detection_service_state_t;

static detection_service_state_t s_state = {0};

static uint64_t get_timestamp_utc_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000ULL);
}

static void attach_monitor_position(raw_detection_t *det)
{
    gps_position_t pos = {0};
    if (hal_gps_get_position(&pos) == ESP_OK) {
        det->monitor_position = pos;
        return;
    }
    memset(&det->monitor_position, 0, sizeof(det->monitor_position));
}

static bool enqueue_detection(const raw_detection_t *det)
{
    if (s_state.detection_queue == NULL) {
        return false;
    }
    if (xQueueSend(s_state.detection_queue, det, 0) != pdTRUE) {
        ESP_LOGD(TAG, "Fila cheia; detecção descartada (fonte=%d)", det->source);
        return false;
    }
    return true;
}

static void wifi_frame_callback(const uint8_t *frame, uint16_t len, int8_t rssi)
{
    if (s_state.stop_requested || frame == NULL || len == 0) {
        return;
    }
    raw_detection_t det = {0};
    det.source = DETECTION_SOURCE_WIFI_RID;
    det.payload_len = len > DETECTION_MAX_PAYLOAD_LEN ? DETECTION_MAX_PAYLOAD_LEN : len;
    memcpy(det.raw_payload, frame, det.payload_len);
    det.rssi_dbm = rssi;
    det.frequency_hz = 2437000000U;
    det.timestamp_utc_ms = get_timestamp_utc_ms();
    attach_monitor_position(&det);
    enqueue_detection(&det);
}

static void ble_adv_callback(const uint8_t *data, uint16_t len, int8_t rssi, const uint8_t *addr)
{
    (void)addr;
    if (s_state.stop_requested || data == NULL || len == 0) {
        return;
    }
    raw_detection_t det = {0};
    det.source = DETECTION_SOURCE_BLE_RID;
    det.payload_len = len > DETECTION_MAX_PAYLOAD_LEN ? DETECTION_MAX_PAYLOAD_LEN : len;
    memcpy(det.raw_payload, data, det.payload_len);
    det.rssi_dbm = rssi;
    det.frequency_hz = 2402000000U;
    det.timestamp_utc_ms = get_timestamp_utc_ms();
    attach_monitor_position(&det);
    enqueue_detection(&det);
}

static void task_wifi_ble_scanner(void *param)
{
    (void)param;
    ESP_LOGI(TAG, "Wi-Fi/BLE Remote ID em execução (heap=%u)",
             (unsigned)esp_get_free_heap_size());
    while (!s_state.stop_requested) {
        if (s_state.source_available[DETECTION_SOURCE_WIFI_RID] &&
            hal_wifi_scanner_start(wifi_frame_callback) == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(WIFI_PHASE_MS));
            hal_wifi_scanner_stop();
        } else {
            vTaskDelay(pdMS_TO_TICKS(WIFI_PHASE_MS));
        }
        if (s_state.stop_requested) {
            break;
        }
        if (s_state.source_available[DETECTION_SOURCE_BLE_RID] &&
            hal_ble_scanner_start(ble_adv_callback) == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(BLE_PHASE_MS));
            hal_ble_scanner_stop();
        } else {
            vTaskDelay(pdMS_TO_TICKS(BLE_PHASE_MS));
        }
    }
    hal_wifi_scanner_stop();
    hal_ble_scanner_stop();
    vTaskDelete(NULL);
}

static void task_lora_monitor(void *param)
{
    (void)param;
    static const uint32_t frequency_plan[] = {868100000U, 868300000U, 868500000U};
    const uint8_t plan_count = sizeof(frequency_plan) / sizeof(frequency_plan[0]);
    uint8_t frequency_index = 0;
    uint8_t payload[HAL_LORA_MAX_PAYLOAD_LEN];

    ESP_LOGI(TAG, "SX1262 em monitoramento passivo");
    while (!s_state.stop_requested) {
        if (hal_lora_get_status() != HAL_STATUS_ACTIVE) {
            vTaskDelay(pdMS_TO_TICKS(LORA_DWELL_TIME_MS));
            continue;
        }
        if (hal_lora_set_frequency(frequency_plan[frequency_index]) == ESP_OK &&
            hal_lora_start_rx() == ESP_OK) {
            lora_packet_t packet = {.payload = payload, .payload_len = 0};
            if (hal_lora_get_packet(&packet, LORA_RX_TIMEOUT_MS) == ESP_OK &&
                packet.payload_len > 0) {
                raw_detection_t det = {0};
                det.source = DETECTION_SOURCE_LORA;
                det.payload_len = packet.payload_len > DETECTION_MAX_PAYLOAD_LEN
                    ? DETECTION_MAX_PAYLOAD_LEN : packet.payload_len;
                memcpy(det.raw_payload, packet.payload, det.payload_len);
                det.rssi_dbm = packet.rssi_dbm;
                det.snr_db = packet.snr_db;
                det.frequency_hz = packet.frequency_hz;
                det.timestamp_utc_ms = get_timestamp_utc_ms();
                attach_monitor_position(&det);
                enqueue_detection(&det);
            }
        }
        frequency_index = (frequency_index + 1U) % plan_count;
    }
    vTaskDelete(NULL);
}

esp_err_t detection_service_init(void)
{
    if (s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    s_state.detection_queue = xQueueCreate(DETECTION_QUEUE_SIZE, sizeof(raw_detection_t));
    if (s_state.detection_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_state.source_available[DETECTION_SOURCE_WIFI_RID] =
        hal_wifi_scanner_get_status() == HAL_STATUS_ACTIVE;
    s_state.source_available[DETECTION_SOURCE_BLE_RID] =
        hal_ble_scanner_get_status() == HAL_STATUS_ACTIVE;
    s_state.source_available[DETECTION_SOURCE_LORA] =
        hal_lora_get_status() == HAL_STATUS_ACTIVE;
    s_state.initialized = true;
    ESP_LOGI(TAG, "Fontes: Wi-Fi=%d BLE=%d SX1262=%d",
             s_state.source_available[0], s_state.source_available[1],
             s_state.source_available[2]);
    return ESP_OK;
}

esp_err_t detection_service_start(void)
{
    if (!s_state.initialized || s_state.started) {
        return ESP_ERR_INVALID_STATE;
    }
    s_state.stop_requested = false;
    if ((s_state.source_available[DETECTION_SOURCE_WIFI_RID] ||
         s_state.source_available[DETECTION_SOURCE_BLE_RID]) &&
        xTaskCreatePinnedToCore(task_wifi_ble_scanner, "det_wifi_ble",
                                DETECTION_TASK_STACK_SIZE, NULL,
                                DETECTION_TASK_PRIO_WIFI, &s_state.task_wifi_ble, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    if (s_state.source_available[DETECTION_SOURCE_LORA] &&
        xTaskCreatePinnedToCore(task_lora_monitor, "det_sx1262",
                                DETECTION_TASK_STACK_SIZE, NULL,
                                DETECTION_TASK_PRIO_RF, &s_state.task_lora, 0) != pdPASS) {
        s_state.stop_requested = true;
        return ESP_ERR_NO_MEM;
    }
    s_state.started = true;
    return ESP_OK;
}

esp_err_t detection_service_stop(void)
{
    if (!s_state.initialized || !s_state.started) {
        return ESP_ERR_INVALID_STATE;
    }
    s_state.stop_requested = true;
    vTaskDelay(pdMS_TO_TICKS(WIFI_BLE_CYCLE_MS + 100U));
    s_state.task_wifi_ble = NULL;
    s_state.task_lora = NULL;
    xQueueReset(s_state.detection_queue);
    s_state.started = false;
    return ESP_OK;
}

esp_err_t detection_service_deinit(void)
{
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_state.started) {
        detection_service_stop();
    }
    vQueueDelete(s_state.detection_queue);
    memset(&s_state, 0, sizeof(s_state));
    return ESP_OK;
}

esp_err_t detection_service_receive(raw_detection_t *out, uint32_t timeout_ms)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_state.initialized || s_state.detection_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return xQueueReceive(s_state.detection_queue, out,
                         timeout_ms == 0 ? 0 : pdMS_TO_TICKS(timeout_ms)) == pdTRUE
        ? ESP_OK : ESP_ERR_TIMEOUT;
}

uint32_t detection_service_get_queue_count(void)
{
    return s_state.initialized && s_state.detection_queue != NULL
        ? (uint32_t)uxQueueMessagesWaiting(s_state.detection_queue) : 0;
}

bool detection_service_is_source_available(detection_source_t source)
{
    return s_state.initialized && source < DETECTION_SOURCE_COUNT &&
        s_state.source_available[source];
}
