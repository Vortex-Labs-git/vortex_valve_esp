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
#include "esp_sntp.h"
#include "esp_log.h"

#include "time_func.h"


static const char *TAG_TIME = "TimeSync";



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
void obtain_time(void)
{
    // Set the SNTP operating mode to polling
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    
    // Set SNTP server
    sntp_setservername(0, "pool.ntp.org");
    sntp_setservername(1, "time.nist.gov");
    sntp_setservername(2, "time.google.com");

    // Initialize the SNTP service
    sntp_init();

    setenv("TZ", "Asia/Colombo", 1);
    tzset();

    // get the correct time
    time_t now = 0;
    struct tm timeinfo = { 0 };
    while (timeinfo.tm_year < (2020 - 1900)) {
        ESP_LOGI(TAG_TIME, "Waiting for time to sync...");
        vTaskDelay(pdMS_TO_TICKS(2000));
        time(&now); // Get the current time
        localtime_r(&now, &timeinfo);
    }

    ESP_LOGI(TAG_TIME, "Time synchronized successfully");
    
    ESP_LOGI(TAG_TIME, "Current time: %04d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
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
void get_current_timestamp(char *timestamp, size_t timestamp_size) {
    time_t rawtime;
    struct tm *timeinfo;

    time(&rawtime); // Get current time
    timeinfo = localtime(&rawtime); // Convert to local time

    // Format the time to ISO 8601 format: YYYY-MM-DDTHH:MM:SSZ
    strftime(timestamp, timestamp_size, "%Y-%m-%dT%H:%M:%SZ", timeinfo);
}