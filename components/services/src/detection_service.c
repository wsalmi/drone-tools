/**
 * @file detection_service.c
 * @brief Detection Service implementation.
 *
 * Orchestrates WiFi, BLE, LoRa, NRF24, and SDR modules, producing unified
 * raw_detection_t events into a shared FreeRTOS queue for downstream consumers.
 *
 * Validates: Requirements 1.1, 1.2, 1.5, 2.1, 3.1, 4.3
 */

#include "detection_service.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "hal_wifi_scanner.h"
#include "hal_ble_scanner.h"
#include "hal_lora.h"
#include "hal_nrf24.h"
#include "hal_sdr.h"
#include "hal_gps.h"
#include "hw_manager.h"

/* ========================================================================
 * Private Constants
 * ======================================================================== */

static const char *TAG = "detection_svc";

/** @brief LoRa dwell time per frequency hop (ms) */
#define LORA_DWELL_TIME_MS      50

/** @brief NRF24 dwell time per channel (ms) */
#define NRF24_DWELL_TIME_MS     100

/** @brief SDR I/Q read timeout (ms) */
#define SDR_READ_TIMEOUT_MS     200

/** @brief WiFi/BLE cycle period (ms) — completes scan in 3s total */
#define WIFI_BLE_CYCLE_MS       3000

/** @brief WiFi scan phase duration (ms) */
#define WIFI_PHASE_MS           1500

/** @brief BLE scan phase duration (ms) */
#define BLE_PHASE_MS            1500

/** @brief LoRa packet receive timeout (ms) */
#define LORA_RX_TIMEOUT_MS      50

/** @brief SDR minimum samples for a valid detection */
#define SDR_MIN_SAMPLES         64

/* ========================================================================
 * Private State
 * ======================================================================== */

/** @brief Internal service state */
typedef struct {
    bool initialized;
    bool started;
    QueueHandle_t detection_queue;
    bool source_available[DETECTION_SOURCE_COUNT];
    TaskHandle_t task_wifi_ble;
    TaskHandle_t task_rf;
    TaskHandle_t task_sdr;
    volatile bool stop_requested;
} detection_service_state_t;

static detection_service_state_t s_state = {0};

/* ========================================================================
 * Private Helper Functions
 * ======================================================================== */

/**
 * @brief Get the current UTC timestamp in milliseconds.
 *
 * Uses esp_timer for a monotonic clock. In a real system, this would
 * be synced to UTC via GPS time.
 */
static uint64_t get_timestamp_utc_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000ULL);
}

/**
 * @brief Attach the monitor's current GPS position to a detection.
 */
static void attach_monitor_position(raw_detection_t *det)
{
    gps_position_t pos = {0};
    esp_err_t err = hal_gps_get_position(&pos);
    if (err == ESP_OK) {
        det->monitor_position = pos;
    } else {
        /* GPS unavailable — leave position zeroed with fix_valid = false */
        memset(&det->monitor_position, 0, sizeof(gps_position_t));
        det->monitor_position.fix_valid = false;
    }
}

/**
 * @brief Enqueue a detection with silent drop when queue is full.
 *
 * @param det Pointer to the detection to enqueue (copied into queue).
 * @return true if enqueued, false if queue was full (dropped).
 */
static bool enqueue_detection(const raw_detection_t *det)
{
    if (s_state.detection_queue == NULL) {
        return false;
    }

    /* Non-blocking send — drop silently if queue is full */
    BaseType_t result = xQueueSend(s_state.detection_queue, det, 0);
    if (result != pdTRUE) {
        ESP_LOGD(TAG, "Queue full, detection dropped (source=%d)", det->source);
        return false;
    }
    return true;
}

/* ========================================================================
 * WiFi Scanner Callback
 * ======================================================================== */

/**
 * @brief Callback invoked by HAL WiFi scanner when a frame is captured.
 *
 * Packages the raw WiFi frame into a raw_detection_t and enqueues it.
 * This runs in the WiFi task context.
 */
static void wifi_frame_callback(const uint8_t *frame, uint16_t len, int8_t rssi)
{
    if (s_state.stop_requested || frame == NULL || len == 0) {
        return;
    }

    raw_detection_t det = {0};
    det.source = DETECTION_SOURCE_WIFI_RID;
    det.payload_len = (len > DETECTION_MAX_PAYLOAD_LEN) ? DETECTION_MAX_PAYLOAD_LEN : len;
    memcpy(det.raw_payload, frame, det.payload_len);
    det.rssi_dbm = (int16_t)rssi;
    det.snr_db = 0;  /* Not applicable for WiFi */
    det.frequency_hz = 2437000000U;  /* Channel 6 default, approximate */
    det.timestamp_utc_ms = get_timestamp_utc_ms();

    attach_monitor_position(&det);
    enqueue_detection(&det);
}

