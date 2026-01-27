#include <stdio.h>
#include <time.h>
#include "esp_sntp.h"
#include "esp_log.h"

#include "time_func.h"


static const char *TAG_TIME = "TimeSync";



// Callback function when SNTP sync is complete. if time isnt synced try in loop
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



// Function to get the current timestamp in ISO format
void get_current_timestamp(char *timestamp, size_t timestamp_size) {
    time_t rawtime;
    struct tm *timeinfo;

    time(&rawtime); // Get current time
    timeinfo = localtime(&rawtime); // Convert to local time

    // Format the time to ISO 8601 format: YYYY-MM-DDTHH:MM:SSZ
    strftime(timestamp, timestamp_size, "%Y-%m-%dT%H:%M:%SZ", timeinfo);
}