/**
 * @file espnow_proto.h
 * @brief Vortex ESP-NOW protocol: frame header, message types, payloads
 *
 * SHARED FILE — keep it identical in valve, sensor unit, smart plug and hub.
 * Any change here is a protocol change: bump ESPNOW_PROTO_VER.
 *
 * Every ESP-NOW frame:
 *
 *   +---------------------+------------------------------+
 *   | espnow_hdr_t (12 B) | payload chunk (0 .. 238 B)   |
 *   +---------------------+------------------------------+
 *
 * App messages carry the SAME JSON as the old MQTT topics.
 * JSON longer than 238 bytes is split into frames and rebuilt
 * by the receiver before it reaches the JSON handlers.
 */

#ifndef ESPNOW_PROTO_H
#define ESPNOW_PROTO_H

#include <stdint.h>


/*---------------------------------------------------------------
 * Sizes and limits
 *--------------------------------------------------------------*/

#define ESPNOW_PROTO_VER        1

#define ESPNOW_FRAME_MAX_LEN    250     // ESP-NOW frame size
#define ESPNOW_HDR_LEN          12
#define ESPNOW_CHUNK_LEN        (ESPNOW_FRAME_MAX_LEN - ESPNOW_HDR_LEN)    // 238

#define ESPNOW_MSG_MAX_LEN      4096    // same as MAX_MQTT_PAYLOAD
#define ESPNOW_MAX_FRAGS        ((ESPNOW_MSG_MAX_LEN + ESPNOW_CHUNK_LEN - 1) / ESPNOW_CHUNK_LEN)  // 18

#define ESPNOW_PAIR_KEY_LEN     16      // typed / stored as 32 hex chars
#define ESPNOW_HMAC_LEN         32      // HMAC-SHA256
#define ESPNOW_NONCE_LEN        8
#define ESPNOW_ID_LEN           32      // same as DeviceIdentity.device_id
#define ESPNOW_FW_VER_LEN       16


/*---------------------------------------------------------------
 * Device types (type of the SENDER)
 *--------------------------------------------------------------*/

typedef enum {
    ESPNOW_DEV_HUB          = 0x00,
    ESPNOW_DEV_VALVE        = 0x01,     // cloud topic: "wifi_valve"
    ESPNOW_DEV_PLUG         = 0x02,     // cloud topic: "smart_plug"
    ESPNOW_DEV_SENSOR_UNIT  = 0x03,     // cloud topic: "sensor_unit"
} espnow_dev_type_t;


/*---------------------------------------------------------------
 * Message types
 *--------------------------------------------------------------*/

typedef enum {
    /* App messages: payload = JSON, same as the old MQTT topic */
    ESPNOW_MSG_STATUS           = 0x01,     // device -> hub   "status"
    ESPNOW_MSG_STATE_DATA       = 0x02,     // device -> hub   "state_data"
    ESPNOW_MSG_ERROR            = 0x03,     // device -> hub   "error"
    ESPNOW_MSG_CMD_DATA         = 0x04,     // hub -> device   "cmd_data"
    ESPNOW_MSG_CONTROL_DATA     = 0x05,     // hub -> device   "control_data"
    ESPNOW_MSG_SCHEDULE_DATA    = 0x06,     // hub -> device   (planned)
    ESPNOW_MSG_SENSOR_DATA      = 0x07,     // hub -> device   (planned)

    /* Link messages: binary payloads below, never sent to the cloud */
    ESPNOW_MSG_PROBE            = 0x10,     // device -> hub   header only, radio ACK = answer
    ESPNOW_MSG_CALLX            = 0x11,     // device -> hub   espnow_callx_t
    ESPNOW_MSG_CALLX_ACK        = 0x12,     // hub -> device   espnow_callx_ack_t
    ESPNOW_MSG_ACK              = 0x13,     // both            espnow_ack_t
    ESPNOW_MSG_TIME_SYNC        = 0x14,     // hub -> device   espnow_time_sync_t
} espnow_msg_type_t;


/*---------------------------------------------------------------
 * Header flags
 *--------------------------------------------------------------*/

#define ESPNOW_FLAG_ACK_REQ     0x01    // receiver must answer with ESPNOW_MSG_ACK


/*---------------------------------------------------------------
 * Frame header (12 bytes)
 *--------------------------------------------------------------*/

typedef struct __attribute__((packed)) {
    uint8_t  ver;           // ESPNOW_PROTO_VER
    uint8_t  msg_type;      // espnow_msg_type_t
    uint8_t  dev_type;      // espnow_dev_type_t of the sender
    uint8_t  flags;         // ESPNOW_FLAG_*
    uint16_t msg_id;        // +1 per message, finds duplicates
    uint8_t  frag_idx;      // 0 .. frag_cnt-1
    uint8_t  frag_cnt;      // frames in this message
    uint16_t total_len;     // full payload length
    uint16_t crc16;         // CRC16 of the full payload
} espnow_hdr_t;

_Static_assert(sizeof(espnow_hdr_t) == ESPNOW_HDR_LEN, "espnow_hdr_t must be 12 bytes");


/*---------------------------------------------------------------
 * Link payloads
 *--------------------------------------------------------------*/

/**
 * CALLX (device -> hub): "I am this device, I know your key".
 * hmac = HMAC-SHA256(pair_key, nonce | device_id | dev_type)
 */
typedef struct __attribute__((packed)) {
    char     device_id[ESPNOW_ID_LEN];
    char     fw_version[ESPNOW_FW_VER_LEN];
    uint32_t schedule_ver;
    uint8_t  nonce[ESPNOW_NONCE_LEN];
    uint8_t  hmac[ESPNOW_HMAC_LEN];
} espnow_callx_t;

/**
 * CALLX_ACK (hub -> device): "accepted, I know the same key".
 * hmac = HMAC-SHA256(pair_key, nonce_echo | hub_id | channel | status)
 */
typedef enum {
    ESPNOW_CALLX_OK         = 0,
    ESPNOW_CALLX_HUB_FULL   = 1,
} espnow_callx_status_t;

typedef struct __attribute__((packed)) {
    char     hub_id[ESPNOW_ID_LEN];
    uint8_t  channel;
    uint8_t  status;                        // espnow_callx_status_t
    uint8_t  nonce_echo[ESPNOW_NONCE_LEN];  // nonce of the CALLX it answers
    uint8_t  hmac[ESPNOW_HMAC_LEN];
} espnow_callx_ack_t;

/** ACK: message msg_id arrived complete and passed CRC */
typedef struct __attribute__((packed)) {
    uint16_t msg_id;
    uint8_t  msg_type;
} espnow_ack_t;

/** TIME_SYNC: UTC seconds since 1970 */
typedef struct __attribute__((packed)) {
    int64_t  epoch_utc;
} espnow_time_sync_t;

#endif // ESPNOW_PROTO_H