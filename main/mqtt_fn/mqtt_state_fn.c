#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include "cJSON.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "time_fn/time_func.h"
#include "global_fn/global_var.h"
#include "mqtt_state_fn.h"


/*---------------------------------------------------------------
 * Configuration
 *--------------------------------------------------------------*/

// Device ID configured from menuconfig
#define DEVICE_ID CONFIG_WIFI_VALVE_ID

static const char *TAG = "MQTT_STATE";




/*===============================================================
 *              HANDLE BASIC COMMAND DATA (cmd_data)
 *==============================================================*/

/**
 * @brief Handle incoming MQTT message from topic: cmd_data
 * 
 * Expected structure:
 * {
 *   "event": "...",
 *   "device_id": "...",
 *   "set_controller": {...},
 *   "valve_data": {...}
 * }
 */
void mqtt_handle_cmd_data(const char *data) {
    cJSON *json_cmd_data = cJSON_Parse(data);
    SetData localCopy;

    if (json_cmd_data == NULL) {
        ESP_LOGE(TAG, "Invalid JSON received");
        return;
    }

    /* Start from the current values: a block missing from the message keeps
       its value instead of becoming random stack data. */
    xSemaphoreTake(serverMutex, portMAX_DELAY);
    localCopy = serverData;
    xSemaphoreGive(serverMutex);

    // Extract top-level fields
    cJSON *event = cJSON_GetObjectItem(json_cmd_data, "event");
    cJSON *device_id = cJSON_GetObjectItem(json_cmd_data, "device_id");
    cJSON *ota_update = cJSON_GetObjectItem(json_cmd_data, "ota_update");

    /*----------------- Controller Settings -----------------*/
    cJSON *set_controller = cJSON_GetObjectItem(json_cmd_data, "set_controller");
    if (cJSON_IsObject(set_controller)) {
        cJSON *schedule = cJSON_GetObjectItem(set_controller, "schedule");
        cJSON *sensor = cJSON_GetObjectItem(set_controller, "sensor");

        if (cJSON_IsBool(schedule) && cJSON_IsBool(sensor)) {
            localCopy.schedule_control = cJSON_IsTrue(schedule);
            localCopy.sensor_control = cJSON_IsTrue(sensor);
        }
    }

    /*----------------- Valve Angle Data -----------------*/
    cJSON *valve_data = cJSON_GetObjectItem(json_cmd_data, "valve_data");
    if (cJSON_IsObject(valve_data)) {
        cJSON *name = cJSON_GetObjectItem(valve_data, "name");
        cJSON *set_angle = cJSON_GetObjectItem(valve_data, "set_angle");
        cJSON *angle = cJSON_GetObjectItem(valve_data, "angle");

        if (cJSON_IsBool(set_angle) && cJSON_IsNumber(angle)) {
            localCopy.set_angle = cJSON_IsTrue(set_angle);
            localCopy.angle = angle->valueint;
        }

    }

    /*----------------- Update Shared Data Safely -----------------*/
    // Protect shared serverData using mutex
    xSemaphoreTake(serverMutex, portMAX_DELAY);
    serverData = localCopy;
    xSemaphoreGive(serverMutex);

    cJSON_Delete(json_cmd_data);
}




/*===============================================================
 *              HANDLE ADVANCED CONTROL DATA (control_data)
 *==============================================================*/

/**
 * @brief Handle incoming MQTT message from topic: control_data
 *
 * Used for:
 *  - Controller enable/disable
 *  - Schedule configuration
 *  - Sensor threshold configuration
 */
