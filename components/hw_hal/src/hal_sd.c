/**
 * @file hal_sd.c
 * @brief HAL driver for microSD card storage via SDMMC/SPI.
 *
 * Implements:
 * - SDMMC/SPI interface initialization
 * - FAT32 filesystem mount at /sdcard
 * - File operations: open, read, write, close
 * - Free space query
 * - Card presence detection
 */

#include "hal_sd.h"
#include "error_codes.h"
#include "esp_log.h"

#include <string.h>
#include <stdio.h>

#ifndef CONFIG_HAL_SD_MOCK
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#endif

/* ========================================================================
 * Configuration
 * ======================================================================== */

#define SD_TAG                  "hal_sd"

/* SD card SPI pins (shared SPI3/VSPI bus with RF modules) */
#define SD_SPI_HOST             SPI3_HOST
#define SD_PIN_CS               12
#define SD_MAX_OPEN_FILES       2
#define SD_ALLOC_UNIT_SIZE      (4 * 1024)

/* ========================================================================
 * Internal state
 * ======================================================================== */

static struct {
    bool initialized;
    bool mounted;
    hal_module_state_t module_state;
#ifndef CONFIG_HAL_SD_MOCK
    sdmmc_card_t *card;
#endif
} sd_ctx = {
    .initialized = false,
    .mounted = false,
    .module_state = {
        .status = HAL_STATUS_INACTIVE,
        .last_activity_ms = 0,
        .error_count = 0
    }
};

/* ========================================================================
 * Internal helpers
 * ======================================================================== */

/**
 * @brief Build full path by prepending mount point if needed.
 */
static void sd_build_path(const char *path, char *full_path, size_t max_len)
{
    if (strncmp(path, HAL_SD_MOUNT_POINT, strlen(HAL_SD_MOUNT_POINT)) == 0) {
        /* Already has mount point prefix */
        strncpy(full_path, path, max_len - 1);
    } else {
        /* Prepend mount point */
        snprintf(full_path, max_len, "%s%s%s",
                 HAL_SD_MOUNT_POINT,
                 (path[0] == '/') ? "" : "/",
                 path);
    }
    full_path[max_len - 1] = '\0';
}

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

esp_err_t hal_sd_init(void)
{
    if (sd_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    sd_ctx.module_state.status = HAL_STATUS_INITIALIZING;
    sd_ctx.module_state.error_count = 0;

#ifndef CONFIG_HAL_SD_MOCK
    /* Configure pull-ups for SPI lines and CS pin */
    gpio_set_pull_mode(SD_PIN_CS, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(GPIO_NUM_39, GPIO_PULLUP_ONLY); /* MISO */
    gpio_set_pull_mode(GPIO_NUM_14, GPIO_PULLUP_ONLY); /* MOSI */
    gpio_set_pull_mode(GPIO_NUM_40, GPIO_PULLUP_ONLY); /* SCK */

    /* Mount configuration */
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = SD_MAX_OPEN_FILES,
        .allocation_unit_size = SD_ALLOC_UNIT_SIZE,
    };

    /* SPI device configuration for SD slot */
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_PIN_CS;
    slot_config.host_id = SD_SPI_HOST;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT; /* 20 MHz default */

    esp_err_t err = esp_vfs_fat_sdspi_mount(
        HAL_SD_MOUNT_POINT,
        &host,
        &slot_config,
        &mount_config,
        &sd_ctx.card
    );

    /* If default frequency fails, attempt fallback at 10 MHz */
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(SD_TAG, "SD mount at 20MHz failed (%s), retrying at 10MHz...", esp_err_to_name(err));
        host.max_freq_khz = 10000;
        err = esp_vfs_fat_sdspi_mount(
            HAL_SD_MOUNT_POINT,
            &host,
            &slot_config,
            &mount_config,
            &sd_ctx.card
        );
    }

    if (err != ESP_OK) {
        if (err == ESP_FAIL) {
            ESP_LOGE(SD_TAG, "Failed to mount filesystem");
        } else {
            ESP_LOGE(SD_TAG, "Failed to initialize SD card: %s",
                     esp_err_to_name(err));
        }
        sd_ctx.module_state.status = HAL_STATUS_ERROR;
        return (err == ESP_ERR_NOT_FOUND) ? ERR_HAL_SD_ABSENT : err;
    }

    /* Log card info */
    sdmmc_card_print_info(stdout, sd_ctx.card);
    sd_ctx.mounted = true;
#else
    sd_ctx.mounted = true;
#endif

    sd_ctx.initialized = true;
    sd_ctx.module_state.status = HAL_STATUS_ACTIVE;

    ESP_LOGI(SD_TAG, "SD card initialized and mounted at %s",
             HAL_SD_MOUNT_POINT);
    return ESP_OK;
}

