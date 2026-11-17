/**
 * @file device_log.c
 * @brief Main-process event log queue (see device_log.h)
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include "time_fn/time_func.h"
#include "log_fn/device_log.h"

static const char *TAG = "DEVICE_LOG";

static QueueHandle_t     log_queue = NULL;
static SemaphoreHandle_t log_mutex = NULL;

static char       last_msg[DEVICE_LOG_MSG_LEN] = "";
static TickType_t last_tick = 0;


void device_log_init(void)
{
    if (log_queue != NULL) return;

    log_queue = xQueueCreate(DEVICE_LOG_QUEUE_LEN, sizeof(DeviceLogEntry));
    log_mutex = xSemaphoreCreateMutex();

    if (log_queue == NULL || log_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create device log queue");
    }
}


void device_log(const char *level, const char *fmt, ...)
{
    if (log_queue == NULL || log_mutex == NULL || fmt == NULL) return;

    DeviceLogEntry entry;
    memset(&entry, 0, sizeof(entry));

    strlcpy(entry.level, (level != NULL) ? level : "info", sizeof(entry.level));

    va_list args;
    va_start(args, fmt);
    vsnprintf(entry.msg, sizeof(entry.msg), fmt, args);
    va_end(args);

    if (entry.msg[0] == '\0') return;

    get_current_timestamp(entry.timestamp, sizeof(entry.timestamp));

    xSemaphoreTake(log_mutex, portMAX_DELAY);

    TickType_t now = xTaskGetTickCount();
    uint32_t   since_sec = (uint32_t)((now - last_tick) / configTICK_RATE_HZ);
    bool repeat = (last_msg[0] != '\0') &&
                  (strcmp(last_msg, entry.msg) == 0) &&
                  (since_sec < DEVICE_LOG_REPEAT_SEC);

    if (!repeat) {
        strlcpy(last_msg, entry.msg, sizeof(last_msg));
        last_tick = now;

        /* Queue full (long time offline): the new entry is dropped */
        if (xQueueSend(log_queue, &entry, 0) != pdTRUE) {
            ESP_LOGW(TAG, "Log queue full, dropped: %s", entry.msg);
        }
    }

    xSemaphoreGive(log_mutex);

    if (!repeat) {
        ESP_LOGI(TAG, "[%s] %s", entry.level, entry.msg);
    }
}


bool device_log_peek(DeviceLogEntry *out)
{
    if (log_queue == NULL || out == NULL) return false;
    return xQueuePeek(log_queue, out, 0) == pdTRUE;
}


void device_log_drop(void)
{
    if (log_queue == NULL) return;

    DeviceLogEntry tmp;
    xQueueReceive(log_queue, &tmp, 0);
}
