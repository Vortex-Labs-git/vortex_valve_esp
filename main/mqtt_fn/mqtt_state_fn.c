#include <string.h>
#include <stdbool.h>
#include "cJSON.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "time_fn/time_func.h"
#include "global_fn/global_var.h"
#include "ota_fn/ota_update_fn.h"
#include "mqtt_state_fn.h"
#include "eeprom_fn/schedule_storage.h"



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
            localCopy.user_control = cJSON_IsTrue(set_angle);
            localCopy.angle = angle->valueint;
        }

    }

    /*----------------- OTA Update Request -----------------*/
    cJSON *ota_update = cJSON_GetObjectItem(json_cmd_data, "ota_update");
    if (cJSON_IsObject(ota_update)) {
        cJSON *url = cJSON_GetObjectItem(ota_update, "url");
        cJSON *version = cJSON_GetObjectItem(ota_update, "version");

        if (cJSON_IsString(url) && cJSON_IsString(version)) {
            ESP_LOGI(TAG, "OTA request: v%s from %s", version->valuestring, url->valuestring);
            ota_start(url->valuestring, version->valuestring);
        }
    }

    /*----------------- Update Shared Data Safely -----------------*/
    // Protect shared serverData using mutex
    xSemaphoreTake(serverMutex, portMAX_DELAY);

    bool old_schedule_state = serverData.schedule_control;
    loaded_schedule_ctrl = localCopy.schedule_control;

    // Update runtime state FIRST
    serverData = localCopy;

    xSemaphoreGive(serverMutex);

    /*----------------- Persist OUTSIDE mutex (IMPORTANT) -----------------*/
    if (old_schedule_state != loaded_schedule_ctrl) {
        esp_err_t err = schedule_set_enable_save(loaded_schedule_ctrl);

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save schedule enable state to NVS");
        }
    }

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
    if (json_control_data == NULL) {
        ESP_LOGE(TAG, "Invalid JSON received");
        return;
    }

    SetControl localCopy;
    xSemaphoreTake(serverMutex, portMAX_DELAY);
    localCopy = serverControl;
    xSemaphoreGive(serverMutex);


    cJSON *event = cJSON_GetObjectItem(json_control_data, "event");
    cJSON *device_id = cJSON_GetObjectItem(json_control_data, "device_id");
    

    /*----------------- Schedule Configuration -----------------*/
    cJSON *schedule = cJSON_GetObjectItem(json_control_data, "schedule");
    if (cJSON_IsObject(schedule)) {
        
        cJSON *schedule_info = cJSON_GetObjectItem(schedule, "schedule_info");
        if (cJSON_IsArray(schedule_info)) {
 
            memset(localCopy.schedule_info, 0, sizeof(localCopy.schedule_info));
            localCopy.schedule_count = 0;
 
            int n = cJSON_GetArraySize(schedule_info);
            for (int i = 0; i < n && localCopy.schedule_count < 10; i++) {
 
                cJSON *item = cJSON_GetArrayItem(schedule_info, i);
                if (!cJSON_IsObject(item)) continue;
 
                ScheduleInfo *slot = &localCopy.schedule_info[localCopy.schedule_count];
 
                cJSON *day = cJSON_GetObjectItem(item, "day");
                if (cJSON_IsString(day)) {
                    snprintf(slot->day, sizeof(slot->day), "%s", day->valuestring);
                }
 
                /* The time window is a DYNAMIC KEY ("HH:MM-HH:MM"),
                   its value is the target angle. Find the first child
                   that isn't "day". */
                bool got_window = false;
                for (cJSON *child = item->child; child != NULL; child = child->next) {
                    if (child->string == NULL) continue;
                    if (strcmp(child->string, "day") == 0) continue;
 
                    const char *win  = child->string;
                    const char *dash = strchr(win, '-');
                    if (dash == NULL) continue;   /* not a window key */
 
                    size_t open_len = (size_t)(dash - win);
                    if (open_len >= sizeof(slot->open)) open_len = sizeof(slot->open) - 1;
                    memcpy(slot->open, win, open_len);
                    slot->open[open_len] = '\0';
 
                    snprintf(slot->close, sizeof(slot->close), "%s", dash + 1);
 
                    if (cJSON_IsString(child)) {
                        char *endp = NULL;
                        long a = strtol(child->valuestring, &endp, 10);
                        if (endp == child->valuestring || a < 0 || a > 90) {
                            ESP_LOGW(TAG, "Bad angle '%s' in window %s, skipping entry", child->valuestring ? child->valuestring : "(null)", win);
                            got_window = false;      // reject this schedule entry
                            break;
                        }
                        slot->angle = (int)a;
                    }
 
                    got_window = true;
                    break;
                }
 
                if (got_window && slot->day[0] != '\0') {
                    localCopy.schedule_count++;
                }
            }

 
            ESP_LOGI(TAG, "Parsed %d schedule entrie(s)", localCopy.schedule_count);
        }

    }

    /* ----------------- SENSOR ----------------- */
    cJSON *sensor = cJSON_GetObjectItem(json_control_data, "sensor");
    if (cJSON_IsObject(sensor)) {
 
        /* sensor_rule: { "low-high": "angle", ... } */
        cJSON *sensor_rule = cJSON_GetObjectItem(sensor, "sensor_rule");
        if (cJSON_IsObject(sensor_rule)) {
 
            memset(localCopy.sensor_rules, 0, sizeof(localCopy.sensor_rules));
            localCopy.sensor_rule_count = 0;
 
            for (cJSON *child = sensor_rule->child; child != NULL; child = child->next) {
                if (localCopy.sensor_rule_count >= 10) break;
                if (child->string == NULL) continue;
 
                int low = 0, high = 0;
                if (sscanf(child->string, "%d-%d", &low, &high) == 2) {
                    SensorRule *r = &localCopy.sensor_rules[localCopy.sensor_rule_count];
                    r->low  = low;
                    r->high = high;
                    if (cJSON_IsString(child))      r->angle = atoi(child->valuestring);
                    else if (cJSON_IsNumber(child)) r->angle = child->valueint;
                    localCopy.sensor_rule_count++;
                }
            }
            ESP_LOGI(TAG, "Parsed %d sensor rule(s)", localCopy.sensor_rule_count);
        }
 
        /* sensor_data: single object */
        cJSON *sensor_data = cJSON_GetObjectItem(sensor, "sensor_data");
        if (cJSON_IsObject(sensor_data)) {
 
            memset(&localCopy.sensor_data, 0, sizeof(localCopy.sensor_data));
 
            cJSON *unit_id   = cJSON_GetObjectItem(sensor_data, "unit_id");
            cJSON *sid       = cJSON_GetObjectItem(sensor_data, "sensor_id");
            cJSON *sname     = cJSON_GetObjectItem(sensor_data, "sensor_name");
            cJSON *last_seen = cJSON_GetObjectItem(sensor_data, "last_seen");
            cJSON *stype     = cJSON_GetObjectItem(sensor_data, "sensor_type");
            cJSON *sval      = cJSON_GetObjectItem(sensor_data, "sensor_value");
 
            if (cJSON_IsString(unit_id))
                snprintf(localCopy.sensor_data.unit_id, sizeof(localCopy.sensor_data.unit_id), "%s", unit_id->valuestring);
            if (cJSON_IsString(sid))
                snprintf(localCopy.sensor_data.sensor_id, sizeof(localCopy.sensor_data.sensor_id), "%s", sid->valuestring);
            if (cJSON_IsString(sname))
                snprintf(localCopy.sensor_data.sensor_name, sizeof(localCopy.sensor_data.sensor_name), "%s", sname->valuestring);
            if (cJSON_IsString(last_seen))
                snprintf(localCopy.sensor_data.last_seen, sizeof(localCopy.sensor_data.last_seen), "%s", last_seen->valuestring);
            if (cJSON_IsString(stype))
                snprintf(localCopy.sensor_data.sensor_type, sizeof(localCopy.sensor_data.sensor_type), "%s", stype->valuestring);
 
            /* sensor_value arrives as a string ("45") or possibly a number */
            if (cJSON_IsString(sval))      localCopy.sensor_data.sensor_value = (float)atof(sval->valuestring);
            else if (cJSON_IsNumber(sval)) localCopy.sensor_data.sensor_value = (float)sval->valuedouble;
 
            ESP_LOGI(TAG, "Sensor %s = %.2f",
                     localCopy.sensor_data.sensor_id,
                     localCopy.sensor_data.sensor_value);
        }
    }
 
    /* ----------------- Commit ----------------- */
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
    cJSON_AddStringToObject(json, "device_id", deviceIdentity.device_id);
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
    cJSON_AddStringToObject(json, "device_id", deviceIdentity.device_id);

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
    cJSON_AddStringToObject(json, "device_id", deviceIdentity.device_id);

    xSemaphoreTake(valveMutex, portMAX_DELAY);
    cJSON_AddStringToObject(json, "error", valveData.error_msg);
    xSemaphoreGive(valveMutex);

    return json;
}

