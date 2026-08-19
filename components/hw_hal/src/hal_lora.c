/**
 * @file hal_lora.c
 * @brief SX1262 LoRa transceiver driver implementation via SPI3 (VSPI).
 *
 * Implements the HAL LoRa interface for the Semtech SX1262 chip used in
 * the M5Stack Cap LoRa module. Communication is done via ESP-IDF SPI master
 * driver on SPI3 (VSPI) bus shared with NRF24 and SD Card.
 *
 * SX1262 command interface uses SPI with NSS (chip select) framing.
 * Commands are sent as opcode + parameters, responses read after a
 * NOP byte following the command.
 */

#include "hal_lora.h"
#include "error_codes.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#if defined(ESP_PLATFORM)
#include "esp_rom_sys.h"
#else
#define esp_rom_delay_us(us) ((void)0)
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "hal_lora";

/* ========================================================================
 * SX1262 Register and Command Definitions
 * ======================================================================== */

/* SX1262 Opcodes */
#define SX1262_CMD_SET_SLEEP            0x84
#define SX1262_CMD_SET_STANDBY          0x80
#define SX1262_CMD_SET_FS               0xC1
#define SX1262_CMD_SET_TX               0x83
#define SX1262_CMD_SET_RX               0x82
#define SX1262_CMD_STOP_TIMER_ON_PREAMBLE 0x9F
#define SX1262_CMD_SET_CAD              0xC5
#define SX1262_CMD_SET_TX_CONTINUOUS    0xD1
#define SX1262_CMD_SET_TX_INFINITE_PREAMBLE 0xD2
#define SX1262_CMD_SET_REGULATOR_MODE   0x96
#define SX1262_CMD_CALIBRATE            0x89
#define SX1262_CMD_CALIBRATE_IMAGE      0x98
#define SX1262_CMD_SET_PA_CONFIG        0x95
#define SX1262_CMD_SET_RX_TX_FALLBACK_MODE 0x93
#define SX1262_CMD_WRITE_REGISTER       0x0D
#define SX1262_CMD_READ_REGISTER        0x1D
#define SX1262_CMD_WRITE_BUFFER         0x0E
#define SX1262_CMD_READ_BUFFER          0x1E
#define SX1262_CMD_SET_DIO_IRQ_PARAMS   0x08
#define SX1262_CMD_GET_IRQ_STATUS       0x12
#define SX1262_CMD_CLEAR_IRQ_STATUS     0x02
#define SX1262_CMD_SET_DIO2_AS_RF_SWITCH 0x9D
#define SX1262_CMD_SET_DIO3_AS_TCXO_CTRL 0x97
#define SX1262_CMD_SET_RF_FREQUENCY     0x86
#define SX1262_CMD_SET_PACKET_TYPE      0x8A
#define SX1262_CMD_GET_PACKET_TYPE      0x11
#define SX1262_CMD_SET_TX_PARAMS        0x8E
#define SX1262_CMD_SET_MODULATION_PARAMS 0x8B
#define SX1262_CMD_SET_PACKET_PARAMS    0x8C
#define SX1262_CMD_SET_CAD_PARAMS       0x88
#define SX1262_CMD_SET_BUFFER_BASE_ADDR 0x8F
#define SX1262_CMD_SET_LORA_SYMB_NUM_TIMEOUT 0xA0
#define SX1262_CMD_GET_STATUS           0xC0
#define SX1262_CMD_GET_RX_BUFFER_STATUS 0x13
#define SX1262_CMD_GET_PACKET_STATUS    0x14
#define SX1262_CMD_GET_RSSI_INST        0x15
#define SX1262_CMD_GET_STATS            0x10
#define SX1262_CMD_RESET_STATS          0x00
#define SX1262_CMD_GET_DEVICE_ERRORS    0x17
#define SX1262_CMD_CLEAR_DEVICE_ERRORS  0x07

/* SX1262 Standby modes */
#define SX1262_STANDBY_RC               0x00
#define SX1262_STANDBY_XOSC             0x01

/* SX1262 Packet types */
#define SX1262_PKT_TYPE_GFSK            0x00
#define SX1262_PKT_TYPE_LORA            0x01

/* SX1262 Regulator modes */
#define SX1262_REGULATOR_LDO            0x00
#define SX1262_REGULATOR_DC_DC          0x01

