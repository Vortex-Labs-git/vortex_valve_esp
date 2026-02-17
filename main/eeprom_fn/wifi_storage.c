/**
 * @file wifi_storage.c
 * @brief NVS-based WiFi credential storage manager
 *
 * This module:
 *  - Loads WiFi credentials from Non-Volatile Storage (NVS)
 *  - Saves updated credentials to NVS
 *  - Restores default credentials from menuconfig
 *
 * WiFi credentials are stored as a binary blob (GetWifi structure).
 */

#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "wifi_storage.h"

/* ======================================================================== */
/* ========================== CONFIGURATION =============================== */
/* ======================================================================== */

/**
 * @brief Default WiFi credentials defined in menuconfig
 */
#define DEFAULT_WIFI_SSID     CONFIG_ESP_WIFI_STA_SSID
#define DEFAULT_WIFI_PASS     CONFIG_ESP_WIFI_STA_PASSWD

/**
 * @brief NVS namespace and key definitions
 *
 * Namespace:
 *   Logical storage group inside NVS
 *
 * Key:
 *   Identifier for stored WiFi blob
 */
#define WIFI_NVS_NAMESPACE    "wifi_cfg"
#define WIFI_NVS_KEY          "sta"

/**
 * @brief Logging tag
 */
static const char *TAG_WIFI = "wifi_storage";


/* ======================================================================== */
/* ========================== LOAD WIFI FROM NVS ========================== */
/* ======================================================================== */

/**
 * @brief Load WiFi credentials from NVS
 *
 * Reads the entire GetWifi structure as a binary blob.
 *
 * Flow:
 *   1. Open NVS namespace (read-only)
 *   2. Read blob into global wifiStaData
 *   3. Close NVS handle
 *
 * @return
 *   - ESP_OK if successful
 *   - ESP_ERR_NVS_NOT_FOUND if no data stored
 *   - Other NVS error codes if failure
 */
esp_err_t wifi_storage_load(void)
{
    nvs_handle_t handle;
    esp_err_t err;
    size_t size = sizeof(GetWifi);

    /**
     * Open NVS namespace in read-only mode
     */
    err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_WIFI, "No stored WiFi config");
        return err;
    }

    /**
     * Read WiFi data blob into global structure
     */
    err = nvs_get_blob(handle, WIFI_NVS_KEY, &wifiStaData, &size);

    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG_WIFI,
                 "WiFi loaded (ssid=%s,password=%s,set_wifi=%d)",
                 wifiStaData.ssid,
                 wifiStaData.password,
                 wifiStaData.set_wifi);
    } else {
        ESP_LOGE(TAG_WIFI,
                 "Failed to read WiFi data (%s)",
                 esp_err_to_name(err));
    }

    return err;
}


/* ======================================================================== */
/* ========================== SAVE WIFI TO NVS ============================ */
/* ======================================================================== */

/**
 * @brief Save WiFi credentials to NVS
 *
 * Writes the entire GetWifi structure as a blob.
 *
 * Flow:
 *   1. Validate WiFi data
 *   2. Open NVS (read-write)
 *   3. Write blob
 *   4. Commit changes
 *   5. Close handle
 *
 * @return
 *   - ESP_OK if successful
 *   - ESP_ERR_INVALID_ARG if invalid data
 *   - Other NVS error codes on failure
 */
esp_err_t wifi_storage_save(void)
{
    nvs_handle_t handle;
    esp_err_t err;

    /**
     * Validate WiFi data:
     * If marked as user-configured (set_wifi == true),
     * SSID must not be empty.
     */
    if (wifiStaData.set_wifi && strlen(wifiStaData.ssid) == 0) {
        ESP_LOGE(TAG_WIFI, "Invalid WiFi data: SSID empty");
        return ESP_ERR_INVALID_ARG;
    }

    /**
     * Open NVS namespace in read-write mode
     */
    err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_WIFI,
                 "Failed to open NVS (%s)",
                 esp_err_to_name(err));
        return err;
    }

    /**
     * Store the entire WiFi structure as a blob
     */
    err = nvs_set_blob(handle,
                       WIFI_NVS_KEY,
                       &wifiStaData,
                       sizeof(GetWifi));

    if (err != ESP_OK) {
        ESP_LOGE(TAG_WIFI,
                 "Failed to write WiFi blob (%s)",
                 esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    /**
     * Commit changes to make them permanent
     */
    err = nvs_commit(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG_WIFI,
                 "NVS commit failed (%s)",
                 esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG_WIFI, "WiFi saved to NVS");
    }

    nvs_close(handle);
    return err;
}


/* ======================================================================== */
/* ====================== RESTORE DEFAULT WIFI ============================ */
/* ======================================================================== */

/**
 * @brief Restore WiFi settings to default values from menuconfig
 *
 * This function:
 *  1. Clears current wifiStaData structure
 *  2. Copies default SSID and password
 *  3. Marks credentials as default (set_wifi = false)
 *  4. Saves the updated structure to NVS
 *
 * Used when:
 *  - Factory reset
 *  - CONFIG_ESP_WIFI_STA_MODE_RESET is enabled
 */
void wifi_storage_restore_default(void)
{
    /**
     * Clear entire WiFi structure
     */
    memset(&wifiStaData, 0, sizeof(GetWifi));

    /**
     * Copy default credentials
     */
    strcpy(wifiStaData.ssid, DEFAULT_WIFI_SSID);
    strcpy(wifiStaData.password, DEFAULT_WIFI_PASS);

    /**
     * Mark as default configuration
     */
    wifiStaData.set_wifi = false;

    /**
     * Save restored defaults to NVS
     */
    wifi_storage_save();

    ESP_LOGI(TAG_WIFI, "WiFi restored to default");
}
