#include "sdkconfig.h" 
#include "esp_log.h"
#include "cJSON.h"
#include "mqtt_client.h"

#include "mqtt_client_fn.h"
#include "mqtt_state_fn.h"

#define MQTT_BROKER_URI CONFIG_MQTT_BROKER_URI
// #define MQTT_BROKER_URI  "mqtts://82.29.161.52:8883"
#define DEVICE_ID CONFIG_WIFI_VALVE_ID
#define BASE_TOPIC "vortex_device/wifi_valve/" DEVICE_ID

#define MAX_MQTT_PAYLOAD 4096

static const char *TAG = "MQTT_CLIENT";
static esp_mqtt_client_handle_t mqtt_client;
static bool mqtt_connected = false;
static TaskHandle_t mqtt_pub_task_handle = NULL;

extern const uint8_t _binary_ca_cert_pem_start[];
extern const uint8_t _binary_ca_cert_pem_end[];

// 82.29.161.52
// mosquitto_pub -h 82.29.161.52 -p 1883   -t test/topic   -m '{"device":"pc","value":1234567890}'


// Publish MQTT Message
static void mqtt_publish_message(const char *sub_topic, cJSON *message)
{
    if (mqtt_client == NULL) {
        ESP_LOGE(TAG, "MQTT client not initialized");
        return;
    }

    if (!mqtt_connected) {
        ESP_LOGW(TAG, "MQTT not connected, skipping publish");
        return;
    }

    if (message == NULL || sub_topic == NULL) {
        ESP_LOGE(TAG, "Invalid publish arguments");
        return;
    }

    char *json_str = cJSON_PrintUnformatted(message);
    if (json_str == NULL) {
        ESP_LOGE(TAG, "Failed to serialize JSON");
        return;
    }

    // topic setup
    char full_topic[128];
    snprintf(full_topic, sizeof(full_topic), "%s/%s", BASE_TOPIC, sub_topic);

    ESP_LOGI(TAG, "Publishing to %s", full_topic);
    ESP_LOGI(TAG, "Payload: %s", json_str);

    int msg_id = esp_mqtt_client_publish( mqtt_client, full_topic, json_str, 0, 0, 0 );

    if (msg_id < 0) {
        ESP_LOGE(TAG, "Publish failed");
    }

    free(json_str);
}

// Publish Valve Data
void mqtt_publish_valve_data( void) {
    cJSON *valve_state_data = create_valve_state_data();
    if (valve_state_data == NULL) {
        ESP_LOGE(TAG, "Failed to create valve state data");
    } else {
        mqtt_publish_message("state_data", valve_state_data);
        ESP_LOGI(TAG, "Valve state data published");
    }
    
    cJSON *valve_status = create_valve_status();
    if (valve_status == NULL) {
        ESP_LOGE(TAG, "Failed to create valve status");
    } else {
        mqtt_publish_message("status", valve_status);
        ESP_LOGI(TAG, "Valve status published");
    }

    cJSON *valve_error = create_valve_error();
    if (valve_error == NULL) {
        ESP_LOGE(TAG, "Failed to create valve error");
    } else {
        mqtt_publish_message("error", valve_error);
        ESP_LOGI(TAG, "Valve error published");
    }

    cJSON_Delete(valve_state_data);
    cJSON_Delete(valve_status);
    cJSON_Delete(valve_error);
}

// Task to publish valve data periodically
void mqtt_publish_valve_data_task(void *pvParameters) {
    while (1) {
        // Publish valve data
        mqtt_publish_valve_data();

        // Wait for a certain period before publishing again (e.g., 5 seconds)
        vTaskDelay(pdMS_TO_TICKS(5000));  // 5 seconds delay
    }
}



