#ifndef ESPNOW_UTIL_H
#define ESPNOW_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "espnow_proto.h"

/* Frame math */
uint16_t espnow_crc16(const void *data, size_t len);
uint8_t  espnow_frag_count(uint16_t total_len);
size_t   espnow_chunk_len(uint16_t total_len, uint8_t frag_idx);

/* Parsing */
bool espnow_parse_mac(const char *str, uint8_t out[6]);
bool espnow_parse_key(const char *hex, uint8_t out[ESPNOW_PAIR_KEY_LEN]);
bool espnow_mac_is_zero(const uint8_t mac[6]);

/* Names */
const char *espnow_msg_name(uint8_t msg_type);
const char *espnow_topic_name(uint8_t msg_type);
const char *espnow_dev_topic_name(uint8_t dev_type);

#endif // ESPNOW_UTIL_H