/* ========================================================================
 * BLE Scanner Callback
 * ======================================================================== */

/**
 * @brief Callback invoked by HAL BLE scanner when an advertisement is captured.
 *
 * Packages the raw BLE advertisement into a raw_detection_t and enqueues it.
 */
static void ble_adv_callback(const uint8_t *adv_data, uint16_t len, int8_t rssi, const uint8_t *addr)
{
    if (s_state.stop_requested || adv_data == NULL || len == 0) {
        return;
    }

    raw_detection_t det = {0};
    det.source = DETECTION_SOURCE_BLE_RID;
    det.payload_len = (len > DETECTION_MAX_PAYLOAD_LEN) ? DETECTION_MAX_PAYLOAD_LEN : len;
    memcpy(det.raw_payload, adv_data, det.payload_len);
    det.rssi_dbm = (int16_t)rssi;
    det.snr_db = 0;  /* Not applicable for BLE */
    det.frequency_hz = 2402000000U;  /* BLE advertising channel 37 */
    det.timestamp_utc_ms = get_timestamp_utc_ms();

    attach_monitor_position(&det);
    enqueue_detection(&det);
}

/* ========================================================================
 * Detection Tasks
 * ======================================================================== */

/**
 * @brief WiFi/BLE scanner task.
 *
 * Alternates between WiFi and BLE scanning in cycles of ~3 seconds total
 * (requirement 1.5: complete cycle in ≤ 3 seconds).
 *
 * WiFi phase: Start WiFi scanner, wait WiFi_PHASE_MS, stop.
 * BLE phase: Start BLE scanner, wait BLE_PHASE_MS, stop.
 */
static void task_wifi_ble_scanner(void *param)
{
    (void)param;
    ESP_LOGI(TAG, "WiFi/BLE scanner task started");

    while (!s_state.stop_requested) {
        /* WiFi phase */
        if (!s_state.stop_requested && hal_wifi_scanner_get_status() != HAL_STATUS_ERROR) {
            esp_err_t err = hal_wifi_scanner_start(wifi_frame_callback);
            if (err == ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(WIFI_PHASE_MS));
                hal_wifi_scanner_stop();
            } else {
                ESP_LOGW(TAG, "WiFi scanner start failed: %s", esp_err_to_name(err));
                vTaskDelay(pdMS_TO_TICKS(WIFI_PHASE_MS));
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(WIFI_PHASE_MS));
        }

        if (s_state.stop_requested) break;

        /* BLE phase */
        if (!s_state.stop_requested && hal_ble_scanner_get_status() != HAL_STATUS_ERROR) {
            esp_err_t err = hal_ble_scanner_start(ble_adv_callback);
            if (err == ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(BLE_PHASE_MS));
                hal_ble_scanner_stop();
            } else {
                ESP_LOGW(TAG, "BLE scanner start failed: %s", esp_err_to_name(err));
                vTaskDelay(pdMS_TO_TICKS(BLE_PHASE_MS));
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(BLE_PHASE_MS));
        }
    }

    ESP_LOGI(TAG, "WiFi/BLE scanner task stopping");
    hal_wifi_scanner_stop();
    hal_ble_scanner_stop();
    vTaskDelete(NULL);
}

/**
 * @brief RF monitor task (LoRa or NRF24, depending on HW Manager state).
 *
 * Reads from whichever RF module is currently active:
 * - LoRa: Cyclic frequency scanning at 862–928 MHz, 50ms per frequency
 * - NRF24: Spectrum scan + channel listening at 2.4 GHz, 100ms per channel
 *
 * The Hardware Manager handles the mutual exclusion; this task checks
 * the current state to decide which module to read from.
 */
