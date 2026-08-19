/**
 * @file serial_bridge.h
 * @brief NDJSON bridge over USB Serial/JTAG for the external GitHub Pages UI.
 */
#ifndef SERIAL_BRIDGE_H
#define SERIAL_BRIDGE_H

#include <stdbool.h>

#include "esp_err.h"

esp_err_t serial_bridge_init(void);
bool serial_bridge_is_ready(void);

#endif
