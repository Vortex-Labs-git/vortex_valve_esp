#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"

#include "ota_fn/ota_update_fn.h"
#include "global_fn/global_var.h"

static const char *TAG = "OTA_UPDATE";

extern const uint8_t _binary_ota_ca_cert_pem_start[];

typedef struct {
    char url[256];
    char version[32];
} OtaRequest;

static bool ota_running = false;

bool ota_in_progress(void) {
    return ota_running;
}

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

    ota_running = true;
    ESP_LOGI(TAG, "Starting OTA from: %s", req->url);

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

void ota_confirm_running_firmware(void) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;

    if (esp_ota_get_state_partition(running, &state) == ESP_OK) {
        if (state == ESP_OTA_IMG_PENDING_VERIFY) {
            esp_ota_mark_app_valid_cancel_rollback();
            ESP_LOGI(TAG, "New firmware validated, rollback cancelled");
        }
    }
}