/**
 * @file espnow_recv.c
 * @brief Receive ESP-NOW frames and rebuild full messages
 *
 * Two contexts:
 *   WiFi task      : frame_in_cb()  -> ACK messages handled at once,
 *                                      other frames copied to a queue
 *   espnow_rx task : handle_frame() -> rebuild, CRC, ACK reply,
 *                                      drop duplicates, call on_message
 *
 * Nothing heavy runs in the WiFi task.
 */

#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_timer.h"

#include "espnow_recv.h"
#include "espnow_send.h"
#include "espnow_util.h"


static const char *TAG = "ESPNOW_RECV";

/* One raw frame, copied out of the WiFi task */
typedef struct {
    uint8_t mac[6];
    int8_t  rssi;
    uint8_t len;
    uint8_t data[ESPNOW_FRAME_MAX_LEN];
} rx_frame_t;

/* Rebuild state for one sender */
typedef struct {
    bool         used;
    uint8_t      mac[6];
    int64_t      last_use_us;

    bool         busy;              // message being rebuilt
    espnow_hdr_t hdr;
    uint32_t     got_mask;          // bit n = frame n received
    int64_t      start_us;
    uint8_t     *buf;               // total_len + 1

    bool         have_last;         // last finished message (duplicate check)
    uint16_t     last_msg_id;
    uint8_t      last_msg_type;
} rx_slot_t;

static QueueHandle_t       s_rx_queue;
static espnow_recv_cb_t    s_on_message;
static rx_slot_t           s_slots[ESPNOW_RECV_SLOTS];
static rx_frame_t          s_frame_tmp;         // WiFi task only
static espnow_recv_stats_t s_stats;



/*===============================================================
 *                  FRAME IN (WiFi task)
 *==============================================================*/

static void frame_in_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (info == NULL || data == NULL || len < ESPNOW_HDR_LEN || len > ESPNOW_FRAME_MAX_LEN) {
        return;
    }

    espnow_hdr_t hdr;
    memcpy(&hdr, data, ESPNOW_HDR_LEN);          // packed: copy, never cast

    if (hdr.ver != ESPNOW_PROTO_VER || hdr.msg_type == ESPNOW_MSG_PROBE) {
        return;                                  // PROBE is answered by the radio itself
    }

    /*----------------- ACK: wake the waiting sender now -----------------*/
    if (hdr.msg_type == ESPNOW_MSG_ACK) {
        if ((size_t)len >= ESPNOW_HDR_LEN + sizeof(espnow_ack_t)) {
            espnow_ack_t ack;
            memcpy(&ack, data + ESPNOW_HDR_LEN, sizeof(ack));
            espnow_send_ack_received(info->src_addr, ack.msg_id);
        }
        return;
    }

    /*----------------- Other frames: to the rx task -----------------*/
    memcpy(s_frame_tmp.mac, info->src_addr, 6);
    s_frame_tmp.rssi = (info->rx_ctrl != NULL) ? info->rx_ctrl->rssi : 0;
    s_frame_tmp.len  = (uint8_t)len;
    memcpy(s_frame_tmp.data, data, len);

    if (xQueueSend(s_rx_queue, &s_frame_tmp, 0) != pdTRUE) {
        s_stats.queue_full++;
    }
}



/*===============================================================
 *                  SENDER SLOTS
 *==============================================================*/

static void slot_drop_msg(rx_slot_t *slot)
{
    free(slot->buf);
    slot->buf      = NULL;
    slot->busy     = false;
    slot->got_mask = 0;
}

/**
 * @brief Slot of this sender; takes the least recently used one when full
 */
static rx_slot_t *slot_get(const uint8_t mac[6], int64_t now)
{
    rx_slot_t *free_slot = NULL;
    rx_slot_t *oldest    = NULL;

    for (int i = 0; i < ESPNOW_RECV_SLOTS; i++) {
        rx_slot_t *slot = &s_slots[i];

        if (slot->used && memcmp(slot->mac, mac, 6) == 0) {
            slot->last_use_us = now;
            return slot;
        }
        if (!slot->used && free_slot == NULL) {
            free_slot = slot;
        }
        if (slot->used && (oldest == NULL || slot->last_use_us < oldest->last_use_us)) {
            oldest = slot;
        }
    }

    rx_slot_t *slot = (free_slot != NULL) ? free_slot : oldest;
    slot_drop_msg(slot);
    memset(slot, 0, sizeof(*slot));
    slot->used = true;
    memcpy(slot->mac, mac, 6);
    slot->last_use_us = now;
    return slot;
}



/*===============================================================
 *                  REBUILD MESSAGE (espnow_rx task)
 *==============================================================*/

