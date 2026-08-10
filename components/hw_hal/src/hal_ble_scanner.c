/**
 * @file hal_ble_scanner.c
 * @brief HAL BLE Scanner implementation for RemoteID detection.
 *
 * Uses the NimBLE stack on ESP-IDF for passive BLE scanning to capture
 * BLE 4.x/5.x Legacy Advertisement packets carrying ASTM F3411 RemoteID
 * service data (UUID 0xFFFA).
 *
 * The scanner operates in passive mode — it does not send scan requests
 * or establish connections, minimizing RF footprint.
 */

#include "hal_ble_scanner.h"

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"

static const char *TAG = "hal_ble_scanner";

/* ---------------------------------------------------------------------------
 * Constants
 * --------------------------------------------------------------------------- */

/**
 * RemoteID BLE service UUID (16-bit): 0xFFFA
 * Per ASTM F3411-19 / ASD-STAN 4709-002, RemoteID data is carried
 * in BLE Legacy Advertisement packets as Service Data with UUID 0xFFFA.
 */
#define REMOTEID_BLE_SERVICE_UUID16  0xFFFA

/** AD Type for 16-bit Service Data */
#define BLE_AD_TYPE_SERVICE_DATA_16  0x16

/** Scan interval and window (in 0.625ms units) */
#define BLE_SCAN_INTERVAL   160  /* 100 ms */
#define BLE_SCAN_WINDOW     160  /* 100 ms (continuous) */

/* ---------------------------------------------------------------------------
 * Internal State
 * --------------------------------------------------------------------------- */

/** Scanner internal state */
typedef struct {
    hal_module_state_t module_state;
    ble_scan_callback_t callback;
    bool initialized;
    bool scanning;
} ble_scanner_state_t;

static ble_scanner_state_t s_state = {
    .module_state = {
        .status = HAL_STATUS_INACTIVE,
        .last_activity_ms = 0,
        .error_count = 0,
    },
    .callback = NULL,
    .initialized = false,
    .scanning = false,
};

/* ---------------------------------------------------------------------------
 * Internal Helper Functions
 * --------------------------------------------------------------------------- */

/**
 * @brief Check if advertisement data contains RemoteID service data.
 *
 * Parses the AD structures looking for Service Data (0x16) with
 * the 16-bit UUID 0xFFFA (RemoteID).
 *
 * @param data     Pointer to advertisement data.
 * @param data_len Length of advertisement data.
 * @return true if RemoteID service data is found.
 */
static bool ble_adv_has_remoteid_service(const uint8_t *data, uint16_t data_len)
{
    uint16_t offset = 0;

    while (offset < data_len) {
        if (offset + 1 > data_len) {
            break;
        }

        uint8_t ad_len = data[offset];
        if (ad_len == 0 || offset + 1 + ad_len > data_len) {
            break;
        }

        uint8_t ad_type = data[offset + 1];

        /* Check for 16-bit Service Data with RemoteID UUID */
        if (ad_type == BLE_AD_TYPE_SERVICE_DATA_16 && ad_len >= 3) {
            /* UUID is in little-endian after the type byte */
            uint16_t uuid16 = (uint16_t)data[offset + 2] |
                              ((uint16_t)data[offset + 3] << 8);
            if (uuid16 == REMOTEID_BLE_SERVICE_UUID16) {
                return true;
            }
        }

        offset += 1 + ad_len;
    }

    return false;
}

/**
 * @brief NimBLE GAP event handler for scan results.
 *
 * Called by the NimBLE stack when a BLE advertisement is received
 * during passive scanning.
 */
static int ble_gap_event_handler(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
        case BLE_GAP_EVENT_DISC: {
            if (!s_state.scanning || s_state.callback == NULL) {
                return 0;
            }

            const struct ble_gap_disc_desc *disc = &event->disc;

            /* Only process legacy advertisements */
            if (disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_ADV_IND &&
                disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_NONCONN_IND &&
                disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_SCAN_IND) {
                return 0;
            }

            /* Check if advertisement contains RemoteID service data */
            if (disc->length_data > 0 && disc->data != NULL) {
                if (ble_adv_has_remoteid_service(disc->data, disc->length_data)) {
                    s_state.module_state.last_activity_ms =
                        (uint32_t)(esp_timer_get_time() / 1000);

                    s_state.callback(
                        disc->data,
                        disc->length_data,
                        disc->rssi,
                        disc->addr.val
                    );
                }
            }
            break;
        }

        case BLE_GAP_EVENT_DISC_COMPLETE: {
            ESP_LOGD(TAG, "BLE scan complete event (reason=%d)",
                     event->disc_complete.reason);

            /* If scanning should still be active, restart */
            if (s_state.scanning) {
                struct ble_gap_disc_params scan_params = {
                    .itvl = BLE_SCAN_INTERVAL,
                    .window = BLE_SCAN_WINDOW,
                    .filter_policy = BLE_HCI_SCAN_FILT_NO_WL,
                    .limited = 0,
                    .passive = 1,  /* Passive scan — no scan requests */
                    .filter_duplicates = 0,  /* Report all duplicates for tracking */
                };

                int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER,
                                      &scan_params, ble_gap_event_handler, NULL);
                if (rc != 0) {
                    ESP_LOGW(TAG, "Failed to restart BLE scan: %d", rc);
                    s_state.module_state.error_count++;
                }
            }
            break;
        }

        default:
            break;
    }

    return 0;
}

