/**
 * @file main.c
 * @brief Smart WiFi Valve — Vortex Hub edition
 *
 * The valve talks only to the Vortex Hub over ESP-NOW.
 * No router, no MQTT on the valve; the hub forwards to the cloud.
 *
 * Start-up:
 *  1. NVS, device identity, hub config
 *  2. Mutexes, schedule, motor calibration
 *  3. RTC time, valve hardware
 *  4. link_manager: WiFi + ESP-NOW + hub search / AP for the phone
 *  5. Valve process and schedule save tasks
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "global_fn/global_var.h"
#include "eeprom_fn/id_storage.h"
#include "eeprom_fn/hub_storage.h"
#include "eeprom_fn/schedule_storage.h"
#include "eeprom_fn/encoder_storage.h"
#include "time_fn/time_func.h"
#include "valve_fn/valve_process.h"
#include "main_fn/main_process.h"
#include "test_fn/test_process.h"
#include "link_fn/link_manager.h"



/* ======================================================================== */
/* ========================== GLOBAL VARIABLES ========================== */
/* ======================================================================== */

/**
 * @brief Mutex to protect valve-related operations
 */
SemaphoreHandle_t valveMutex = NULL;
/**
 * @brief Mutex to protect schedule operations
 */
SemaphoreHandle_t scheduleMutex = NULL;
/**
 * @brief Mutex to protect server (hub / phone) data
 */
SemaphoreHandle_t serverMutex = NULL;

static const char *TAG_MAIN = "MAIN LOOP";



/* ======================================================================== */
/* ================================ MAIN ================================== */
/* ======================================================================== */

/**
 * @brief Application entry point
 */
void app_main(void) {

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // NVS Init
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);


    id_storage_load();

#if CONFIG_HUB_STORAGE_RESET
    hub_storage_restore_default();
#endif
    hub_storage_load();

    valveMutex = xSemaphoreCreateMutex();
    if (valveMutex == NULL) {
        ESP_LOGE(TAG_MAIN, "Failed to create valveMutex");
        return;
    }

    scheduleMutex = xSemaphoreCreateMutex();
    if (scheduleMutex == NULL) {
        ESP_LOGE(TAG_MAIN, "Failed to create scheduleMutex");
        return;
    }

    serverMutex = xSemaphoreCreateMutex();
    if (serverMutex == NULL) {
        ESP_LOGE(TAG_MAIN, "Failed to create serverMutex");
        return;
    }

    load_eeprom_schedule();
    load_eeprom_calibration();

    time_module_init();
    init_valve_system();
    // start_limit_test();
    // start_pot_test();
    // start_motor_test();
    // start_motor_pos();
    // start_valve_toggle_test();


    link_manager_start();

    xTaskCreate(schedule_save_task, "schedule_save_task", 4096, NULL, 5, NULL);

    xTaskCreate(valve_sync_process, "valve_sync_process", 4096, NULL, 5, NULL);

}