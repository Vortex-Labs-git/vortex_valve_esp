#ifndef ESPNOW_PAIR_H
#define ESPNOW_PAIR_H

#include <stdbool.h>
#include <stdint.h>

#include "espnow_proto.h"

/* Device side */
void espnow_pair_sign_callx(espnow_callx_t *callx, uint8_t dev_type, const uint8_t key[ESPNOW_PAIR_KEY_LEN]);
bool espnow_pair_check_callx_ack(const espnow_callx_ack_t *ack, const uint8_t key[ESPNOW_PAIR_KEY_LEN]);

/* Hub side */
bool espnow_pair_check_callx(const espnow_callx_t *callx, uint8_t dev_type, const uint8_t key[ESPNOW_PAIR_KEY_LEN]);
void espnow_pair_sign_callx_ack(espnow_callx_ack_t *ack, const uint8_t key[ESPNOW_PAIR_KEY_LEN]);

#endif // ESPNOW_PAIR_H