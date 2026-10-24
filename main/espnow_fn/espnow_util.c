/**
 * @file espnow_util.c
 * @brief Small helpers with no state
 *
 *  - Frame math : CRC16, number of frames, bytes per frame
 *  - Parsing    : "84:F7:03:12:AB:CD" MAC, 32-hex-char pair key
 *  - Names      : message names for logs, MQTT topic names for the hub
 */

#include <string.h>

#include "esp_rom_crc.h"

#include "espnow_util.h"



/*===============================================================
 *                  FRAME MATH
 *==============================================================*/

/**
 * @brief CRC16 of a payload (0 for an empty payload)
 */
uint16_t espnow_crc16(const void *data, size_t len)
{
    if (data == NULL || len == 0) {
        return 0;
    }
    return esp_rom_crc16_le(0, (const uint8_t *)data, (uint32_t)len);
}

/**
 * @brief Frames needed for a payload (an empty payload still takes 1)
 */
uint8_t espnow_frag_count(uint16_t total_len)
{
    if (total_len == 0) {
        return 1;
    }
    return (uint8_t)((total_len + ESPNOW_CHUNK_LEN - 1) / ESPNOW_CHUNK_LEN);
}

/**
 * @brief Payload bytes carried by frame frag_idx
 */
size_t espnow_chunk_len(uint16_t total_len, uint8_t frag_idx)
{
    uint8_t frag_cnt = espnow_frag_count(total_len);

    if (frag_idx + 1 < frag_cnt) {
        return ESPNOW_CHUNK_LEN;
    }
    return total_len - (size_t)ESPNOW_CHUNK_LEN * (frag_cnt - 1);
}



/*===============================================================
 *                  PARSING
 *==============================================================*/

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool hex_byte(const char *p, uint8_t *out)
{
    int hi = hex_nibble(p[0]);
    int lo = hex_nibble(p[1]);

    if (hi < 0 || lo < 0) {
        return false;
    }
    *out = (uint8_t)((hi << 4) | lo);
    return true;
}

/**
 * @brief "84:F7:03:12:AB:CD" -> 6 bytes
 */
bool espnow_parse_mac(const char *str, uint8_t out[6])
{
    if (str == NULL || strlen(str) != 17) {
        return false;
    }

    for (int i = 0; i < 6; i++) {
        const char *p = str + i * 3;

        if (!hex_byte(p, &out[i])) {
            return false;
        }
        if (i < 5 && p[2] != ':') {
            return false;
        }
    }
    return true;
}

/**
 * @brief 32 hex chars -> 16-byte pair key
 */
bool espnow_parse_key(const char *hex, uint8_t out[ESPNOW_PAIR_KEY_LEN])
{
    if (hex == NULL || strlen(hex) != ESPNOW_PAIR_KEY_LEN * 2) {
        return false;
    }

    for (int i = 0; i < ESPNOW_PAIR_KEY_LEN; i++) {
        if (!hex_byte(hex + i * 2, &out[i])) {
            return false;
        }
    }
    return true;
}

/**
 * @brief True for 00:00:00:00:00:00 (no hub saved)
 */
bool espnow_mac_is_zero(const uint8_t mac[6])
{
    for (int i = 0; i < 6; i++) {
        if (mac[i] != 0) {
            return false;
        }
    }
    return true;
}



/*===============================================================
 *                  NAMES
 *==============================================================*/

/**
 * @brief Message name for logs, e.g. "STATE_DATA"
 */
const char *espnow_msg_name(uint8_t msg_type)
{
    switch (msg_type) {
        case ESPNOW_MSG_STATUS:         return "STATUS";
        case ESPNOW_MSG_STATE_DATA:     return "STATE_DATA";
        case ESPNOW_MSG_ERROR:          return "ERROR";
        case ESPNOW_MSG_CMD_DATA:       return "CMD_DATA";
        case ESPNOW_MSG_CONTROL_DATA:   return "CONTROL_DATA";
        case ESPNOW_MSG_SCHEDULE_DATA:  return "SCHEDULE_DATA";
        case ESPNOW_MSG_SENSOR_DATA:    return "SENSOR_DATA";
        case ESPNOW_MSG_PROBE:          return "PROBE";
        case ESPNOW_MSG_CALLX:          return "CALLX";
        case ESPNOW_MSG_CALLX_ACK:      return "CALLX_ACK";
        case ESPNOW_MSG_ACK:            return "ACK";
        case ESPNOW_MSG_TIME_SYNC:      return "TIME_SYNC";
        case ESPNOW_MSG_REJOIN:         return "REJOIN";
        default:                        return "UNKNOWN";
    }
}

/**
 * @brief MQTT sub-topic of an app message ("state_data"), NULL for link messages
 */
const char *espnow_topic_name(uint8_t msg_type)
{
    switch (msg_type) {
        case ESPNOW_MSG_STATUS:         return "status";
        case ESPNOW_MSG_STATE_DATA:     return "state_data";
        case ESPNOW_MSG_ERROR:          return "error";
        case ESPNOW_MSG_CMD_DATA:       return "cmd_data";
        case ESPNOW_MSG_CONTROL_DATA:   return "control_data";
        case ESPNOW_MSG_SCHEDULE_DATA:  return "schedule_data";
        case ESPNOW_MSG_SENSOR_DATA:    return "sensor_data";
        default:                        return NULL;
    }
}

/**
 * @brief Cloud topic name of a device type ("wifi_valve"), NULL for the hub
 */
const char *espnow_dev_topic_name(uint8_t dev_type)
{
    switch (dev_type) {
        case ESPNOW_DEV_VALVE:          return "wifi_valve";
        case ESPNOW_DEV_PLUG:           return "smart_plug";
        case ESPNOW_DEV_SENSOR_UNIT:    return "sensor_unit";
        default:                        return NULL;
    }
}