esp_err_t hal_sd_open(const char *path, const char *mode, hal_sd_file_t *file)
{
    if (path == NULL || mode == NULL || file == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!sd_ctx.initialized || !sd_ctx.mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Build full path */
    char full_path[HAL_SD_MAX_PATH_LEN];
    sd_build_path(path, full_path, sizeof(full_path));

    /* Open file */
    FILE *fp = fopen(full_path, mode);
    if (fp == NULL) {
        ESP_LOGW(SD_TAG, "Failed to open file: %s", full_path);
        sd_ctx.module_state.error_count++;
        return ESP_ERR_NOT_FOUND;
    }

    /* Fill handle */
    file->fp = (void *)fp;
    strncpy(file->path, full_path, HAL_SD_MAX_PATH_LEN - 1);
    file->path[HAL_SD_MAX_PATH_LEN - 1] = '\0';
    file->is_open = true;

    return ESP_OK;
}

esp_err_t hal_sd_write(hal_sd_file_t *file, const void *data, size_t len,
                        size_t *written)
{
    if (file == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!file->is_open || file->fp == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t bytes_written = fwrite(data, 1, len, (FILE *)file->fp);

    if (written != NULL) {
        *written = bytes_written;
    }

    if (bytes_written < len) {
        /* Could be disk full or other write error */
        if (ferror((FILE *)file->fp)) {
            sd_ctx.module_state.error_count++;
            ESP_LOGW(SD_TAG, "Write error on %s", file->path);
            return ERR_HAL_SD_FULL;
        }
    }

    return ESP_OK;
}

esp_err_t hal_sd_read(hal_sd_file_t *file, void *buf, size_t len,
                       size_t *read_bytes)
{
    if (file == NULL || buf == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!file->is_open || file->fp == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t bytes_read = fread(buf, 1, len, (FILE *)file->fp);

    if (read_bytes != NULL) {
        *read_bytes = bytes_read;
    }

    return ESP_OK;
}

esp_err_t hal_sd_close(hal_sd_file_t *file)
{
    if (file == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!file->is_open || file->fp == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    fclose((FILE *)file->fp);
    file->fp = NULL;
    file->is_open = false;

    return ESP_OK;
}

esp_err_t hal_sd_get_free_space(uint64_t *free_bytes)
{
    if (free_bytes == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!sd_ctx.initialized || !sd_ctx.mounted) {
        return ESP_ERR_INVALID_STATE;
    }

#ifndef CONFIG_HAL_SD_MOCK
    FATFS *fs;
    DWORD free_clusters;
    FRESULT res = f_getfree("0:", &free_clusters, &fs);
    if (res != FR_OK) {
        sd_ctx.module_state.error_count++;
        return ESP_FAIL;
    }

    uint64_t total_sectors = (uint64_t)free_clusters * fs->csize;
    /* Sector size is typically 512 bytes */
    *free_bytes = total_sectors * 512;
#else
    /* Mock: return 1 GB free */
    *free_bytes = (uint64_t)1024 * 1024 * 1024;
#endif

    return ESP_OK;
}

bool hal_sd_is_mounted(void)
{
    return sd_ctx.initialized && sd_ctx.mounted;
}

hal_status_t hal_sd_get_status(void)
{
    return sd_ctx.module_state.status;
}

esp_err_t hal_sd_deinit(void)
{
    if (!sd_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

#ifndef CONFIG_HAL_SD_MOCK
    /* Unmount filesystem */
    esp_vfs_fat_sdcard_unmount(HAL_SD_MOUNT_POINT, sd_ctx.card);
    sd_ctx.card = NULL;
#endif

    sd_ctx.mounted = false;
    sd_ctx.initialized = false;
    sd_ctx.module_state.status = HAL_STATUS_INACTIVE;

    ESP_LOGI(SD_TAG, "SD card deinitialized");
    return ESP_OK;
}
