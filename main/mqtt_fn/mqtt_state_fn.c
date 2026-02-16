#include <string.h>
#include <stdbool.h>
#include "cJSON.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "time_func.h"
#include "global_var.h"
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

    /*----------------- Schedule Configuration -----------------*/
    cJSON *set_scheduledata = cJSON_GetObjectItem(json_control_data, "set_scheduledata");
    if (cJSON_IsObject(set_scheduledata)) {
        
        cJSON *set_schedule = cJSON_GetObjectItem(set_scheduledata, "set_schedule");
        if (cJSON_IsBool(set_schedule)) {
            localCopy.set_schedule = cJSON_IsTrue(set_schedule);
        }

        cJSON *schedule_info = cJSON_GetObjectItem(set_scheduledata, "schedule_info");
        if (cJSON_IsArray(schedule_info)) {
            int schedule_count = cJSON_GetArraySize(schedule_info);
            for (int i = 0; i < schedule_count && i < 10; i++) {

                cJSON *schedule_item = cJSON_GetArrayItem(schedule_info, i);
                if (cJSON_IsObject(schedule_item)) {

                    cJSON *day = cJSON_GetObjectItem(schedule_item, "day");
                    cJSON *open = cJSON_GetObjectItem(schedule_item, "open");
                    cJSON *close = cJSON_GetObjectItem(schedule_item, "close");
                    if (cJSON_IsString(day) && cJSON_IsString(open) && cJSON_IsString(close)) {
                        strncpy(localCopy.schedule_info[i].day, day->valuestring, sizeof(localCopy.schedule_info[i].day) - 1);
                        strncpy(localCopy.schedule_info[i].open, open->valuestring, sizeof(localCopy.schedule_info[i].open) - 1);
                        strncpy(localCopy.schedule_info[i].close, close->valuestring, sizeof(localCopy.schedule_info[i].close) - 1);
                    }
                }
            }
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
    if (cJSON_IsString(event)) {
        ESP_LOGI(TAG, "Received event: %s", event->valuestring);
    } else {
        ESP_LOGW(TAG, "Event field missing or not a string");
    }

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

    char timestamp[20];
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

    char timestamp[20];
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

    char timestamp[20];
    get_current_timestamp(timestamp, sizeof(timestamp));

    cJSON_AddStringToObject(json, "event", "valve_error");
    cJSON_AddStringToObject(json, "timestamp", timestamp);
    cJSON_AddStringToObject(json, "device_id", DEVICE_ID);

    xSemaphoreTake(valveMutex, portMAX_DELAY);
    cJSON_AddStringToObject(json, "error", valveData.error_msg);
    xSemaphoreGive(valveMutex);

    return json;
}

