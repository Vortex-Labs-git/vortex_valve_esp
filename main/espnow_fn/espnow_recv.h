#ifndef ESPNOW_RECV_H
#define ESPNOW_RECV_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#include "espnow_proto.h"

#ifndef ESPNOW_RECV_SLOTS
#define ESPNOW_RECV_SLOTS           4       // senders rebuilt at once (hub: 20)
#endif
#define ESPNOW_RECV_QUEUE_LEN       12      // raw frames waiting for the rx task
#define ESPNOW_RECV_TIMEOUT_MS      2000    // drop a half-received message after this

/**
 * @brief Called once per complete, CRC-checked, new message
 *
 * Runs in the "espnow_rx" task. data[len] == '\0', so JSON can be
 * used as a C string. data is only valid during the call.
 */
typedef void (*espnow_recv_cb_t)(const uint8_t mac[6], const espnow_hdr_t *hdr, const uint8_t *data, size_t len, int rssi);

typedef struct {
    uint32_t msgs;              // messages delivered
    uint32_t dup;               // duplicates dropped
    uint32_t bad;               // bad header / length / CRC
    uint32_t queue_full;        // frames lost, rx queue full
} espnow_recv_stats_t;

void      espnow_recv_get_stats(espnow_recv_stats_t *out);

/* Used by espnow_link_fn only */
esp_err_t espnow_recv_init(espnow_recv_cb_t on_message, uint32_t task_stack, UBaseType_t task_prio);

#endif // ESPNOW_RECV_H