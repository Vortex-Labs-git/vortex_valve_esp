/**
 * @file main.c
 * @brief Smart WiFi Manager (AP + STA) with automatic mode switching
 *
 * This module initializes WiFi in AP+STA mode and dynamically switches
 * behavior depending on:
 *   - Router availability
 *   - SoftAP client connection status
 *
 * Features:
 *  - Automatically connects to configured router (STA mode)
 *  - Enables SoftAP for local configuration
 *  - Disables AP when router is connected
 *  - Stops router scanning when AP client is active
 *  - Controls LEDs based on WiFi state
 *  - Starts MQTT when router connected
 *  - Starts WebServer when AP client connected
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif_net_stack.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#if IP_NAPT
#include "lwip/lwip_napt.h"
#endif
#include "lwip/err.h"
#include "lwip/sys.h"

#include "global_var.h"
#include "eeprom_fn/wifi_storage.h"
#include "time_func.h"
#include "websocket_fn/websocket_server_fn.h"
#include "mqtt_fn/mqtt_client_fn.h"
#include "valve_fn/valve_process.h"
#include "main_process.h"


/* ========================== CONFIGURATION MACROS ========================== */

/* STA Configuration */
#define ESP_WIFI_STA_SSID                   CONFIG_ESP_WIFI_STA_SSID 
#define ESP_WIFI_STA_PASSWD                 CONFIG_ESP_WIFI_STA_PASSWD  
#define ESP_WIFI_STA_MAXIMUM_RETRY          CONFIG_ESP_WIFI_STA_MAXIMUM_RETRY
#define ESP_WIFI_STA_MODE_RESET             CONFIG_ESP_WIFI_STA_MODE_RESET


/* AP Configuration */
#define ESP_WIFI_AP_SSID                    CONFIG_ESP_WIFI_AP_SSID
#define ESP_WIFI_AP_PASSWD                  CONFIG_ESP_WIFI_AP_PASSWORD
#define ESP_WIFI_CHANNEL                    CONFIG_ESP_WIFI_AP_CHANNEL
#define MAX_STA_CONN                        CONFIG_ESP_MAX_STA_CONN_AP


/* ========================== GLOBAL VARIABLES ========================== */

/**
 * @brief Mutex to protect valve-related operations
 */
SemaphoreHandle_t valveMutex = NULL;
/**
 * @brief Mutex to protect web server operations
 */
SemaphoreHandle_t serverMutex = NULL;

static const char *TAG_MAIN = "MAIN LOOP";
static const char *TAG_AP = "WiFi SoftAP";
static const char *TAG_STA = "WiFi Sta";

static int s_ap_client_count = 0;


/* ======================================================================== */
/* ========================== WIFI EVENT HANDLER ========================== */
/* ======================================================================== */

/**
 * @brief Central WiFi event handler
 *
 * Handles:
 *  - STA start
 *  - STA disconnect
 *  - STA got IP
 *  - AP client connected
 *  - AP client disconnected
 *
 * Smart behavior:
 *  - If router connects → disable AP
 *  - If router disconnects → enable AP
 *  - If AP client connects → stop router scanning
 *  - If AP client disconnects → resume router search
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{

    /* ================= ROUTER (STA) EVENTS ================= */

    /**
     * @event WIFI_EVENT_STA_START
     * Triggered when STA interface starts.
     * Immediately attempt to connect to router.
     */
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG_STA, "STA Started. Connecting to Router...");
        esp_wifi_connect();

        led_blink(&greenLED, 500);
        led_blink(&redLED, 500);
    }

    /**
     * @event WIFI_EVENT_STA_DISCONNECTED
     * Triggered when router connection fails or is lost.
     */
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG_STA, "Router Disconnected/Not Found.");

        stop_mqtt_client();

        led_off(&greenLED);
        led_off(&redLED);

        /**
         * If we were only in STA mode,
         * switch to AP+STA so user can configure device.
         */
        wifi_mode_t current_mode;
        esp_wifi_get_mode(&current_mode);
        
        if (current_mode == WIFI_MODE_STA) {
            ESP_LOGI(TAG_STA, "Switching to AP+STA mode (Turning AP ON)...");
            esp_wifi_set_mode(WIFI_MODE_APSTA);

            led_blink2(&greenLED, 500, 2000);
            led_blink2(&redLED, 500, 2000);
        }

        /**
         * If no AP clients are connected,
         * retry router connection.
         */
        if (s_ap_client_count == 0) {
            ESP_LOGI(TAG_STA, "Retrying Router connection...");
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_wifi_connect();

            led_blink(&greenLED, 500);
            led_blink(&redLED, 500);
        } else {
            ESP_LOGI(TAG_STA, "AP is busy. NOT searching for router.");
        }

    }
    
    /**
     * @event IP_EVENT_STA_GOT_IP
     * Router connection successful.
     */
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG_STA, "Connected to Router! IP: " IPSTR, IP2STR(&event->ip_info.ip));

        /**
         * Router connected → Disable AP
         */
        ESP_LOGI(TAG_STA, "Router connected. Switching to STA Mode (Turning AP OFF)...");
        esp_wifi_set_mode(WIFI_MODE_STA);

        start_mqtt_client();

        led_blink2(&greenLED, 500, 2000);
        led_off(&redLED);
    }

    
     /* ================= SOFTAP EVENTS ================= */

    /**
     * @event WIFI_EVENT_AP_STACONNECTED
     * A client connected to SoftAP.
     */
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG_AP, "Client joined AP: "MACSTR, MAC2STR(event->mac));
        start_webserver();

        led_blink(&greenLED, 500);
        led_off(&redLED);

        s_ap_client_count++;

        /**
         * Stop router scanning while user configures device.
         */
        ESP_LOGI(TAG_AP, "Client connected. Stopping Router search (STA Idle).");
        esp_wifi_disconnect(); 
    }
    
    /**
     * @event WIFI_EVENT_AP_STADISCONNECTED
     * A client disconnected from SoftAP.
     */
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG_AP, "Client left AP: "MACSTR, MAC2STR(event->mac));
        stop_webserver();

        led_off(&greenLED);
        led_off(&redLED);

        if (s_ap_client_count > 0) s_ap_client_count--;

        /**
         * If no clients remain,
         * resume searching for router.
         */
        if (s_ap_client_count == 0) {
            ESP_LOGI(TAG_AP, "No clients on AP. Resuming Router search...");
            esp_wifi_connect();

            led_blink(&greenLED, 500);
            led_blink(&redLED, 500);
        }
    }

}



