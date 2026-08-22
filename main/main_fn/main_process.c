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
#include <math.h>
#include <string.h>
#include "esp_log.h"

#include "global_fn/global_var.h"
#include "time_fn/time_func.h"
#include "valve_fn/valve_process.h"
#include "eeprom_fn/schedule_storage.h" 

#include "main_fn/main_process.h"

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


/**
 * @brief Sensor process tuning
 */
#define SENSOR_OFFLINE_SEC      30      /* no fresh reading for 30 sec = offline */
#define SENSOR_ANGLE_TOLERANCE  2.0f        /* same band the schedule process uses  */
#define SENSOR_FAILSAFE_ANGLE   0       /* value outside every rule range -> close */


static const char *TAG_SENSOR = "SENSOR";
static const char *TAG_SYNC = "SYNC";
static const char *TAG_SCHEDULE = "SCHEDULE";


int parse_time_str(const char *str) {
    int h, m;
    if (sscanf(str, "%d:%d", &h, &m) == 2) {
        return h * 60 + m; // minutes since midnight
    }
    return -1; // error
}

/*
 * Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm).
 * Used instead of timegm(), which is not guaranteed on this newlib build.
 */
static long days_from_civil(int y, unsigned m, unsigned d)
{
    y -= (m <= 2);
    const long     era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097L + (long)doe - 719468L;
}

static time_t utc_epoch_from_parts(int Y, int M, int D, int h, int m, int s)
{
    return (time_t)(days_from_civil(Y, (unsigned)M, (unsigned)D) * 86400L
                    + h * 3600L + m * 60L + s);
}

/*
 * Parse the sensor's "last_seen" string into an absolute epoch.
 *
 * Accepted forms:
 *   2026-08-22T10:15:30Z        -> UTC
 *   2026-08-22T10:15:30+05:30   -> explicit offset
 *   2026-08-22T10:15:30+0530    -> explicit offset, compact
 *   2026-08-22T10:15:30         -> assumed to be device LOCAL time
 *   2026-08-22 10:15:30         -> same, space separator
 *
 * Returns false for empty/unparsable input. The caller MUST treat that as
 * "sensor offline" — never as "fresh".
 */
