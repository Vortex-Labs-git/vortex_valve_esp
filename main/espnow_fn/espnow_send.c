/**
 * @file espnow_send.c
 * @brief Send one message over ESP-NOW
 *
 * Flow of espnow_send_msg():
 *   1. Build the header (msg_id, frame count, CRC)
 *   2. Send every frame; each frame is retried until the
 *      peer's radio confirms it (radio ACK)
 *   3. With ESPNOW_FLAG_ACK_REQ: wait for the peer's ACK message,
 *      send the whole message again if it does not come
 *
 * Only one message is on air at a time (s_tx_mutex).
 */

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_random.h"

#include "espnow_send.h"
#include "espnow_link.h"
#include "espnow_util.h"


static const char *TAG = "ESPNOW_SEND";

static bool              s_ready = false;
static uint8_t           s_dev_type;
static uint16_t          s_msg_id;

static SemaphoreHandle_t s_tx_mutex;        // one message on air at a time
static SemaphoreHandle_t s_radio_sem;       // given by radio_done_cb
static volatile esp_now_send_status_t s_radio_status;

static SemaphoreHandle_t s_ack_sem;         // given when the expected ACK arrives
static portMUX_TYPE      s_ack_lock = portMUX_INITIALIZER_UNLOCKED;
static bool              s_ack_waiting;
static uint8_t           s_ack_mac[6];
static uint16_t          s_ack_msg_id;

static espnow_send_stats_t s_stats;



/*===============================================================
 *                  RADIO RESULT (WiFi task)
 *==============================================================*/

/**
 * @brief Radio result of the last frame (ESP-IDF 5.4 signature)
 *
 * On ESP-IDF >= 5.5 the first argument is `const esp_now_send_info_t *`.
 */
static void radio_done_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    (void)mac_addr;
    s_radio_status = status;
    xSemaphoreGive(s_radio_sem);
}



/*===============================================================
 *                  END-TO-END ACK
 *==============================================================*/

static void ack_wait_start(const uint8_t mac[6], uint16_t msg_id)
{
    xSemaphoreTake(s_ack_sem, 0);               // clear an old give

    portENTER_CRITICAL(&s_ack_lock);
    memcpy(s_ack_mac, mac, 6);
    s_ack_msg_id  = msg_id;
    s_ack_waiting = true;
    portEXIT_CRITICAL(&s_ack_lock);
}

static void ack_wait_stop(void)
{
    portENTER_CRITICAL(&s_ack_lock);
    s_ack_waiting = false;
    portEXIT_CRITICAL(&s_ack_lock);
}

/**
 * @brief An ACK message arrived (called by espnow_recv_fn, WiFi task)
 */
void espnow_send_ack_received(const uint8_t mac[6], uint16_t msg_id)
{
    bool match = false;

    portENTER_CRITICAL(&s_ack_lock);
    if (s_ack_waiting && s_ack_msg_id == msg_id && memcmp(s_ack_mac, mac, 6) == 0) {
        s_ack_waiting = false;
        match = true;
    }
    portEXIT_CRITICAL(&s_ack_lock);

    if (match) {
        xSemaphoreGive(s_ack_sem);
    }
}

/**
 * @brief Answer a message that asked for an ACK (called by espnow_recv_fn)
 */
void espnow_send_ack_reply(const uint8_t mac[6], const espnow_hdr_t *hdr)
{
    espnow_ack_t ack = {
        .msg_id   = hdr->msg_id,
        .msg_type = hdr->msg_type,
    };

    if (espnow_send_msg(mac, ESPNOW_MSG_ACK, &ack, sizeof(ack), 0) != ESP_OK) {
        ESP_LOGW(TAG, "ACK for msg %u to " MACSTR " not delivered", hdr->msg_id, MAC2STR(mac));
    }
}



/*===============================================================
 *                  SEND FRAMES
 *==============================================================*/

/**
 * @brief Send one frame, up to `tries` times, until the peer's radio confirms it
 */
