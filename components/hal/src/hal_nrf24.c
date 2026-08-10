/**
 * @file hal_nrf24.c
 * @brief NRF24L01+ HAL driver implementation over SPI.
 *
 * Implements spectrum scanning (126 channels), packet reception,
 * presence detection, and module lifecycle management.
 *
 * Uses SPI3 (VSPI) bus shared with LoRa and SD Card.
 * NRF24 and LoRa are mutually exclusive on this bus.
 *
 * Validates: Requirements 2.1, 2.2, 2.5, 10.5
 */

#include "hal_nrf24.h"
#include "error_codes.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "hal_nrf24";

/* ========================================================================
 * Pin Configuration (M5Stack Cardputer ADV EXT 14-pin — SPI3/VSPI)
 * ======================================================================== */

#define NRF24_SPI_HOST          SPI3_HOST
#define NRF24_PIN_MOSI          GPIO_NUM_35
#define NRF24_PIN_MISO          GPIO_NUM_37
#define NRF24_PIN_CLK           GPIO_NUM_36
#define NRF24_PIN_CS            GPIO_NUM_9
#define NRF24_PIN_CE            GPIO_NUM_10

/** @brief SPI clock speed for NRF24L01+ (max 10 MHz) */
#define NRF24_SPI_CLOCK_HZ      8000000

/** @brief Number of RPD samples per channel during spectrum scan */
#define NRF24_SCAN_SAMPLES      5

/** @brief Settling time after channel change (us) */
#define NRF24_SETTLE_TIME_US    130

/** @brief Time to wait for RPD to be valid after entering RX (us) */
#define NRF24_RPD_WAIT_US       170

/* ========================================================================
 * Module State
 * ======================================================================== */

/** @brief Internal state of the NRF24 HAL module */
typedef struct {
    spi_device_handle_t spi_handle;
    hal_module_state_t module_state;
    nrf24_config_t config;
    bool initialized;
} nrf24_state_t;

static nrf24_state_t s_nrf24 = {
    .spi_handle = NULL,
    .module_state = {
        .status = HAL_STATUS_INACTIVE,
        .last_activity_ms = 0,
        .error_count = 0,
    },
    .initialized = false,
};

/* ========================================================================
 * Internal SPI Helper Functions
 * ======================================================================== */

/**
 * @brief Get current time in milliseconds since boot.
 */
static uint32_t get_time_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/**
 * @brief Write a single byte to an NRF24 register.
 */
static esp_err_t nrf24_write_register(uint8_t reg, uint8_t value)
{
    spi_transaction_t txn = {
        .length = 16,
        .tx_data = { (uint8_t)(NRF24_CMD_W_REGISTER | (reg & 0x1F)), value, 0, 0 },
        .flags = SPI_TRANS_USE_TXDATA,
    };
    return spi_device_polling_transmit(s_nrf24.spi_handle, &txn);
}

/**
 * @brief Read a single byte from an NRF24 register.
 */
static esp_err_t nrf24_read_register(uint8_t reg, uint8_t *value)
{
    uint8_t tx_buf[2] = { (uint8_t)(NRF24_CMD_R_REGISTER | (reg & 0x1F)), 0xFF };
    uint8_t rx_buf[2] = { 0 };

    spi_transaction_t txn = {
        .length = 16,
        .tx_buffer = tx_buf,
        .rx_buffer = rx_buf,
    };

    esp_err_t ret = spi_device_polling_transmit(s_nrf24.spi_handle, &txn);
    if (ret == ESP_OK && value) {
        *value = rx_buf[1];
    }
    return ret;
}

/**
 * @brief Send a single-byte SPI command (no data).
 */
static esp_err_t nrf24_send_command(uint8_t cmd)
{
    spi_transaction_t txn = {
        .length = 8,
        .tx_data = { cmd, 0, 0, 0 },
        .flags = SPI_TRANS_USE_TXDATA,
    };
    return spi_device_polling_transmit(s_nrf24.spi_handle, &txn);
}

/**
 * @brief Read the STATUS register via NOP command.
 */
