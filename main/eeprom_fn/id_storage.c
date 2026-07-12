/**
 * @file id_storage.c
 * @brief NVS-based per-device identity storage (device_id + AP SSID)
 *
 * This module:
 *  - Loads device identity from NVS at boot
 *  - Seeds identity from menuconfig on a fresh device (empty NVS)
 *  - Saves updated identity to NVS
 *  - Restores default identity from menuconfig (factory reset)
 *
 * Identity is stored as a binary blob (DeviceIdentity structure).
 */

#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "id_storage.h"


/* ======================================================================== */
/* ============================ CONFIGURATION ============================= */
/* ======================================================================== */

/* Default device ID from menuconfig (the seed value for a fresh unit) */
#define DEFAULT_DEVICE_ID     CONFIG_WIFI_VALVE_ID

/* Prefix used to build the SoftAP SSID from the device ID */
#define AP_SSID_PREFIX        "Vortex_"

#define ID_NVS_NAMESPACE      "id_cfg"
#define ID_NVS_KEY            "identity"

static const char *TAG_ID = "id_storage";



/* ======================================================================== */
/* ========================== LOAD FROM NVS =============================== */
/* ======================================================================== */

/**
 * @brief Load device identity from NVS into the global deviceIdentity.
 *
 * Fresh-device behavior (your "seed if empty" idea):
 *   - If the namespace/key is missing, restore defaults from menuconfig,
 *     which also populates the RAM global and writes it to NVS, then return.
 *
 * @return
 *   - ESP_OK on success (including the seed-from-default path)
 *   - NVS error code if a read failed after the namespace opened
 */
esp_err_t id_storage_load(void) {
    nvs_handle_t handle;
    esp_err_t err;
    size_t size = sizeof(DeviceIdentity);

    err = nvs_open(ID_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_ID, "No stored identity, seeding from menuconfig");
        id_storage_restore_default();
        return ESP_OK;
    }

    err = nvs_get_blob(handle, ID_NVS_KEY, &deviceIdentity, &size);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGW(TAG_ID, "Identity key missing (%s), seeding defaults",  esp_err_to_name(err));
        id_storage_restore_default();
        return ESP_OK;
    }

    if (strlen(deviceIdentity.device_id) == 0) {
        ESP_LOGW(TAG_ID, "Stored device_id empty, seeding defaults");
        id_storage_restore_default();
        return ESP_OK;
    }

    ESP_LOGI(TAG_ID, "Identity loaded (id=%s, ap_ssid=%s)", deviceIdentity.device_id, deviceIdentity.ap_ssid);
    return ESP_OK;
}



/* ======================================================================== */
/* ============================ SAVE TO NVS =============================== */
/* ======================================================================== */

/**
 * @brief Save the global deviceIdentity to NVS as a blob.
 */
esp_err_t id_storage_save(void) {
    nvs_handle_t handle;
    esp_err_t err;


    err = nvs_open(ID_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_ID, "Failed to open NVS (%s)", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(handle, ID_NVS_KEY, &deviceIdentity, sizeof(DeviceIdentity));
    if (err != ESP_OK) {
        ESP_LOGE(TAG_ID, "Failed to write identity blob (%s)", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_ID, "NVS commit failed (%s)", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG_ID, "Identity saved (id=%s, ap_ssid=%s)", deviceIdentity.device_id, deviceIdentity.ap_ssid);
    }

    nvs_close(handle);
    return err;
}



/* ======================================================================== */
/* ====================== RESTORE DEFAULT IDENTITY ======================= */
/* ======================================================================== */

/**
 * @brief Restore identity to menuconfig defaults and persist.
 *
 *  1. Clear the structure
 *  2. Copy DEFAULT_DEVICE_ID (from menuconfig)
 *  3. Derive ap_ssid = AP_SSID_PREFIX + device_id
 *  4. Mark as default (set_id = false = "not yet provisioned")
 *  5. Save to NVS
 *
 * Used on a fresh device, or when the menuconfig reset toggle is set.
 */
void id_storage_restore_default(void) {
    memset(&deviceIdentity, 0, sizeof(DeviceIdentity));

    strlcpy(deviceIdentity.device_id, DEFAULT_DEVICE_ID, sizeof(deviceIdentity.device_id));

    snprintf(deviceIdentity.ap_ssid, sizeof(deviceIdentity.ap_ssid), "%s%s", AP_SSID_PREFIX, deviceIdentity.device_id);

    id_storage_save();

    ESP_LOGI(TAG_ID, "Identity restored to default (id=%s, ap_ssid=%s)", deviceIdentity.device_id, deviceIdentity.ap_ssid);
}