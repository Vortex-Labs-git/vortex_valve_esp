/**
 * @file time_func.c
 * @brief SNTP Time Synchronization and Timestamp Utilities
 *
 * This module:
 *  - Synchronizes system time using SNTP (NTP servers)
 *  - Configures timezone
 *  - Waits until valid time is obtained
 *  - Provides helper function to get ISO 8601 timestamps
 */

#include <stdio.h>
#include <time.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"

#include "i2cdev.h"
#include "ds3231.h"

#include "esp_sntp.h"
#include "esp_log.h"

#include "time_fn/time_func.h"

#define I2C_PORT I2C_NUM_0
#define SDA_GPIO 21
#define SCL_GPIO 22


static const char *TAG_TIME = "TimeSync";

static i2c_dev_t rtc_dev;

/* ======================================================================== */
/* ========================== RTC FUNCTIONS ======================== */
/* ======================================================================== */
void rtcmodule_init(void)
{
    memset(&rtc_dev, 0, sizeof(i2c_dev_t));
    ESP_ERROR_CHECK(i2cdev_init());
    ESP_ERROR_CHECK(ds3231_init_desc(&rtc_dev, I2C_PORT, SDA_GPIO, SCL_GPIO));

    ESP_LOGI(TAG_TIME, "RTC initialized");
}

bool rtc_get_time(struct tm *timeinfo) {
    if (ds3231_get_time(&rtc_dev, timeinfo) == ESP_OK) {
        return true;
    }
    return false;
}

void rtc_set_time(const struct tm *timeinfo) {
    ds3231_set_time(&rtc_dev, timeinfo);
}

void set_system_time_from_rtc(void)
{
    struct tm rtc_time;

    if (!rtc_get_time(&rtc_time)) {
        ESP_LOGW(TAG_TIME, "RTC read failed");
        return;
    }

    setenv("TZ", "UTC0", 1);
    tzset();

    time_t utc_time = mktime(&rtc_time);

    struct timeval now = {
        .tv_sec = utc_time,
        .tv_usec = 0
    };
    settimeofday(&now, NULL);

    time_t current;
    struct tm local_time;

    time(&current);
    localtime_r(&current, &local_time);

    ESP_LOGI(TAG_TIME,
             "System time restored from RTC: %04d-%02d-%02d %02d:%02d:%02d",
             local_time.tm_year + 1900,
             local_time.tm_mon + 1,
             local_time.tm_mday,
             local_time.tm_hour,
             local_time.tm_min,
             local_time.tm_sec);
}


void update_rtc_after_ntp(void)
{
    time_t now;
    struct tm utc_time;
    time(&now);
    gmtime_r(&now, &utc_time);
    rtc_set_time(&utc_time); // Update RTC
    ESP_LOGI(TAG_TIME, "RTC updated from NTP");
}


/* ======================================================================== */
/* ========================== TIME SYNCHRONIZATION ======================== */
/* ======================================================================== */

/**
 * @brief Synchronize system time using SNTP
 *
 * This function:
 *  1. Configures SNTP in polling mode
 *  2. Sets multiple NTP servers (for redundancy)
 *  3. Initializes SNTP service
 *  4. Sets timezone to Asia/Colombo
 *  5. Blocks until a valid time is received
 *
 * The function waits in a loop until the year becomes >= 2020,
 * which ensures that the system time has been properly updated
 * from the NTP server (instead of default epoch time).
 *
 * @note This function blocks until time is synchronized.
 *       Should be called after WiFi connection is established.
 */
static bool s_sntp_started = false;   /* SNTP configured at least once */
static bool s_sync_task_running = false;
void obtain_time(void *pvParameters)
{
    (void) pvParameters;

    if (s_sync_task_running) {
        ESP_LOGW(TAG_TIME, "Time sync task already running, skipping");
        vTaskDelete(NULL);
        return;
    }
    s_sync_task_running = true;

    if (!s_sntp_started) {
        ESP_LOGI(TAG_TIME, "Initializing SNTP");
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_setservername(1, "time.nist.gov");
        esp_sntp_setservername(2, "time.google.com");
        esp_sntp_init();
        s_sntp_started = true;
    } else {
        ESP_LOGI(TAG_TIME, "SNTP already running, restarting sync");
        sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);  /* force a fresh sync */
        esp_sntp_restart();
    }


    time_t now = 0;
    struct tm timeinfo = {0};
    
    int retry = 0;
    const int retry_count = 30;

    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET &&
        retry < retry_count)
    {
        ESP_LOGI(TAG_TIME, "Waiting for SNTP sync...");
        vTaskDelay(pdMS_TO_TICKS(2000));
        retry++;
    }

    time(&now);
    localtime_r(&now, &timeinfo);

    if (timeinfo.tm_year >= (2020 - 1900)) {
        setenv("TZ", "IST-5:30", 1);
        tzset();

        ESP_LOGI(TAG_TIME, "NTP Time synchronized successfully");
        ESP_LOGI(TAG_TIME, "Current ntp time: %04d-%02d-%02d %02d:%02d:%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

        // Update RTC after NTP sync
        update_rtc_after_ntp();
    } else {
        ESP_LOGW(TAG_TIME, "NTP Time sync failed, using default system time");
    }

    vTaskDelete(NULL); // Delete task after finishing
}


void Real_time_read(void *pvParameters){
    (void) pvParameters;

    setenv("TZ", "IST-5:30", 1);
    tzset();

    while(1) {

        char timestamp[32];
        get_current_timestamp(timestamp, sizeof(timestamp));
        ESP_LOGI(TAG_TIME, "Obtain Current Time loop: %s", timestamp);

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}




/* ======================================================================== */
/* ========================== TIMESTAMP UTILITY =========================== */
/* ======================================================================== */

/**
 * @brief Get current local timestamp in ISO 8601 format
 *
 * Format:
 *     YYYY-MM-DDTHH:MM:SSZ
 *
 * Example:
 *     2026-02-18T14:25:30Z
 *
 * @param[out] timestamp        Pointer to buffer to store formatted string
 * @param[in]  timestamp_size   Size of the buffer
 *
 * @note Ensure buffer size is at least 25 bytes to safely hold ISO string.
 */
void get_current_timestamp(char *timestamp, size_t timestamp_size)
{
    time_t rawtime;
    struct tm local_time;

    time(&rawtime);

    localtime_r(&rawtime, &local_time);

    strftime(timestamp,
             timestamp_size,
             "%Y-%m-%dT%H:%M:%S%z",
             &local_time);
}

/* ======================================================================== */
/* ======================= Module Initialization ======================= */
/* ======================================================================== */

void time_module_init(void)
{
    ESP_LOGI(TAG_TIME, "Initializing Time Module");

    setenv("TZ", "IST-5:30", 1);
    tzset();

    // Initialize RTC
    rtcmodule_init();

    // Set system time from RTC first
    set_system_time_from_rtc();

    xTaskCreate(Real_time_read, "real_time_read", 4096, NULL, 5, NULL);
    
}