static bool parse_last_seen(const char *str, time_t *out)
{
    if (str == NULL || str[0] == '\0' || out == NULL) return false;

    int Y = 0, M = 0, D = 0, h = 0, mi = 0, s = 0;

    int n = sscanf(str, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &mi, &s);
    if (n < 5) {
        n = sscanf(str, "%d-%d-%d %d:%d:%d", &Y, &M, &D, &h, &mi, &s);
    }
    if (n < 5) return false;

    if (M < 1 || M > 12 || D < 1 || D > 31 ||
        h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 60) {
        return false;
    }

    /* Skip the date's own '-' separators before hunting for a zone marker */
    const char *tz = (strlen(str) > 10) ? strpbrk(str + 10, "Zz+-") : NULL;

    if (tz == NULL) {
        /* No zone info: interpret the stamp in the device's local timezone */
        struct tm lt = { 0 };
        lt.tm_year  = Y - 1900;
        lt.tm_mon   = M - 1;
        lt.tm_mday  = D;
        lt.tm_hour  = h;
        lt.tm_min   = mi;
        lt.tm_sec   = s;
        lt.tm_isdst = -1;

        time_t e = mktime(&lt);
        if (e == (time_t)-1) return false;
        *out = e;
        return true;
    }

    /* Zone-aware: build UTC epoch, then remove the stated offset */
    time_t epoch = utc_epoch_from_parts(Y, M, D, h, mi, s);

    if (*tz == '+' || *tz == '-') {
        char digits[5] = { 0 };
        int  j = 0;
        for (const char *p = tz + 1; *p != '\0' && j < 4; p++) {
            if (*p >= '0' && *p <= '9')      digits[j++] = *p;
            else if (*p == ':')              continue;
            else                             break;
        }

        int oh = 0, om = 0;
        if (j == 4) {
            oh = (digits[0] - '0') * 10 + (digits[1] - '0');
            om = (digits[2] - '0') * 10 + (digits[3] - '0');
        } else if (j == 2) {
            oh = (digits[0] - '0') * 10 + (digits[1] - '0');
        }

        long off = oh * 3600L + om * 60L;
        epoch += (*tz == '+') ? -off : off;
    }

    *out = epoch;
    return true;
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

        // ESP_LOGI(TAG_SYNC, "Sync process");

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
            if (localServerData.user_control) {

                valve_busy = true;

                int err_code = 0;

                /**
                 * Execute motor movement based on requested angle
                 * (Assumes 0° = Closed, 90° = Open)
                 */
                const float tolerance = 5.0; 
                int adc = pot_read_filtered(&potentiometer);
                float current_angle = pot_to_angle(&potentiometer, adc);
                if (fabs(localServerData.angle - current_angle) >= tolerance) {
                    err_code = motor_set_angle(localServerData.angle);
                }
                
                /* ===================================================== */
                /* 4. UPDATE VALVE STATUS AND ERROR MESSAGE             */
                /* ===================================================== */

                adc = pot_read_filtered(&potentiometer);
                current_angle = pot_to_angle(&potentiometer, adc);
                xSemaphoreTake(valveMutex, portMAX_DELAY);
                if (err_code == 0) {
                    /**
                     * Successful movement
                     */
                    valveData.encoder_value = adc;
                    valveData.angle = current_angle;
                    valveData.error_msg[0] = '\0';   // Clear error message
                }
                else {
                    /**
                     * Movement failed — store error message
                     */
                    sprintf(valveData.error_msg, "Failed to set angle to %d, error code: %d", localServerData.angle, err_code);
                }
                xSemaphoreGive(valveMutex);

                valve_busy = false;
            }
        } else {
            ESP_LOGI(TAG_SYNC, "Manual mode disable");
        }

        /* ============================================================= */
        /* 5. SCHEDULE-BASED AUTO CONTROL                                */
        /* ============================================================= */

        if ((localServerData.schedule_control || loaded_schedule_ctrl) && !localServerData.sensor_control) {

            time_t now;
            struct tm timeinfo;
            time(&now);
            localtime_r(&now, &timeinfo);
            ESP_LOGI(TAG_SCHEDULE,
                "System time inside of process: %04d-%02d-%02d %02d:%02d:%02d",
                timeinfo.tm_year + 1900,
                timeinfo.tm_mon + 1,
                timeinfo.tm_mday,
                timeinfo.tm_hour,
                timeinfo.tm_min,
                timeinfo.tm_sec);

            const char *week_days[7] = {"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
            const char *today_str = week_days[timeinfo.tm_wday];
            int current_minutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;

            int  target_angle = 0;
            bool matched      = false;
            
            xSemaphoreTake(scheduleMutex, portMAX_DELAY);
            for (int i = 0; i < 10; i++) {
                ScheduleInfo *sched = &loaded_schedule[i];

                if (sched->day[0] == '\0') continue;

                // Apply if today or "Everyday"
                ESP_LOGI(TAG_SCHEDULE, "Checking schedule: %s, open: %s, close: %s, angle: %d\n", sched->day, sched->open, sched->close, sched->angle);
                if (strcmp(sched->day, today_str) != 0 && strcmp(sched->day, "Every day") != 0) {
                    continue;
                }

                int open_minutes  = parse_time_str(sched->open);
                int close_minutes = parse_time_str(sched->close);
                ESP_LOGI(TAG_SCHEDULE, "Parsed times - Open: %d, Close: %d, Current: %d\n", open_minutes, close_minutes, current_minutes);

                if (open_minutes < 0 || close_minutes < 0) continue;

                if (current_minutes >= open_minutes && current_minutes < close_minutes) {
                    target_angle = sched->angle;
                    matched      = true;
                    break;  // IMPORTANT: stop checking further
                }
            }
            xSemaphoreGive(scheduleMutex);  

            (void) matched;

            float current_angle;
            const float tolerance = 2.0;
            int adc = pot_read_filtered(&potentiometer);
            current_angle = pot_to_angle(&potentiometer, adc);

            if (!valve_busy && fabs(target_angle - current_angle) > tolerance) {

                valve_busy = true;

                int err_code = motor_set_angle(target_angle);

                int adc = pot_read_filtered(&potentiometer);
                float updated_angle = pot_to_angle(&potentiometer, adc);
                xSemaphoreTake(valveMutex, portMAX_DELAY);
                if (err_code == 0) {
                    valveData.angle = updated_angle;
                    valveData.error_msg[0] = '\0';
                } else {
                    sprintf(valveData.error_msg,
                            "Schedule failed to set angle to %d, err=%d",
                            target_angle,
                            err_code);
                }
                xSemaphoreGive(valveMutex);

                valve_busy = false;
            }
        } else {
            ESP_LOGI(TAG_SYNC, "Schedule mode disable");
        }


        /* ============================================================= */
        /* 6. SENSOR-BASED AUTO CONTROL                                  */
        /* ============================================================= */

        if (localServerData.sensor_control) {

            /* --- 6.1 Private copy of the sensor config + latest reading --- */
            SensorRule rules[10];
            int        rule_count;
            SensorData sdata;

            xSemaphoreTake(serverMutex, portMAX_DELAY);
            memcpy(rules, serverControl.sensor_rules, sizeof(rules));
            rule_count = serverControl.sensor_rule_count;
            sdata      = serverControl.sensor_data;
            xSemaphoreGive(serverMutex);

            if (rule_count > 10) rule_count = 10;

            /* --- 6.2 Freshness gate: is the sensor unit still alive? --- */
            time_t now_epoch;
            time(&now_epoch);

            time_t seen_epoch    = 0;
            bool   sensor_online = false;
            long   age_sec       = 0;

            if (!parse_last_seen(sdata.last_seen, &seen_epoch)) {
                /* Unknown age is NOT "fresh" — fail closed. */
                ESP_LOGW(TAG_SENSOR,
                         "Sensor '%s': last_seen \"%s\" empty/unparsable -> treating as OFFLINE",
                         sdata.sensor_id, sdata.last_seen);
            } else {
                age_sec = (long)(now_epoch - seen_epoch);

                if (age_sec > SENSOR_OFFLINE_SEC) {
                    ESP_LOGW(TAG_SENSOR,
                             "Sensor '%s' OFFLINE (last seen %ld s ago, limit %d s)",
                             sdata.sensor_id, age_sec, SENSOR_OFFLINE_SEC);
                } else {
                    sensor_online = true;
                    ESP_LOGI(TAG_SENSOR,
                             "Sensor '%s' ONLINE (age %ld s, value %.2f)",
                             sdata.sensor_id, age_sec, sdata.sensor_value);
                }
            }

            /* --- 6.3 Decide the target angle --------------------------- */
            /*   offline                -> fail safe (close)               */
            /*   value <= 0             -> no action, hold position        */
            /*   value inside a rule    -> that rule's angle               */
            /*   value outside all rules-> fail safe (close)               */
            int target_angle = -1;   /* -1 = no command this cycle */

            if (!sensor_online) {
                target_angle = SENSOR_FAILSAFE_ANGLE;
                ESP_LOGW(TAG_SENSOR, "Sensor offline -> closing valve (fail safe)");
            }
            else if (sdata.sensor_value <= 0.0f) {
                ESP_LOGI(TAG_SENSOR, "Sensor value %.2f not > 0 - holding position",
                         sdata.sensor_value);
            }
            else {
                float value = sdata.sensor_value;

                /* Pass 1: half-open [low, high) so "0-30" and "30-60" don't
                   both claim the value 30. */
                for (int i = 0; i < rule_count; i++) {
                    ESP_LOGI(TAG_SENSOR, "Checking rule: %d-%d -> angle %d",
                             rules[i].low, rules[i].high, rules[i].angle);

                    if (value >= (float)rules[i].low && value < (float)rules[i].high) {
                        target_angle = rules[i].angle;
                        break;
                    }
                }

                /* Pass 2: inclusive upper bound, so the top-most range still
                   matches a value sitting exactly on its high edge. */
                if (target_angle < 0) {
                    for (int i = 0; i < rule_count; i++) {
                        if (value >= (float)rules[i].low && value <= (float)rules[i].high) {
                            target_angle = rules[i].angle;
                            break;
                        }
                    }
                }

                if (target_angle < 0) {
                    target_angle = SENSOR_FAILSAFE_ANGLE;
                    ESP_LOGW(TAG_SENSOR,
                             "Sensor value %.2f matches no rule (%d rule(s)) -> closing valve",
                             value, rule_count);
                }
            }

            /* --- 6.4 Drive the valve ----------------------------------- */
            if (target_angle >= 0 && target_angle <= 90) {

                int   adc           = pot_read_filtered(&potentiometer);
                float current_angle = pot_to_angle(&potentiometer, adc);

                ESP_LOGI(TAG_SENSOR, "Sensor target %d deg (current %.2f)",
                         target_angle, current_angle);

                if (!valve_busy &&
                    fabs((float)target_angle - current_angle) > SENSOR_ANGLE_TOLERANCE) {

                    valve_busy = true;

                    int err_code = motor_set_angle(target_angle);

                    adc = pot_read_filtered(&potentiometer);
                    float updated_angle = pot_to_angle(&potentiometer, adc);

                    xSemaphoreTake(valveMutex, portMAX_DELAY);
                    if (err_code == 0) {
                        valveData.encoder_value = adc;
                        valveData.angle         = updated_angle;
                        valveData.error_msg[0]  = '\0';
                    } else {
                        sprintf(valveData.error_msg,
                                "Sensor failed to set angle to %d, err=%d",
                                target_angle, err_code);
                    }
                    xSemaphoreGive(valveMutex);

                    valve_busy = false;
                }
            }
            else if (target_angle > 90) {
                ESP_LOGW(TAG_SENSOR, "Rule angle %d out of range - ignoring", target_angle);
            }

        } else {
            ESP_LOGI(TAG_SYNC, "Sensor mode disable");
        }

        /**
         * Delay before next cycle
         */
        vTaskDelay(pdMS_TO_TICKS(VALVE_TASK_PERIOD_MS));
    }
}