static esp_err_t nrf24_get_status_reg(uint8_t *status)
{
    uint8_t tx = NRF24_CMD_NOP;
    uint8_t rx = 0;

    spi_transaction_t txn = {
        .length = 8,
        .tx_buffer = &tx,
        .rx_buffer = &rx,
    };

    esp_err_t ret = spi_device_polling_transmit(s_nrf24.spi_handle, &txn);
    if (ret == ESP_OK && status) {
        *status = rx;
    }
    return ret;
}

/**
 * @brief Read RX payload from the FIFO.
 */
static esp_err_t nrf24_read_payload(uint8_t *buf, uint8_t len)
{
    if (len > NRF24_MAX_PAYLOAD_LEN) {
        len = NRF24_MAX_PAYLOAD_LEN;
    }

    uint8_t tx_buf[NRF24_MAX_PAYLOAD_LEN + 1];
    uint8_t rx_buf[NRF24_MAX_PAYLOAD_LEN + 1];
    memset(tx_buf, 0xFF, sizeof(tx_buf));
    memset(rx_buf, 0, sizeof(rx_buf));
    tx_buf[0] = NRF24_CMD_R_RX_PAYLOAD;

    spi_transaction_t txn = {
        .length = (uint32_t)((len + 1) * 8),
        .tx_buffer = tx_buf,
        .rx_buffer = rx_buf,
    };

    esp_err_t ret = spi_device_polling_transmit(s_nrf24.spi_handle, &txn);
    if (ret == ESP_OK) {
        memcpy(buf, &rx_buf[1], len);
    }
    return ret;
}

/**
 * @brief Set CE pin high (enable RX/TX).
 */
static void nrf24_ce_high(void)
{
    gpio_set_level(NRF24_PIN_CE, 1);
}

/**
 * @brief Set CE pin low (disable RX/TX).
 */
static void nrf24_ce_low(void)
{
    gpio_set_level(NRF24_PIN_CE, 0);
}

/**
 * @brief Configure data rate in RF_SETUP register.
 */
static esp_err_t nrf24_set_data_rate(uint8_t data_rate)
{
    uint8_t rf_setup = 0;
    esp_err_t ret = nrf24_read_register(NRF24_REG_RF_SETUP, &rf_setup);
    if (ret != ESP_OK) return ret;

    /* Clear data rate bits */
    rf_setup &= ~(NRF24_RF_SETUP_RF_DR_HIGH | NRF24_RF_SETUP_RF_DR_LOW);

    switch (data_rate) {
        case NRF24_DATA_RATE_1MBPS:
            /* Both bits cleared = 1 Mbps */
            break;
        case NRF24_DATA_RATE_2MBPS:
            rf_setup |= NRF24_RF_SETUP_RF_DR_HIGH;
            break;
        case NRF24_DATA_RATE_250KBPS:
            rf_setup |= NRF24_RF_SETUP_RF_DR_LOW;
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }

    return nrf24_write_register(NRF24_REG_RF_SETUP, rf_setup);
}

/**
 * @brief Set the RF channel (0–125).
 */
static esp_err_t nrf24_set_channel(uint8_t channel)
{
    if (channel > 125) {
        return ESP_ERR_INVALID_ARG;
    }
    return nrf24_write_register(NRF24_REG_RF_CH, channel);
}

/**
 * @brief Set the module into RX mode and enable CE.
 */
static esp_err_t nrf24_enter_rx_mode(void)
{
    uint8_t config = NRF24_CONFIG_PWR_UP | NRF24_CONFIG_PRIM_RX | NRF24_CONFIG_EN_CRC;
    esp_err_t ret = nrf24_write_register(NRF24_REG_CONFIG, config);
    if (ret != ESP_OK) return ret;

    nrf24_ce_high();
    /* Startup delay: 1.5ms from power up to RX mode */
    esp_rom_delay_us(1500);
    return ESP_OK;
}

/**
 * @brief Power down the module and disable CE.
 */
static esp_err_t nrf24_power_down(void)
{
    nrf24_ce_low();
    return nrf24_write_register(NRF24_REG_CONFIG, NRF24_CONFIG_EN_CRC);
}

/**
 * @brief Verify that the NRF24 module responds on SPI.
 *
 * Writes a known value to SETUP_AW register, reads it back,
 * and verifies the round-trip matches.
 */
