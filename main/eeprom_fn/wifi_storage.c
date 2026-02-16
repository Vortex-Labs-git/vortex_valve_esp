#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "wifi_storage.h"

// Default WiFi credentials from menuconfig
#define DEFAULT_WIFI_SSID     CONFIG_ESP_WIFI_STA_SSID
#define DEFAULT_WIFI_PASS     CONFIG_ESP_WIFI_STA_PASSWD

// NVS namespace and key for storing WiFi credentials
#define WIFI_NVS_NAMESPACE    "wifi_cfg"
#define WIFI_NVS_KEY          "sta"

// Tag used for ESP logging
static const char *TAG_WIFI = "wifi_storage";


/**
 * @brief Load WiFi credentials from NVS
 *
 * @return ESP_OK on success, or an error code from NVS functions
 */
esp_err_t wifi_storage_load(void)
{
    nvs_handle_t handle;
    esp_err_t err;
    size_t size = sizeof(GetWifi);  // size of the structure to read

    // Open NVS namespace in read-only mode
    err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_WIFI, "No stored WiFi config");  // No stored WiFi info yet
        return err;
    }

    // Read WiFi data blob from NVS
    err = nvs_get_blob(handle, WIFI_NVS_KEY, &wifiStaData, &size);
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG_WIFI, "WiFi loaded from NVS (ssid=%s,password=%s,set_wifi=%d)",
                 wifiStaData.ssid, wifiStaData.password, wifiStaData.set_wifi);
    } else {
        ESP_LOGE(TAG_WIFI, "Failed to read WiFi data (%s)", esp_err_to_name(err));
    }

    return err;
}


/**
 * @brief Save WiFi credentials to NVS
 *
 * @return ESP_OK on success, or an error code from NVS functions
 */
esp_err_t wifi_storage_save(void)
{
    nvs_handle_t handle;
    esp_err_t err;

    // Validate WiFi data: if set_wifi is true, SSID cannot be empty
    if (wifiStaData.set_wifi && strlen(wifiStaData.ssid) == 0) {
        ESP_LOGE(TAG_WIFI, "Invalid WiFi data: SSID empty");
        return ESP_ERR_INVALID_ARG;
    }

    // Open NVS namespace in read-write mode
    err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_WIFI, "Failed to open NVS (%s)", esp_err_to_name(err));
        return err;
    }

    // Write the WiFi structure as a blob
    err = nvs_set_blob(handle, WIFI_NVS_KEY, &wifiStaData, sizeof(GetWifi));
    if (err != ESP_OK) {
        ESP_LOGE(TAG_WIFI, "Failed to write WiFi blob (%s)", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    // Commit changes to NVS to persist data
    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_WIFI, "NVS commit failed (%s)", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG_WIFI, "WiFi saved to NVS");
    }

    // Close the NVS handle
    nvs_close(handle);
    return err;
}


/**
 * @brief Restore WiFi settings to default credentials and save to NVS
 */
void wifi_storage_restore_default(void)
{
    // Clear existing WiFi data structure
    memset(&wifiStaData, 0, sizeof(GetWifi));

    // Restore default credentials from menuconfig
    strcpy(wifiStaData.ssid, DEFAULT_WIFI_SSID);
    strcpy(wifiStaData.password, DEFAULT_WIFI_PASS);
    wifiStaData.set_wifi = false;  // mark as default, not user-configured

    // Save the restored default settings to NVS
    wifi_storage_save();

    ESP_LOGI(TAG_WIFI, "WiFi restored to default");
}