bool schedules_are_equal(ScheduleInfo *a, ScheduleInfo *b, int count) {
    for (int i = 0; i < count; i++) {
        if (strcmp(a[i].day, b[i].day) != 0)     return false;
        if (strcmp(a[i].open, b[i].open) != 0)   return false;
        if (strcmp(a[i].close, b[i].close) != 0) return false;
        if (a[i].angle != b[i].angle)            return false;   /* NEW */
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
 
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "%s: ", title);
 
    for (int i = 0; i < loaded_count && i < MAX_SCHEDULES; i++) {
        if (sched[i].day[0] != '\0') {
            offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                               "[%s %s-%s @%d] ",
                               sched[i].day,
                               sched[i].open,
                               sched[i].close,
                               sched[i].angle);      /* NEW */
        }
        if (offset >= (int)sizeof(buffer)) break;
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
        bool set_schedule = serverData.schedule_control;
        xSemaphoreGive(serverMutex);

        if (set_schedule) {
            // printf("Comparing schedules...\n");
            xSemaphoreTake(scheduleMutex, portMAX_DELAY);
            print_schedule("Loaded schedule:", loaded_schedule);
            print_schedule("New schedule:", schedule_copy);
            schedule_changed = !schedules_are_equal( loaded_schedule, schedule_copy, MAX_SCHEDULES);
            xSemaphoreGive(scheduleMutex);
        }

        if (schedule_changed) {
            if (schedule_storage_save(schedule_copy, MAX_SCHEDULES) == ESP_OK) {
                ESP_LOGI(TAG_SCHEDULE,"SCHEDULE_TASK: Schedule saved to NVS\n");
                xSemaphoreTake(scheduleMutex, portMAX_DELAY);
                memcpy(loaded_schedule, schedule_copy, sizeof(loaded_schedule));
                xSemaphoreGive(scheduleMutex);
            } else {
                ESP_LOGE(TAG_SCHEDULE,"SCHEDULE_TASK: Failed to save schedule\n");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}