void mqtt_handle_control_data(const char *data) {
    cJSON *json_control_data = cJSON_Parse(data);
    SetControl localCopy;

    if (json_control_data == NULL) {
        ESP_LOGE(TAG, "Invalid JSON received");
        return;
    }

    /* Start from the current values: a block missing from the message keeps
       its value instead of becoming random stack data. */
    xSemaphoreTake(serverMutex, portMAX_DELAY);
    localCopy = serverControl;
    xSemaphoreGive(serverMutex);

    cJSON *event = cJSON_GetObjectItem(json_control_data, "event");
    cJSON *device_id = cJSON_GetObjectItem(json_control_data, "device_id");
    
    /*----------------- Controller Enable Settings -----------------*/
    cJSON *set_controllerdata = cJSON_GetObjectItem(json_control_data, "set_controllerdata");
    if (cJSON_IsObject(set_controllerdata)) {
        
        cJSON *schedule = cJSON_GetObjectItem(set_controllerdata, "schedule");
        cJSON *sensor = cJSON_GetObjectItem(set_controllerdata, "sensor");

        if (cJSON_IsBool(schedule) && cJSON_IsBool(sensor)) {
            localCopy.schedule_control = cJSON_IsTrue(schedule);
            localCopy.sensor_control = cJSON_IsTrue(sensor);
        }
    }

    /*----------------- Schedule (server_device_mqtt format) -----------------*/
    /*
     * {"schedule": {"schedule_info": [ {"day":"Monday","06:00:00-06:30:00":"90"}, ... ]}}
     * angle > 0 = OPEN window, "0" = skipped (outside every open window = closed).
     * An empty list is valid: no open windows.
     */
    cJSON *schedule = cJSON_GetObjectItem(json_control_data, "schedule");
    if (cJSON_IsObject(schedule)) {
        cJSON *schedule_info = cJSON_GetObjectItem(schedule, "schedule_info");
        if (cJSON_IsArray(schedule_info)) {

            memset(localCopy.schedule_info, 0, sizeof(localCopy.schedule_info));
            int count = 0;

            cJSON *item = NULL;
            cJSON_ArrayForEach(item, schedule_info) {
                if (count >= 10) break;                 /* the valve keeps 10 */
                if (!cJSON_IsObject(item)) continue;

                cJSON *day = cJSON_GetObjectItem(item, "day");
                if (!cJSON_IsString(day)) continue;

                for (cJSON *child = item->child; child != NULL; child = child->next) {
                    if (child->string == NULL || strcmp(child->string, "day") == 0) continue;

                    const char *dash = strchr(child->string, '-');
                    if (dash == NULL) continue;         /* not a time window */

                    int angle = 0;
                    if (cJSON_IsString(child))      angle = atoi(child->valuestring);
                    else if (cJSON_IsNumber(child)) angle = child->valueint;
                    if (angle <= 0) break;              /* closed window: nothing to store */

                    ScheduleInfo *slot = &localCopy.schedule_info[count];
                    size_t open_len = (size_t)(dash - child->string);
                    if (open_len >= sizeof(slot->open)) open_len = sizeof(slot->open) - 1;

                    snprintf(slot->day, sizeof(slot->day), "%s", day->valuestring);
                    memcpy(slot->open, child->string, open_len);
                    slot->open[open_len] = '\0';
                    snprintf(slot->close, sizeof(slot->close), "%s", dash + 1);
                    count++;
                    break;                              /* one window per entry */
                }
            }

            localCopy.set_schedule = true;              /* let schedule_save_task compare + store it */
            ESP_LOGI(TAG, "Parsed %d open window(s) from schedule_info", count);
        }
    }

    /*----------------- Sensor Limits -----------------*/
    cJSON *set_sensordata = cJSON_GetObjectItem(json_control_data, "set_sensordata");
    if (cJSON_IsObject(set_sensordata)) {

        cJSON *upper_limit = cJSON_GetObjectItem(set_sensordata, "upper_limit");
        cJSON *lower_limit = cJSON_GetObjectItem(set_sensordata, "lower_limit");
        if (cJSON_IsNumber(upper_limit) && cJSON_IsNumber(lower_limit)) {
            localCopy.sensor_upper_limit = upper_limit->valueint;
            localCopy.sensor_lower_limit = lower_limit->valueint;
        }
    }

    /*----------------- Update Shared Control Data -----------------*/
    xSemaphoreTake(serverMutex, portMAX_DELAY);
    serverControl = localCopy;
    xSemaphoreGive(serverMutex);

    cJSON_Delete(json_control_data);
    
}


/*===============================================================
 *                 GENERIC TOPIC ROUTER
 *==============================================================*/

/**
 * @brief Routes MQTT messages based on "event" field
 */
