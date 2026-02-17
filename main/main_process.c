/**
 * @file valve_sync_process.c
 * @brief Synchronization task between server commands and valve hardware
 *
 * This task:
 *  - Reads control data from serverData
 *  - Updates valveData status
 *  - Executes motor actions (open/close)
 *  - Handles error reporting
 *
 * It runs periodically every VALVE_TASK_PERIOD_MS.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "global_var.h"
#include "time_func.h"
#include "valve_fn/valve_process.h"

#include "main_process.h"

/**
 * @brief Task execution period in milliseconds
 */
#define VALVE_TASK_PERIOD_MS     1000

/**
 * @brief Internal flag to prevent concurrent motor operations
 *
 * Ensures that a new motor command is not executed
 * while another one is still being processed.
 */
static bool valve_busy = false;


/* ======================================================================== */
/* ========================== VALVE SYNC TASK ============================= */
/* ======================================================================== */

/**
 * @brief Valve synchronization FreeRTOS task
 *
 * Responsibilities:
 *
 * 1. Copy serverData safely using serverMutex
 * 2. Update valveData control flags
 * 3. If manual angle command is requested:
 *      - Execute motor_open() or motor_close()
 *      - Update valveData status
 *      - Store error message if failure occurs
 *
 * Execution Flow:
 *
 *   serverData  --->  local copy  --->  motor control
 *         ↓
 *   valveData (feedback update)
 *
 * @param pvParameters  Not used
 */
void valve_sync_process(void *pvParameters) 
{
    (void) pvParameters;

    while (1) {

        /* ============================================================= */
        /* 1. SAFELY COPY SERVER DATA                                   */
        /* ============================================================= */

        SetData localServerData;

        /**
         * Lock serverMutex before reading shared serverData
         */
        xSemaphoreTake(serverMutex, portMAX_DELAY);
        localServerData = serverData;
        xSemaphoreGive(serverMutex);


        /* ============================================================= */
        /* 2. UPDATE VALVE CONTROL MODE FLAGS                           */
        /* ============================================================= */

        /**
         * Update valveData control mode flags
         * (Schedule or Sensor mode status)
         */
        xSemaphoreTake(valveMutex, portMAX_DELAY);
        valveData.schedule_control = localServerData.schedule_control;
        valveData.sensor_control   = localServerData.sensor_control;
        xSemaphoreGive(valveMutex);


        /* ============================================================= */
        /* 3. MANUAL ANGLE CONTROL (ONLY WHEN NOT IN AUTO MODES)       */
        /* ============================================================= */

        /**
         * Manual control allowed only when:
         *  - Valve is not busy
         *  - Schedule control is disabled
         *  - Sensor control is disabled
         */
        if (!valve_busy &&
            !localServerData.schedule_control &&
            !localServerData.sensor_control) {

            /**
             * Check if manual angle set command is requested
             */
            if (localServerData.set_angle) {

                valve_busy = true;

                int err_code = 0;

                /**
                 * Execute motor movement based on requested angle
                 * (Assumes 0° = Closed, 90° = Open)
                 */
                if (localServerData.angle == 0) {
                    err_code = motor_close();
                }
                else if (localServerData.angle == 90) {
                    err_code = motor_open();
                }

                /* ===================================================== */
                /* 4. UPDATE VALVE STATUS AND ERROR MESSAGE             */
                /* ===================================================== */

                xSemaphoreTake(valveMutex, portMAX_DELAY);

                if (err_code == 0) {

                    /**
                     * Successful movement
                     */
                    valveData.angle = localServerData.angle;
                    valveData.error_msg[0] = '\0';   // Clear error message
                }
                else {

                    /**
                     * Movement failed — store error message
                     */
                    sprintf(valveData.error_msg,
                            "Failed to set angle to %d, error code: %d",
                            localServerData.angle,
                            err_code);
                }

                xSemaphoreGive(valveMutex);

                valve_busy = false;
            }
        }

        /**
         * Delay before next cycle
         */
        vTaskDelay(pdMS_TO_TICKS(VALVE_TASK_PERIOD_MS));
    }
}
