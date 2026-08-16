/**
 * @file simulation_service.h
 * @brief Autonomous Simulation Service for testing & field demonstration.
 */

#ifndef SIMULATION_SERVICE_H
#define SIMULATION_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the simulation service.
 *
 * @return ESP_OK on success.
 */
esp_err_t simulation_service_init(void);

/**
 * @brief Process one simulation tick (updates simulated drone trajectories and injects detections).
 *
 * Should be called periodically (e.g., every 500ms).
 *
 * @return ESP_OK on success.
 */
esp_err_t simulation_service_tick(void);

/**
 * @brief Enable or disable the simulation engine.
 *
 * @param enabled true to enable simulation, false to disable.
 */
void simulation_service_set_enabled(bool enabled);

/**
 * @brief Check if simulation is currently enabled.
 *
 * @return true if enabled, false otherwise.
 */
bool simulation_service_is_enabled(void);

/**
 * @brief Deinitialize the simulation service.
 *
 * @return ESP_OK on success.
 */
esp_err_t simulation_service_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* SIMULATION_SERVICE_H */
