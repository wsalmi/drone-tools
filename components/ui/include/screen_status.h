/**
 * @file screen_status.h
 * @brief Field status screen for sources, storage and the Serial link.
 */
#ifndef SCREEN_STATUS_H
#define SCREEN_STATUS_H

#include "esp_err.h"

esp_err_t screen_status_init(void);
esp_err_t screen_status_render(void);
esp_err_t screen_status_deinit(void);

#endif
