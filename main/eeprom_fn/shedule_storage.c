#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "shedule_storage.h"


#define SHEDULE_NVS_NAMESPACE    "shedule_cfg"
#define SHEDULE_NVS_KEY          "shedule"

static const char *TAG_SCHEDULE = "schedule_storage";


esp_err_t schedule_storage_save( ScheduleInfo *scheList, size_t listSize) {

    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open(SHEDULE_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(handle, SHEDULE_NVS_KEY, scheList, listSize * sizeof(ScheduleInfo));
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    return err;
}


esp_err_t schedule_storage_load( ScheduleInfo *scheList, size_t maxListSize, size_t *listSize) {

    nvs_handle_t handle;
    esp_err_t err;
    size_t size = 0;

    err = nvs_open(SHEDULE_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_SCHEDULE, "No stored Schedule config");
        return err;
    }

    err = nvs_get_blob(handle, SHEDULE_NVS_KEY, scheList, &size);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    size_t count = size / sizeof(ScheduleInfo);
    if (count > maxListSize) {
        nvs_close(handle);
        return ESP_ERR_NO_MEM;
    }

    err = nvs_get_blob(handle, SHEDULE_NVS_KEY, scheList, &size);
    if (err == ESP_OK && listSize) {
        *listSize = count;
    }

    nvs_close(handle);

    return err;
}




// ScheduleInfo schedule[7] = {
//     {"Mon", "08:00", "17:00"},
//     {"Tue", "08:00", "17:00"},
// };

// schedule_save(schedule, 2);

// ScheduleInfo loaded[7];
// size_t loaded_count;
// schedule_load(loaded, 7, &loaded_count);