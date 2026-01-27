#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "wifi_storage.h"


#define DEFAULT_WIFI_SSID     CONFIG_ESP_WIFI_STA_SSID
#define DEFAULT_WIFI_PASS     CONFIG_ESP_WIFI_STA_PASSWD

#define WIFI_NVS_NAMESPACE    "wifi_cfg"
#define WIFI_NVS_KEY          "sta"

static const char *TAG_WIFI = "wifi_storage";

// load the wifi save data
esp_err_t wifi_storage_load(void)
{
    nvs_handle_t handle;
    esp_err_t err;
    size_t size = sizeof(GetWifi);

    err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_WIFI, "No stored WiFi config");
        return err;
    }

    err = nvs_get_blob(handle, WIFI_NVS_KEY, &wifiStaData, &size);
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG_WIFI, "WiFi loaded from NVS (ssid=%s,password=%s,set_wifi=%d)", wifiStaData.ssid, wifiStaData.password, wifiStaData.set_wifi);
    }
    else {
        ESP_LOGE(TAG_WIFI, "Failed to read WiFi data (%s)", esp_err_to_name(err));
    }

    return err;
}

// save the wifi data to nvs
esp_err_t wifi_storage_save(void)
{
    nvs_handle_t handle;
    esp_err_t err;

    if (wifiStaData.set_wifi && strlen(wifiStaData.ssid) == 0) {
        ESP_LOGE(TAG_WIFI, "Invalid WiFi data: SSID empty");
        return ESP_ERR_INVALID_ARG;
    }

    err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_WIFI, "Failed to open NVS (%s)", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(handle, WIFI_NVS_KEY, &wifiStaData, sizeof(GetWifi));
    if (err != ESP_OK) {
        ESP_LOGE(TAG_WIFI, "Failed to write WiFi blob (%s)", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_WIFI, "NVS commit failed (%s)", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG_WIFI, "WiFi saved to NVS");
    }

    nvs_close(handle);
    return err;
}

/* ---------- Restore Default WiFi ---------- */
void wifi_storage_restore_default(void)
{
    memset(&wifiStaData, 0, sizeof(GetWifi));

    strcpy(wifiStaData.ssid, DEFAULT_WIFI_SSID);
    strcpy(wifiStaData.password, DEFAULT_WIFI_PASS);
    wifiStaData.set_wifi = false;

    wifi_storage_save();

    ESP_LOGI(TAG_WIFI, "WiFi restored to default");
}
