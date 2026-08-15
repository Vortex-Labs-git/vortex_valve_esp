#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"

#include "ota_fn/ota_update_fn.h"
#include "global_fn/global_var.h"
#include "valve_fn/led_indicators.h"
#include "valve_fn/valve_process.h"

static const char *TAG = "OTA_UPDATE";

/**
 * @brief Root CA for the firmware download server
 *
 */
extern const uint8_t _binary_ota_ca_cert_pem_start[];

typedef struct {
    char url[256];
    char version[32];
} OtaRequest;

static bool ota_running = false;


/*===============================================================
 *                  OTA STATE QUERY
 *==============================================================*/
bool ota_in_progress(void) {
    return ota_running;
}


/*===============================================================
 *                  VERSION COMPARISON
 *==============================================================*/
static int fw_version_compare(const char *a, const char *b) {
    int a_parts[3] = {0}, b_parts[3] = {0};

    sscanf(a, "%d.%d.%d", &a_parts[0], &a_parts[1], &a_parts[2]);
    sscanf(b, "%d.%d.%d", &b_parts[0], &b_parts[1], &b_parts[2]);

    for (int i = 0; i < 3; i++) {
        if (a_parts[i] != b_parts[i]) {
            return a_parts[i] - b_parts[i];
        }
    }
    return 0;
}


/*===============================================================
 *                  OTA TASK (FreeRTOS)
 *==============================================================*/
/**
 * @brief Background task that performs the full OTA update
 *
 * Runs in parallel with normal operation — the valve control,
 * MQTT publishing, and scheduling all keep working during the
 * download. The new image is written to the INACTIVE partition,
 * so the running firmware is never touched.
 *
 * Safety gates (checked before download starts):
 *   1. Requested version must be strictly newer than running
 *   2. Valve must be in fully closed (safe) state
 *
 * Failure behavior:
 *   - Any failure before esp_https_ota_finish() leaves the
 *     device untouched — it keeps running current firmware.
 *   - After reboot, if new firmware crashes before validation,
 *     the bootloader rolls back to this firmware automatically.
 *
 * @param pvParameter  OtaRequest* (heap), freed before task exit
 */
static void ota_task(void *pvParameter) {
    OtaRequest *req = (OtaRequest *)pvParameter;

    /* 1. Version check against running firmware */
    const esp_app_desc_t *running_app = esp_app_get_description();
    ESP_LOGI(TAG, "Running: %s | Requested: %s", running_app->version, req->version);

    if (fw_version_compare(req->version, running_app->version) <= 0) {
        ESP_LOGW(TAG, "Requested v%s is not newer than running v%s, skipping OTA", req->version, running_app->version);
        goto cleanup;
    }

    if (strcmp(req->version, running_app->version) == 0) {
        ESP_LOGW(TAG, "Same version, skipping OTA");
        goto cleanup;
    }

    /* 2. Don't update while valve is moving */
    bool valve_closed;
    xSemaphoreTake(valveMutex, portMAX_DELAY);
    valve_closed = valveData.is_close;
    xSemaphoreGive(valveMutex);

    if (!valve_closed) {
        ESP_LOGW(TAG, "Valve not close, deferring OTA");
        goto cleanup;
    }

    /*----------------- 3. Start HTTPS Download -----------------*/
    ota_running = true;
    ESP_LOGI(TAG, "Starting OTA from: %s", req->url);
    led_blink2(&redLED, 1000, 1000);

    esp_http_client_config_t http_config = {
        .url = req->url,
        .cert_pem = (const char *)_binary_ota_ca_cert_pem_start,
        .timeout_ms = 15000,
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    esp_https_ota_handle_t handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
        ota_running = false;
        goto cleanup;
    }

    /*----------------- 4. Download Loop -----------------*/
    int total_size = esp_https_ota_get_image_size(handle);
    int last_percent = -1;

    while (1) {
        err = esp_https_ota_perform(handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;

        int bytes_read = esp_https_ota_get_image_len_read(handle);
        int percent = (total_size > 0) ? (bytes_read * 100 / total_size) : 0;

        /* Publish every 10%, not every chunk */
        if (percent / 10 != last_percent / 10) {
            last_percent = percent;
            ESP_LOGI(TAG, "Progress: %d%%", percent);
        }
    }

    /*----------------- 5. Finish or Abort -----------------*/
    if (err == ESP_OK && esp_https_ota_is_complete_data_received(handle)) {
        err = esp_https_ota_finish(handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "OTA success, rebooting...");
            vTaskDelay(pdMS_TO_TICKS(1500));
            esp_restart();
        } else {
            ESP_LOGE(TAG, "OTA finish failed: %s", esp_err_to_name(err));
        }
    } else {
        esp_https_ota_abort(handle);
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err));
    }

    ota_running = false;

