#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "global_var.h"
#include "time_func.h"
#include "valve_fn/valve_process.h"

#include "main_process.h"

#define VALVE_TASK_PERIOD_MS     1000

static bool valve_busy = false;

void valve_sync_process(void *pvParameters) 
{
    (void) pvParameters;

    while (1) {

        SetData localServerData;
        xSemaphoreTake(serverMutex, portMAX_DELAY);
        localServerData = serverData;
        xSemaphoreGive(serverMutex);

        xSemaphoreTake(valveMutex, portMAX_DELAY);
        valveData.schedule_control = localServerData.schedule_control;
        valveData.sensor_control = localServerData.sensor_control;
        xSemaphoreGive(valveMutex);

        if (!valve_busy && !localServerData.schedule_control && !localServerData.sensor_control) {
            if (localServerData.set_angle) {
                valve_busy = true;
                int err_code = 0;
                if ( localServerData.angle == 0 ) {
                    err_code = motor_close();
                } else if ( localServerData.angle == 90 ) {
                    err_code = motor_open();
                }

                xSemaphoreTake(valveMutex, portMAX_DELAY);
                if (err_code == 0) {
                    valveData.angle = localServerData.angle;
                    valveData.error_msg[0] = '\0'; 
                } else {
                    sprintf(valveData.error_msg, "Failed to set angle to %d, error code: %d", localServerData.angle, err_code);
                }
                xSemaphoreGive(valveMutex);

                valve_busy = false;
            }

        }


        vTaskDelay(pdMS_TO_TICKS(VALVE_TASK_PERIOD_MS));

    }

}