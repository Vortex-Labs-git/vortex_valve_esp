/**
 * @file link_manager.c
 * @brief Connection to the Vortex Hub over ESP-NOW
 *
 * Owns WiFi, the SoftAP, the web server, the green LED and pairing.
 *
 *            boot
 *              |
 *              v
 *   SEARCH (T1, AP off) --- hub found + CALLX_ACK ---> LINKED (AP off)
 *      |    ^                                            |
 *      |    +---------- link lost (failed sends) --------+
 *      | T1 over / no hub saved
 *      v
 *   AP_WINDOW (T2, AP on) --- hub found, no phone ---> LINKED
 *      |        |
 *      |        +-- T2 over --> SEARCH
 *      | phone connects
 *      v
 *   AP_CLIENT (no timer) --- phone leaves ---> SEARCH
 *
 * Pairing (SEARCH / AP_WINDOW):
 *   1. PROBE the hub MAC on a channel; a radio answer = right channel
 *   2. Send CALLX (device id, fw version, HMAC with the pair key)
 *   3. Wait for CALLX_ACK; check its HMAC and our nonce
 */

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_app_desc.h"

#include "link_manager.h"
#include "global_fn/global_var.h"
#include "eeprom_fn/hub_storage.h"
#include "espnow_fn/espnow_link.h"
#include "espnow_fn/espnow_pair.h"
#include "espnow_fn/espnow_util.h"
#include "hub_fn/hub_client.h"
#include "websocket_fn/websocket_server_fn.h"
#include "valve_fn/valve_process.h"
#include "time_fn/time_func.h"
#include "ota_fn/ota_update_fn.h"


/*---------------------------------------------------------------
 * Configuration
 *--------------------------------------------------------------*/

#define LINK_SEARCH_TIME_MS     (CONFIG_LINK_SEARCH_TIME_S * 1000)
#define LINK_AP_TIME_MS         (CONFIG_LINK_AP_TIME_S * 1000)
#define LINK_FAIL_LIMIT         CONFIG_LINK_FAIL_LIMIT

#define AP_PASSWORD             CONFIG_ESP_WIFI_AP_PASSWORD
#define AP_DEFAULT_CHANNEL      CONFIG_ESP_WIFI_AP_CHANNEL
#define AP_MAX_CLIENTS          CONFIG_ESP_MAX_STA_CONN_AP

#define SWEEP_PAUSE_MS          1000    // between two channel sweeps
#define AP_PROBE_PERIOD_MS      3000    // hub probe while the AP is on
#define PROBE_TRIES             2       // probes per channel
#define CALLX_ACK_TIMEOUT_MS    1000

/* Event bits */
#define EV_PHONE_JOINED         BIT0
#define EV_PHONE_LEFT           BIT1
#define EV_CALLX_ACK            BIT2
#define EV_LINK_LOST            BIT3

static const char *TAG = "LINK_MANAGER";


/*---------------------------------------------------------------
 * State
 *--------------------------------------------------------------*/

static volatile link_state_t s_state = LINK_STATE_SEARCH;
static EventGroupHandle_t    s_events;

static volatile int          s_ap_clients = 0;
static bool                  s_web_running = false;

static uint8_t               s_callx_nonce[ESPNOW_NONCE_LEN];
static volatile int          s_fail_count = 0;



/*===============================================================
 *                  HELPERS
 *==============================================================*/

static const char *state_name(link_state_t state)
{
    switch (state) {
        case LINK_STATE_SEARCH:     return "SEARCH";
        case LINK_STATE_AP_WINDOW:  return "AP_WINDOW";
        case LINK_STATE_AP_CLIENT:  return "AP_CLIENT";
        case LINK_STATE_LINKED:     return "LINKED";
        default:                    return "?";
    }
}

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static bool hub_is_set(void)
{
    return !espnow_mac_is_zero(hubConfig.hub_mac);
}

/**
 * @brief Green LED shows the link state (same patterns as the old WiFi states)
 */
