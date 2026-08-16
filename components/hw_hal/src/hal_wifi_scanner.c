/**
 * @file hal_wifi_scanner.c
 * @brief HAL WiFi Scanner implementation for RemoteID detection.
 *
 * Uses ESP-IDF WiFi driver in promiscuous mode to capture:
 * - WiFi NAN (Neighbor Awareness Networking) Action frames
 * - Beacon frames with Vendor Specific IEs carrying RemoteID data
 *
 * The scanner cycles through channels 1, 6, and 11 to maximize
 * coverage of 2.4 GHz RemoteID transmissions.
 */

#include "hal_wifi_scanner.h"

#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_timer.h"

static const char *TAG = "hal_wifi_scanner";

/* ---------------------------------------------------------------------------
 * Constants
 * --------------------------------------------------------------------------- */

/** RemoteID WiFi NAN OUI (FA:0B:BC per ASTM F3411 / ASD-STAN) */
static const uint8_t REMOTEID_NAN_OUI[3] = {0xFA, 0x0B, 0xBC};

/** RemoteID Vendor Specific IE OUI for WiFi Beacon (same as NAN) */
static const uint8_t REMOTEID_BEACON_OUI[3] = {0xFA, 0x0B, 0xBC};

/** WiFi channels to scan (2.4 GHz non-overlapping) */
static const uint8_t SCAN_CHANNELS[] = {1, 6, 11};
#define NUM_SCAN_CHANNELS (sizeof(SCAN_CHANNELS) / sizeof(SCAN_CHANNELS[0]))

/** Channel dwell time in milliseconds */
#define CHANNEL_DWELL_MS 500

/** IEEE 802.11 frame type/subtype definitions */
#define WIFI_FRAME_TYPE_MGMT        0x00
#define WIFI_FRAME_SUBTYPE_BEACON   0x08
#define WIFI_FRAME_SUBTYPE_ACTION   0x0D

/** Vendor Specific IE element ID */
#define WIFI_IE_VENDOR_SPECIFIC     0xDD

/** NAN Service Discovery Frame (SDF) category */
#define WIFI_ACTION_CATEGORY_PUBLIC 0x04
#define WIFI_NAN_ACTION_SUBTYPE     0x09

/* ---------------------------------------------------------------------------
 * Internal State
 * --------------------------------------------------------------------------- */

/** Scanner internal state */
typedef struct {
    hal_module_state_t module_state;
    wifi_scan_callback_t callback;
    esp_timer_handle_t channel_timer;
    uint8_t current_channel_idx;
    bool initialized;
    bool scanning;
} wifi_scanner_state_t;

static wifi_scanner_state_t s_state = {
    .module_state = {
        .status = HAL_STATUS_INACTIVE,
        .last_activity_ms = 0,
        .error_count = 0,
    },
    .callback = NULL,
    .channel_timer = NULL,
    .current_channel_idx = 0,
    .initialized = false,
    .scanning = false,
};

/* ---------------------------------------------------------------------------
 * Internal Helper Functions
 * --------------------------------------------------------------------------- */

/**
 * @brief Check if a frame contains RemoteID data in Vendor Specific IE.
 *
 * Scans the information elements of a Beacon frame for a Vendor Specific
 * IE with the RemoteID OUI.
 *
 * @param ie_start   Pointer to start of IE data (after fixed fields).
 * @param ie_len     Length of IE data in bytes.
 * @return true if RemoteID Vendor Specific IE is found.
 */
static bool wifi_frame_has_remoteid_ie(const uint8_t *ie_start, uint16_t ie_len)
{
    uint16_t offset = 0;

    while (offset + 2 <= ie_len) {
        uint8_t ie_id = ie_start[offset];
        uint8_t ie_length = ie_start[offset + 1];

        if (offset + 2 + ie_length > ie_len) {
            break; /* Truncated IE */
        }

        if (ie_id == WIFI_IE_VENDOR_SPECIFIC && ie_length >= 3) {
            /* Check OUI matches RemoteID */
            if (memcmp(&ie_start[offset + 2], REMOTEID_BEACON_OUI, 3) == 0) {
                return true;
            }
        }

        offset += 2 + ie_length;
    }

    return false;
}

/**
 * @brief Check if a NAN Action frame carries RemoteID service data.
 *
 * NAN Service Discovery Frames (SDF) with RemoteID use the NAN OUI
 * in the action frame body.
 *
 * @param body       Pointer to the action frame body (after header).
 * @param body_len   Length of the body in bytes.
 * @return true if this is a RemoteID NAN frame.
 */
