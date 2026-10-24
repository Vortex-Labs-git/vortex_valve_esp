#ifndef ESPNOW_SEND_H
#define ESPNOW_SEND_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#include "espnow_proto.h"

#define ESPNOW_SEND_FRAME_RETRIES   3       // radio tries per frame
#define ESPNOW_SEND_CB_TIMEOUT_MS   100     // wait for radio result of one frame
#define ESPNOW_SEND_ACK_TIMEOUT_MS  300     // wait for end-to-end ACK
#define ESPNOW_SEND_ACK_RETRIES     3       // whole-message tries with ACK_REQ

typedef struct {
    uint32_t msgs;              // messages sent
    uint32_t fail;              // messages failed
    uint32_t frame_retries;     // frames sent again
} espnow_send_stats_t;

/* Public */
esp_err_t espnow_send_msg(const uint8_t mac[6], uint8_t msg_type, const void *data, size_t len, uint8_t flags);
esp_err_t espnow_send_probe(const uint8_t mac[6]);
void      espnow_send_get_stats(espnow_send_stats_t *out);

/* Used by espnow_link_fn / espnow_recv_fn only */
esp_err_t espnow_send_init(uint8_t own_dev_type);
void      espnow_send_ack_reply(const uint8_t mac[6], const espnow_hdr_t *hdr);
void      espnow_send_ack_received(const uint8_t mac[6], uint16_t msg_id);

#endif // ESPNOW_SEND_H