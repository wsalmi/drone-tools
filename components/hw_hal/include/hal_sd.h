/**
 * @file hal_sd.h
 * @brief HAL interface for microSD card storage.
 *
 * Provides file operations for the microSD card connected via SDMMC/SPI.
 * Used for data logging (CSV telemetry logs), configuration files,
 * and protocol signature databases.
 *
 * Mount point: /sdcard
 * Filesystem: FAT32
 */

#ifndef HAL_SD_H
#define HAL_SD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "hal_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Types
 * ======================================================================== */

/** @brief SD card mount point path */
#define HAL_SD_MOUNT_POINT      "/sdcard"

/** @brief Maximum file path length */
#define HAL_SD_MAX_PATH_LEN     128

/**
 * @brief SD card file handle.
 *
 * Wraps a stdio FILE pointer with additional metadata for tracking.
 */
typedef struct {
    void *fp;                   /**< Internal file pointer (FILE*) */
    char path[HAL_SD_MAX_PATH_LEN]; /**< Full path of the opened file */
    bool is_open;               /**< Whether this handle is currently valid */
} hal_sd_file_t;

/* ========================================================================
 * Public API
 * ======================================================================== */

/**
 * @brief Initialize the SD card and mount the filesystem.
 *
 * Configures the SDMMC/SPI interface, detects the card, and mounts
 * the FAT32 filesystem at /sdcard.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already initialized,
 *         ERR_HAL_SD_ABSENT if card is not inserted
 */
esp_err_t hal_sd_init(void);

/**
 * @brief Open a file on the SD card.
 *
 * @param path  File path relative to mount point (e.g., "/logs/data.csv")
 *              or absolute path starting with /sdcard/
 * @param mode  File open mode (same as fopen: "r", "w", "a", "rb", "wb", etc.)
 * @param[out] file  Pointer to file handle to initialize
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if arguments are NULL,
 *         ESP_ERR_NOT_FOUND if file doesn't exist (in read mode),
 *         ESP_ERR_INVALID_STATE if SD not initialized
 */
esp_err_t hal_sd_open(const char *path, const char *mode, hal_sd_file_t *file);

/**
 * @brief Write data to an open file.
 *
 * @param file     Pointer to an open file handle
 * @param data     Pointer to data buffer to write
 * @param len      Number of bytes to write
 * @param[out] written  Pointer to receive actual bytes written (may be NULL)
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if arguments are invalid,
 *         ERR_HAL_SD_FULL if card is full
 */
esp_err_t hal_sd_write(hal_sd_file_t *file, const void *data, size_t len, size_t *written);

/**
 * @brief Read data from an open file.
 *
 * @param file       Pointer to an open file handle
 * @param[out] buf   Buffer to receive read data
 * @param len        Maximum number of bytes to read
 * @param[out] read_bytes  Pointer to receive actual bytes read (may be NULL)
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if arguments are invalid
 */
esp_err_t hal_sd_read(hal_sd_file_t *file, void *buf, size_t len, size_t *read_bytes);

/**
 * @brief Close an open file.
 *
 * Flushes any buffered data and releases the file handle.
 *
 * @param file  Pointer to the file handle to close
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if file is NULL or not open
 */
esp_err_t hal_sd_close(hal_sd_file_t *file);

/**
 * @brief Get the free space available on the SD card.
 *
 * @param[out] free_bytes  Pointer to receive free space in bytes
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if free_bytes is NULL,
 *         ESP_ERR_INVALID_STATE if SD not initialized
 */
esp_err_t hal_sd_get_free_space(uint64_t *free_bytes);

/**
 * @brief Check if the SD card is currently mounted and accessible.
 *
 * @return true if card is mounted and filesystem is accessible
 */
bool hal_sd_is_mounted(void);

/**
 * @brief Get the current operational status of the SD card module.
 *
 * @return Current hal_status_t value
 */
hal_status_t hal_sd_get_status(void);

/**
 * @brief Deinitialize the SD card and unmount the filesystem.
 *
 * Closes any open files, unmounts the filesystem, and releases
 * the SPI/SDMMC bus.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t hal_sd_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_SD_H */