cleanup:
    free(req);
    vTaskDelete(NULL);
}



/*===============================================================
 *                  START OTA UPDATE
 *==============================================================*/
/**
 * @brief Entry point: validate request and launch the OTA task
 *
 * Called from the MQTT handler (mqtt_handle_cmd_data) when an
 * "ota_update" object is received. Returns immediately — the
 * actual update runs in a background task.
 *
 * @param url      HTTPS URL of the firmware binary
 * @param version  Target firmware version (e.g. "3.2.2")
 */
void ota_start(const char *url, const char *version) {
    if (ota_running) {
        ESP_LOGW(TAG, "OTA already in progress");
        return;
    }
    if (url == NULL || strlen(url) == 0) {
        ESP_LOGE(TAG, "Empty OTA URL");
        return;
    }

    OtaRequest *req = calloc(1, sizeof(OtaRequest));
    if (!req) return;
    strlcpy(req->url, url, sizeof(req->url));
    strlcpy(req->version, version ? version : "", sizeof(req->version));

    xTaskCreate(ota_task, "ota_task", 8192, req, 5, NULL);
}



/*===============================================================
 *                  CONFIRM NEW FIRMWARE (ROLLBACK)
 *==============================================================*/
/**
 * @brief Validate the running firmware and cancel rollback
 *
 * With CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE, freshly updated
 * firmware boots in PENDING_VERIFY state. If the device reboots
 * (crash/watchdog) before this function runs, the bootloader
 * automatically rolls back to the previous firmware.
 *
 * Called from MQTT_EVENT_CONNECTED — reaching that point proves
 * WiFi, TLS, and broker connectivity all work on the new build,
 * which is our definition of "healthy".
 *
 * Safe to call on every MQTT connect: does nothing unless the
 * partition state is PENDING_VERIFY.
 */
void ota_confirm_running_firmware(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;

    ESP_LOGI(TAG, "confirm() called: partition <%s> @0x%08" PRIx32,
             running->label, running->address);

    esp_err_t err = esp_ota_get_state_partition(running, &state);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "get_state_partition failed: %s", esp_err_to_name(err));
        return;
    }

    switch (state) {
        case ESP_OTA_IMG_PENDING_VERIFY: {
            esp_err_t merr = esp_ota_mark_app_valid_cancel_rollback();
            if (merr == ESP_OK) {
                ESP_LOGI(TAG, "New firmware validated, rollback cancelled");
            } else {
                ESP_LOGE(TAG, "mark_app_valid failed: %s", esp_err_to_name(merr));
            }
            break;
        }
        case ESP_OTA_IMG_VALID:
            ESP_LOGI(TAG, "Already VALID — nothing to do");
            break;
        case ESP_OTA_IMG_NEW:
            ESP_LOGW(TAG, "State NEW — unexpected at runtime");
            break;
        case ESP_OTA_IMG_INVALID:
            ESP_LOGW(TAG, "State INVALID");
            break;
        case ESP_OTA_IMG_ABORTED:
            ESP_LOGW(TAG, "State ABORTED");
            break;
        case ESP_OTA_IMG_UNDEFINED:
            ESP_LOGW(TAG, "State UNDEFINED — rollback support likely disabled "
                          "in menuconfig, or app was serial-flashed");
            break;
        default:
            ESP_LOGW(TAG, "Unknown state %d", (int)state);
            break;
    }
}