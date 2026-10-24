/**
 * @file espnow_pair.c
 * @brief CALLX / CALLX_ACK signing with the pair key (HMAC-SHA256)
 *
 * Pairing flow:
 *   1. Device sends CALLX     signed with the pair key
 *   2. Hub checks CALLX       -> same key? accept the device
 *   3. Hub sends CALLX_ACK    signed with the same key + device nonce
 *   4. Device checks CALLX_ACK -> real hub, not a copy of an old reply
 *
 * The key itself never goes over the air.
 */

#include <string.h>

#include "esp_random.h"
#include "mbedtls/md.h"

#include "espnow_pair.h"



/*===============================================================
 *                  HMAC HELPERS
 *==============================================================*/

static void hmac_sha256(const uint8_t key[ESPNOW_PAIR_KEY_LEN], const uint8_t *msg, size_t len, uint8_t out[ESPNOW_HMAC_LEN])
{
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                    key, ESPNOW_PAIR_KEY_LEN, msg, len, out);
}

/* Constant-time compare, so timing does not leak how many bytes matched */
static bool hmac_equal(const uint8_t *a, const uint8_t *b)
{
    uint8_t diff = 0;
    for (int i = 0; i < ESPNOW_HMAC_LEN; i++) {
        diff |= a[i] ^ b[i];
    }
    return diff == 0;
}

/* CALLX: nonce | device_id | dev_type */
static void callx_hmac(const espnow_callx_t *callx, uint8_t dev_type, const uint8_t key[ESPNOW_PAIR_KEY_LEN], uint8_t out[ESPNOW_HMAC_LEN])
{
    uint8_t msg[ESPNOW_NONCE_LEN + ESPNOW_ID_LEN + 1];

    memcpy(msg, callx->nonce, ESPNOW_NONCE_LEN);
    memcpy(msg + ESPNOW_NONCE_LEN, callx->device_id, ESPNOW_ID_LEN);
    msg[ESPNOW_NONCE_LEN + ESPNOW_ID_LEN] = dev_type;

    hmac_sha256(key, msg, sizeof(msg), out);
}

/* CALLX_ACK: nonce_echo | hub_id | channel | status */
static void callx_ack_hmac(const espnow_callx_ack_t *ack, const uint8_t key[ESPNOW_PAIR_KEY_LEN], uint8_t out[ESPNOW_HMAC_LEN])
{
    uint8_t msg[ESPNOW_NONCE_LEN + ESPNOW_ID_LEN + 2];

    memcpy(msg, ack->nonce_echo, ESPNOW_NONCE_LEN);
    memcpy(msg + ESPNOW_NONCE_LEN, ack->hub_id, ESPNOW_ID_LEN);
    msg[ESPNOW_NONCE_LEN + ESPNOW_ID_LEN]     = ack->channel;
    msg[ESPNOW_NONCE_LEN + ESPNOW_ID_LEN + 1] = ack->status;

    hmac_sha256(key, msg, sizeof(msg), out);
}



/*===============================================================
 *                  DEVICE SIDE
 *==============================================================*/

/**
 * @brief Fill a new random nonce and sign the CALLX
 *
 * device_id, fw_version, schedule_ver must be set before calling.
 */
void espnow_pair_sign_callx(espnow_callx_t *callx, uint8_t dev_type, const uint8_t key[ESPNOW_PAIR_KEY_LEN])
{
    esp_fill_random(callx->nonce, ESPNOW_NONCE_LEN);
    callx_hmac(callx, dev_type, key, callx->hmac);
}

/**
 * @brief Check the hub's CALLX_ACK
 *
 * Caller must also check ack->nonce_echo == the nonce it sent.
 */
bool espnow_pair_check_callx_ack(const espnow_callx_ack_t *ack, const uint8_t key[ESPNOW_PAIR_KEY_LEN])
{
    uint8_t expect[ESPNOW_HMAC_LEN];
    callx_ack_hmac(ack, key, expect);
    return hmac_equal(expect, ack->hmac);
}



/*===============================================================
 *                  HUB SIDE
 *==============================================================*/

/**
 * @brief Check a device's CALLX (dev_type comes from the frame header)
 */
bool espnow_pair_check_callx(const espnow_callx_t *callx, uint8_t dev_type, const uint8_t key[ESPNOW_PAIR_KEY_LEN])
{
    uint8_t expect[ESPNOW_HMAC_LEN];
    callx_hmac(callx, dev_type, key, expect);
    return hmac_equal(expect, callx->hmac);
}

/**
 * @brief Sign a CALLX_ACK
 *
 * hub_id, channel, status, nonce_echo must be set before calling.
 */
void espnow_pair_sign_callx_ack(espnow_callx_ack_t *ack, const uint8_t key[ESPNOW_PAIR_KEY_LEN])
{
    callx_ack_hmac(ack, key, ack->hmac);
}