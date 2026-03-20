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
#include "freertos/semphr.h"

#include <time.h>
#include <stdbool.h>
#include <string.h>
#include "esp_log.h"

#include "global_var.h"
#include "time_func.h"
#include "valve_fn/valve_process.h"
#include "eeprom_fn/schedule_storage.h" 

#include "main_process.h"

/**
 * @brief Task execution period in milliseconds
 */
#define VALVE_TASK_PERIOD_MS     1000
#define MAX_SCHEDULES 10

/**
 * @brief Internal flag to prevent concurrent motor operations
 *
 * Ensures that a new motor command is not executed
 * while another one is still being processed.
 */
static bool valve_busy = false;

static const char *TAG_SCHEDULE = "SCHEDULE";


int parse_time_str(const char *str) {
    int h, m;
    if (sscanf(str, "%d:%d", &h, &m) == 2) {
        return h * 60 + m; // minutes since midnight
    }
    return -1; // error
}


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


        // localServerData.schedule_control = true;


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

        /* ============================================================= */
        /* 5. SCHEDULE-BASED AUTO CONTROL                                */
        /* ============================================================= */

        if (localServerData.schedule_control) {

            time_t now;
            struct tm timeinfo;
            time(&now);
            localtime_r(&now, &timeinfo);

            const char *week_days[7] = {"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
            const char *today_str = week_days[timeinfo.tm_wday];
            int current_minutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;

            bool should_open = false;

            for (int i = 0; i < 10; i++) {
                ScheduleInfo *sched = &loaded_schedule[i];

                if (sched->day[0] == '\0') continue;

                // Apply if today or "Everyday"
                ESP_LOGI(TAG_SCHEDULE, "Checking schedule: %s, open: %s, close: %s\n", sched->day, sched->open, sched->close);
                if (strcmp(sched->day, today_str) != 0 && strcmp(sched->day, "Every day") != 0) {
                    continue;
                }

                int open_minutes  = parse_time_str(sched->open);
                int close_minutes = parse_time_str(sched->close);
                ESP_LOGI(TAG_SCHEDULE, "Parsed times - Open: %d, Close: %d, Current: %d\n", open_minutes, close_minutes, current_minutes);

                if (open_minutes < 0 || close_minutes < 0) continue;

                if (current_minutes >= open_minutes &&
                    current_minutes < close_minutes) {

                    should_open = true;
                    break;  // 🚀 IMPORTANT: stop checking further
                }
            }

            int target_angle = should_open ? 90 : 0;

            if (!valve_busy && valveData.angle != target_angle) {

                valve_busy = true;
                int err_code = 0;

                if (target_angle == 90) {
                    err_code = motor_open();
                }
                else {
                    err_code = motor_close();
                }

                if (xSemaphoreTake(valveMutex, portMAX_DELAY) == pdTRUE) {
                    if (err_code == 0) {
                        valveData.angle = target_angle;
                        valveData.error_msg[0] = '\0';
                    }
                    else {
                        sprintf(valveData.error_msg,
                                "Schedule control failed to set angle to %d, error code: %d",
                                target_angle,
                                err_code);
                    }

                    xSemaphoreGive(valveMutex);
                }
   

                valve_busy = false;
                
            }
        }

        /**
         * Delay before next cycle
         */
        vTaskDelay(pdMS_TO_TICKS(VALVE_TASK_PERIOD_MS));
    }
}





bool schedules_are_equal(ScheduleInfo *a, ScheduleInfo *b, int count) {
    for (int i = 0; i < count; i++) {
        if (strcmp(a[i].day, b[i].day) != 0) return false;
        if (strcmp(a[i].open, b[i].open) != 0) return false;
        if (strcmp(a[i].close, b[i].close) != 0) return false;
    }
    return true;
}


// void print_schedule(const char *title, ScheduleInfo *sched) {
//     printf("%s\n", title);

//     for (int i = 0; i < loaded_count; i++) {  // <-- use loaded_count
//         if (sched[i].day[0] != '\0') {
//             printf("  %s: %s - %s\n",
//                 sched[i].day,
//                 sched[i].open,
//                 sched[i].close);
//         }
//     }
// }

void print_schedule(const char *title, ScheduleInfo *sched) {
    char buffer[512];
    int offset = 0;

    // Add title
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "%s: ", title);

    for (int i = 0; i < loaded_count && i < 10; i++) {
        if (sched[i].day[0] != '\0') {
            offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                               "[%s %s-%s] ",
                               sched[i].day,
                               sched[i].open,
                               sched[i].close);
        }

        if (offset >= sizeof(buffer)) {
            break;
        }
    }

    ESP_LOGI(TAG_SCHEDULE, "%s", buffer);
}


void schedule_save_task(void *pvParameters) {
    (void) pvParameters;

    ScheduleInfo schedule_copy[MAX_SCHEDULES];

    while (1) {
        bool schedule_changed = false;

        memset(schedule_copy, 0, sizeof(schedule_copy));

        xSemaphoreTake(serverMutex, portMAX_DELAY);
        memcpy(schedule_copy, serverControl.schedule_info, sizeof(schedule_copy));
        bool set_schedule = serverControl.set_schedule;
        xSemaphoreGive(serverMutex);

        if (set_schedule) {

            // printf("Comparing schedules...\n");
            print_schedule("Loaded schedule:", loaded_schedule);
            // print_schedule("New schedule:", schedule_copy);

            schedule_changed = !schedules_are_equal(
                loaded_schedule,
                schedule_copy,
                MAX_SCHEDULES
            );
        }

        if (schedule_changed) {
            if (schedule_storage_save(schedule_copy, MAX_SCHEDULES) == ESP_OK) {

                ESP_LOGI(TAG_SCHEDULE,"SCHEDULE_TASK: Schedule saved to NVS\n");

                memcpy(loaded_schedule, schedule_copy, sizeof(loaded_schedule));

                // IMPORTANT: Reset flag
                xSemaphoreTake(serverMutex, portMAX_DELAY);
                serverControl.set_schedule = false;
                xSemaphoreGive(serverMutex);

            } else {
                ESP_LOGE(TAG_SCHEDULE,"SCHEDULE_TASK: Failed to save schedule\n");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}