static void led_show_state(link_state_t state)
{
    switch (state) {
        case LINK_STATE_SEARCH:     led_blink(&greenLED, 500);          break;
        case LINK_STATE_AP_WINDOW:
        case LINK_STATE_AP_CLIENT:  led_blink2(&greenLED, 500, 2000);   break;
        case LINK_STATE_LINKED:     led_blink2(&greenLED, 500, 5000);   break;
    }
}



/*===============================================================
 *                  WIFI / SOFTAP
 *==============================================================*/

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Phone joined AP: " MACSTR, MAC2STR(event->mac));

        s_ap_clients++;
        xEventGroupSetBits(s_events, EV_PHONE_JOINED);
    }
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "Phone left AP: " MACSTR, MAC2STR(event->mac));

        if (s_ap_clients > 0) s_ap_clients--;
        xEventGroupSetBits(s_events, EV_PHONE_LEFT);
    }
}

/**
 * @brief Start WiFi in STA mode (no router) — ESP-NOW runs on the STA interface
 */
static void wifi_init(void)
{
    esp_netif_create_default_wifi_ap();         // the phone gets an IP from the AP

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_wifi_set_ps(WIFI_PS_NONE);              // never miss an ESP-NOW frame
}

/**
 * @brief Turn the SoftAP on, on the given channel
 */
static void ap_on(uint8_t channel)
{
    wifi_config_t ap_config = {
        .ap = {
            .ssid_len       = strlen(deviceIdentity.ap_ssid),
            .channel        = channel,
            .password       = AP_PASSWORD,
            .max_connection = AP_MAX_CLIENTS,
            .authmode       = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg        = { .required = false },
        },
    };
    strlcpy((char *)ap_config.ap.ssid, deviceIdentity.ap_ssid, sizeof(ap_config.ap.ssid));

    if (strlen(AP_PASSWORD) == 0) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);

    ESP_LOGI(TAG, "AP on: %s, channel %u", deviceIdentity.ap_ssid, channel);
}

/**
 * @brief Turn the SoftAP off (and the web server with it)
 */
static void ap_off(void)
{
    if (s_web_running) {
        stop_webserver();
        s_web_running = false;
    }

    esp_wifi_set_mode(WIFI_MODE_STA);
    s_ap_clients = 0;                           // AP gone: no disconnect events will come
}



/*===============================================================
 *                  PAIRING (PROBE + CALLX)
 *==============================================================*/

/**
 * @brief Does the hub's radio answer on the current channel?
 */