static bool nrf24_verify_spi(void)
{
    /* Read current SETUP_AW value */
    uint8_t original = 0;
    if (nrf24_read_register(NRF24_REG_SETUP_AW, &original) != ESP_OK) {
        return false;
    }

    /* Write test value (address width = 5 bytes = 0x03) */
    uint8_t test_val = 0x03;
    if (nrf24_write_register(NRF24_REG_SETUP_AW, test_val) != ESP_OK) {
        return false;
    }

    /* Read back and verify */
    uint8_t readback = 0;
    if (nrf24_read_register(NRF24_REG_SETUP_AW, &readback) != ESP_OK) {
        return false;
    }

    /* Restore original value if different */
    if (original != test_val) {
        nrf24_write_register(NRF24_REG_SETUP_AW, original);
    }

    return (readback == test_val);
}

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

esp_err_t hal_nrf24_init(const nrf24_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->channel > 125) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->data_rate > NRF24_DATA_RATE_250KBPS) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->address_width < 3 || config->address_width > 5) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_nrf24.initialized) {
        ESP_LOGW(TAG, "Already initialized, deinit first");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Initializing NRF24L01+ (ch=%d, rate=%d, aw=%d)",
             config->channel, config->data_rate, config->address_width);

    s_nrf24.module_state.status = HAL_STATUS_INITIALIZING;

    /* Configure CE pin as output */
    gpio_config_t ce_cfg = {
        .pin_bit_mask = (1ULL << NRF24_PIN_CE),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&ce_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure CE pin: %s", esp_err_to_name(ret));
        s_nrf24.module_state.status = HAL_STATUS_ERROR;
        return ret;
    }
    nrf24_ce_low();

    /* Configure SPI bus (may already be initialized by other modules) */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = NRF24_PIN_MOSI,
        .miso_io_num = NRF24_PIN_MISO,
        .sclk_io_num = NRF24_PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = NRF24_MAX_PAYLOAD_LEN + 1,
    };

    ret = spi_bus_initialize(NRF24_SPI_HOST, &bus_cfg, SPI_DMA_DISABLED);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        /* ESP_ERR_INVALID_STATE means bus already initialized (shared) */
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        s_nrf24.module_state.status = HAL_STATUS_ERROR;
        return ERR_HAL_NRF_SPI_FAIL;
    }

    /* Add NRF24 as SPI device */
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = NRF24_SPI_CLOCK_HZ,
        .mode = 0,  /* CPOL=0, CPHA=0 */
        .spics_io_num = NRF24_PIN_CS,
        .queue_size = 4,
        .flags = 0,
    };

    ret = spi_bus_add_device(NRF24_SPI_HOST, &dev_cfg, &s_nrf24.spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(ret));
        s_nrf24.module_state.status = HAL_STATUS_ERROR;
        return ERR_HAL_NRF_SPI_FAIL;
    }

    /* Wait for module to respond within timeout (Req 2.5: 3s) */
    uint32_t start_ms = get_time_ms();
    bool present = false;

    while ((get_time_ms() - start_ms) < NRF24_INIT_TIMEOUT_MS) {
        if (nrf24_verify_spi()) {
            present = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (!present) {
        ESP_LOGE(TAG, "NRF24 not responding within %d ms", NRF24_INIT_TIMEOUT_MS);
        spi_bus_remove_device(s_nrf24.spi_handle);
        s_nrf24.spi_handle = NULL;
        s_nrf24.module_state.status = HAL_STATUS_ERROR;
        s_nrf24.module_state.error_count++;
        return ERR_HAL_NRF_TIMEOUT;
    }

    /* Module is present — configure it */
    /* Power down first for clean state */
    nrf24_power_down();

    /* Flush FIFOs */
    nrf24_send_command(NRF24_CMD_FLUSH_TX);
    nrf24_send_command(NRF24_CMD_FLUSH_RX);

    /* Clear interrupt flags */
    nrf24_write_register(NRF24_REG_STATUS,
        NRF24_STATUS_RX_DR | NRF24_STATUS_TX_DS | NRF24_STATUS_MAX_RT);

    /* Disable auto-acknowledgment (promiscuous/sniffing mode) */
    nrf24_write_register(NRF24_REG_EN_AA, 0x00);

    /* Enable RX pipe 0 */
    nrf24_write_register(NRF24_REG_EN_RXADDR, 0x01);

    /* Set address width (register value = address_width - 2) */
    nrf24_write_register(NRF24_REG_SETUP_AW, config->address_width - 2);

    /* Disable retransmission */
    nrf24_write_register(NRF24_REG_SETUP_RETR, 0x00);

    /* Set RF channel */
    ret = nrf24_set_channel(config->channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set channel");
        s_nrf24.module_state.status = HAL_STATUS_ERROR;
        return ret;
    }

    /* Set data rate */
    ret = nrf24_set_data_rate(config->data_rate);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set data rate");
        s_nrf24.module_state.status = HAL_STATUS_ERROR;
        return ret;
    }

    /* Set max payload width for pipe 0 */
    nrf24_write_register(NRF24_REG_RX_PW_P0, NRF24_MAX_PAYLOAD_LEN);

    /* Store configuration */
    memcpy(&s_nrf24.config, config, sizeof(nrf24_config_t));
    s_nrf24.initialized = true;
    s_nrf24.module_state.status = HAL_STATUS_ACTIVE;
    s_nrf24.module_state.last_activity_ms = get_time_ms();

    ESP_LOGI(TAG, "NRF24L01+ initialized successfully");
    return ESP_OK;
}

