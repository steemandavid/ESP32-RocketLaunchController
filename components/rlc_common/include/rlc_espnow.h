/**
 * RLC ESP-NOW Communication Driver
 *
 * Wraps ESP-NOW init, peer management, send/receive,
 * encryption setup, and RSSI capture.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "rlc_protocol.h"

/**
 * Callback invoked when a message is received.
 *
 * @param src_mac      Sender MAC address (6 bytes)
 * @param data         Raw message data (header + payload)
 * @param len          Length of data in bytes
 * @param rssi         RSSI of the received frame (dBm)
 * @param received_ms  Wire-receive timestamp (ms since boot), captured in
 *                     the ESP-NOW recv callback itself — before the frame
 *                     enters any queue — so downstream freshness checks
 *                     (dead-man, §6.4.1b) never count queue latency as
 *                     airtime. Not deferred to processing time (C3).
 */
typedef void (*rlc_espnow_recv_cb_t)(const uint8_t *src_mac,
                                      const uint8_t *data,
                                      int len,
                                      int rssi,
                                      int64_t received_ms);

/**
 * Callback invoked when a send completes.
 *
 * @param dst_mac  Destination MAC address (6 bytes)
 * @param success  true if delivery confirmed, false otherwise
 */
typedef void (*rlc_espnow_send_cb_t)(const uint8_t *dst_mac, bool success);

/**
 * Callback invoked when 5 consecutive ESP-NOW send failures occur
 * (FSD §6.4.1a — immediate link loss).
 *
 * Runs in Wi-Fi task context (esp_now send_cb). Implementations MUST NOT
 * block: no mutex takes, no timed queue sends, no logging. Latch a flag and
 * defer the actual state transition to a task (see rlc_link.c, 2.7).
 */
typedef void (*rlc_espnow_send_failure_cb_t)(void);

/**
 * Initialise ESP-NOW with Wi-Fi in station mode on the configured channel.
 * Sets up PMK encryption.
 *
 * @return 0 on success
 */
int rlc_espnow_init(void);

/**
 * Register the peer (the other unit) with LMK encryption.
 *
 * @param peer_mac  6-byte MAC address of the peer
 * @return 0 on success
 */
int rlc_espnow_add_peer(const uint8_t *peer_mac);

/**
 * Register receive and send callbacks.
 */
void rlc_espnow_register_recv_cb(rlc_espnow_recv_cb_t cb);
void rlc_espnow_register_send_cb(rlc_espnow_send_cb_t cb);

/**
 * Register callback for 5 consecutive send failures (FSD §6.4.1a).
 * The callback fires at most once per failure burst; the counter
 * resets on any successful send.
 */
void rlc_espnow_register_send_failure_cb(rlc_espnow_send_failure_cb_t cb);

/**
 * Send a raw message to the peer.
 *
 * @param peer_mac  Destination MAC (6 bytes)
 * @param data      Message buffer (header + payload)
 * @param len       Length in bytes
 * @return 0 on success
 */
int rlc_espnow_send(const uint8_t *peer_mac, const uint8_t *data, int len);

/**
 * De-initialise ESP-NOW.
 */
void rlc_espnow_deinit(void);
