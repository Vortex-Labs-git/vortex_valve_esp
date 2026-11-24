/**
 * @file device_log.c
 * @brief Device event log buffer (see device_log.h)
 *
 * The buffer lives in RTC memory (RTC_NOINIT_ATTR): it is kept across a
 * software restart, crash or watchdog reset, and cleared on power on.
 * So logs written just before a restart (WiFi updated, firmware update)
 * are still sent after it.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_app_desc.h"

#include "time_fn/time_func.h"
#include "log_fn/device_log.h"

static const char *TAG = "DEVICE_LOG";

#define LOG_STORE_MAGIC   0x564C4F47u     /* "VLOG" */

typedef struct {
    uint32_t       magic;
    uint32_t       head;                  /* index of the oldest entry */
    uint32_t       count;
    uint32_t       next_seq;
    uint32_t       seq[DEVICE_LOG_BUF_LEN];
    char           restart_reason[48];    /* set by device_restart() */
    DeviceLogEntry entries[DEVICE_LOG_BUF_LEN];
} LogStore;

static RTC_NOINIT_ATTR LogStore store;

static SemaphoreHandle_t log_mutex = NULL;
static uint32_t          peeked_seq = 0;

static char       last_msg[DEVICE_LOG_MSG_LEN] = "";
static TickType_t last_tick = 0;


/* ======================================================================== */
/* ============================ RESTART REASON ============================ */
/* ======================================================================== */

static const char *reset_reason_text(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON:   return "power on";
        case ESP_RST_SW:        return "restart";
        case ESP_RST_PANIC:     return "crash";
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:       return "watchdog";
        case ESP_RST_BROWNOUT:  return "low power";
        case ESP_RST_DEEPSLEEP: return "deep sleep";
        case ESP_RST_EXT:       return "reset pin";
        default:                return "unknown";
    }
}

/* One boot log: why the device (re)started, and which firmware runs now */
static void log_boot(esp_reset_reason_t reason) {
    const char *ver = esp_app_get_description()->version;

    switch (reason) {
        case ESP_RST_POWERON:
            DLOG_INFO("Powered on (v%s)", ver);
            break;
        case ESP_RST_SW:
            if (store.restart_reason[0] != '\0') {
                DLOG_INFO("Restarted: %s (v%s)", store.restart_reason, ver);
            } else {
                DLOG_INFO("Restarted (v%s)", ver);
            }
            break;
        case ESP_RST_DEEPSLEEP:
        case ESP_RST_EXT:
            DLOG_INFO("Restarted: %s (v%s)", reset_reason_text(reason), ver);
            break;
        default:   /* crash, watchdog, low power, unknown */
            DLOG_ERROR("Restarted after %s (v%s)", reset_reason_text(reason), ver);
            break;
    }

    store.restart_reason[0] = '\0';
}


/* ======================================================================== */
/* ================================ INIT ================================== */
/* ======================================================================== */

void device_log_init(void) {
    if (log_mutex != NULL) return;

    log_mutex = xSemaphoreCreateMutex();
    if (log_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create device log mutex");
        return;
    }

    esp_reset_reason_t reason = esp_reset_reason();

    bool kept = (reason != ESP_RST_POWERON) &&
                (store.magic == LOG_STORE_MAGIC) &&
                (store.head  <  DEVICE_LOG_BUF_LEN) &&
                (store.count <= DEVICE_LOG_BUF_LEN);

    if (!kept) {
        memset(&store, 0, sizeof(store));
        store.magic = LOG_STORE_MAGIC;
    } else {
        /* RTC memory survived the restart: make sure every string is terminated */
        store.restart_reason[sizeof(store.restart_reason) - 1] = '\0';
        for (int i = 0; i < DEVICE_LOG_BUF_LEN; i++) {
            store.entries[i].level[sizeof(store.entries[i].level) - 1]         = '\0';
            store.entries[i].timestamp[sizeof(store.entries[i].timestamp) - 1] = '\0';
            store.entries[i].msg[sizeof(store.entries[i].msg) - 1]             = '\0';
        }
        ESP_LOGI(TAG, "%lu unsent log(s) kept from before the restart", (unsigned long)store.count);
    }

    log_boot(reason);
}


/* ======================================================================== */
/* ================================= LOG ================================== */
/* ======================================================================== */

void device_log(const char *level, const char *fmt, ...) {
    if (log_mutex == NULL || fmt == NULL) return;

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

        /* Full (long time offline): drop the oldest entry */
        if (store.count == DEVICE_LOG_BUF_LEN) {
            ESP_LOGW(TAG, "Log buffer full, dropped: %s", store.entries[store.head].msg);
            store.head = (store.head + 1) % DEVICE_LOG_BUF_LEN;
            store.count--;
        }

        uint32_t idx = (store.head + store.count) % DEVICE_LOG_BUF_LEN;
        store.entries[idx] = entry;
        store.seq[idx]     = ++store.next_seq;
        store.count++;
    }

    xSemaphoreGive(log_mutex);

    if (!repeat) {
        ESP_LOGI(TAG, "[%s] %s", entry.level, entry.msg);
    }
}


/* ======================================================================== */
/* ============================ RESTART =================================== */
/* ======================================================================== */

void device_restart(const char *reason) {
    if (log_mutex != NULL) xSemaphoreTake(log_mutex, portMAX_DELAY);
    strlcpy(store.restart_reason, (reason != NULL) ? reason : "", sizeof(store.restart_reason));
    if (log_mutex != NULL) xSemaphoreGive(log_mutex);

    ESP_LOGW(TAG, "Restarting: %s", store.restart_reason);
    esp_restart();
}


/* ======================================================================== */
/* ============================ SEND HELPERS ============================== */
/* ======================================================================== */

bool device_log_peek(DeviceLogEntry *out) {
    if (log_mutex == NULL || out == NULL) return false;

    bool found = false;
    xSemaphoreTake(log_mutex, portMAX_DELAY);
    if (store.count > 0) {
        *out       = store.entries[store.head];
        peeked_seq = store.seq[store.head];
        found      = true;
    }
    xSemaphoreGive(log_mutex);
    return found;
}


void device_log_drop(void) {
    if (log_mutex == NULL) return;

    xSemaphoreTake(log_mutex, portMAX_DELAY);
    /* Only drop the entry that was peeked (it may have been pushed out meanwhile) */
    if (store.count > 0 && store.seq[store.head] == peeked_seq) {
        store.head = (store.head + 1) % DEVICE_LOG_BUF_LEN;
        store.count--;
    }
    xSemaphoreGive(log_mutex);
}
