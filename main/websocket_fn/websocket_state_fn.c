#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "cJSON.h"
#include "sdkconfig.h" 

#include "global_fn/global_var.h"
#include "websocket_state_fn.h"
#include "time_fn/time_func.h"
#include "eeprom_fn/wifi_storage.h"
#include "eeprom_fn/encoder_storage.h"
#include "valve_fn/valve_process.h"



// WebSocket authentication passkey (menuconfig)
#define PASSKEY_VALUE CONFIG_WS_PASSKEY_VALUE

static const char *TAG = "STATE UPDATE OFFLINE";



/*===============================================================
 *                  SEND DEVICE BASIC INFO
 *==============================================================*/

/**
 * @brief Send device identification information via WebSocket.
 * 
 * This is typically sent after successful authentication.
 */
void send_device_info(void) {
    if (esp_server == NULL) return;

    char timestamp[20];
    get_current_timestamp(timestamp, sizeof(timestamp));

    // Create the JSON object
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "event", "device_info");
    cJSON_AddStringToObject(json, "timestamp", timestamp);
    cJSON_AddStringToObject(json, "device_id", deviceIdentity.device_id);

    // Convert the JSON object to string (allocate memory)
    char *json_string = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    // Send via WebSocket
    if (httpd_queue_work(esp_server, websocket_async_send, json_string) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to queue work");
        free(json_string);
    }
}



/*===============================================================
 *                  SEND VALVE DATA (OFFLINE MODE)
 *==============================================================*/

/**
 * @brief Send complete valve state data via WebSocket.
 * 
 * Includes:
 *   - Controller state
 *   - Valve position
 *   - Limit switch state
 *   - Error message
 */
void send_device_data(void) {
    if (esp_server == NULL) return;

    /*----------------- Copy Shared Data Safely -----------------*/
    GetData localCopy;
    xSemaphoreTake(valveMutex, portMAX_DELAY);
    localCopy = valveData;
    xSemaphoreGive(valveMutex);

    char timestamp[25];
    get_current_timestamp(timestamp, sizeof(timestamp));


    /*----------------- Create Root JSON -----------------*/
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "event", "valve_data");
    cJSON_AddStringToObject(json, "timestamp", timestamp);
    cJSON_AddStringToObject(json, "device_id", deviceIdentity.device_id);

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


/*===============================================================
 *                  SEND MOTOR CALIBRATION DATA (OFFLINE MODE)
 *==============================================================*/
void send_motorcalibration_data(void) {
    if (esp_server == NULL) return;

    /*----------------- Copy Shared Data Safely -----------------*/
    GetData localCopy;
    xSemaphoreTake(valveMutex, portMAX_DELAY);
    localCopy = valveData;
    xSemaphoreGive(valveMutex);

    char timestamp[25];
    get_current_timestamp(timestamp, sizeof(timestamp));


    /*----------------- Create Root JSON -----------------*/
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "event", "get_motor_calibration");
    cJSON_AddStringToObject(json, "timestamp", timestamp);
    cJSON_AddStringToObject(json, "device_id", deviceIdentity.device_id);

    // get_encoder object
    cJSON *encoder_data = cJSON_CreateObject();
    cJSON_AddNumberToObject(encoder_data, "value", localCopy.encoder_value);
    cJSON_AddNumberToObject(encoder_data, "close_limit", localCopy.close_limit_encode);
    cJSON_AddNumberToObject(encoder_data, "open_limit", localCopy.open_limit_encode);
    cJSON_AddItemToObject(json, "encoder_data", encoder_data);

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


/*===============================================================
 *                OFFLINE EVENT HANDLER
 *==============================================================*/

/**
 * @brief Handles WebSocket events after authentication.
 * 
 * Supported events:
 *   - device_basic_info
 *   - set_valve_basic
 *   - set_valve_wifi
 */