static void task_rf_monitor(void *param)
{
    (void)param;
    ESP_LOGI(TAG, "RF monitor task started");

    /* LoRa frequency plan for ELRS 900 MHz (subset) */
    static const uint32_t lora_freq_plan[] = {
        862500000U, 863500000U, 864500000U, 865500000U, 866500000U,
        867500000U, 868500000U, 869500000U, 870500000U, 902500000U,
        903500000U, 904500000U, 905500000U, 906500000U, 907500000U,
        908500000U, 909500000U, 910500000U, 911500000U, 912500000U,
        913500000U, 914500000U, 915500000U, 916500000U, 917500000U,
        918500000U, 919500000U, 920500000U, 921500000U, 922500000U,
        923500000U, 924500000U, 925500000U, 926500000U, 927500000U
    };
    static const uint8_t lora_freq_count = sizeof(lora_freq_plan) / sizeof(lora_freq_plan[0]);
    uint8_t lora_freq_idx = 0;

    /* NRF24 channel scan state */
    uint8_t nrf24_channel = 0;

    /* Pre-allocate payload buffer for LoRa packets */
    uint8_t lora_payload_buf[HAL_LORA_MAX_PAYLOAD_LEN];

    /* Pre-allocate payload buffer for NRF24 packets */
    uint8_t nrf24_payload_buf[NRF24_MAX_PAYLOAD_LEN];

    while (!s_state.stop_requested) {
        hw_manager_state_t hw_state = hw_manager_get_state();

        if (hw_state == HW_STATE_LORA_ACTIVE || hw_state == HW_STATE_LORA_RECOVERY) {
            /* --- LoRa scanning mode --- */
            if (hal_lora_get_status() == HAL_STATUS_ACTIVE) {
                /* Hop to next frequency */
                hal_lora_set_frequency(lora_freq_plan[lora_freq_idx]);
                hal_lora_start_rx();

                /* Listen for packets at this frequency */
                lora_packet_t pkt = {
                    .payload = lora_payload_buf,
                    .payload_len = 0
                };

                esp_err_t err = hal_lora_get_packet(&pkt, LORA_RX_TIMEOUT_MS);
                if (err == ESP_OK && pkt.payload_len > 0) {
                    raw_detection_t det = {0};
                    det.source = DETECTION_SOURCE_LORA;
                    det.payload_len = (pkt.payload_len > DETECTION_MAX_PAYLOAD_LEN)
                                      ? DETECTION_MAX_PAYLOAD_LEN : pkt.payload_len;
                    memcpy(det.raw_payload, pkt.payload, det.payload_len);
                    det.rssi_dbm = pkt.rssi_dbm;
                    det.snr_db = pkt.snr_db;
                    det.frequency_hz = pkt.frequency_hz;
                    det.timestamp_utc_ms = get_timestamp_utc_ms();

                    attach_monitor_position(&det);
                    enqueue_detection(&det);
                }

                /* Advance to next frequency */
                lora_freq_idx = (lora_freq_idx + 1) % lora_freq_count;
            } else {
                /* LoRa not active, wait before retrying */
                vTaskDelay(pdMS_TO_TICKS(LORA_DWELL_TIME_MS));
            }

        } else if (hw_state == HW_STATE_NRF24_ACTIVE) {
            /* --- NRF24 scanning mode --- */
            if (hal_nrf24_get_status() == HAL_STATUS_ACTIVE) {
                nrf24_packet_t pkt = {
                    .payload = nrf24_payload_buf,
                    .payload_len = 0,
                    .channel = nrf24_channel
                };

                esp_err_t err = hal_nrf24_listen_channel(nrf24_channel, &pkt, NRF24_DWELL_TIME_MS);
                if (err == ESP_OK && pkt.payload_len > 0) {
                    raw_detection_t det = {0};
                    det.source = DETECTION_SOURCE_NRF24;
                    det.payload_len = (pkt.payload_len > DETECTION_MAX_PAYLOAD_LEN)
                                      ? DETECTION_MAX_PAYLOAD_LEN : pkt.payload_len;
                    memcpy(det.raw_payload, pkt.payload, det.payload_len);
                    det.rssi_dbm = pkt.rssi_level ? -60 : -90; /* NRF24 RPD is binary */
                    det.snr_db = 0;
                    det.frequency_hz = (2400U + nrf24_channel) * 1000000U;
                    det.timestamp_utc_ms = get_timestamp_utc_ms();

                    attach_monitor_position(&det);
                    enqueue_detection(&det);
                }

                /* Advance to next channel (126 channels total) */
                nrf24_channel = (nrf24_channel + 1) % NRF24_NUM_CHANNELS;
            } else {
                vTaskDelay(pdMS_TO_TICKS(NRF24_DWELL_TIME_MS));
            }

        } else {
            /* No RF module active (transitioning, error, etc.) */
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    ESP_LOGI(TAG, "RF monitor task stopping");
    vTaskDelete(NULL);
}

/**
 * @brief SDR receiver task.
 *
 * Continuously reads I/Q data from the RTL-SDR and produces detections
 * when signal energy is present. The raw I/Q samples are trimmed to fit
 * within raw_detection_t payload for downstream processing.
 */
static void task_sdr_receiver(void *param)
{
    (void)param;
    ESP_LOGI(TAG, "SDR receiver task started");

    /* Allocate I/Q buffer */
    int8_t iq_buf[SDR_FFT_SIZE * 2];  /* I/Q interleaved */
    sdr_iq_buffer_t iq = {
        .iq_samples = iq_buf,
        .num_samples = SDR_FFT_SIZE,
        .center_freq_hz = 0,
        .timestamp_ms = 0
    };

    sdr_spectrum_t spectrum = {0};

    while (!s_state.stop_requested) {
        if (hal_sdr_get_status() != HAL_STATUS_ACTIVE) {
            vTaskDelay(pdMS_TO_TICKS(SDR_READ_TIMEOUT_MS));
            continue;
        }

        esp_err_t err = hal_sdr_read_iq(&iq, SDR_READ_TIMEOUT_MS);
        if (err != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        /* Compute spectrum to check for signal presence */
        err = hal_sdr_compute_spectrum(&iq, &spectrum);
        if (err != ESP_OK) {
            continue;
        }

        /* Find peak power in spectrum */
        float peak_power = -200.0f;
        uint32_t peak_bin = 0;
        if (spectrum.power_db != NULL && spectrum.num_bins > 0) {
            for (uint32_t i = 0; i < spectrum.num_bins; i++) {
                if (spectrum.power_db[i] > peak_power) {
                    peak_power = spectrum.power_db[i];
                    peak_bin = i;
                }
            }
        }

        /* Only enqueue if signal is above a basic threshold (-60 dBm default) */
        if (peak_power > -60.0f) {
            raw_detection_t det = {0};
            det.source = DETECTION_SOURCE_SDR;

            /* Copy a portion of the I/Q data as raw payload */
            uint16_t copy_len = (iq.num_samples * 2 > DETECTION_MAX_PAYLOAD_LEN)
                                ? DETECTION_MAX_PAYLOAD_LEN
                                : (uint16_t)(iq.num_samples * 2);
            memcpy(det.raw_payload, iq_buf, copy_len);
            det.payload_len = copy_len;

            det.rssi_dbm = (int16_t)peak_power;
            det.snr_db = 0;
            det.frequency_hz = iq.center_freq_hz +
                               (peak_bin * spectrum.freq_step_hz);
            det.timestamp_utc_ms = get_timestamp_utc_ms();

            attach_monitor_position(&det);
            enqueue_detection(&det);
        }

        /* Small yield to prevent starving lower priority tasks */
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(TAG, "SDR receiver task stopping");
    vTaskDelete(NULL);
}

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

esp_err_t detection_service_init(void)
{
    if (s_state.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    /* Create detection queue */
    s_state.detection_queue = xQueueCreate(DETECTION_QUEUE_SIZE, sizeof(raw_detection_t));
    if (s_state.detection_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create detection queue");
        return ESP_ERR_NO_MEM;
    }

    /* Probe each module for availability */
    s_state.source_available[DETECTION_SOURCE_WIFI_RID] =
        (hal_wifi_scanner_get_status() != HAL_STATUS_ERROR);

    s_state.source_available[DETECTION_SOURCE_BLE_RID] =
        (hal_ble_scanner_get_status() != HAL_STATUS_ERROR);

    /* LoRa/NRF24 availability depends on HW Manager state */
    hw_manager_state_t hw_state = hw_manager_get_state();
    s_state.source_available[DETECTION_SOURCE_LORA] =
        (hw_state == HW_STATE_LORA_ACTIVE || hw_state == HW_STATE_INITIALIZING);
    s_state.source_available[DETECTION_SOURCE_NRF24] =
        (hw_state == HW_STATE_NRF24_ACTIVE);

    s_state.source_available[DETECTION_SOURCE_SDR] =
        (hal_sdr_get_status() != HAL_STATUS_ERROR &&
         hal_sdr_get_status() != HAL_STATUS_INACTIVE);

    /* Log module availability */
    ESP_LOGI(TAG, "Detection sources: WiFi=%d BLE=%d LoRa=%d NRF24=%d SDR=%d",
             s_state.source_available[DETECTION_SOURCE_WIFI_RID],
             s_state.source_available[DETECTION_SOURCE_BLE_RID],
             s_state.source_available[DETECTION_SOURCE_LORA],
             s_state.source_available[DETECTION_SOURCE_NRF24],
             s_state.source_available[DETECTION_SOURCE_SDR]);

    s_state.initialized = true;
    s_state.started = false;
    s_state.stop_requested = false;
    s_state.task_wifi_ble = NULL;
    s_state.task_rf = NULL;
    s_state.task_sdr = NULL;

    ESP_LOGI(TAG, "Detection service initialized");
    return ESP_OK;
}

esp_err_t detection_service_start(void)
{
    if (!s_state.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_state.started) {
        ESP_LOGW(TAG, "Already started");
        return ESP_ERR_INVALID_STATE;
    }

    s_state.stop_requested = false;
    BaseType_t ret;

    /* Start WiFi/BLE scanner task if either source is available */
    if (s_state.source_available[DETECTION_SOURCE_WIFI_RID] ||
        s_state.source_available[DETECTION_SOURCE_BLE_RID]) {

        ret = xTaskCreatePinnedToCore(
            task_wifi_ble_scanner,
            "det_wifi_ble",
            DETECTION_TASK_STACK_SIZE,
            NULL,
            DETECTION_TASK_PRIO_WIFI,
            &s_state.task_wifi_ble,
            0  /* Core 0 (PRO_CPU) */
        );
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create WiFi/BLE task");
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGI(TAG, "WiFi/BLE scanner task created on Core 0");
    }

    /* Start RF monitor task if LoRa or NRF24 is available */
    if (s_state.source_available[DETECTION_SOURCE_LORA] ||
        s_state.source_available[DETECTION_SOURCE_NRF24]) {

        ret = xTaskCreatePinnedToCore(
            task_rf_monitor,
            "det_rf_mon",
            DETECTION_TASK_STACK_SIZE,
            NULL,
            DETECTION_TASK_PRIO_RF,
            &s_state.task_rf,
            0  /* Core 0 (PRO_CPU) */
        );
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create RF monitor task");
            /* Clean up already created tasks */
            if (s_state.task_wifi_ble != NULL) {
                s_state.stop_requested = true;
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGI(TAG, "RF monitor task created on Core 0");
    }

    /* Start SDR receiver task if SDR is available */
    if (s_state.source_available[DETECTION_SOURCE_SDR]) {

        ret = xTaskCreatePinnedToCore(
            task_sdr_receiver,
            "det_sdr",
            DETECTION_TASK_STACK_SIZE,
            NULL,
            DETECTION_TASK_PRIO_SDR,
            &s_state.task_sdr,
            0  /* Core 0 (PRO_CPU) */
        );
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create SDR task");
            s_state.stop_requested = true;
            vTaskDelay(pdMS_TO_TICKS(100));
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGI(TAG, "SDR receiver task created on Core 0");
    }

    s_state.started = true;
    ESP_LOGI(TAG, "Detection service started");
    return ESP_OK;
}

esp_err_t detection_service_stop(void)
{
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_state.started) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Stopping detection service...");
    s_state.stop_requested = true;

    /* Wait for tasks to self-terminate.
     * Tasks check stop_requested and call vTaskDelete(NULL).
     * We give them a generous window to finish. */
    vTaskDelay(pdMS_TO_TICKS(WIFI_BLE_CYCLE_MS + 500));

    s_state.task_wifi_ble = NULL;
    s_state.task_rf = NULL;
    s_state.task_sdr = NULL;

    /* Clear the queue */
    if (s_state.detection_queue != NULL) {
        xQueueReset(s_state.detection_queue);
    }

    s_state.started = false;
    ESP_LOGI(TAG, "Detection service stopped");
    return ESP_OK;
}

esp_err_t detection_service_deinit(void)
{
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Stop first if still running */
    if (s_state.started) {
        detection_service_stop();
    }

    /* Delete the queue */
    if (s_state.detection_queue != NULL) {
        vQueueDelete(s_state.detection_queue);
        s_state.detection_queue = NULL;
    }

    /* Reset all state */
    memset(&s_state, 0, sizeof(s_state));

    ESP_LOGI(TAG, "Detection service deinitialized");
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

    TickType_t ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
    BaseType_t result = xQueueReceive(s_state.detection_queue, out, ticks);

    return (result == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

uint32_t detection_service_get_queue_count(void)
{
    if (!s_state.initialized || s_state.detection_queue == NULL) {
        return 0;
    }
    return (uint32_t)uxQueueMessagesWaiting(s_state.detection_queue);
}

bool detection_service_is_source_available(detection_source_t source)
{
    if (!s_state.initialized || source >= DETECTION_SOURCE_COUNT) {
        return false;
    }
    return s_state.source_available[source];
}