static void handle_frame(const rx_frame_t *f)
{
    espnow_hdr_t hdr;
    memcpy(&hdr, f->data, ESPNOW_HDR_LEN);

    const uint8_t *chunk     = f->data + ESPNOW_HDR_LEN;
    size_t         chunk_len = f->len - ESPNOW_HDR_LEN;

    /*----------------- 1. Header check -----------------*/
    if (hdr.total_len > ESPNOW_MSG_MAX_LEN ||
        hdr.frag_cnt != espnow_frag_count(hdr.total_len) ||
        hdr.frag_idx >= hdr.frag_cnt ||
        chunk_len != espnow_chunk_len(hdr.total_len, hdr.frag_idx)) {
        s_stats.bad++;
        ESP_LOGW(TAG, "Bad frame from " MACSTR, MAC2STR(f->mac));
        return;
    }

    int64_t    now  = esp_timer_get_time();
    rx_slot_t *slot = slot_get(f->mac, now);

    /*----------------- 2. Duplicate (sender missed our ACK) -----------------*/
    if (slot->have_last && slot->last_msg_id == hdr.msg_id && slot->last_msg_type == hdr.msg_type) {
        if (hdr.frag_idx == 0) {                 // answer once per re-send
            s_stats.dup++;
            if (hdr.flags & ESPNOW_FLAG_ACK_REQ) {
                espnow_send_ack_reply(f->mac, &hdr);
            }
        }
        return;
    }

    /*----------------- 3. Start a new message if needed -----------------*/
    bool other_msg = slot->busy && (slot->hdr.msg_id != hdr.msg_id || slot->hdr.msg_type != hdr.msg_type);
    bool too_old   = slot->busy && (now - slot->start_us) > (int64_t)ESPNOW_RECV_TIMEOUT_MS * 1000;

    if (other_msg || too_old) {
        slot_drop_msg(slot);
    }

    if (!slot->busy) {
        slot->buf = malloc((size_t)hdr.total_len + 1);
        if (slot->buf == NULL) {
            ESP_LOGE(TAG, "No memory for %u-byte message", hdr.total_len);
            return;
        }
        slot->busy     = true;
        slot->hdr      = hdr;
        slot->got_mask = 0;
        slot->start_us = now;
    }

    if (hdr.total_len != slot->hdr.total_len || hdr.crc16 != slot->hdr.crc16) {
        s_stats.bad++;
        slot_drop_msg(slot);
        return;
    }

    /*----------------- 4. Store the chunk -----------------*/
    if (chunk_len > 0) {
        memcpy(slot->buf + (size_t)hdr.frag_idx * ESPNOW_CHUNK_LEN, chunk, chunk_len);
    }
    slot->got_mask |= (1UL << hdr.frag_idx);

    if (slot->got_mask != (1UL << hdr.frag_cnt) - 1) {
        return;                                  // more frames to come
    }

    /*----------------- 5. Complete: check CRC -----------------*/
    espnow_hdr_t msg_hdr = slot->hdr;
    uint8_t     *msg     = slot->buf;
    slot->buf  = NULL;
    slot->busy = false;

    if (espnow_crc16(msg, msg_hdr.total_len) != msg_hdr.crc16) {
        s_stats.bad++;
        ESP_LOGW(TAG, "CRC error from " MACSTR, MAC2STR(f->mac));
        free(msg);
        return;
    }
    msg[msg_hdr.total_len] = '\0';

    slot->have_last     = true;
    slot->last_msg_id   = msg_hdr.msg_id;
    slot->last_msg_type = msg_hdr.msg_type;

    /*----------------- 6. ACK reply + deliver -----------------*/
    if (msg_hdr.flags & ESPNOW_FLAG_ACK_REQ) {
        espnow_send_ack_reply(f->mac, &msg_hdr);
    }

    s_stats.msgs++;
    if (s_on_message != NULL) {
        s_on_message(f->mac, &msg_hdr, msg, msg_hdr.total_len, f->rssi);
    }

    free(msg);
}

static void espnow_rx_task(void *pvParameters)
{
    rx_frame_t frame;

    while (1) {
        if (xQueueReceive(s_rx_queue, &frame, portMAX_DELAY) == pdTRUE) {
            handle_frame(&frame);
        }
    }
}



/*===============================================================
 *                  INIT / STATS
 *==============================================================*/

esp_err_t espnow_recv_init(espnow_recv_cb_t on_message, uint32_t task_stack, UBaseType_t task_prio)
{
    s_on_message = on_message;

    s_rx_queue = xQueueCreate(ESPNOW_RECV_QUEUE_LEN, sizeof(rx_frame_t));
    if (s_rx_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create rx queue");
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(espnow_rx_task, "espnow_rx", task_stack, NULL, task_prio, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create espnow_rx task");
        return ESP_ERR_NO_MEM;
    }

    esp_now_register_recv_cb(frame_in_cb);
    return ESP_OK;
}

void espnow_recv_get_stats(espnow_recv_stats_t *out)
{
    *out = s_stats;
}