/**
 * @file hal_sd_mock.c
 * @brief Mock implementation of HAL SD Card for host tests.
 *
 * Simulates an in-memory filesystem for testing data logging
 * without requiring an actual SD card. Supports mount/unmount
 * simulation and controllable failure injection.
 */

#include "hal_sd.h"
#include "hal_common.h"
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ========================================================================
 * Mock Configuration
 * ======================================================================== */

/** Maximum number of simultaneously open mock files */
#define MOCK_SD_MAX_FILES       8

/** Maximum content size per mock file (256 KB for testing) */
#define MOCK_SD_MAX_FILE_SIZE   (256 * 1024)

/* ========================================================================
 * Mock State
 * ======================================================================== */

/** In-memory file storage */
typedef struct {
    char path[HAL_SD_MAX_PATH_LEN];
    char *content;
    size_t size;
    size_t capacity;
    size_t read_pos;
    bool in_use;
} mock_file_t;

static bool s_mock_sd_mounted = false;
static bool s_mock_sd_initialized = false;
static uint64_t s_mock_free_space = 1024ULL * 1024 * 1024; /* 1 GB default */
static hal_status_t s_mock_sd_status = HAL_STATUS_INACTIVE;
static mock_file_t s_mock_files[MOCK_SD_MAX_FILES];
static esp_err_t s_mock_write_result = ESP_OK;

/* ========================================================================
 * Mock Control Functions
 * ======================================================================== */

void mock_hal_sd_reset(void)
{
    s_mock_sd_mounted = false;
    s_mock_sd_initialized = false;
    s_mock_free_space = 1024ULL * 1024 * 1024;
    s_mock_sd_status = HAL_STATUS_INACTIVE;
    s_mock_write_result = ESP_OK;

    for (int i = 0; i < MOCK_SD_MAX_FILES; i++) {
        if (s_mock_files[i].content != NULL) {
            free(s_mock_files[i].content);
            s_mock_files[i].content = NULL;
        }
        memset(&s_mock_files[i], 0, sizeof(mock_file_t));
    }
}

void mock_hal_sd_set_mounted(bool mounted)
{
    s_mock_sd_mounted = mounted;
    s_mock_sd_status = mounted ? HAL_STATUS_ACTIVE : HAL_STATUS_INACTIVE;
}

void mock_hal_sd_set_free_space(uint64_t free_bytes)
{
    s_mock_free_space = free_bytes;
}

void mock_hal_sd_set_write_result(esp_err_t result)
{
    s_mock_write_result = result;
}

/**
 * @brief Get content written to a specific file path (for assertions).
 */
const char *mock_hal_sd_get_file_content(const char *path, size_t *out_size)
{
    for (int i = 0; i < MOCK_SD_MAX_FILES; i++) {
        if (s_mock_files[i].in_use && strcmp(s_mock_files[i].path, path) == 0) {
            if (out_size) {
                *out_size = s_mock_files[i].size;
            }
            return s_mock_files[i].content;
        }
    }
    if (out_size) *out_size = 0;
    return NULL;
}

/* ========================================================================
 * HAL SD Interface Implementation (Mock)
 * ======================================================================== */

