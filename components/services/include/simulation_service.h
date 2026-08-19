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

/** Reset trajectories to a deterministic initial state for repeatable tests. */
esp_err_t simulation_service_reset(void);

/** Select the deterministic trajectory profile used by the emulator. */
esp_err_t simulation_service_set_scenario(simulation_scenario_t scenario);

/** Get the profile currently used by the emulator. */
simulation_scenario_t simulation_service_get_scenario(void);

/** Return the number of deterministic emulator ticks since the last reset. */
uint32_t simulation_service_get_tick_count(void);

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
typedef enum {
    SIM_SCENARIO_FIELD_DEMO = 0,
    SIM_SCENARIO_SPARSE,
    SIM_SCENARIO_DENSE,
    SIM_SCENARIO_COUNT
} simulation_scenario_t;
