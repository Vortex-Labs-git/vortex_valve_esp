#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "cJSON.h"
#include "sdkconfig.h" 

#include "global_var.h"
#include "websocket_state_fn.h"
#include "time_func.h"
#include "eeprom_fn/wifi_storage.h"

#define DEVICE_ID CONFIG_WIFI_VALVE_ID
#define PASSKEY_VALUE CONFIG_WS_PASSKEY_VALUE

static const char *TAG = "STATE UPDATE OFFLINE";




// Send device information
void send_device_info(void) {
    if (esp_server == NULL) return;

    char timestamp[20];
    get_current_timestamp(timestamp, sizeof(timestamp));

    // Create the JSON object
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "event", "device_info");
    cJSON_AddStringToObject(json, "timestamp", timestamp);
    cJSON_AddStringToObject(json, "device_id", DEVICE_ID);

    // Convert the JSON object to string (allocate memory)
    char *json_string = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    // Send via WebSocket
    if (httpd_queue_work(esp_server, websocket_async_send, json_string) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to queue work");
        free(json_string);
    }
}


// Send valve basic data
void send_device_data(void) {
    if (esp_server == NULL) return;

    // Lock data
    GetData localCopy;
    xSemaphoreTake(valveMutex, portMAX_DELAY);
    localCopy = valveData;
    xSemaphoreGive(valveMutex);

    char timestamp[25];
    get_current_timestamp(timestamp, sizeof(timestamp));

    // Root object
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "event", "valve_data");
    cJSON_AddStringToObject(json, "timestamp", timestamp);
    cJSON_AddStringToObject(json, "device_id", DEVICE_ID);

    // get_controller object
    cJSON *controller_data = cJSON_CreateObject();
    cJSON_AddBoolToObject(controller_data, "schedule", localCopy.schedule_control);
    cJSON_AddBoolToObject(controller_data, "sensor", localCopy.sensor_control);
    cJSON_AddItemToObject(json, "get_controller", controller_data);

    // get_valvedata object
    cJSON *valve_data = cJSON_CreateObject();
    cJSON_AddNumberToObject(valve_data, "angle", localCopy.angle);
    cJSON_AddBoolToObject(valve_data, "is_open", localCopy.is_open);
    cJSON_AddBoolToObject(valve_data, "is_close", localCopy.is_close);
    cJSON_AddItemToObject(json, "get_valvedata", valve_data);

    // get_limitdata object
    cJSON *limit_data = cJSON_CreateObject();
    cJSON_AddBoolToObject(limit_data, "is_open_limit", localCopy.open_limit_available);
    cJSON_AddBoolToObject(limit_data, "open_limit", localCopy.open_limit_click);
    cJSON_AddBoolToObject(limit_data, "is_close_limit", localCopy.close_limit_available);
    cJSON_AddBoolToObject(limit_data, "close_limit", localCopy.close_limit_click);
    cJSON_AddItemToObject(json, "get_limitdata", limit_data);

    // Error
    cJSON_AddStringToObject(json, "Error", localCopy.error_msg);

    /* Convert to string */
    char *json_string = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    if (json_string == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON string");
        return;
    }

    /* Send asynchronously via WebSocket */
    if (httpd_queue_work(esp_server, websocket_async_send, json_string) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to queue websocket work");
        free(json_string);
    }
}