esp_err_t hal_nrf24_scan_spectrum(nrf24_spectrum_t *result)
{
    if (result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_nrf24.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGD(TAG, "Starting spectrum scan (126 channels)");
    uint32_t scan_start_ms = get_time_ms();

    memset(result->channel_energy, 0, sizeof(result->channel_energy));

    /* Power down to reconfigure */
    nrf24_ce_low();

    /* Set to RX mode with PRX */
    uint8_t config_reg = NRF24_CONFIG_PWR_UP | NRF24_CONFIG_PRIM_RX | NRF24_CONFIG_EN_CRC;
    esp_err_t ret = nrf24_write_register(NRF24_REG_CONFIG, config_reg);
    if (ret != ESP_OK) {
        s_nrf24.module_state.error_count++;
        return ERR_HAL_NRF_SPI_FAIL;
    }

    /* Power-up delay */
    esp_rom_delay_us(1500);

    /*
     * Scan each of the 126 channels.
     * For each channel:
     *   1. Set RF_CH register
     *   2. Enable CE (enter RX)
     *   3. Wait for RPD to settle (~170us)
     *   4. Read RPD register
     *   5. Disable CE
     *   6. Accumulate RPD readings for energy estimate
     *
     * Multiple samples improve accuracy since RPD is binary (0/1).
     * Energy value = (sum of RPD readings / NRF24_SCAN_SAMPLES) * 255
     */
    for (uint8_t ch = 0; ch < NRF24_NUM_CHANNELS; ch++) {
        uint8_t rpd_count = 0;

        ret = nrf24_write_register(NRF24_REG_RF_CH, ch);
        if (ret != ESP_OK) {
            s_nrf24.module_state.error_count++;
            return ERR_HAL_NRF_SPI_FAIL;
        }

        for (uint8_t sample = 0; sample < NRF24_SCAN_SAMPLES; sample++) {
            /* Enable CE to start receiving */
            nrf24_ce_high();

            /* Wait for RPD to be valid (>170us in RX mode) */
            esp_rom_delay_us(NRF24_RPD_WAIT_US);

            /* Disable CE */
            nrf24_ce_low();

            /* Read RPD (Received Power Detector) register */
            uint8_t rpd = 0;
            ret = nrf24_read_register(NRF24_REG_RPD, &rpd);
            if (ret != ESP_OK) {
                s_nrf24.module_state.error_count++;
                return ERR_HAL_NRF_SPI_FAIL;
            }

            if (rpd & 0x01) {
                rpd_count++;
            }
        }

        /* Scale RPD hit count to 0–255 energy level */
        result->channel_energy[ch] = (uint8_t)((rpd_count * 255) / NRF24_SCAN_SAMPLES);
    }

    result->scan_duration_ms = get_time_ms() - scan_start_ms;
    s_nrf24.module_state.last_activity_ms = get_time_ms();

    ESP_LOGD(TAG, "Spectrum scan complete in %lu ms", (unsigned long)result->scan_duration_ms);
    return ESP_OK;
}

esp_err_t hal_nrf24_listen_channel(uint8_t channel, nrf24_packet_t *packet, uint32_t timeout_ms)
{
    if (channel > 125) {
        return ESP_ERR_INVALID_ARG;
    }
    if (packet == NULL || packet->payload == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_nrf24.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Set channel */
    esp_err_t ret = nrf24_set_channel(channel);
    if (ret != ESP_OK) {
        s_nrf24.module_state.error_count++;
        return ERR_HAL_NRF_SPI_FAIL;
    }

    /* Flush RX FIFO to clear stale data */
    nrf24_send_command(NRF24_CMD_FLUSH_RX);

    /* Clear RX_DR flag */
    nrf24_write_register(NRF24_REG_STATUS, NRF24_STATUS_RX_DR);

    /* Enter RX mode */
    ret = nrf24_enter_rx_mode();
    if (ret != ESP_OK) {
        s_nrf24.module_state.error_count++;
        return ERR_HAL_NRF_SPI_FAIL;
    }

    /* Poll for incoming data until timeout */
    uint32_t start_ms = get_time_ms();
    bool received = false;

    while ((get_time_ms() - start_ms) < timeout_ms) {
        uint8_t status = 0;
        ret = nrf24_get_status_reg(&status);
        if (ret != ESP_OK) {
            nrf24_ce_low();
            s_nrf24.module_state.error_count++;
            return ERR_HAL_NRF_SPI_FAIL;
        }

        if (status & NRF24_STATUS_RX_DR) {
            /* Data available in RX FIFO */
            received = true;
            break;
        }

        /* Short delay to avoid hammering SPI */
        esp_rom_delay_us(100);
    }

    /* Disable RX */
    nrf24_ce_low();

    if (!received) {
        return ESP_ERR_TIMEOUT;
    }

    /* Read RPD for RSSI indication */
    uint8_t rpd = 0;
    nrf24_read_register(NRF24_REG_RPD, &rpd);
    packet->rssi_level = rpd & 0x01;

    /* Read payload */
    ret = nrf24_read_payload(packet->payload, NRF24_MAX_PAYLOAD_LEN);
    if (ret != ESP_OK) {
        s_nrf24.module_state.error_count++;
        return ERR_HAL_NRF_SPI_FAIL;
    }

    packet->channel = channel;
    packet->payload_len = NRF24_MAX_PAYLOAD_LEN;
    packet->timestamp_ms = get_time_ms();

    /* Clear RX_DR flag */
    nrf24_write_register(NRF24_REG_STATUS, NRF24_STATUS_RX_DR);

    /* Flush remaining FIFO data */
    nrf24_send_command(NRF24_CMD_FLUSH_RX);

    s_nrf24.module_state.last_activity_ms = get_time_ms();
    return ESP_OK;
}

bool hal_nrf24_is_present(void)
{
    if (s_nrf24.spi_handle == NULL) {
        /*
         * If SPI handle not configured, try a quick init to check.
         * For hot-swap detection, we need the SPI device to be added.
         * If not initialized, we try a simple verification.
         */
        return false;
    }

    return nrf24_verify_spi();
}

hal_status_t hal_nrf24_get_status(void)
{
    return s_nrf24.module_state.status;
}

esp_err_t hal_nrf24_deinit(void)
{
    if (!s_nrf24.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Deinitializing NRF24L01+");

    /* Power down the module */
    nrf24_power_down();

    /* Remove SPI device */
    if (s_nrf24.spi_handle != NULL) {
        spi_bus_remove_device(s_nrf24.spi_handle);
        s_nrf24.spi_handle = NULL;
    }

    /*
     * Note: We do NOT call spi_bus_free() here because the SPI bus
     * is shared with LoRa and SD Card. Bus lifecycle is managed
     * by the hardware manager at a higher level.
     */

    /* Reset CE pin */
    gpio_reset_pin(NRF24_PIN_CE);

    /* Reset module state */
    s_nrf24.initialized = false;
    s_nrf24.module_state.status = HAL_STATUS_INACTIVE;
    s_nrf24.module_state.last_activity_ms = 0;

    ESP_LOGI(TAG, "NRF24L01+ deinitialized");
    return ESP_OK;
}
