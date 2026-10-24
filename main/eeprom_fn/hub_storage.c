/**
 * @file hub_storage.c
 * @brief NVS storage of the hub this valve belongs to
 *
 * This module:
 *  - Loads hub MAC, pair key and last channel from NVS
 *  - Saves them when the phone changes the hub, or the hub channel moves
 *  - Restores the defaults from menuconfig
 *
 * Stored as one binary blob (HubConfig structure).
 * Empty defaults = valve is not paired; it goes straight to AP mode.
 */

#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_mac.h"

#include "hub_storage.h"
#include "espnow_fn/espnow_util.h"

/* ======================================================================== */
/* ========================== CONFIGURATION =============================== */
/* ======================================================================== */

/**
 * @brief Default hub defined in menuconfig (empty = not paired)
 */
#define DEFAULT_HUB_MAC         CONFIG_HUB_DEFAULT_MAC
#define DEFAULT_HUB_PAIR_KEY    CONFIG_HUB_DEFAULT_PAIR_KEY

/**
 * @brief NVS namespace and key
 */
#define HUB_NVS_NAMESPACE       "hub_cfg"
#define HUB_NVS_KEY             "hub"

static const char *TAG_HUB = "hub_storage";


/* ======================================================================== */
/* =========================== LOAD HUB FROM NVS ========================== */
/* ======================================================================== */

/**
 * @brief Load the hub config from NVS into hubConfig
 *
 * Nothing stored yet (or the struct changed size) -> restore defaults.
 */
esp_err_t hub_storage_load(void)
{
    nvs_handle_t handle;
    size_t size = sizeof(HubConfig);

    esp_err_t err = nvs_open(HUB_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        err = nvs_get_blob(handle, HUB_NVS_KEY, &hubConfig, &size);
        nvs_close(handle);
    }

    if (err != ESP_OK || size != sizeof(HubConfig)) {
        ESP_LOGW(TAG_HUB, "No stored hub config, using defaults");
        hub_storage_restore_default();
        return err;
    }

    ESP_LOGI(TAG_HUB, "Hub loaded (mac=" MACSTR ", channel=%u, set_hub=%d)",
             MAC2STR(hubConfig.hub_mac), hubConfig.last_channel, hubConfig.set_hub);
    return ESP_OK;
}


/* ======================================================================== */
/* ============================ SAVE HUB TO NVS =========================== */
/* ======================================================================== */

/**
 * @brief Save hubConfig to NVS
 */
esp_err_t hub_storage_save(void)
{
    nvs_handle_t handle;

    esp_err_t err = nvs_open(HUB_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_HUB, "Failed to open NVS (%s)", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(handle, HUB_NVS_KEY, &hubConfig, sizeof(HubConfig));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG_HUB, "Failed to save hub config (%s)", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG_HUB, "Hub config saved");
    }
    return err;
}

/**
 * @brief Save the hub channel, only when it changed (saves flash writes)
 */
void hub_storage_save_channel(uint8_t channel)
{
    if (hubConfig.last_channel == channel) {
        return;
    }
    hubConfig.last_channel = channel;
    hub_storage_save();
}


/* ======================================================================== */
/* ========================= RESTORE DEFAULT HUB ========================== */
/* ======================================================================== */

/**
 * @brief Restore the hub from menuconfig defaults and save it
 *
 * Used when:
 *  - First boot (nothing stored)
 *  - CONFIG_HUB_STORAGE_RESET is enabled
 */
void hub_storage_restore_default(void)
{
    memset(&hubConfig, 0, sizeof(HubConfig));

    /*----------------- Default MAC -----------------*/
    if (strlen(DEFAULT_HUB_MAC) > 0 && !espnow_parse_mac(DEFAULT_HUB_MAC, hubConfig.hub_mac)) {
        ESP_LOGE(TAG_HUB, "Invalid default hub MAC \"%s\"", DEFAULT_HUB_MAC);
        memset(hubConfig.hub_mac, 0, sizeof(hubConfig.hub_mac));
    }

    /*----------------- Default pair key -----------------*/
    if (strlen(DEFAULT_HUB_PAIR_KEY) > 0 && !espnow_parse_key(DEFAULT_HUB_PAIR_KEY, hubConfig.pair_key)) {
        ESP_LOGE(TAG_HUB, "Invalid default pair key (need 32 hex chars)");
        memset(hubConfig.pair_key, 0, sizeof(hubConfig.pair_key));
    }

    hubConfig.last_channel = 0;         // unknown -> full scan
    hubConfig.set_hub      = false;     // default, not set by phone

    hub_storage_save();

    ESP_LOGI(TAG_HUB, "Hub restored to default (mac=" MACSTR ")", MAC2STR(hubConfig.hub_mac));
}