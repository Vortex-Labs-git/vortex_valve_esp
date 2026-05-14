/**
 * @file valve_calibration_storage.c
 * @brief NVS storage for valve calibration values
 */

#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "encoder_storage.h"
#include "global_fn/global_var.h" 

/* ===================== NVS CONFIG ===================== */

#define CALIB_NVS_NAMESPACE   "valve_cfg"
#define CALIB_NVS_KEY         "calib"

static const char *TAG = "VALVE_CALIB";

/* ===================== SAVE ===================== */

esp_err_t valve_calib_save(ValveCalib *calib)
{
    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open(CALIB_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed");
        return err;
    }

    err = nvs_set_blob(handle, CALIB_NVS_KEY, calib, sizeof(ValveCalib));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write blob");
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Calibration saved");
    } else {
        ESP_LOGE(TAG, "Commit failed");
    }

    return err;
}

/* ===================== LOAD ===================== */

esp_err_t valve_calib_load(ValveCalib *calib)
{
    nvs_handle_t handle;
    esp_err_t err;
    size_t size = sizeof(ValveCalib);

    err = nvs_open(CALIB_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No calibration namespace found");
        return err;
    }

    err = nvs_get_blob(handle, CALIB_NVS_KEY, calib, &size);

    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Calibration loaded");
    } else {
        ESP_LOGW(TAG, "No calibration stored");
    }

    return err;
}

/* ===================== LOAD INTO RUNTIME ===================== */

void load_eeprom_calibration(void)
{
    ValveCalib calib;

    if (valve_calib_load(&calib) == ESP_OK) {

        if (calib.close_limit_encode >= calib.open_limit_encode) {
            ESP_LOGE(TAG, "Invalid stored calibration! Using defaults");

            calib.close_limit_encode = 1500;
            calib.open_limit_encode  = 2500;
        }

        xSemaphoreTake(valveMutex, portMAX_DELAY);

        valveData.close_limit_encode = calib.close_limit_encode;
        valveData.open_limit_encode  = calib.open_limit_encode;

        xSemaphoreGive(valveMutex);

        ESP_LOGI(TAG, "Loaded -> close: %.2f open: %.2f",
                 calib.close_limit_encode,
                 calib.open_limit_encode);

    } else {

        ESP_LOGW(TAG, "Using default calibration (1500 - 2500)");

        xSemaphoreTake(valveMutex, portMAX_DELAY);

        valveData.close_limit_encode = 1500;
        valveData.open_limit_encode  = 2500;

        xSemaphoreGive(valveMutex);
    }
}

/* ===================== SAVE FROM RUNTIME ===================== */

void save_eeprom_calibration(void)
{
    ValveCalib calib;

    xSemaphoreTake(valveMutex, portMAX_DELAY);

    calib.close_limit_encode = valveData.close_limit_encode;
    calib.open_limit_encode  = valveData.open_limit_encode;

    xSemaphoreGive(valveMutex);

    /* -------- Validation (VERY IMPORTANT) -------- */
    if (calib.close_limit_encode >= calib.open_limit_encode) {
        ESP_LOGE(TAG, "Invalid calibration: close >= open");
        return;
    }

    if (valve_calib_save(&calib) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save calibration");
    }
}