/**
 * @file time_func.c
 * @brief System time from the Vortex Hub (no RTC, no SNTP on the valve)
 *
 * This module:
 *  - Sets the local timezone
 *  - Applies the time the hub sends (TIME_SYNC over ESP-NOW)
 *  - Tells the valve logic whether the time is valid yet
 *  - Provides the ISO 8601 timestamp used in every JSON message
 *
 * Time flow:
 *   boot           -> clock starts at 1970, time_is_synced() = false
 *   CALLX_ACK      -> hub sends TIME_SYNC right after pairing
 *   every 1 hour   -> hub sends TIME_SYNC again (drift correction)
 *   hub lost       -> clock keeps running on the ESP32 crystal
 *   reboot         -> back to "not synced" until the hub is found again
 *
 * Rule: no schedule runs while time_is_synced() is false.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

#include "esp_log.h"

#include "time_fn/time_func.h"


/* ======================================================================== */
/* ========================== CONFIGURATION =============================== */
/* ======================================================================== */

/**
 * @brief Local timezone (POSIX format: Sri Lanka / India, UTC+5:30)
 */
#define LOCAL_TIMEZONE          "IST-5:30"

/**
 * @brief Any time before 2024-01-01 00:00 UTC is not a real time
 */
#define MIN_VALID_EPOCH         1704067200LL

static const char *TAG_TIME = "TimeSync";

static volatile bool s_time_synced = false;



/* ======================================================================== */
/* ======================= MODULE INITIALIZATION ========================== */
/* ======================================================================== */

/**
 * @brief Set the timezone; the clock waits for the hub
 */
void time_module_init(void)
{
    setenv("TZ", LOCAL_TIMEZONE, 1);
    tzset();

    s_time_synced = false;

    ESP_LOGI(TAG_TIME, "Time module ready, waiting for time from hub");
}



/* ======================================================================== */
/* ============================ HUB TIME SYNC ============================= */
/* ======================================================================== */

/**
 * @brief Apply time received from the hub (TIME_SYNC)
 *
 * Called from the ESP-NOW receive task. The hub is the only time source,
 * so its time always overwrites the valve clock.
 *
 * @param epoch_utc  UTC seconds since 1970
 */
void time_apply_sync(int64_t epoch_utc)
{
    if (epoch_utc < MIN_VALID_EPOCH) {
        ESP_LOGW(TAG_TIME, "Ignored invalid hub time %lld", (long long)epoch_utc);
        return;
    }

    struct timeval now = {
        .tv_sec  = (time_t)epoch_utc,
        .tv_usec = 0
    };
    settimeofday(&now, NULL);

    s_time_synced = true;

    char timestamp[32];
    get_current_timestamp(timestamp, sizeof(timestamp));
    ESP_LOGI(TAG_TIME, "Time synced from hub: %s", timestamp);
}

/**
 * @brief True once the hub has given us a valid time since boot
 */
bool time_is_synced(void)
{
    return s_time_synced;
}



/* ======================================================================== */
/* ========================== TIMESTAMP UTILITY =========================== */
/* ======================================================================== */

/**
 * @brief Get current local timestamp in ISO 8601 format
 *
 * Format:
 *     YYYY-MM-DDTHH:MM:SS+0530
 *
 * Example:
 *     2026-02-18T14:25:30+0530
 *
 * @param[out] timestamp        Buffer for the string (at least 25 bytes)
 * @param[in]  timestamp_size   Size of the buffer
 */
void get_current_timestamp(char *timestamp, size_t timestamp_size)
{
    time_t rawtime;
    struct tm local_time;

    time(&rawtime);
    localtime_r(&rawtime, &local_time);

    /* strftime returns 0 when the buffer is too small; never hand back garbage */
    if (strftime(timestamp, timestamp_size, "%Y-%m-%dT%H:%M:%S%z", &local_time) == 0) {
        timestamp[0] = '\0';
        ESP_LOGW(TAG_TIME, "Timestamp buffer too small (%u bytes, need 25)", (unsigned)timestamp_size);
    }
}