void mqtt_handle_topic(const char *data) {
    cJSON *json_data = cJSON_Parse(data);

    if (json_data == NULL) {
        ESP_LOGE(TAG, "Invalid JSON received");
        return;
    }

    cJSON *event = cJSON_GetObjectItem(json_data, "event");
    if (!cJSON_IsString(event)) {
        ESP_LOGW(TAG, "Event field missing or not a string");
        cJSON_Delete(json_data);
        return;
    }
    ESP_LOGI(TAG, "Received event: %s", event->valuestring);

    if (strcmp(event->valuestring, "set_valve_control") == 0) {
        mqtt_handle_control_data(data);

    } else if (strcmp(event->valuestring, "set_valve_basic") == 0) {
        mqtt_handle_cmd_data(data);

    } else {
        ESP_LOGW(TAG, "Unknown event type: %s", event->valuestring);
    }

    cJSON_Delete(json_data);
}



/*===============================================================
 *              CREATE JSON: VALVE STATUS
 *==============================================================*/

/**
 * @brief Create JSON object for valve online status
 */
cJSON* create_valve_status() {
    cJSON *json = cJSON_CreateObject();

    char timestamp[32];
    get_current_timestamp(timestamp, sizeof(timestamp));

    cJSON_AddStringToObject(json, "event", "valve_status");
    cJSON_AddStringToObject(json, "timestamp", timestamp);
    cJSON_AddStringToObject(json, "device_id", DEVICE_ID);
    cJSON_AddStringToObject(json, "status", "online");

    return json;
}



/*===============================================================
 *              CREATE JSON: VALVE STATE DATA
 *==============================================================*/

/**
 * @brief Create JSON object containing:
 *        - controller state
 *        - valve state
 *        - limit switch data
 */
cJSON* create_valve_state_data() {

    // Lock data
    GetData localCopy;
    xSemaphoreTake(valveMutex, portMAX_DELAY);
    localCopy = valveData;
    xSemaphoreGive(valveMutex);

    cJSON *json = cJSON_CreateObject();

    char timestamp[32];
    get_current_timestamp(timestamp, sizeof(timestamp));

    cJSON_AddStringToObject(json, "event", "valve_basic_data");
    cJSON_AddStringToObject(json, "timestamp", timestamp);
    cJSON_AddStringToObject(json, "device_id", DEVICE_ID);

    cJSON *controller_data = cJSON_CreateObject();
    cJSON_AddBoolToObject(controller_data, "schedule", localCopy.schedule_control);
    cJSON_AddBoolToObject(controller_data, "sensor", localCopy.sensor_control);
    cJSON_AddItemToObject(json, "get_controller", controller_data);

    cJSON *valve_data = cJSON_CreateObject();
    cJSON_AddNumberToObject(valve_data, "angle", localCopy.angle);
    cJSON_AddBoolToObject(valve_data, "is_open", localCopy.is_open);
    cJSON_AddBoolToObject(valve_data, "is_close", localCopy.is_close);
    cJSON_AddItemToObject(json, "get_valvedata", valve_data);

    cJSON *limit_data = cJSON_CreateObject();
    cJSON_AddBoolToObject(limit_data, "is_open_limit", localCopy.open_limit_available);
    cJSON_AddBoolToObject(limit_data, "open_limit", localCopy.open_limit_click);
    cJSON_AddBoolToObject(limit_data, "is_close_limit", localCopy.close_limit_available);
    cJSON_AddBoolToObject(limit_data, "close_limit", localCopy.close_limit_click);
    cJSON_AddItemToObject(json, "get_limitdata", limit_data);

    return json;
}



/*===============================================================
 *              CREATE JSON: VALVE ERROR
 *==============================================================*/

/**
 * @brief Create JSON object for error reporting
 */
cJSON* create_valve_error() {
    cJSON *json = cJSON_CreateObject();

    char timestamp[32];
    get_current_timestamp(timestamp, sizeof(timestamp));

    cJSON_AddStringToObject(json, "event", "valve_error");
    cJSON_AddStringToObject(json, "timestamp", timestamp);
    cJSON_AddStringToObject(json, "device_id", DEVICE_ID);

    xSemaphoreTake(valveMutex, portMAX_DELAY);
    cJSON_AddStringToObject(json, "error", valveData.error_msg);
    xSemaphoreGive(valveMutex);

    return json;
}