esp_err_t hal_sd_init(void)
{
    if (s_mock_sd_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    s_mock_sd_initialized = true;
    s_mock_sd_mounted = true;
    s_mock_sd_status = HAL_STATUS_ACTIVE;
    return ESP_OK;
}

esp_err_t hal_sd_open(const char *path, const char *mode, hal_sd_file_t *file)
{
    if (path == NULL || mode == NULL || file == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_mock_sd_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Find or allocate a mock file slot */
    int slot = -1;

    /* Check if file already exists */
    for (int i = 0; i < MOCK_SD_MAX_FILES; i++) {
        if (s_mock_files[i].in_use && strcmp(s_mock_files[i].path, path) == 0) {
            slot = i;
            break;
        }
    }

    /* If mode is "w", create new or truncate existing */
    if (mode[0] == 'w') {
        if (slot < 0) {
            /* Find empty slot */
            for (int i = 0; i < MOCK_SD_MAX_FILES; i++) {
                if (!s_mock_files[i].in_use) {
                    slot = i;
                    break;
                }
            }
            if (slot < 0) {
                return ESP_FAIL; /* No free slots */
            }
        }
        /* Reset file content */
        if (s_mock_files[slot].content != NULL) {
            free(s_mock_files[slot].content);
        }
        s_mock_files[slot].content = (char *)calloc(1, MOCK_SD_MAX_FILE_SIZE);
        if (s_mock_files[slot].content == NULL) {
            return ESP_ERR_NO_MEM;
        }
        s_mock_files[slot].capacity = MOCK_SD_MAX_FILE_SIZE;
        s_mock_files[slot].size = 0;
        s_mock_files[slot].read_pos = 0;
        s_mock_files[slot].in_use = true;
        strncpy(s_mock_files[slot].path, path, HAL_SD_MAX_PATH_LEN - 1);
    } else if (mode[0] == 'a') {
        /* Append mode */
        if (slot < 0) {
            /* Create new */
            for (int i = 0; i < MOCK_SD_MAX_FILES; i++) {
                if (!s_mock_files[i].in_use) {
                    slot = i;
                    break;
                }
            }
            if (slot < 0) {
                return ESP_FAIL;
            }
            s_mock_files[slot].content = (char *)calloc(1, MOCK_SD_MAX_FILE_SIZE);
            if (s_mock_files[slot].content == NULL) {
                return ESP_ERR_NO_MEM;
            }
            s_mock_files[slot].capacity = MOCK_SD_MAX_FILE_SIZE;
            s_mock_files[slot].size = 0;
            s_mock_files[slot].in_use = true;
            strncpy(s_mock_files[slot].path, path, HAL_SD_MAX_PATH_LEN - 1);
        }
        s_mock_files[slot].read_pos = s_mock_files[slot].size;
    } else if (mode[0] == 'r') {
        /* Read mode — file must exist */
        if (slot < 0) {
            return ESP_ERR_NOT_FOUND;
        }
        s_mock_files[slot].read_pos = 0;
    }

    /* Set up file handle */
    file->fp = (void *)(intptr_t)(slot + 1); /* Use slot+1 as non-NULL identifier */
    strncpy(file->path, path, HAL_SD_MAX_PATH_LEN - 1);
    file->is_open = true;

    return ESP_OK;
}

esp_err_t hal_sd_write(hal_sd_file_t *file, const void *data, size_t len, size_t *written)
{
    if (file == NULL || !file->is_open || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_mock_write_result != ESP_OK) {
        if (written) *written = 0;
        return s_mock_write_result;
    }

    int slot = (int)(intptr_t)file->fp - 1;
    if (slot < 0 || slot >= MOCK_SD_MAX_FILES || !s_mock_files[slot].in_use) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Check capacity */
    size_t avail = s_mock_files[slot].capacity - s_mock_files[slot].size;
    size_t to_write = (len <= avail) ? len : avail;

    if (to_write > 0) {
        memcpy(s_mock_files[slot].content + s_mock_files[slot].size, data, to_write);
        s_mock_files[slot].size += to_write;
    }

    if (written) {
        *written = to_write;
    }

    return ESP_OK;
}

esp_err_t hal_sd_read(hal_sd_file_t *file, void *buf, size_t len, size_t *read_bytes)
{
    if (file == NULL || !file->is_open || buf == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int slot = (int)(intptr_t)file->fp - 1;
    if (slot < 0 || slot >= MOCK_SD_MAX_FILES || !s_mock_files[slot].in_use) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t remaining = s_mock_files[slot].size - s_mock_files[slot].read_pos;
    size_t to_read = (len <= remaining) ? len : remaining;

    if (to_read > 0) {
        memcpy(buf, s_mock_files[slot].content + s_mock_files[slot].read_pos, to_read);
        s_mock_files[slot].read_pos += to_read;
    }

    if (read_bytes) {
        *read_bytes = to_read;
    }

    return ESP_OK;
}

esp_err_t hal_sd_close(hal_sd_file_t *file)
{
    if (file == NULL || !file->is_open) {
        return ESP_ERR_INVALID_ARG;
    }
    file->is_open = false;
    file->fp = NULL;
    return ESP_OK;
}

esp_err_t hal_sd_get_free_space(uint64_t *free_bytes)
{
    if (free_bytes == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_mock_sd_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    *free_bytes = s_mock_free_space;
    return ESP_OK;
}

bool hal_sd_is_mounted(void)
{
    return s_mock_sd_mounted;
}

hal_status_t hal_sd_get_status(void)
{
    return s_mock_sd_status;
}

esp_err_t hal_sd_deinit(void)
{
    if (!s_mock_sd_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    s_mock_sd_initialized = false;
    s_mock_sd_mounted = false;
    s_mock_sd_status = HAL_STATUS_INACTIVE;
    return ESP_OK;
}