static esp_err_t send_frame(const uint8_t mac[6], const uint8_t *frame, size_t len, int tries)
{
    for (int i = 0; i < tries; i++) {

        if (i > 0) {
            s_stats.frame_retries++;
            vTaskDelay(1);
        }

        xSemaphoreTake(s_radio_sem, 0);          // clear a late result

        if (esp_now_send(mac, frame, len) != ESP_OK) {
            continue;
        }
        if (xSemaphoreTake(s_radio_sem, pdMS_TO_TICKS(ESPNOW_SEND_CB_TIMEOUT_MS)) != pdTRUE) {
            continue;
        }
        if (s_radio_status == ESP_NOW_SEND_SUCCESS) {
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}

/**
 * @brief Send every frame of one message, in order
 */
static esp_err_t send_all_frames(const uint8_t mac[6], espnow_hdr_t *hdr, const uint8_t *data)
{
    uint8_t frame[ESPNOW_FRAME_MAX_LEN];

    for (uint8_t idx = 0; idx < hdr->frag_cnt; idx++) {
        size_t chunk_len = espnow_chunk_len(hdr->total_len, idx);

        hdr->frag_idx = idx;
        memcpy(frame, hdr, ESPNOW_HDR_LEN);
        if (chunk_len > 0) {
            memcpy(frame + ESPNOW_HDR_LEN, data + (size_t)idx * ESPNOW_CHUNK_LEN, chunk_len);
        }

        if (send_frame(mac, frame, ESPNOW_HDR_LEN + chunk_len, ESPNOW_SEND_FRAME_RETRIES) != ESP_OK) {
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}



/*===============================================================
 *                  PUBLIC: SEND MESSAGE / PROBE
 *==============================================================*/

/**
 * @brief Send one message (up to ESPNOW_MSG_MAX_LEN bytes)
 *
 * Blocks until every frame is confirmed by the radio and, with
 * ESPNOW_FLAG_ACK_REQ, until the peer's ACK arrives.
 *
 * @return ESP_OK, ESP_FAIL (frame not confirmed), ESP_ERR_TIMEOUT (no ACK)
 */
esp_err_t espnow_send_msg(const uint8_t mac[6], uint8_t msg_type, const void *data, size_t len, uint8_t flags)
{
    /*----------------- Check arguments -----------------*/
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (mac == NULL || (len > 0 && data == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len > ESPNOW_MSG_MAX_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (espnow_link_add_peer(mac) != ESP_OK) {
        return ESP_FAIL;
    }

    bool want_ack = (flags & ESPNOW_FLAG_ACK_REQ) && memcmp(mac, ESPNOW_BROADCAST_MAC, 6) != 0;
    if (!want_ack) {
        flags &= (uint8_t)~ESPNOW_FLAG_ACK_REQ;
    }

    xSemaphoreTake(s_tx_mutex, portMAX_DELAY);

    /*----------------- Build header -----------------*/
    espnow_hdr_t hdr = {
        .ver       = ESPNOW_PROTO_VER,
        .msg_type  = msg_type,
        .dev_type  = s_dev_type,
        .flags     = flags,
        .msg_id    = ++s_msg_id,
        .frag_idx  = 0,
        .frag_cnt  = espnow_frag_count((uint16_t)len),
        .total_len = (uint16_t)len,
        .crc16     = espnow_crc16(data, len),
    };

    /*----------------- Send (and wait for ACK) -----------------*/
    int tries = want_ack ? ESPNOW_SEND_ACK_RETRIES : 1;
    esp_err_t err = ESP_FAIL;

    for (int t = 0; t < tries; t++) {

        if (want_ack) {
            ack_wait_start(mac, hdr.msg_id);
        }

        err = send_all_frames(mac, &hdr, (const uint8_t *)data);
        if (err != ESP_OK) {
            ack_wait_stop();
            continue;
        }
        if (!want_ack) {
            break;
        }

        if (xSemaphoreTake(s_ack_sem, pdMS_TO_TICKS(ESPNOW_SEND_ACK_TIMEOUT_MS)) == pdTRUE) {
            err = ESP_OK;
            break;
        }
        ack_wait_stop();
        err = ESP_ERR_TIMEOUT;
    }

    xSemaphoreGive(s_tx_mutex);

    /*----------------- Count result -----------------*/
    if (err == ESP_OK) {
        s_stats.msgs++;
    } else {
        s_stats.fail++;
        ESP_LOGD(TAG, "%s to " MACSTR " failed: %s", espnow_msg_name(msg_type), MAC2STR(mac), esp_err_to_name(err));
    }
    return err;
}

/**
 * @brief One header-only PROBE frame, single try
 *
 * @return ESP_OK only if the peer's radio answered on the current channel
 */
esp_err_t espnow_send_probe(const uint8_t mac[6])
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (espnow_link_add_peer(mac) != ESP_OK) {
        return ESP_FAIL;
    }

    xSemaphoreTake(s_tx_mutex, portMAX_DELAY);

    espnow_hdr_t hdr = {
        .ver      = ESPNOW_PROTO_VER,
        .msg_type = ESPNOW_MSG_PROBE,
        .dev_type = s_dev_type,
        .msg_id   = ++s_msg_id,
        .frag_cnt = 1,
    };

    esp_err_t err = send_frame(mac, (const uint8_t *)&hdr, ESPNOW_HDR_LEN, 1);

    xSemaphoreGive(s_tx_mutex);
    return err;
}

void espnow_send_get_stats(espnow_send_stats_t *out)
{
    *out = s_stats;
}



/*===============================================================
 *                  INIT (called by espnow_link_init)
 *==============================================================*/

esp_err_t espnow_send_init(uint8_t own_dev_type)
{
    s_tx_mutex  = xSemaphoreCreateMutex();
    s_radio_sem = xSemaphoreCreateBinary();
    s_ack_sem   = xSemaphoreCreateBinary();

    if (s_tx_mutex == NULL || s_radio_sem == NULL || s_ack_sem == NULL) {
        ESP_LOGE(TAG, "Failed to create semaphores");
        return ESP_ERR_NO_MEM;
    }

    s_dev_type = own_dev_type;
    s_msg_id   = (uint16_t)esp_random();     // a rebooted sender is not taken for a duplicate

    esp_now_register_send_cb(radio_done_cb);

    s_ready = true;
    return ESP_OK;
}