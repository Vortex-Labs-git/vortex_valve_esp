#ifndef ESPNOW_LINK_H
#define ESPNOW_LINK_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#include "espnow_proto.h"
#include "espnow_send.h"
#include "espnow_recv.h"

typedef struct {
    uint8_t          dev_type;          // own espnow_dev_type_t
    espnow_recv_cb_t on_message;        // complete messages
    uint32_t         rx_task_stack;     // 0 -> 6144
    UBaseType_t      rx_task_prio;      // 0 -> 5
} espnow_link_config_t;

typedef struct {
    espnow_send_stats_t tx;
    espnow_recv_stats_t rx;
} espnow_stats_t;

extern const uint8_t ESPNOW_BROADCAST_MAC[6];

esp_err_t espnow_link_init(const espnow_link_config_t *config);

esp_err_t espnow_link_add_peer(const uint8_t mac[6]);
esp_err_t espnow_link_del_peer(const uint8_t mac[6]);

esp_err_t espnow_link_set_channel(uint8_t channel);
uint8_t   espnow_link_get_channel(void);

void      espnow_link_get_stats(espnow_stats_t *out);

#endif // ESPNOW_LINK_H