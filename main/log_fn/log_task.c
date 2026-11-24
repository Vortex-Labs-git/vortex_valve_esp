/**
 * @file log_task.c
 * @brief Device log task: logs errors once and sends the log to the server
 *
 * Every LOG_TASK_PERIOD_MS:
 *  1. Error watch - valveData.error_msg is written in many places; a new
 *     error text is logged once here. When the error clears, the next
 *     error (even the same one) is logged again.
 *  2. Send - while MQTT is connected, unsent entries go out through the
 *     normal publisher on .../error. An entry is removed only after it
 *     was handed to MQTT; otherwise it waits (also across a restart).
 */

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "cJSON.h"

#include "global_fn/global_var.h"
#include "mqtt_fn/mqtt_client_fn.h"
#include "mqtt_fn/mqtt_state_fn.h"
#include "log_fn/device_log.h"
#include "log_fn/log_task.h"

#define LOG_TASK_PERIOD_MS   1000
#define LOG_SEND_PER_CYCLE   5

static const char *TAG = "LOG_TASK";


static void log_watch_error(void) {
    static char last_error[sizeof(valveData.error_msg)] = "";
    char error[sizeof(valveData.error_msg)];

    xSemaphoreTake(valveMutex, portMAX_DELAY);
    strlcpy(error, valveData.error_msg, sizeof(error));
    xSemaphoreGive(valveMutex);

    if (error[0] != '\0' && strcmp(error, last_error) != 0) {
        DLOG_ERROR("%s", error);
    }
    strlcpy(last_error, error, sizeof(last_error));
}


static void log_send_pending(void) {
    if (!mqtt_is_connected()) return;

    DeviceLogEntry entry;
    for (int i = 0; i < LOG_SEND_PER_CYCLE && device_log_peek(&entry); i++) {
        cJSON *json = create_valve_log(&entry);
        bool sent = (json != NULL) && mqtt_publish_message("error", json);
        cJSON_Delete(json);

        if (!sent) return;      /* keep it, retry next cycle */
        device_log_drop();
    }
}


static void log_task(void *pvParameters) {
    (void) pvParameters;

    while (1) {
        log_watch_error();
        log_send_pending();
        vTaskDelay(pdMS_TO_TICKS(LOG_TASK_PERIOD_MS));
    }
}


void log_task_start(void) {
    static bool started = false;
    if (started) return;
    started = true;

    if (xTaskCreate(log_task, "log_task", 4096, NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create log task");
    }
}
