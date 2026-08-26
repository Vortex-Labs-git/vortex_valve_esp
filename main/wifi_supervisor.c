#include "wifi_supervisor.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_system.h"

static const char *TAG = "WIFI_SUP______TASK";

#define SUPERVISOR_PERIOD_MS   30000
#define STRIKES_RECONNECT      2      // ~1 min  → nudge
#define STRIKES_RESTART_WIFI   6      // ~3 min  → restart the stack
#define STRIKES_REBOOT         40     // ~20 min → last resort

static volatile bool s_link_up = false;

// ---------------------------------------------------------------------------
// Guards. Declared weak so this file compiles on its own; implement the real
// ones in the modules that own that state and they override automatically.
// ---------------------------------------------------------------------------

/// True while the device is in setup / AP provisioning mode.
__attribute__((weak)) bool wifi_sup_provisioning(void) { return false; }

/// True while the phone app holds the direct AP WebSocket. Restarting WiFi
/// would drop it, and the server has only one client slot to give back.
__attribute__((weak)) bool wifi_sup_direct_client(void) { return false; }

/// True when no valve is open and the motor is not moving. Guards the reboot.
__attribute__((weak)) bool wifi_sup_valve_idle(void) { return true; }

bool wifi_link_is_up(void) { return s_link_up; }

static void wifi_supervisor_task(void *arg)
{
    int strikes = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(SUPERVISOR_PERIOD_MS));

        if (wifi_sup_provisioning() || wifi_sup_direct_client()) {
            strikes = 0;
            continue;
        }

        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        wifi_ap_record_t ap;
        esp_netif_ip_info_t ip = {0};

        bool assoc  = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK);
        bool has_ip = netif
                   && esp_netif_get_ip_info(netif, &ip) == ESP_OK
                   && ip.ip.addr != 0;

        if (assoc && has_ip) {
            if (strikes) ESP_LOGI(TAG, "link healthy again after %d strikes", strikes);
            strikes = 0;
            s_link_up = true;
            continue;
        }

        strikes++;
        s_link_up = false;   // LED tells the truth within 30 s, always
        ESP_LOGW(TAG, "link check failed (assoc=%d ip=%d) strike=%d rssi=%d",  assoc, has_ip, strikes, assoc ? ap.rssi : 0);

        if (strikes == STRIKES_RECONNECT) {
            ESP_LOGW(TAG, "esp_wifi_connect()");
            esp_wifi_connect();

        } else if (strikes == STRIKES_RESTART_WIFI) {
            ESP_LOGW(TAG, "restarting wifi stack");
            esp_wifi_stop();
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_wifi_start();   // STA_START handler reconnects

        } else if (strikes >= STRIKES_REBOOT) {
            if (wifi_sup_valve_idle()) {
                ESP_LOGE(TAG, "offline ~20 min — rebooting");
                vTaskDelay(pdMS_TO_TICKS(200));  // let the log flush
                esp_restart();
            } else {
                ESP_LOGW(TAG, "reboot deferred — valve is active");
            }
        }
    }
}

void wifi_supervisor_start(void)
{
    static bool started = false;
    if (started) return;
    started = true;
    xTaskCreate(wifi_supervisor_task, "wifi_sup", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "supervisor started (%d s period)", SUPERVISOR_PERIOD_MS / 1000);
}