static bool wifi_frame_is_remoteid_nan(const uint8_t *body, uint16_t body_len)
{
    /* Minimum: category (1) + action (1) + OUI (3) = 5 bytes */
    if (body_len < 5) {
        return false;
    }

    /* Check Public Action frame with NAN subtype */
    if (body[0] == WIFI_ACTION_CATEGORY_PUBLIC && body[1] == WIFI_NAN_ACTION_SUBTYPE) {
        /* Check OUI in NAN SDF */
        if (body_len >= 8 && memcmp(&body[5], REMOTEID_NAN_OUI, 3) == 0) {
            return true;
        }
    }

    /* Also check for Vendor Specific Action frames */
    if (body[0] == 0x7F && body_len >= 4) { /* Vendor specific category */
        if (memcmp(&body[1], REMOTEID_NAN_OUI, 3) == 0) {
            return true;
        }
    }

    return false;
}

/**
 * @brief Promiscuous mode receive callback from ESP-IDF WiFi driver.
 *
 * Filters incoming frames for management frames (Beacons, Action frames)
 * that may contain RemoteID data.
 */
static void wifi_promiscuous_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    if (!s_state.scanning || s_state.callback == NULL) {
        return;
    }

    /* Only process management frames */
    if (type != WIFI_PKT_MGMT) {
        return;
    }

    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    const uint8_t *frame = pkt->payload;
    uint16_t frame_len = pkt->rx_ctrl.sig_len;
    int8_t rssi = pkt->rx_ctrl.rssi;

    if (frame_len < 24) {
        return; /* Frame too short for 802.11 header */
    }

    /* Extract frame control fields */
    uint8_t frame_type = (frame[0] >> 2) & 0x03;
    uint8_t frame_subtype = (frame[0] >> 4) & 0x0F;

    if (frame_type != WIFI_FRAME_TYPE_MGMT) {
        return;
    }

    bool is_remoteid = false;

    if (frame_subtype == WIFI_FRAME_SUBTYPE_BEACON) {
        /* Beacon frame: fixed params (12 bytes) start at offset 24 */
        /* IEs start after fixed params at offset 24 + 12 = 36 */
        if (frame_len > 36) {
            const uint8_t *ie_start = &frame[36];
            uint16_t ie_len = frame_len - 36;
            is_remoteid = wifi_frame_has_remoteid_ie(ie_start, ie_len);
        }
    } else if (frame_subtype == WIFI_FRAME_SUBTYPE_ACTION) {
        /* Action frame: body starts at offset 24 */
        if (frame_len > 24) {
            const uint8_t *body = &frame[24];
            uint16_t body_len = frame_len - 24;
            is_remoteid = wifi_frame_is_remoteid_nan(body, body_len);
        }
    }

    if (is_remoteid) {
        s_state.module_state.last_activity_ms = (uint32_t)(esp_timer_get_time() / 1000);
        s_state.callback(frame, frame_len, rssi);
    }
}

/**
 * @brief Timer callback for channel hopping.
 *
 * Cycles through scan channels to maximize coverage.
 */
static void wifi_channel_hop_cb(void *arg)
{
    (void)arg;

    if (!s_state.scanning) {
        return;
    }

    s_state.current_channel_idx = (s_state.current_channel_idx + 1) % NUM_SCAN_CHANNELS;
    uint8_t channel = SCAN_CHANNELS[s_state.current_channel_idx];

    esp_err_t err = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set channel %d: %s", channel, esp_err_to_name(err));
        s_state.module_state.error_count++;
    }
}

/* ---------------------------------------------------------------------------
 * Public API Implementation
 * --------------------------------------------------------------------------- */