void offline_data(cJSON *event, cJSON *json) {

    /*----------------- DEVICE BASIC INFO REQUEST -----------------*/
    if ( strcmp(event->valuestring, "device_basic_info") == 0) {
        ESP_LOGI(TAG, "Event matched: device_basic_info");

        cJSON *data = cJSON_GetObjectItem(json, "data");
        if (data != NULL && cJSON_IsObject(data)) {

            cJSON *user_id = cJSON_GetObjectItem(data, "user_id");
            cJSON *device_id = cJSON_GetObjectItem(data, "device_id");
            if (device_id != NULL && cJSON_IsString(device_id)) {
                if (strcmp(device_id->valuestring, deviceIdentity.device_id) == 0) {
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
    /*----------------- SET VALVE BASIC CONTROL -----------------*/
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
            ESP_LOGI(TAG, "ESP global variable updated with new valve control data");

        } else {
            ESP_LOGW(TAG, "\"valve_data\" field is missing or not an object");
        }

    }
    /*----------------- UPDATE WIFI CREDENTIALS -----------------*/
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
    /*----------------- GET MOTOR CALIBRATION READINGS -----------------*/
    else if ( strcmp(event->valuestring, "get_motor_calibration") == 0) {
        ESP_LOGI(TAG, "Event matched: get_motor_calibration");

        cJSON *device_id = cJSON_GetObjectItem(json, "device_id");
        if (device_id != NULL && cJSON_IsString(device_id)) {
            if (strcmp(device_id->valuestring, deviceIdentity.device_id) == 0) {
                ESP_LOGW(TAG, "User send the correct device ID %s", device_id->valuestring);
                send_motorcalibration_data();
                ESP_LOGW(TAG, "Send motor calibration data");
            } else {
                ESP_LOGW(TAG, "User dont send the correct device ID");
            }
        } else {
            ESP_LOGW(TAG, "\"device_id\" is false or missing");
        }
    }
    /*----------------- SET MOTOR CALIBRATION -----------------*/
    else if ( strcmp(event->valuestring, "set_motor_calibration") == 0) {
        ESP_LOGI(TAG, "Event matched: set_motor_calibration");

        cJSON *device_id = cJSON_GetObjectItem(json, "device_id");
        if (device_id != NULL && cJSON_IsString(device_id)) {
            if (strcmp(device_id->valuestring, deviceIdentity.device_id) == 0) {
                ESP_LOGW(TAG, "User send the correct device ID %s", device_id->valuestring);

                cJSON *encoder_data = cJSON_GetObjectItem(json, "encoder_data");
                if (encoder_data != NULL && cJSON_IsObject(encoder_data)) {
                    cJSON *close_limit = cJSON_GetObjectItem(encoder_data, "close_limit");
                    cJSON *open_limit = cJSON_GetObjectItem(encoder_data, "open_limit");

                    if (close_limit && cJSON_IsNumber(close_limit) &&
                        open_limit && cJSON_IsNumber(open_limit)) {

                        int close = close_limit->valueint;
                        int open = open_limit->valueint;

                        if (close < open && close > 0 && open > 0) {
                            xSemaphoreTake(valveMutex, portMAX_DELAY);
                            valveData.close_limit_encode = close;
                            valveData.open_limit_encode = open;
                            xSemaphoreGive(valveMutex);
                            pot_update_calibration(&potentiometer);

                            save_eeprom_calibration();
                        } else {
                            ESP_LOGW(TAG, "encoder_data values invalid.");
                        }
                    } else {
                        ESP_LOGW(TAG, "encoder_data missing or not numbers.");
                    }
                } else {
                    ESP_LOGW(TAG, "encoder_data false or missing.");
                }
            } else {
                ESP_LOGW(TAG, "User dont send the correct device ID");
            }
        } else {
            ESP_LOGW(TAG, "\"device_id\" is false or missing");
        }

    }
    /*----------------- SET MOTOR ROTATION -----------------*/
    else if ( strcmp(event->valuestring, "set_motor_rotation") == 0) {
        ESP_LOGI(TAG, "Event matched: set_motor_rotation");

        cJSON *device_id = cJSON_GetObjectItem(json, "device_id");
        if (device_id != NULL && cJSON_IsString(device_id)) {
            if (strcmp(device_id->valuestring, deviceIdentity.device_id) == 0) {
                ESP_LOGW(TAG, "User send the correct device ID %s", device_id->valuestring);
                
                cJSON *set_motor = cJSON_GetObjectItem(json, "set_motor");
                if (set_motor != NULL && cJSON_IsObject(set_motor)) {
                    cJSON *turn_clkwise = cJSON_GetObjectItem(set_motor, "turn_clkwise");
                    cJSON *turn_anticlkwise = cJSON_GetObjectItem(set_motor, "turn_anticlkwise");

                    bool clk = cJSON_IsTrue(turn_clkwise);
                    bool aclk = cJSON_IsTrue(turn_anticlkwise);

                    if (clk && !aclk) {
                        xSemaphoreTake(serverMutex, portMAX_DELAY);
                        serverData.set_angle = false;
                        xSemaphoreGive(serverMutex);
                        motor_rotate_clk();
                        send_motorcalibration_data();
                        ESP_LOGW(TAG, "Send motor calibration data");
                    } 
                    else if (!clk && aclk) {
                        xSemaphoreTake(serverMutex, portMAX_DELAY);
                        serverData.set_angle = false;
                        xSemaphoreGive(serverMutex);
                        motor_rotate_aclk();
                        send_motorcalibration_data();
                        ESP_LOGW(TAG, "Send motor calibration data");
                    }
                    else {
                        ESP_LOGW(TAG, "Invalid motor command (both true/false or missing)");
                    }

                } else {
                    ESP_LOGW(TAG, "set_motor data false or missing.");
                }
            } else {
                ESP_LOGW(TAG, "User dont send the correct device ID");
            }
        } else {
            ESP_LOGW(TAG, "\"device_id\" is false or missing");
        }
    }
    else {
        ESP_LOGW(TAG, "Event type does not match: %s", event->valuestring);
    }
}




/*===============================================================
 *                  PROCESS WEBSOCKET MESSAGE
 *==============================================================*/

/**
 * @brief Entry point for WebSocket JSON message processing.
 * 
 * Handles:
 *   - Authentication (passkey validation)
 *   - Routing to offline_data after authorization
 */
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


    /*----------------- If Already Authorized -----------------*/
    if ( *connection_authorized) {
        offline_data( event, json);
    } 
    /*----------------- Authentication Phase -----------------*/
    else {
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