/* SX1262 IRQ flags */
#define SX1262_IRQ_TX_DONE              (1 << 0)
#define SX1262_IRQ_RX_DONE              (1 << 1)
#define SX1262_IRQ_PREAMBLE_DETECTED    (1 << 2)
#define SX1262_IRQ_SYNC_WORD_VALID      (1 << 3)
#define SX1262_IRQ_HEADER_VALID         (1 << 4)
#define SX1262_IRQ_HEADER_ERR           (1 << 5)
#define SX1262_IRQ_CRC_ERR              (1 << 6)
#define SX1262_IRQ_CAD_DONE             (1 << 7)
#define SX1262_IRQ_CAD_DETECTED         (1 << 8)
#define SX1262_IRQ_TIMEOUT              (1 << 9)
#define SX1262_IRQ_RX_TX_TIMEOUT        (1 << 9)
#define SX1262_IRQ_ALL                  0x03FF

/* Compatibility aliases */
#define SX1262_CMD_SET_PKT_PARAMS       SX1262_CMD_SET_PACKET_PARAMS
#define SX1262_CMD_SET_PKT_TYPE         SX1262_CMD_SET_PACKET_TYPE
#define SX1262_CMD_GET_PKT_STATUS       SX1262_CMD_GET_PACKET_STATUS
#define SX1262_CMD_CLR_IRQ_STATUS       SX1262_CMD_CLEAR_IRQ_STATUS
#define SX1262_RX_CONTINUOUS            0xFFFFFF

/* SX1262 LoRa Bandwidth options */
#define SX1262_LORA_BW_7                0x00  /* 7.81 kHz */
#define SX1262_LORA_BW_10               0x08  /* 10.42 kHz */
#define SX1262_LORA_BW_15               0x01  /* 15.63 kHz */
#define SX1262_LORA_BW_20               0x09  /* 20.83 kHz */
#define SX1262_LORA_BW_31               0x02  /* 31.25 kHz */
#define SX1262_LORA_BW_41               0x0A  /* 41.67 kHz */
#define SX1262_LORA_BW_62               0x03  /* 62.50 kHz */
#define SX1262_LORA_BW_125              0x04  /* 125.00 kHz */
#define SX1262_LORA_BW_250              0x05  /* 250.00 kHz */
#define SX1262_LORA_BW_500              0x06  /* 500.00 kHz */

/* SX1262 LoRa Coding Rate options */
#define SX1262_LORA_CR_4_5              0x01
#define SX1262_LORA_CR_4_6              0x02
#define SX1262_LORA_CR_4_7              0x03
#define SX1262_LORA_CR_4_8              0x04

/* ========================================================================
 * GPIO Pin Definitions for Cardputer Cap LoRa
 * ======================================================================== */

/* M5 Cap LoRa868 for Cardputer ADV: SCK=40, MOSI=14, MISO=39,
 * NSS=5, RST=3, IRQ=4 and BUSY=6.  SPI pins are configured by main.c. */
#define PIN_LORA_CS         5
#define PIN_LORA_RESET      3
#define PIN_LORA_IRQ        4
#define PIN_LORA_BUSY       6

/* ========================================================================
 * Module Internal State
 * ======================================================================== */

typedef struct {
    spi_device_handle_t spi_handle;
    hal_module_state_t state;
    lora_config_t config;
    bool spi_bus_initialized;
    bool in_rx_mode;
} hal_lora_ctx_t;

static hal_lora_ctx_t s_ctx = {
    .spi_handle = NULL,
    .state = {
        .status = HAL_STATUS_INACTIVE,
        .last_activity_ms = 0,
        .error_count = 0,
    },
    .config = {0},
    .spi_bus_initialized = false,
    .in_rx_mode = false,
};

/* ========================================================================
 * Internal Helper Functions
 * ======================================================================== */

/**
 * @brief Get current time in milliseconds since boot.
 */
static uint32_t get_time_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/**
 * @brief Wait until the SX1262 BUSY pin goes low or timeout.
 * @param timeout_ms Maximum time to wait.
 * @return ESP_OK if BUSY went low, ESP_ERR_TIMEOUT otherwise.
 */