esp_err_t hal_wifi_scanner_init(void)
{
    if (s_state.initialized) {
        ESP_LOGW(TAG, "WiFi scanner already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Initializing WiFi scanner");
    s_state.module_state.status = HAL_STATUS_INITIALIZING;

    /* Initialize WiFi in station mode without NVS storage requirement */
    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    wifi_cfg.nvs_enable = false;
    esp_err_t err = esp_wifi_init(&wifi_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        s_state.module_state.status = HAL_STATUS_ERROR;
        s_state.module_state.error_count++;
        return ESP_FAIL;
    }

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(err));
        esp_wifi_deinit();
        s_state.module_state.status = HAL_STATUS_ERROR;
        s_state.module_state.error_count++;
        return ESP_FAIL;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(err));
        esp_wifi_deinit();
        s_state.module_state.status = HAL_STATUS_ERROR;
        s_state.module_state.error_count++;
        return ESP_FAIL;
    }

    /* Create channel hopping timer */
    const esp_timer_create_args_t timer_args = {
        .callback = wifi_channel_hop_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "wifi_ch_hop",
    };

    err = esp_timer_create(&timer_args, &s_state.channel_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create channel hop timer: %s", esp_err_to_name(err));
        esp_wifi_stop();
        esp_wifi_deinit();
        s_state.module_state.status = HAL_STATUS_ERROR;
        s_state.module_state.error_count++;
        return ESP_FAIL;
    }

    s_state.initialized = true;
    s_state.scanning = false;
    s_state.module_state.status = HAL_STATUS_INACTIVE;
    ESP_LOGI(TAG, "WiFi scanner initialized successfully");

    return ESP_OK;
}

esp_err_t hal_wifi_scanner_start(wifi_scan_callback_t callback)
{
    if (!s_state.initialized) {
        ESP_LOGE(TAG, "WiFi scanner not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_state.scanning) {
        ESP_LOGW(TAG, "WiFi scanner already scanning");
        return ESP_ERR_INVALID_STATE;
    }

    if (callback == NULL) {
        ESP_LOGE(TAG, "Callback must not be NULL");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting WiFi scanner");

    s_state.callback = callback;
    s_state.current_channel_idx = 0;

    /* Set initial channel */
    esp_err_t err = esp_wifi_set_channel(SCAN_CHANNELS[0], WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set initial channel: %s", esp_err_to_name(err));
        s_state.module_state.error_count++;
        return ESP_FAIL;
    }

    /* Configure promiscuous filter for management frames only */
    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT,
    };
    err = esp_wifi_set_promiscuous_filter(&filter);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set promiscuous filter: %s", esp_err_to_name(err));
        s_state.module_state.error_count++;
        return ESP_FAIL;
    }

    /* Register promiscuous callback and enable */
    err = esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_rx_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register promiscuous callback: %s", esp_err_to_name(err));
        s_state.module_state.error_count++;
        return ESP_FAIL;
    }

    err = esp_wifi_set_promiscuous(true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable promiscuous mode: %s", esp_err_to_name(err));
        s_state.module_state.error_count++;
        return ESP_FAIL;
    }

    /* Start channel hopping timer */
    err = esp_timer_start_periodic(s_state.channel_timer, CHANNEL_DWELL_MS * 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start channel hop timer: %s", esp_err_to_name(err));
        esp_wifi_set_promiscuous(false);
        s_state.module_state.error_count++;
        return ESP_FAIL;
    }

    s_state.scanning = true;
    s_state.module_state.status = HAL_STATUS_ACTIVE;
    s_state.module_state.last_activity_ms = (uint32_t)(esp_timer_get_time() / 1000);

    ESP_LOGI(TAG, "WiFi scanner started, cycling channels 1/6/11");
    return ESP_OK;
}

esp_err_t hal_wifi_scanner_stop(void)
{
    if (!s_state.scanning) {
        ESP_LOGW(TAG, "WiFi scanner not scanning");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Stopping WiFi scanner");

    /* Stop channel hopping timer */
    esp_timer_stop(s_state.channel_timer);

    /* Disable promiscuous mode */
    esp_wifi_set_promiscuous(false);

    s_state.scanning = false;
    s_state.callback = NULL;
    s_state.module_state.status = HAL_STATUS_INACTIVE;

    ESP_LOGI(TAG, "WiFi scanner stopped");
    return ESP_OK;
}

hal_status_t hal_wifi_scanner_get_status(void)
{
    return s_state.module_state.status;
}

esp_err_t hal_wifi_scanner_deinit(void)
{
    if (!s_state.initialized) {
        ESP_LOGW(TAG, "WiFi scanner not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Deinitializing WiFi scanner");

    /* Stop scanning if active */
    if (s_state.scanning) {
        hal_wifi_scanner_stop();
    }

    /* Delete channel hop timer */
    if (s_state.channel_timer != NULL) {
        esp_timer_delete(s_state.channel_timer);
        s_state.channel_timer = NULL;
    }

    /* Stop and deinitialize WiFi */
    esp_wifi_stop();
    esp_wifi_deinit();

    s_state.initialized = false;
    s_state.module_state.status = HAL_STATUS_INACTIVE;
    s_state.module_state.error_count = 0;

    ESP_LOGI(TAG, "WiFi scanner deinitialized");
    return ESP_OK;
}
