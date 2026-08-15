/**
 * @file schedule_storage.c
 * @brief NVS-based Schedule Storage Manager
 *
 * This module:
 *  - Saves an array of ScheduleInfo structures to NVS
 *  - Loads stored schedule data from NVS
 *
 * Data is stored as a binary blob.
 */

#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "global_fn/global_var.h"

#include "schedule_storage.h" 

/* ======================================================================== */
/* ========================== NVS CONFIGURATION =========================== */
/* ======================================================================== */

/**
 * @brief NVS namespace used for schedule configuration
 */
#define SCHEDULE_NVS_NAMESPACE    "schedule_cfg"

/**
 * @brief Key used to store schedule blob inside namespace
 */
#define SCHEDULE_NVS_KEY          "schedule"

#define ENABLE_NVS_KEY         "set_schedule"

/**
 * @brief Logging tag
 */
static const char *TAG_SCHEDULE = "schedule_storage";


/* ======================================================================== */
/* ============================ SAVE SCHEDULE ============================= */
/* ======================================================================== */

/**
 * @brief Save schedule list to NVS (Non-Volatile Storage)
 *
 * The entire array of ScheduleInfo structures is stored
 * as a binary blob.
 *
 * Flow:
 *   1. Open NVS namespace (read-write)
 *   2. Write blob (array of ScheduleInfo)
 *   3. Commit changes
 *   4. Close NVS handle
 *
 * @param scheList   Pointer to array of ScheduleInfo structures
 * @param listSize   Number of ScheduleInfo elements in the array
 *
 * @return
 *   - ESP_OK on success
 *   - Error code from NVS functions on failure
 */
esp_err_t schedule_storage_save(ScheduleInfo *scheList, size_t listSize)
{
    nvs_handle_t handle;
    esp_err_t err;

    /**
     * Open NVS namespace in read-write mode
     */
    err = nvs_open(SCHEDULE_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    /**
     * Store schedule array as binary blob
     */
    err = nvs_set_blob(handle,
                       SCHEDULE_NVS_KEY,
                       scheList,
                       listSize * sizeof(ScheduleInfo));

    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    /**
     * Commit changes to flash
     */
    err = nvs_commit(handle);

    /**
     * Close NVS handle
     */
    nvs_close(handle);

    return err;
}


/* ======================================================================== */
/* ============================ LOAD SCHEDULE ============================= */
/* ======================================================================== */

/**
 * @brief Load schedule list from NVS
 *
 * Flow:
 *   1. Open NVS namespace (read-only)
 *   2. Query blob size
 *   3. Validate buffer capacity
 *   4. Read blob into provided array
 *   5. Return number of loaded entries
 *
 * @param scheList     Pointer to array where loaded data will be stored
 * @param maxListSize  Maximum number of ScheduleInfo elements
 *                     that can be stored in scheList
 * @param listSize     Output parameter: actual number of loaded entries
 *                     (can be NULL)
 *
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_NO_MEM if buffer is too small
 *   - ESP_ERR_NVS_NOT_FOUND if no schedule stored
 *   - Other NVS error codes on failure
 */
esp_err_t schedule_storage_load(ScheduleInfo *scheList,
                                size_t maxListSize,
                                size_t *listSize)
{
    nvs_handle_t handle;
    esp_err_t err;
    size_t size = 0;

    err = nvs_open(SCHEDULE_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_SCHEDULE, "No stored Schedule config");
        return err;
    }

    // Step 1: Get required size correctly
    err = nvs_get_blob(handle, SCHEDULE_NVS_KEY, NULL, &size);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    size_t count = size / sizeof(ScheduleInfo);

    if (count > maxListSize) {
        nvs_close(handle);
        return ESP_ERR_NO_MEM;
    }

    // Step 2: Clear buffer before loading
    memset(scheList, 0, maxListSize * sizeof(ScheduleInfo));

    // Step 3: Load actual data
    err = nvs_get_blob(handle, SCHEDULE_NVS_KEY, scheList, &size);

    if (err == ESP_OK && listSize) {
        *listSize = count;
    }

    nvs_close(handle);
    return err;
}


/* ======================================================================== */
/* ======================= LOAD EEPROM SCHEDULE =========================== */
/* ======================================================================== */

/**
 * @brief Load schedule from NVS into runtime system
 *
 * This function acts as a bridge between persistent storage (NVS)
 * and the application's runtime data structure.
 *
 * Flow:
 *   1. Clear temporary schedule buffer
 *   2. Load schedule data from NVS into buffer
 *   3. If successful:
 *        a. Acquire mutex for thread-safe access
 *        b. Copy loaded data into global serverControl structure
 *        c. Release mutex
 *   4. Log result (success or fallback condition)
 *
 * Notes:
 *   - Uses a temporary buffer (loaded_schedule) to avoid partial updates
 *   - Protects shared data using serverMutex (FreeRTOS synchronization)
 *   - If no data exists in NVS, system waits for external update (e.g., MQTT)
 *
 * Globals Used:
 *   - loaded_schedule   : Temporary buffer for NVS data
 *   - loaded_count      : Number of loaded schedule entries
 *   - serverControl     : Global runtime schedule storage
 *   - serverMutex       : Mutex for thread-safe access
 *
 * @return None
 */
void load_eeprom_schedule(){

    bool enabled = false;
    schedule_set_enable_load(&enabled);

    ESP_LOGI(TAG_SCHEDULE, "Loaded schedule enable state: %s", enabled ? "true" : "false");
    loaded_schedule_ctrl = enabled;
    /**
     * Step 1: Clear temporary buffer
     */
    memset(loaded_schedule, 0, sizeof(loaded_schedule));
    
    /**
     * Step 2: Load schedule data from NVS
     */
    if (schedule_storage_load(loaded_schedule, 10, &loaded_count) == ESP_OK) {
        ESP_LOGI(TAG_SCHEDULE, "Loaded %d schedule entries from NVS", loaded_count);
    } else {
        ESP_LOGI(TAG_SCHEDULE, "No schedule stored in NVS, waiting for MQTT update");
    }
}


/* ======================================================================== */
/* ============================ SAVE SCHEDULE ENABLE ====================== */
/* ======================================================================== */
esp_err_t schedule_set_enable_save(bool enable)
{
    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open(SCHEDULE_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_u8(handle, ENABLE_NVS_KEY, (uint8_t)enable);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    return err;
}


/* ======================================================================== */
/* ============================ LOAD SCHEDULE ENABLE ====================== */
/* ======================================================================== */
esp_err_t schedule_set_enable_load(bool *enable)
{
    nvs_handle_t handle;
    esp_err_t err;
    uint8_t value = 0;

    err = nvs_open(SCHEDULE_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;

    err = nvs_get_u8(handle, ENABLE_NVS_KEY, &value);
    nvs_close(handle);

    if (err == ESP_OK && enable) {
        *enable = (bool)value;
    }

    return err;
}