// offline data communication
void offline_data(cJSON *event, cJSON *json) {
    if ( strcmp(event->valuestring, "device_basic_info") == 0) {
        ESP_LOGI(TAG, "Event matched: device_basic_info");

        cJSON *data = cJSON_GetObjectItem(json, "data");
        if (data != NULL && cJSON_IsObject(data)) {

            cJSON *user_id = cJSON_GetObjectItem(data, "user_id");
            cJSON *device_id = cJSON_GetObjectItem(data, "device_id");
            if (device_id != NULL && cJSON_IsString(device_id)) {
                if (strcmp(device_id->valuestring, DEVICE_ID) == 0) {
                    ESP_LOGW(TAG, "User send the correct device ID %s, user ID %s", device_id->valuestring, user_id->valuestring);
                    send_device_data();
                    ESP_LOGW(TAG, "Send valve data");
                } else {
                    ESP_LOGW(TAG, "User dont send the correct device ID");
                }
            } else {
                ESP_LOGW(TAG, "\"device_id\" is false or missing");
            }
        } else {
            ESP_LOGW(TAG, "\"data\" is false or missing");
        }
    } else if ( strcmp(event->valuestring, "set_valve_basic") == 0) {
        ESP_LOGI(TAG, "Event matched: set_valve_basic");
        SetData localCopy;

        cJSON *valve_data = cJSON_GetObjectItem(json, "valve_data");
        if (valve_data != NULL && cJSON_IsObject(valve_data)) {
            localCopy.schedule_control = false;
            localCopy.sensor_control = false;

            cJSON *set_angle = cJSON_GetObjectItem(valve_data, "set_angle");
            if (set_angle != NULL && cJSON_IsBool(set_angle) && set_angle->valueint == 1) {
                
                cJSON *angle = cJSON_GetObjectItem(valve_data, "angle");
                if (angle != NULL && cJSON_IsNumber(angle)) {
                    localCopy.set_angle = true;
                    localCopy.angle = angle->valueint;
                    ESP_LOGI(TAG, "Angle: %d", angle->valueint);

                } else {
                    ESP_LOGW(TAG, "Angle field is missing or not a number");
                }
            } else {
                ESP_LOGW(TAG, "\"set_angle\" is false or missing");
                localCopy.set_angle = false;
                localCopy.angle = 0;
            }

            xSemaphoreTake(serverMutex, portMAX_DELAY);
            serverData = localCopy;
            xSemaphoreGive(serverMutex);

        } else {
            ESP_LOGW(TAG, "\"valve_data\" field is missing or not an object");
        }

    }
    else if ( strcmp(event->valuestring, "set_valve_wifi") == 0) {
        ESP_LOGI(TAG, "Event matched: set_valve_wifi");

        cJSON *wifi_data = cJSON_GetObjectItem(json, "wifi_data");
        if (wifi_data != NULL && cJSON_IsObject(wifi_data)) {
            cJSON *ssid = cJSON_GetObjectItem(wifi_data, "ssid");
            cJSON *password = cJSON_GetObjectItem(wifi_data, "password");
            ESP_LOGI(TAG, "ssid: %s, password: %s", ssid->valuestring, password->valuestring);

            if (!cJSON_IsString(ssid) || !cJSON_IsString(password)) {
                ESP_LOGE(TAG, "Invalid WiFi JSON format");
            }

            if (strcmp(wifiStaData.ssid, ssid->valuestring) != 0 || strcmp(wifiStaData.password, password->valuestring) != 0) {
                memset(wifiStaData.ssid, 0, sizeof(wifiStaData.ssid));
                memset(wifiStaData.password, 0, sizeof(wifiStaData.password));

                strncpy(wifiStaData.ssid, ssid->valuestring, sizeof(wifiStaData.ssid) - 1);
                strncpy(wifiStaData.password, password->valuestring, sizeof(wifiStaData.password) - 1);

                wifiStaData.set_wifi = true;

                wifi_storage_save();

                ESP_LOGI(TAG, "WiFi updated. Restarting...");
                esp_restart();

            } else {
                ESP_LOGI(TAG, "WiFi data unchanged. No action taken.");
            }
        
            

        } else {
            ESP_LOGW(TAG, "\"wifi_data\" field is missing or not an object");
        }
    }
    else {
        ESP_LOGW(TAG, "Event type does not match: %s", event->valuestring);
    }
}


// Extract the json msg
void process_message(const char *payload, bool *connection_authorized) {

    // Parse the JSON string into a cJSON object
    cJSON *json = cJSON_Parse(payload);
    if (json == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return;
    }

    // Extract the "event" field
    cJSON *event = cJSON_GetObjectItem(json, "event");
    if (event == NULL || !cJSON_IsString(event)) {
        ESP_LOGW(TAG, "\"event\" field is missing in the JSON message");
        cJSON_Delete(json);
        return;
    }


    if ( *connection_authorized) {
        offline_data( event, json);
    } else {
        if (strcmp(event->valuestring, "request_device_info") == 0) {
            ESP_LOGI(TAG, "Event matched: request_device_info");

            cJSON *passkey   = cJSON_GetObjectItem(json, "passkey");
            if ( cJSON_IsString(passkey) && (strcmp(passkey->valuestring, PASSKEY_VALUE) == 0)) {
                *connection_authorized = true;
                ESP_LOGI(TAG, "Passkey accept");

                send_device_info();
                ESP_LOGI(TAG, "Send Device info");
            } else {
                *connection_authorized = false;
                ESP_LOGI(TAG, "Passkey not accept");
            }
        } else {
            ESP_LOGW(TAG, "Connection not authorized");
        }
    }

    // Free memory to prevent leaks
    cJSON_Delete(json);
}

