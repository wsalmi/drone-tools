/**
 * @file esp_log_mock.c
 * @brief Implementation of esp_log.h mock for host tests.
 *
 * This file is intentionally nearly empty because logging macros
 * are implemented as either no-ops or fprintf in the header.
 * This .c file exists to satisfy the CMake source requirement.
 */

#include "esp_log.h"

/* No runtime functions needed — macros handle everything. */