/**
 * @brief NimBLE host task entry point.
 *
 * This task runs the NimBLE host processing loop.
 */
static void ble_host_task(void *param)
{
    (void)param;
    ESP_LOGI(TAG, "BLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/**
 * @brief NimBLE host reset callback.
 */
static void ble_on_reset(int reason)
{
    ESP_LOGW(TAG, "BLE host reset, reason=%d", reason);
    s_state.module_state.error_count++;
}

/**
 * @brief NimBLE host sync callback — called when BLE stack is ready.
 */
static void ble_on_sync(void)
{
    ESP_LOGI(TAG, "BLE host synced and ready");
}

/* ---------------------------------------------------------------------------
 * Public API Implementation
 * --------------------------------------------------------------------------- */

esp_err_t hal_ble_scanner_init(void)
{
    if (s_state.initialized) {
        ESP_LOGW(TAG, "BLE scanner already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Initializing BLE scanner");
    s_state.module_state.status = HAL_STATUS_INITIALIZING;

    /* Initialize NimBLE port */
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(err));
        s_state.module_state.status = HAL_STATUS_ERROR;
        s_state.module_state.error_count++;
        return ESP_FAIL;
    }

    /* Configure NimBLE host callbacks */
    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;

    /* Start NimBLE host task */
    nimble_port_freertos_init(ble_host_task);

    s_state.initialized = true;
    s_state.scanning = false;
    s_state.module_state.status = HAL_STATUS_INACTIVE;

    ESP_LOGI(TAG, "BLE scanner initialized successfully");
    return ESP_OK;
}

esp_err_t hal_ble_scanner_start(ble_scan_callback_t callback)
{
    if (!s_state.initialized) {
        ESP_LOGE(TAG, "BLE scanner not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_state.scanning) {
        ESP_LOGW(TAG, "BLE scanner already scanning");
        return ESP_ERR_INVALID_STATE;
    }

    if (callback == NULL) {
        ESP_LOGE(TAG, "Callback must not be NULL");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting BLE scanner");

    s_state.callback = callback;

    /* Configure passive scanning parameters */
    struct ble_gap_disc_params scan_params = {
        .itvl = BLE_SCAN_INTERVAL,
        .window = BLE_SCAN_WINDOW,
        .filter_policy = BLE_HCI_SCAN_FILT_NO_WL,
        .limited = 0,
        .passive = 1,  /* Passive scan — no scan requests sent */
        .filter_duplicates = 0,  /* Report all duplicates for continuous tracking */
    };

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER,
                          &scan_params, ble_gap_event_handler, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_disc failed: %d", rc);
        s_state.callback = NULL;
        s_state.module_state.status = HAL_STATUS_ERROR;
        s_state.module_state.error_count++;
        return ESP_FAIL;
    }

    s_state.scanning = true;
    s_state.module_state.status = HAL_STATUS_ACTIVE;
    s_state.module_state.last_activity_ms = (uint32_t)(esp_timer_get_time() / 1000);

    ESP_LOGI(TAG, "BLE scanner started (passive, no duplicate filter)");
    return ESP_OK;
}

esp_err_t hal_ble_scanner_stop(void)
{
    if (!s_state.scanning) {
        ESP_LOGW(TAG, "BLE scanner not scanning");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Stopping BLE scanner");

    /* Cancel ongoing scan */
    int rc = ble_gap_disc_cancel();
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGW(TAG, "ble_gap_disc_cancel returned: %d", rc);
    }

    s_state.scanning = false;
    s_state.callback = NULL;
    s_state.module_state.status = HAL_STATUS_INACTIVE;

    ESP_LOGI(TAG, "BLE scanner stopped");
    return ESP_OK;
}

hal_status_t hal_ble_scanner_get_status(void)
{
    return s_state.module_state.status;
}

esp_err_t hal_ble_scanner_deinit(void)
{
    if (!s_state.initialized) {
        ESP_LOGW(TAG, "BLE scanner not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Deinitializing BLE scanner");

    /* Stop scanning if active */
    if (s_state.scanning) {
        hal_ble_scanner_stop();
    }

    /* Deinitialize NimBLE */
    int rc = nimble_port_stop();
    if (rc != 0) {
        ESP_LOGW(TAG, "nimble_port_stop returned: %d", rc);
    }

    nimble_port_deinit();

    s_state.initialized = false;
    s_state.module_state.status = HAL_STATUS_INACTIVE;
    s_state.module_state.error_count = 0;

    ESP_LOGI(TAG, "BLE scanner deinitialized");
    return ESP_OK;
}
