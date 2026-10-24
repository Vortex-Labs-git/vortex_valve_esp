/**
 * @file hub_client.c
 * @brief Valve data to / from the Vortex Hub (replaces mqtt_client_fn.c)
 *
 * Same JSON as the old MQTT client, only the transport changed:
 *
 *   old MQTT topic                      ESP-NOW message type
 *   ------------------------------      ------------------------------
 *   .../<id>/state_data   (publish)     ESPNOW_MSG_STATE_DATA
 *   .../<id>/status       (publish)     ESPNOW_MSG_STATUS
 *   .../<id>/error        (publish)     ESPNOW_MSG_ERROR
 *   .../<id>/cmd_data     (subscribe)   ESPNOW_MSG_CMD_DATA
 *   .../<id>/control_data (subscribe)   ESPNOW_MSG_CONTROL_DATA
 *
 * The hub rebuilds the topic, so the server sees no change.
 */

#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "cJSON.h"

#include "hub_client.h"
#include "global_fn/global_var.h"
#include "link_fn/link_manager.h"
#include "mqtt_fn/mqtt_state_fn.h"
#include "espnow_fn/espnow_send.h"
#include "espnow_fn/espnow_util.h"


/*---------------------------------------------------------------
 * Configuration
 *--------------------------------------------------------------*/

#define HUB_PUBLISH_PERIOD_MS   CONFIG_HUB_PUBLISH_PERIOD_MS

static const char *TAG = "HUB_CLIENT";



/*===============================================================
 *                  PUBLISH ONE MESSAGE
 *==============================================================*/

/**
 * @brief Send one JSON object to the hub, then free it
 *
 * @param msg_type  ESPNOW_MSG_STATE_DATA / STATUS / ERROR
 * @param message   cJSON object (deleted here)
 */
static void hub_publish_message(uint8_t msg_type, cJSON *message)
{
    if (message == NULL) {
        ESP_LOGE(TAG, "No JSON for %s", espnow_msg_name(msg_type));
        return;
    }

    char *json_str = cJSON_PrintUnformatted(message);
    cJSON_Delete(message);

    if (json_str == NULL) {
        ESP_LOGE(TAG, "Failed to serialize JSON");
        return;
    }

    esp_err_t err = espnow_send_msg(hubConfig.hub_mac, msg_type, json_str, strlen(json_str), 0);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "TX %s: %s", espnow_msg_name(msg_type), json_str);
    } else {
        ESP_LOGW(TAG, "TX %s failed (%s)", espnow_msg_name(msg_type), esp_err_to_name(err));
    }

    link_manager_report_send(err == ESP_OK);
    free(json_str);
}



/*===============================================================
 *                  PUBLISH VALVE DATA (MAIN DATA SET)
 *==============================================================*/

/**
 * @brief Publish state_data, status and error to the hub
 */
void hub_publish_valve_data(void)
{
    hub_publish_message(ESPNOW_MSG_STATE_DATA, create_valve_state_data());
    hub_publish_message(ESPNOW_MSG_STATUS,     create_valve_status());
    hub_publish_message(ESPNOW_MSG_ERROR,      create_valve_error());
}

/**
 * @brief FreeRTOS task: publish every HUB_PUBLISH_PERIOD_MS while linked
 */
static void hub_publish_task(void *pvParameters)
{
    while (1) {
        if (link_manager_is_linked()) {
            hub_publish_valve_data();
        }
        vTaskDelay(pdMS_TO_TICKS(HUB_PUBLISH_PERIOD_MS));
    }
}



/*===============================================================
 *                  MESSAGES FROM THE HUB
 *==============================================================*/

/**
 * @brief Route a JSON message from the hub to the existing handlers
 *
 * Called by link_manager from the espnow_rx task.
 */
void hub_client_handle_message(const espnow_hdr_t *hdr, const char *json)
{
    switch (hdr->msg_type) {

        case ESPNOW_MSG_CMD_DATA:
            ESP_LOGI(TAG, "RX cmd_data: %s", json);
            mqtt_handle_cmd_data(json);
            break;

        case ESPNOW_MSG_CONTROL_DATA:
            ESP_LOGI(TAG, "RX control_data: %s", json);
            mqtt_handle_control_data(json);
            break;

        default:
            ESP_LOGW(TAG, "Unhandled message %s", espnow_msg_name(hdr->msg_type));
            break;
    }
}



/*===============================================================
 *                  START
 *==============================================================*/

/**
 * @brief Create the publish task (it sends only while linked)
 */
void hub_client_start(void)
{
    BaseType_t ok = xTaskCreate(hub_publish_task, "hub_publish", 4096, NULL, tskIDLE_PRIORITY + 1, NULL);

    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create hub publish task");
    }
}