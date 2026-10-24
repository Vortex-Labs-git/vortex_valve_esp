/**
 * @file espnow_link.c
 * @brief ESP-NOW start-up, peers and radio channel
 *
 * Start order (espnow_link_init):
 *   1. esp_now_init()        WiFi must already be started (STA or APSTA)
 *   2. espnow_send_init()    send side + radio result callback
 *   3. espnow_recv_init()    receive side + rx task
 *   4. Broadcast peer
 *
 * Every peer uses the STA interface and "current channel", so
 * espnow_link_set_channel() moves all peers together.
 */

#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_wifi.h"

#include "espnow_link.h"


#define RX_TASK_STACK_DEFAULT   6144        // JSON handlers run on this stack
#define RX_TASK_PRIO_DEFAULT    5

static const char *TAG = "ESPNOW_LINK";

const uint8_t ESPNOW_BROADCAST_MAC[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static bool s_started = false;



/*===============================================================
 *                  INIT
 *==============================================================*/

/**
 * @brief Start ESP-NOW, the send side and the receive side
 */
esp_err_t espnow_link_init(const espnow_link_config_t *config)
{
    if (s_started) {
        return ESP_OK;
    }
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t    stack = config->rx_task_stack ? config->rx_task_stack : RX_TASK_STACK_DEFAULT;
    UBaseType_t prio  = config->rx_task_prio  ? config->rx_task_prio  : RX_TASK_PRIO_DEFAULT;

    /*----------------- 1. ESP-NOW driver -----------------*/
    esp_err_t err = esp_now_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_init failed: %s", esp_err_to_name(err));
        return err;
    }

    /*----------------- 2. Send side -----------------*/
    err = espnow_send_init(config->dev_type);
    if (err != ESP_OK) {
        return err;
    }

    /*----------------- 3. Receive side -----------------*/
    err = espnow_recv_init(config->on_message, stack, prio);
    if (err != ESP_OK) {
        return err;
    }

    /*----------------- 4. Broadcast peer -----------------*/
    err = espnow_link_add_peer(ESPNOW_BROADCAST_MAC);
    if (err != ESP_OK) {
        return err;
    }

    s_started = true;

    uint8_t own_mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, own_mac);
    ESP_LOGI(TAG, "ESP-NOW ready. MAC " MACSTR ", channel %u", MAC2STR(own_mac), espnow_link_get_channel());

    return ESP_OK;
}



/*===============================================================
 *                  PEERS
 *==============================================================*/

/**
 * @brief Add a peer (does nothing if it already exists)
 */
esp_err_t espnow_link_add_peer(const uint8_t mac[6])
{
    if (mac == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (esp_now_is_peer_exist(mac)) {
        return ESP_OK;
    }

    esp_now_peer_info_t peer = { 0 };
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = 0;                   // follow the current radio channel
    peer.ifidx   = WIFI_IF_STA;
    peer.encrypt = false;

    esp_err_t err = esp_now_add_peer(&peer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Add peer " MACSTR " failed: %s", MAC2STR(mac), esp_err_to_name(err));
    }
    return err;
}

/**
 * @brief Remove a peer (does nothing if it does not exist)
 */
esp_err_t espnow_link_del_peer(const uint8_t mac[6])
{
    if (mac == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!esp_now_is_peer_exist(mac)) {
        return ESP_OK;
    }
    return esp_now_del_peer(mac);
}



/*===============================================================
 *                  CHANNEL
 *==============================================================*/

/**
 * @brief Set the radio channel (1..13)
 *
 * Only while STA is NOT connected to a router.
 */
esp_err_t espnow_link_set_channel(uint8_t channel)
{
    if (channel < 1 || channel > 13) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

uint8_t espnow_link_get_channel(void)
{
    uint8_t            primary = 0;
    wifi_second_chan_t second;

    if (esp_wifi_get_channel(&primary, &second) != ESP_OK) {
        return 0;
    }
    return primary;
}



/*===============================================================
 *                  STATS
 *==============================================================*/

/**
 * @brief Send + receive counters, for bench testing
 */
void espnow_link_get_stats(espnow_stats_t *out)
{
    espnow_send_get_stats(&out->tx);
    espnow_recv_get_stats(&out->rx);
}