static esp_err_t sx1262_wait_busy(uint32_t timeout_ms)
{
    uint32_t start = get_time_ms();
    while (gpio_get_level(PIN_LORA_BUSY)) {
        if ((get_time_ms() - start) > timeout_ms) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return ESP_OK;
}

/**
 * @brief Send a command to the SX1262 via SPI.
 * @param opcode Command opcode byte.
 * @param params Pointer to parameter bytes (can be NULL if param_len == 0).
 * @param param_len Number of parameter bytes to send.
 * @return ESP_OK on success, error code on failure.
 */
static esp_err_t sx1262_write_command(uint8_t opcode, const uint8_t *params,
                                      size_t param_len)
{
    if (s_ctx.spi_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    sx1262_wait_busy(100);

    uint8_t tx[36];
    tx[0] = opcode;
    if (param_len > 0 && params != NULL) {
        size_t copy_len = param_len > 32 ? 32 : param_len;
        memcpy(&tx[1], params, copy_len);
    }

    spi_transaction_t t = {
        .length = (1 + param_len) * 8,
        .tx_buffer = tx,
    };
    return spi_device_polling_transmit(s_ctx.spi_handle, &t);
}

/**
 * @brief Read data from SX1262 via command + NOP read pattern.
 * @param opcode Command opcode byte.
 * @param params Command parameters (sent after opcode).
 * @param param_len Number of parameter bytes.
 * @param result Buffer to store read bytes.
 * @param result_len Number of bytes to read.
 * @return ESP_OK on success, error code on failure.
 */
static esp_err_t sx1262_read_command(uint8_t opcode, const uint8_t *params,
                                     size_t param_len, uint8_t *result,
                                     size_t result_len)
{
    if (s_ctx.spi_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    sx1262_wait_busy(100);

    uint8_t tx[36] = {0};
    uint8_t rx[36] = {0};
    tx[0] = opcode;
    if (param_len > 0 && params != NULL) {
        size_t copy_len = param_len > 30 ? 30 : param_len;
        memcpy(&tx[1], params, copy_len);
    }

    size_t total_len = 1 + param_len + 1 + result_len;
    if (opcode == SX1262_CMD_GET_STATUS) {
        total_len = 2;
    }

    spi_transaction_t t = {
        .length = total_len * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };

    esp_err_t ret = spi_device_polling_transmit(s_ctx.spi_handle, &t);
    if (ret == ESP_OK && result != NULL && result_len > 0) {
        if (opcode == SX1262_CMD_GET_STATUS) {
            result[0] = rx[1];
        } else {
            memcpy(result, &rx[1 + param_len + 1], result_len);
        }
    }

    return ret;
}

/**
 * @brief Write to SX1262 buffer memory.
 */
static esp_err_t __attribute__((unused)) sx1262_write_buffer(uint8_t offset, const uint8_t *data,
                                                             size_t len)
{
    if (s_ctx.spi_handle == NULL) return ESP_ERR_INVALID_STATE;
    sx1262_wait_busy(100);
    if (len > 256) len = 256;

    uint8_t tx[260];
    tx[0] = SX1262_CMD_WRITE_BUFFER;
    tx[1] = offset;
    if (data && len > 0) {
        memcpy(&tx[2], data, len);
    }

    spi_transaction_t t = {
        .length = (2 + len) * 8,
        .tx_buffer = tx,
    };
    return spi_device_polling_transmit(s_ctx.spi_handle, &t);
}

/**
 * @brief Read from SX1262 buffer memory.
 */
static esp_err_t sx1262_read_buffer(uint8_t offset, uint8_t *data, size_t len)
{
    if (s_ctx.spi_handle == NULL) return ESP_ERR_INVALID_STATE;
    sx1262_wait_busy(100);
    if (len > 256) len = 256;

    uint8_t tx[260] = {0};
    uint8_t rx[260] = {0};
    tx[0] = SX1262_CMD_READ_BUFFER;
    tx[1] = offset;
    tx[2] = 0x00; /* Status NOP */

    spi_transaction_t t = {
        .length = (3 + len) * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };

    esp_err_t ret = spi_device_polling_transmit(s_ctx.spi_handle, &t);
    if (ret == ESP_OK && data != NULL) {
        memcpy(data, &rx[3], len);
    }

    return ret;
}

/**
 * @brief Hardware reset the SX1262.
 */
static void sx1262_hw_reset(void)
{
    gpio_set_level(PIN_LORA_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_level(PIN_LORA_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

/**
 * @brief Verify SX1262 communication by reading status.
 * @return ESP_OK if communication is working and device is present.
 */
static esp_err_t sx1262_verify_communication(void)
{
    uint8_t status = 0xFF;
    esp_err_t err = sx1262_read_command(SX1262_CMD_GET_STATUS, NULL, 0, &status, 1);
    ESP_LOGI(TAG, "SX1262 GetStatus -> err=%d, status=0x%02X", err, status);

    if (err == ESP_OK && status != 0x00 && status != 0xFF) {
        uint8_t mode = (status >> 4) & 0x07;
        ESP_LOGI(TAG, "SX1262 detected successfully! (status=0x%02X, mode=%d)", status, mode);
        return ESP_OK;
    }

    return ERR_HAL_LORA_NO_DEVICE;
}

/**
 * @brief Convert bandwidth in Hz to SX1262 register value.
 */
static uint8_t bandwidth_to_reg(uint32_t bw_hz)
{
    switch (bw_hz) {
        case 125000:  return 0x04;  /* BW 125 kHz */
        case 250000:  return 0x05;  /* BW 250 kHz */
        case 500000:  return 0x06;  /* BW 500 kHz */
        default:      return 0x04;  /* Default to 125 kHz */
    }
}

/**
 * @brief Convert coding rate to SX1262 register value.
 * Coding rate is specified as denominator (5–8 for 4/5 to 4/8).
 */
static uint8_t coding_rate_to_reg(uint8_t cr)
{
    if (cr < 5) cr = 5;
    if (cr > 8) cr = 8;
    return cr - 4;  /* 4/5 = 1, 4/6 = 2, 4/7 = 3, 4/8 = 4 */
}

/**
 * @brief Configure SX1262 modulation parameters for LoRa.
 */
static esp_err_t sx1262_set_modulation_params(uint8_t sf, uint32_t bw_hz,
                                               uint8_t cr)
{
    uint8_t params[4];
    params[0] = sf;                      /* Spreading Factor */
    params[1] = bandwidth_to_reg(bw_hz); /* Bandwidth */
    params[2] = coding_rate_to_reg(cr);  /* Coding Rate */
    params[3] = 0x00;                    /* Low Data Rate Optimize (auto) */

    /* Enable LDRO for SF11/SF12 with 125 kHz BW */
    if (bw_hz == 125000 && sf >= 11) {
        params[3] = 0x01;
    }

    return sx1262_write_command(SX1262_CMD_SET_MODULATION_PARAMS, params, 4);
}

/**
 * @brief Configure SX1262 packet parameters for LoRa reception.
 */
static esp_err_t sx1262_set_packet_params(void)
{
    uint8_t params[6];
    params[0] = 0x00;  /* Preamble Length MSB */
    params[1] = 0x08;  /* Preamble Length LSB (8 symbols) */
    params[2] = 0x00;  /* Header Type: Explicit (variable length) */
    params[3] = 0xFF;  /* Max Payload Length (255 bytes) */
    params[4] = 0x01;  /* CRC On */
    params[5] = 0x00;  /* Standard IQ */

    return sx1262_write_command(SX1262_CMD_SET_PKT_PARAMS, params, 6);
}

/**
 * @brief Set the RF frequency on the SX1262.
 */
static esp_err_t sx1262_set_rf_frequency(uint32_t freq_hz)
{
    /* Frequency = (freq_hz * 2^25) / 32 MHz crystal */
    uint32_t freq_reg = (uint32_t)((uint64_t)freq_hz * (1ULL << 25) /
                                    32000000ULL);

    uint8_t params[4];
    params[0] = (freq_reg >> 24) & 0xFF;
    params[1] = (freq_reg >> 16) & 0xFF;
    params[2] = (freq_reg >> 8) & 0xFF;
    params[3] = freq_reg & 0xFF;

    return sx1262_write_command(SX1262_CMD_SET_RF_FREQUENCY, params, 4);
}

/**
 * @brief Configure DIO and IRQ mapping for RX.
 */
static esp_err_t sx1262_configure_irq_for_rx(void)
{
    /* Enable RX_DONE and CRC_ERR on DIO1 */
    uint16_t irq_mask = SX1262_IRQ_RX_DONE | SX1262_IRQ_CRC_ERR |
                        SX1262_IRQ_RX_TX_TIMEOUT;
    uint16_t dio1_mask = irq_mask;
    uint16_t dio2_mask = 0x0000;
    uint16_t dio3_mask = 0x0000;

    uint8_t params[8];
    params[0] = (irq_mask >> 8) & 0xFF;
    params[1] = irq_mask & 0xFF;
    params[2] = (dio1_mask >> 8) & 0xFF;
    params[3] = dio1_mask & 0xFF;
    params[4] = (dio2_mask >> 8) & 0xFF;
    params[5] = dio2_mask & 0xFF;
    params[6] = (dio3_mask >> 8) & 0xFF;
    params[7] = dio3_mask & 0xFF;

    return sx1262_write_command(SX1262_CMD_SET_DIO_IRQ_PARAMS, params, 8);
}

/**
 * @brief Full SX1262 configuration sequence for LoRa RX mode.
 */
static esp_err_t sx1262_configure(const lora_config_t *config)
{
    esp_err_t ret;

    /* Set standby mode (RC oscillator) */
    uint8_t standby_param = SX1262_STANDBY_RC;
    ret = sx1262_write_command(SX1262_CMD_SET_STANDBY, &standby_param, 1);
    if (ret != ESP_OK) return ret;

    /* Set packet type to LoRa */
    uint8_t pkt_type = SX1262_PKT_TYPE_LORA;
    ret = sx1262_write_command(SX1262_CMD_SET_PKT_TYPE, &pkt_type, 1);
    if (ret != ESP_OK) return ret;

    /* Set DIO2 as RF switch control (common for SX1262 modules) */
    uint8_t dio2_param = 0x01;
    ret = sx1262_write_command(SX1262_CMD_SET_DIO2_AS_RF_SWITCH, &dio2_param, 1);
    if (ret != ESP_OK) return ret;

    /* Set regulator mode to DC-DC for better efficiency */
    uint8_t reg_mode = SX1262_REGULATOR_DC_DC;
    ret = sx1262_write_command(SX1262_CMD_SET_REGULATOR_MODE, &reg_mode, 1);
    if (ret != ESP_OK) return ret;

    /* Calibrate image for the frequency band */
    uint8_t cal_params[2];
    if (config->frequency_hz < 900000000) {
        cal_params[0] = 0xD7;  /* 863–870 MHz */
        cal_params[1] = 0xDB;
    } else {
        cal_params[0] = 0xE1;  /* 902–928 MHz */
        cal_params[1] = 0xE9;
    }
    ret = sx1262_write_command(SX1262_CMD_CALIBRATE_IMAGE, cal_params, 2);
    if (ret != ESP_OK) return ret;

    /* Set RF frequency */
    ret = sx1262_set_rf_frequency(config->frequency_hz);
    if (ret != ESP_OK) return ret;

    /* Set modulation parameters */
    ret = sx1262_set_modulation_params(config->spreading_factor,
                                       config->bandwidth_hz,
                                       config->coding_rate);
    if (ret != ESP_OK) return ret;

    /* Set packet parameters (explicit header, variable length, CRC on) */
    ret = sx1262_set_packet_params();
    if (ret != ESP_OK) return ret;

    /* Set buffer base addresses (TX=0x00, RX=0x00) */
    uint8_t buf_addr[2] = { 0x00, 0x00 };
    ret = sx1262_write_command(SX1262_CMD_SET_BUFFER_BASE_ADDR, buf_addr, 2);
    if (ret != ESP_OK) return ret;

    /* Configure IRQ for RX done, CRC error, timeout */
    ret = sx1262_configure_irq_for_rx();
    if (ret != ESP_OK) return ret;

    /* Set sync word for public LoRa network (0x3444) via register write */
    uint8_t sync_word_params[5];
    sync_word_params[0] = 0x07;  /* Register addr MSB (0x0740) */
    sync_word_params[1] = 0x40;  /* Register addr LSB */
    sync_word_params[2] = 0x34;  /* Sync word byte 1 */
    /* Second register at 0x0741 */
    ret = sx1262_write_command(SX1262_CMD_WRITE_REGISTER, sync_word_params, 3);
    if (ret != ESP_OK) return ret;

    uint8_t sync_word_params2[3];
    sync_word_params2[0] = 0x07;
    sync_word_params2[1] = 0x41;
    sync_word_params2[2] = 0x44;
    ret = sx1262_write_command(SX1262_CMD_WRITE_REGISTER, sync_word_params2, 3);
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}

/**
 * @brief Initialize SPI bus and GPIO pins for SX1262.
 */
static esp_err_t sx1262_spi_init(void)
{
    if (s_ctx.spi_bus_initialized && s_ctx.spi_handle != NULL) {
        return ESP_OK;
    }

    gpio_reset_pin(PIN_LORA_BUSY);
    gpio_set_direction(PIN_LORA_BUSY, GPIO_MODE_INPUT);
    gpio_set_pull_mode(PIN_LORA_BUSY, GPIO_PULLDOWN_ONLY);

    gpio_reset_pin(PIN_LORA_IRQ);
    gpio_set_direction(PIN_LORA_IRQ, GPIO_MODE_INPUT);

    gpio_reset_pin(PIN_LORA_RESET);
    gpio_set_direction(PIN_LORA_RESET, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_LORA_RESET, 1);

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 2000000, /* 2 MHz for SX1262 */
        .mode = 0,                 /* SPI Mode 0 (CPOL=0, CPHA=0) */
        .spics_io_num = PIN_LORA_CS,
        .queue_size = 3,
    };

    esp_err_t err = spi_bus_add_device(SPI3_HOST, &devcfg, &s_ctx.spi_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add LoRa device to SPI3_HOST: %s", esp_err_to_name(err));
        return err;
    }

    s_ctx.spi_bus_initialized = true;
    return ESP_OK;
}

/**
 * @brief Release SPI device and optionally free the bus.
 */
static void sx1262_spi_deinit(void)
{
    if (s_ctx.spi_handle != NULL) {
        spi_bus_remove_device(s_ctx.spi_handle);
        s_ctx.spi_handle = NULL;
    }
    s_ctx.spi_bus_initialized = false;
}

/**
 * @brief Validate LoRa configuration parameters.
 */
static esp_err_t validate_config(const lora_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (config->frequency_hz < HAL_LORA_FREQ_MIN_HZ ||
        config->frequency_hz > HAL_LORA_FREQ_MAX_HZ) {
        ESP_LOGE(TAG, "Frequency %lu Hz out of range [%lu, %lu]",
                 (unsigned long)config->frequency_hz,
                 (unsigned long)HAL_LORA_FREQ_MIN_HZ,
                 (unsigned long)HAL_LORA_FREQ_MAX_HZ);
        return ESP_ERR_INVALID_ARG;
    }

    if (config->spreading_factor < 6 || config->spreading_factor > 12) {
        ESP_LOGE(TAG, "SF %d out of range [6, 12]", config->spreading_factor);
        return ESP_ERR_INVALID_ARG;
    }

    if (config->bandwidth_hz != 125000 && config->bandwidth_hz != 250000 &&
        config->bandwidth_hz != 500000) {
        ESP_LOGE(TAG, "BW %lu Hz invalid (125000/250000/500000)",
                 (unsigned long)config->bandwidth_hz);
        return ESP_ERR_INVALID_ARG;
    }

    if (config->coding_rate < 5 || config->coding_rate > 8) {
        ESP_LOGE(TAG, "CR %d out of range [5, 8]", config->coding_rate);
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

esp_err_t hal_lora_init(const lora_config_t *config)
{
    esp_err_t ret;

    /* Validate configuration */
    ret = validate_config(config);
    if (ret != ESP_OK) {
        return ret;
    }

    /* Already initialized? */
    if (s_ctx.state.status == HAL_STATUS_ACTIVE) {
        ESP_LOGW(TAG, "Already initialized, deinit first");
        return ESP_ERR_INVALID_STATE;
    }

    s_ctx.state.status = HAL_STATUS_INITIALIZING;
    memcpy(&s_ctx.config, config, sizeof(lora_config_t));

    /* Initialize SPI */
    ret = sx1262_spi_init();
    if (ret != ESP_OK) {
        s_ctx.state.status = HAL_STATUS_ERROR;
        s_ctx.state.error_count++;
        return ret;
    }

    /* Retry loop: up to 3 attempts with 2s interval */
    for (int attempt = 1; attempt <= HAL_LORA_INIT_RETRY_COUNT; attempt++) {
        ESP_LOGI(TAG, "Init attempt %d/%d", attempt, HAL_LORA_INIT_RETRY_COUNT);

        /* Hardware reset */
        sx1262_hw_reset();

        /* Wait for BUSY to go low after reset */
        ret = sx1262_wait_busy(500);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Attempt %d: BUSY timeout after reset", attempt);
            s_ctx.state.error_count++;
            if (attempt < HAL_LORA_INIT_RETRY_COUNT) {
                vTaskDelay(pdMS_TO_TICKS(HAL_LORA_INIT_RETRY_INTERVAL_MS));
            }
            continue;
        }

        /* Wake from reset into Standby RC mode */
        uint8_t stdby_mode = SX1262_STANDBY_RC;
        sx1262_write_command(SX1262_CMD_SET_STANDBY, &stdby_mode, 1);
        vTaskDelay(pdMS_TO_TICKS(10));

        /* Verify SPI communication */
        ret = sx1262_verify_communication();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Attempt %d: Communication verify failed", attempt);
            s_ctx.state.error_count++;
            if (attempt < HAL_LORA_INIT_RETRY_COUNT) {
                vTaskDelay(pdMS_TO_TICKS(HAL_LORA_INIT_RETRY_INTERVAL_MS));
            }
            continue;
        }

        /* Full configuration */
        ret = sx1262_configure(config);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Attempt %d: Configuration failed", attempt);
            s_ctx.state.error_count++;
            if (attempt < HAL_LORA_INIT_RETRY_COUNT) {
                vTaskDelay(pdMS_TO_TICKS(HAL_LORA_INIT_RETRY_INTERVAL_MS));
            }
            continue;
        }

        /* Success */
        s_ctx.state.status = HAL_STATUS_ACTIVE;
        s_ctx.state.last_activity_ms = get_time_ms();
        s_ctx.in_rx_mode = false;
        ESP_LOGI(TAG, "Initialized successfully (attempt %d, freq=%lu Hz, "
                 "SF%d, BW=%lu, CR=4/%d)",
                 attempt,
                 (unsigned long)config->frequency_hz,
                 config->spreading_factor,
                 (unsigned long)config->bandwidth_hz,
                 config->coding_rate);
        return ESP_OK;
    }

    /* All retries exhausted — enter ERROR state */
    ESP_LOGE(TAG, "Initialization failed after %d attempts",
             HAL_LORA_INIT_RETRY_COUNT);
    s_ctx.state.status = HAL_STATUS_ERROR;
    sx1262_spi_deinit();
    return ERR_HAL_LORA_TIMEOUT;
}

esp_err_t hal_lora_set_frequency(uint32_t freq_hz)
{
    if (s_ctx.state.status != HAL_STATUS_ACTIVE) {
        return ESP_ERR_INVALID_STATE;
    }

    if (freq_hz < HAL_LORA_FREQ_MIN_HZ || freq_hz > HAL_LORA_FREQ_MAX_HZ) {
        ESP_LOGE(TAG, "Frequency %lu Hz out of range", (unsigned long)freq_hz);
        return ESP_ERR_INVALID_ARG;
    }

    /* Need to go to standby to change frequency */
    uint8_t standby_param = SX1262_STANDBY_RC;
    esp_err_t ret = sx1262_write_command(SX1262_CMD_SET_STANDBY,
                                          &standby_param, 1);
    if (ret != ESP_OK) {
        s_ctx.state.error_count++;
        return ret;
    }

    /* Re-calibrate image if band changed significantly */
    uint8_t cal_params[2];
    if (freq_hz < 900000000) {
        cal_params[0] = 0xD7;
        cal_params[1] = 0xDB;
    } else {
        cal_params[0] = 0xE1;
        cal_params[1] = 0xE9;
    }
    ret = sx1262_write_command(SX1262_CMD_CALIBRATE_IMAGE, cal_params, 2);
    if (ret != ESP_OK) {
        s_ctx.state.error_count++;
        return ret;
    }

    /* Set new frequency */
    ret = sx1262_set_rf_frequency(freq_hz);
    if (ret != ESP_OK) {
        s_ctx.state.error_count++;
        return ret;
    }

    s_ctx.config.frequency_hz = freq_hz;
    s_ctx.in_rx_mode = false;  /* Went to standby, no longer in RX */
    s_ctx.state.last_activity_ms = get_time_ms();

    ESP_LOGD(TAG, "Frequency set to %lu Hz", (unsigned long)freq_hz);
    return ESP_OK;
}

esp_err_t hal_lora_start_rx(void)
{
    if (s_ctx.state.status != HAL_STATUS_ACTIVE) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Clear all IRQ flags before starting RX */
    uint8_t clr_params[2] = { 0x03, 0xFF };  /* Clear all IRQs */
    esp_err_t ret = sx1262_write_command(SX1262_CMD_CLR_IRQ_STATUS,
                                          clr_params, 2);
    if (ret != ESP_OK) {
        s_ctx.state.error_count++;
        return ret;
    }

    /* Set RX continuous mode (timeout = 0xFFFFFF) */
    uint8_t rx_params[3] = {
        (SX1262_RX_CONTINUOUS >> 16) & 0xFF,
        (SX1262_RX_CONTINUOUS >> 8) & 0xFF,
        SX1262_RX_CONTINUOUS & 0xFF
    };
    ret = sx1262_write_command(SX1262_CMD_SET_RX, rx_params, 3);
    if (ret != ESP_OK) {
        s_ctx.state.error_count++;
        return ret;
    }

    s_ctx.in_rx_mode = true;
    s_ctx.state.last_activity_ms = get_time_ms();
    ESP_LOGD(TAG, "Continuous RX started at %lu Hz",
             (unsigned long)s_ctx.config.frequency_hz);
    return ESP_OK;
}

esp_err_t hal_lora_get_packet(lora_packet_t *packet, uint32_t timeout_ms)
{
    if (packet == NULL || packet->payload == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_ctx.state.status != HAL_STATUS_ACTIVE) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_ctx.in_rx_mode) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Poll DIO1 or IRQ status for RX_DONE */
    uint32_t start = get_time_ms();
    bool packet_received = false;

    while (!packet_received) {
        /* Check elapsed time */
        uint32_t elapsed = get_time_ms() - start;
        if (elapsed > timeout_ms) {
            return ESP_ERR_TIMEOUT;
        }

        /* Check DIO1 pin for interrupt indication */
        if (gpio_get_level(PIN_LORA_IRQ)) {
            /* Read IRQ status to confirm RX_DONE */
            uint8_t irq_raw[2] = {0};
            esp_err_t ret = sx1262_read_command(SX1262_CMD_GET_IRQ_STATUS,
                                                 NULL, 0, irq_raw, 2);
            if (ret != ESP_OK) {
                s_ctx.state.error_count++;
                return ret;
            }

            uint16_t irq_status = ((uint16_t)irq_raw[0] << 8) | irq_raw[1];

            if (irq_status & SX1262_IRQ_RX_DONE) {
                packet_received = true;

                /* Check for CRC error */
                if (irq_status & SX1262_IRQ_CRC_ERR) {
                    /* Clear IRQ and continue waiting */
                    uint8_t clr[2] = { 0x03, 0xFF };
                    sx1262_write_command(SX1262_CMD_CLR_IRQ_STATUS, clr, 2);
                    packet_received = false;
                    continue;
                }
            } else {
                /* Spurious DIO1 — clear and continue */
                uint8_t clr[2] = { 0x03, 0xFF };
                sx1262_write_command(SX1262_CMD_CLR_IRQ_STATUS, clr, 2);
            }
        }

        if (!packet_received) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    /* Get RX buffer status: payload length and start offset */
    uint8_t rx_buf_status[2] = {0};
    esp_err_t ret = sx1262_read_command(SX1262_CMD_GET_RX_BUFFER_STATUS,
                                         NULL, 0, rx_buf_status, 2);
    if (ret != ESP_OK) {
        s_ctx.state.error_count++;
        return ret;
    }

    uint8_t payload_len = rx_buf_status[0];
    uint8_t rx_start_offset = rx_buf_status[1];

    if (payload_len == 0) {
        /* Clear IRQ and report no data */
        uint8_t clr[2] = { 0x03, 0xFF };
        sx1262_write_command(SX1262_CMD_CLR_IRQ_STATUS, clr, 2);
        return ESP_ERR_TIMEOUT;
    }

    /* Read payload from buffer */
    ret = sx1262_read_buffer(rx_start_offset, packet->payload, payload_len);
    if (ret != ESP_OK) {
        s_ctx.state.error_count++;
        return ret;
    }
    packet->payload_len = payload_len;

    /* Get packet status (RSSI, SNR) */
    uint8_t pkt_status[3] = {0};
    ret = sx1262_read_command(SX1262_CMD_GET_PKT_STATUS, NULL, 0,
                              pkt_status, 3);
    if (ret != ESP_OK) {
        s_ctx.state.error_count++;
        return ret;
    }

    /* For LoRa: byte[0] = RssiPkt, byte[1] = SnrPkt, byte[2] = SignalRssiPkt */
    packet->rssi_dbm = -((int16_t)pkt_status[0] / 2);
    packet->snr_db = (int8_t)pkt_status[1] / 4;
    packet->frequency_hz = s_ctx.config.frequency_hz;
    packet->timestamp_ms = get_time_ms();

    /* Clear IRQ flags */
    uint8_t clr[2] = { 0x03, 0xFF };
    sx1262_write_command(SX1262_CMD_CLR_IRQ_STATUS, clr, 2);

    s_ctx.state.last_activity_ms = get_time_ms();
    ESP_LOGD(TAG, "Packet received: %d bytes, RSSI=%d dBm, SNR=%d dB",
             payload_len, packet->rssi_dbm, packet->snr_db);

    return ESP_OK;
}

hal_status_t hal_lora_get_status(void)
{
    return s_ctx.state.status;
}

esp_err_t hal_lora_deinit(void)
{
    if (s_ctx.state.status == HAL_STATUS_INACTIVE) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Deinitializing LoRa module");

    /* Put SX1262 in sleep mode to save power */
    if (s_ctx.spi_handle != NULL) {
        uint8_t sleep_params = 0x00;  /* Cold start (no config retention) */
        /* Best effort — don't fail deinit if sleep command fails */
        sx1262_write_command(SX1262_CMD_SET_SLEEP, &sleep_params, 1);
    }

    /* Release SPI resources */
    sx1262_spi_deinit();

    /* Reset internal state */
    s_ctx.state.status = HAL_STATUS_INACTIVE;
    s_ctx.state.last_activity_ms = 0;
    s_ctx.in_rx_mode = false;
    memset(&s_ctx.config, 0, sizeof(lora_config_t));

    ESP_LOGI(TAG, "LoRa module deinitialized (errors: %lu)",
             (unsigned long)s_ctx.state.error_count);
    return ESP_OK;
}