static bool hub_answers(void)
{
    for (int i = 0; i < PROBE_TRIES; i++) {
        if (espnow_send_probe(hubConfig.hub_mac) == ESP_OK) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Send CALLX and wait for a valid CALLX_ACK
 */
static bool callx_handshake(void)
{
    espnow_callx_t callx = { 0 };

    strlcpy(callx.device_id, deviceIdentity.device_id, sizeof(callx.device_id));
    strlcpy(callx.fw_version, esp_app_get_description()->version, sizeof(callx.fw_version));
    callx.schedule_ver = 0;                     // schedule versioning comes later

    espnow_pair_sign_callx(&callx, ESPNOW_DEV_VALVE, hubConfig.pair_key);
    memcpy(s_callx_nonce, callx.nonce, ESPNOW_NONCE_LEN);

    xEventGroupClearBits(s_events, EV_CALLX_ACK);

    if (espnow_send_msg(hubConfig.hub_mac, ESPNOW_MSG_CALLX, &callx, sizeof(callx), 0) != ESP_OK) {
        ESP_LOGW(TAG, "CALLX not delivered");
        return false;
    }

    EventBits_t bits = xEventGroupWaitBits(s_events, EV_CALLX_ACK, pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(CALLX_ACK_TIMEOUT_MS));
    if (!(bits & EV_CALLX_ACK)) {
        ESP_LOGW(TAG, "No valid CALLX_ACK (wrong pair key?)");
        return false;
    }
    return true;
}

/**
 * @brief Hub on the current channel? Then pair with it.
 */
static bool pair_on_current_channel(void)
{
    if (!hub_answers()) {
        return false;
    }

    uint8_t channel = espnow_link_get_channel();
    ESP_LOGI(TAG, "Hub answered on channel %u, sending CALLX", channel);

    if (!callx_handshake()) {
        return false;
    }

    hub_storage_save_channel(channel);
    ota_confirm_running_firmware();             // link to hub works = firmware is healthy
    return true;
}

/**
 * @brief One sweep: last known channel first, then 1..13
 */
static bool search_sweep(void)
{
    uint8_t last = hubConfig.last_channel;

    if (last >= 1 && last <= 13) {
        espnow_link_set_channel(last);
        if (pair_on_current_channel()) return true;
    }

    for (uint8_t channel = 1; channel <= 13; channel++) {
        if (channel == last) continue;

        espnow_link_set_channel(channel);
        if (pair_on_current_channel()) return true;
    }
    return false;
}



/*===============================================================
 *                  STATE HANDLERS
 *==============================================================*/

/**
 * @brief SEARCH: AP off, sweep channels for T1
 */
static link_state_t run_search(void)
{
    if (s_ap_clients > 0) {
        return LINK_STATE_AP_CLIENT;            // phone joined just before we left AP
    }
    if (!hub_is_set()) {
        ESP_LOGW(TAG, "No hub saved, waiting for the phone");
        return LINK_STATE_AP_WINDOW;
    }

    ap_off();

    int64_t end = now_ms() + LINK_SEARCH_TIME_MS;
    while (now_ms() < end) {
        if (search_sweep()) {
            return LINK_STATE_LINKED;
        }
        vTaskDelay(pdMS_TO_TICKS(SWEEP_PAUSE_MS));
    }

    ESP_LOGW(TAG, "Hub not found in %d s", CONFIG_LINK_SEARCH_TIME_S);
    return LINK_STATE_AP_WINDOW;
}

/**
 * @brief AP_WINDOW: AP on for T2, wait for a phone, keep probing the hub
 */
static link_state_t run_ap_window(void)
{
    uint8_t channel = hubConfig.last_channel ? hubConfig.last_channel : AP_DEFAULT_CHANNEL;
    ap_on(channel);

    int64_t end = now_ms() + LINK_AP_TIME_MS;
    while (now_ms() < end) {

        xEventGroupWaitBits(s_events, EV_PHONE_JOINED, pdTRUE, pdFALSE, pdMS_TO_TICKS(AP_PROBE_PERIOD_MS));

        if (s_ap_clients > 0) {
            return LINK_STATE_AP_CLIENT;
        }

        /* Hub back on the AP channel (e.g. after a short hub reboot)? */
        if (hub_is_set() && pair_on_current_channel()) {
            ap_off();
            return LINK_STATE_LINKED;
        }
    }

    return LINK_STATE_SEARCH;
}

/**
 * @brief AP_CLIENT: phone connected, serve it until it leaves
 */
static link_state_t run_ap_client(void)
{
    if (!s_web_running) {
        start_webserver();
        s_web_running = true;
    }

    while (s_ap_clients > 0) {
        xEventGroupWaitBits(s_events, EV_PHONE_LEFT, pdTRUE, pdFALSE, portMAX_DELAY);
    }

    stop_webserver();
    s_web_running = false;

    return LINK_STATE_SEARCH;
}

/**
 * @brief LINKED: AP off, hub_client publishes, wait until the link is lost
 */
static link_state_t run_linked(void)
{
    ap_off();

    s_fail_count = 0;
    xEventGroupClearBits(s_events, EV_LINK_LOST);

    xEventGroupWaitBits(s_events, EV_LINK_LOST, pdTRUE, pdFALSE, portMAX_DELAY);

    ESP_LOGW(TAG, "Link to hub lost");
    return LINK_STATE_SEARCH;
}



/*===============================================================
 *                  MESSAGES FROM ESP-NOW (espnow_rx task)
 *==============================================================*/

static void on_callx_ack(const uint8_t *data, size_t len)
{
    espnow_callx_ack_t ack;

    if (len != sizeof(ack)) {
        return;
    }
    memcpy(&ack, data, sizeof(ack));

    if (memcmp(ack.nonce_echo, s_callx_nonce, ESPNOW_NONCE_LEN) != 0) {
        ESP_LOGW(TAG, "CALLX_ACK for an old CALLX, ignored");
        return;
    }
    if (!espnow_pair_check_callx_ack(&ack, hubConfig.pair_key)) {
        ESP_LOGW(TAG, "CALLX_ACK signature wrong (pair key mismatch)");
        return;
    }
    if (ack.status != ESPNOW_CALLX_OK) {
        ESP_LOGW(TAG, "Hub refused the valve (status %u)", ack.status);
        return;
    }

    ESP_LOGI(TAG, "Paired with hub \"%.*s\"", ESPNOW_ID_LEN, ack.hub_id);
    xEventGroupSetBits(s_events, EV_CALLX_ACK);
}

static void on_time_sync(const uint8_t *data, size_t len)
{
    espnow_time_sync_t sync;

    if (len != sizeof(sync)) {
        return;
    }
    memcpy(&sync, data, sizeof(sync));
    time_apply_sync(sync.epoch_utc);
}

/**
 * @brief Hub does not know us (e.g. it rebooted): pair again
 *
 * Only acted on while LINKED; the link task goes to SEARCH,
 * finds the hub on the last channel and sends CALLX.
 */
static void on_rejoin(void)
{
    if (s_state != LINK_STATE_LINKED) {
        return;
    }

    ESP_LOGW(TAG, "Hub asked us to pair again");
    xEventGroupSetBits(s_events, EV_LINK_LOST);
}

/**
 * @brief Every complete message from ESP-NOW lands here
 */
static void on_espnow_message(const uint8_t mac[6], const espnow_hdr_t *hdr, const uint8_t *data, size_t len, int rssi)
{
    /* Only our own hub may talk to us */
    if (memcmp(mac, hubConfig.hub_mac, 6) != 0) {
        return;
    }

    ESP_LOGI(TAG, "RX %s (%u bytes, rssi %d)", espnow_msg_name(hdr->msg_type), (unsigned)len, rssi);

        switch (hdr->msg_type) {
        case ESPNOW_MSG_CALLX_ACK:  on_callx_ack(data, len);                             break;
        case ESPNOW_MSG_TIME_SYNC:  on_time_sync(data, len);                             break;
        case ESPNOW_MSG_REJOIN:     on_rejoin();                                         break;
        default:                    hub_client_handle_message(hdr, (const char *)data);  break;
    }
}



/*===============================================================
 *                  LINK TASK
 *==============================================================*/

static void link_manager_task(void *pvParameters)
{
    link_state_t next = LINK_STATE_SEARCH;

    while (1) {
        if (next != s_state) {
            ESP_LOGI(TAG, "State %s -> %s", state_name(s_state), state_name(next));
        }
        s_state = next;
        led_show_state(s_state);

        switch (s_state) {
            case LINK_STATE_SEARCH:     next = run_search();     break;
            case LINK_STATE_AP_WINDOW:  next = run_ap_window();  break;
            case LINK_STATE_AP_CLIENT:  next = run_ap_client();  break;
            case LINK_STATE_LINKED:     next = run_linked();     break;
        }
    }
}



/*===============================================================
 *                  PUBLIC API
 *==============================================================*/

/**
 * @brief Start WiFi, ESP-NOW, the hub client and the link task
 */
void link_manager_start(void)
{
    s_events = xEventGroupCreate();

    wifi_init();

    espnow_link_config_t espnow_config = {
        .dev_type   = ESPNOW_DEV_VALVE,
        .on_message = on_espnow_message,
    };
    ESP_ERROR_CHECK(espnow_link_init(&espnow_config));

    hub_client_start();

    xTaskCreate(link_manager_task, "link_manager", 4096, NULL, 5, NULL);
}

link_state_t link_manager_get_state(void)
{
    return s_state;
}

bool link_manager_is_linked(void)
{
    return s_state == LINK_STATE_LINKED;
}

/**
 * @brief hub_client reports every send; too many failures in a row = link lost
 */
void link_manager_report_send(bool ok)
{
    if (ok) {
        s_fail_count = 0;
        return;
    }

    s_fail_count++;
    ESP_LOGW(TAG, "Send to hub failed (%d/%d)", s_fail_count, LINK_FAIL_LIMIT);

    if (s_fail_count >= LINK_FAIL_LIMIT) {
        xEventGroupSetBits(s_events, EV_LINK_LOST);
    }
}