/* ======================================================================== */
/* ========================== SOFTAP INITIALIZATION ======================= */
/* ======================================================================== */

/**
 * @brief Initialize WiFi in SoftAP mode
 *
 * - Creates AP network interface
 * - Configures SSID, password, channel
 * - Sets authentication mode
 *
 * @return Pointer to AP network interface
 */
esp_netif_t *wifi_init_softap(void)
{
    // Create netifs
    esp_netif_t *esp_netif_ap = esp_netif_create_default_wifi_ap();

    wifi_config_t wifi_ap_config = {
        .ap = {
            .ssid = ESP_WIFI_AP_SSID,
            .ssid_len = strlen(ESP_WIFI_AP_SSID),
            .channel = ESP_WIFI_CHANNEL,
            .password = ESP_WIFI_AP_PASSWD,
            .max_connection = MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = false,
            },
        },
    };

    if (strlen(ESP_WIFI_AP_PASSWD) == 0) {
        wifi_ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));

    ESP_LOGI(TAG_AP, "wifi_init_softap finished. SSID:%s password:%s channel:%d", ESP_WIFI_AP_SSID, ESP_WIFI_AP_PASSWD, ESP_WIFI_CHANNEL);

    return esp_netif_ap;
}



/* ======================================================================== */
/* ========================== STA INITIALIZATION ========================== */
/* ======================================================================== */

/**
 * @brief Initialize WiFi in Station (Router) mode
 *
 * - Loads SSID & password from stored configuration
 * - Sets scan method and authentication threshold
 *
 * @return Pointer to STA network interface
 */
esp_netif_t *wifi_init_sta(void)
{
    // Create netifs
    esp_netif_t *esp_netif_sta = esp_netif_create_default_wifi_sta();

    wifi_config_t wifi_sta_config = { 0 };   // IMPORTANT

    memcpy(wifi_sta_config.sta.ssid,
           wifiStaData.ssid,
           sizeof(wifi_sta_config.sta.ssid));

    memcpy(wifi_sta_config.sta.password,
           wifiStaData.password,
           sizeof(wifi_sta_config.sta.password));

    wifi_sta_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_sta_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config) );

    ESP_LOGI(TAG_STA, "wifi_init_sta finished.");

    return esp_netif_sta;
}



/* ======================================================================== */
/* ========================== SMART MODE INIT ============================= */
/* ======================================================================== */

/**
 * @brief Initialize WiFi in Smart Mode (AP + STA)
 *
 * Flow:
 *  1. Register event handlers
 *  2. Initialize WiFi driver
 *  3. Set mode to APSTA
 *  4. Configure AP
 *  5. Configure STA
 *  6. Start WiFi
 */
void wifi_init_smart_mode(void)
{
    // Register Event handler
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    ESP_EVENT_ANY_ID,
                    &wifi_event_handler,
                    NULL,
                    NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                    IP_EVENT_STA_GOT_IP,
                    &wifi_event_handler,
                    NULL,
                    NULL));

    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // START IN AP+STA MODE
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    // Initialize AP
    ESP_LOGI(TAG_AP, "ESP_WIFI_MODE_AP");
    esp_netif_t *esp_netif_ap = wifi_init_softap();

    // Initialize STA
    ESP_LOGI(TAG_STA, "ESP_WIFI_MODE_STA");
    esp_netif_t *esp_netif_sta = wifi_init_sta();

    // Start WiFi
    ESP_ERROR_CHECK(esp_wifi_start() );


}



/* ======================================================================== */
/* ================================ MAIN ================================== */
/* ======================================================================== */

/**
 * @brief Application entry point
 *
 * Responsibilities:
 *  - Initialize TCP/IP stack
 *  - Initialize NVS
 *  - Load WiFi credentials
 *  - Create mutexes
 *  - Initialize valve system
 *  - Start Smart WiFi
 *  - Obtain system time
 */
void app_main(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // NVS Init
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);


#if CONFIG_ESP_WIFI_STA_MODE_RESET
    wifi_storage_restore_default();
#endif


    wifi_storage_load();

    valveMutex = xSemaphoreCreateMutex();
    if (valveMutex == NULL) {
        ESP_LOGE(TAG_MAIN, "Failed to create valveMutex");
        return;
    }

    serverMutex = xSemaphoreCreateMutex();
    if (serverMutex == NULL) {
        ESP_LOGE(TAG_MAIN, "Failed to create serverMutex");
        return;
    }

    init_valve_system();

    wifi_init_smart_mode();

    obtain_time();

    // xTaskCreate(valve_sync_process, "valve_sync_process", 4096, NULL, 5, NULL);

}