// MQTT Event Handler Callback
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    char topic[128];
    static char *rx_data = NULL;
    static int rx_data_len = 0;
    static int rx_bytes_received = 0;


    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ( (esp_mqtt_event_id_t)event_id) {

        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            mqtt_connected = true;

            // subscribe process init
            char topic_cmd_data[128];
            char topic_control_data[128];

            snprintf(topic_cmd_data, sizeof(topic_cmd_data), "%s/cmd_data", BASE_TOPIC);
            snprintf(topic_control_data, sizeof(topic_control_data), "%s/control_data", BASE_TOPIC);

            esp_mqtt_client_subscribe(client, topic_cmd_data, 0);
            esp_mqtt_client_subscribe(client, topic_control_data, 0);

            ESP_LOGI(TAG, "Subscribed to:");
            ESP_LOGI(TAG, "  %s", topic_cmd_data);
            ESP_LOGI(TAG, "  %s", topic_control_data);

            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            mqtt_connected = false;
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");

            if (event->current_data_offset == 0) {
                // Copy topic
                if (event->topic_len < sizeof(topic)) {
                    memcpy(topic, event->topic, event->topic_len);
                    topic[event->topic_len] = '\0';
                } else {
                    ESP_LOGE(TAG, "Topic too long");
                    break;
                }
                // Copy data
                rx_data_len = event->total_data_len;
                rx_bytes_received = 0;
                if (rx_data_len > MAX_MQTT_PAYLOAD) {
                    ESP_LOGE(TAG, "Payload too large");
                    break;
                }
                rx_data = malloc(rx_data_len + 1);
                if (!rx_data) {
                    ESP_LOGE(TAG, "Payload memory allocation failed");
                    break;
                }
            }

            
            if (event->data_len > 0){
                memcpy(rx_data + event->current_data_offset, event->data, event->data_len);
                rx_bytes_received += event->data_len;
            }
            
            if(rx_bytes_received < rx_data_len) {
                break;
            }
            
            rx_data[rx_data_len] = '\0'; 
                
            ESP_LOGI(TAG, "RX topic: %s", topic);
            ESP_LOGI(TAG, "RX data : %s", rx_data);

            if (strstr(topic, "/cmd_data")) {
                mqtt_handle_cmd_data(rx_data);
            } else if (strstr(topic, "/control_data")) {
                mqtt_handle_control_data(rx_data);
            } else {
                mqtt_handle_topic(rx_data);
            }

            free(rx_data);
            rx_data = NULL;
            rx_data_len = 0;
            rx_bytes_received = 0;

            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;

        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }

}


// Start MQTT Client
void start_mqtt_client(void)
{
    if (mqtt_client != NULL) {
        ESP_LOGE(TAG, "MQTT client already initialized. Reconnecting...");
        esp_mqtt_client_reconnect(mqtt_client);
        return;
    }

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .credentials.client_id = DEVICE_ID,
        .broker.verification.certificate = (const char *)_binary_ca_cert_pem_start,
        .network.disable_auto_reconnect = false,
        .session.keepalive = 60,
    };

    ESP_LOGI("MQTT", "Broker URI = %s", MQTT_BROKER_URI);
    ESP_LOGI("TLS", "CA cert first byte: %02X", _binary_ca_cert_pem_start[0]);
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);

    if (mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to initiate MQTT client");
        return;
    }

    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);


    // Create the valve data publishing task
    BaseType_t xReturned = xTaskCreate(
        mqtt_publish_valve_data_task,
        "mqtt_publish_valve_data",
        4096,
        NULL,
        tskIDLE_PRIORITY + 1,
        &mqtt_pub_task_handle
    );

    if (xReturned != pdPASS) {
        ESP_LOGE(TAG, "Failed to create valve data publishing task");
    }

}


// Stop MQTT Client
void stop_mqtt_client(void)
{
    if (mqtt_client != NULL) {
        ESP_LOGI(TAG, "Stopping MQTT Client...");
        esp_mqtt_client_stop(mqtt_client);
    }

    // Delete the valve data publishing task
    if (mqtt_pub_task_handle != NULL) {
        vTaskDelete(mqtt_pub_task_handle);
        mqtt_pub_task_handle = NULL;
    }
}