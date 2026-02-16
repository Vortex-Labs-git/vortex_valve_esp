#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "shedule_storage.h"

// NVS namespace and key used to store schedule data
#define SHEDULE_NVS_NAMESPACE    "shedule_cfg"
#define SHEDULE_NVS_KEY          "shedule"

// Tag used for ESP logging
static const char *TAG_SCHEDULE = "schedule_storage";


/**
 * @brief Save schedule list to NVS (Non-Volatile Storage)
 *
 * @param scheList   Pointer to array of ScheduleInfo structures
 * @param listSize   Number of ScheduleInfo elements in the array
 *
 * @return ESP_OK on success, or error code from NVS functions
 */
esp_err_t schedule_storage_save(ScheduleInfo *scheList, size_t listSize) {

    nvs_handle_t handle;
    esp_err_t err;

    // Open NVS namespace in read-write mode
    err = nvs_open(SHEDULE_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    // Store the entire schedule array as a binary blob
    err = nvs_set_blob(handle,
                       SHEDULE_NVS_KEY,
                       scheList,
                       listSize * sizeof(ScheduleInfo));
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    // Commit changes to ensure data is written to flash
    err = nvs_commit(handle);

    // Close the NVS handle
    nvs_close(handle);

    return err;
}


/**
 * @brief Load schedule list from NVS
 *
 * @param scheList     Pointer to array where loaded data will be stored
 * @param maxListSize  Maximum number of ScheduleInfo elements that can be stored in scheList
 * @param listSize     Output: actual number of elements loaded (can be NULL)
 *
 * @return ESP_OK on success, or error code from NVS functions
 */
esp_err_t schedule_storage_load(ScheduleInfo *scheList,
                                size_t maxListSize,
                                size_t *listSize) {

    nvs_handle_t handle;
    esp_err_t err;
    size_t size = 0;

    // Open NVS namespace in read-only mode
    err = nvs_open(SHEDULE_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_SCHEDULE, "No stored Schedule config");
        return err;
    }

    // First call to get required blob size
    err = nvs_get_blob(handle, SHEDULE_NVS_KEY, NULL, &size);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    // Calculate number of ScheduleInfo entries stored
    size_t count = size / sizeof(ScheduleInfo);

    // Check if provided buffer is large enough
    if (count > maxListSize) {
        nvs_close(handle);
        return ESP_ERR_NO_MEM;
    }

    // Read the actual blob data into provided buffer
    err = nvs_get_blob(handle, SHEDULE_NVS_KEY, scheList, &size);
    if (err == ESP_OK && listSize) {
        *listSize = count;  // Return number of loaded entries
    }

    // Close NVS handle
    nvs_close(handle);

    return err;
}


/*
Example usage:

// Define schedule entries
ScheduleInfo schedule[7] = {
    {"Mon", "08:00", "17:00"},
    {"Tue", "08:00", "17:00"},
};

// Save 2 entries to NVS
schedule_storage_save(schedule, 2);

// Load schedule from NVS
ScheduleInfo loaded[7];
size_t loaded_count;
schedule_storage_load(loaded, 7, &loaded_count);
*/
