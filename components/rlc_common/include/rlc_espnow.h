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
 * @param src_mac  Sender MAC address (6 bytes)
 * @param data     Raw message data (header + payload)
 * @param len      Length of data in bytes
 * @param rssi     RSSI of the received frame (dBm)
 */
typedef void (*rlc_espnow_recv_cb_t)(const uint8_t *src_mac,
                                      const uint8_t *data,
                                      int len,
                                      int rssi);

/**
 * Callback invoked when a send completes.
 *
 * @param dst_mac  Destination MAC address (6 bytes)
 * @param success  true if delivery confirmed, false otherwise
 */
typedef void (*rlc_espnow_send_cb_t)(const uint8_t *dst_mac, bool success);

/**
 * Initialise ESP-NOW with Wi-Fi in station mode on the configured channel.
 * Sets up PMK encryption.
 *
 * @return ESP_OK on success
 */
int rlc_espnow_init(void);

/**
 * Register the peer (the other unit) with LMK encryption.
 *
 * @param peer_mac  6-byte MAC address of the peer
 * @return ESP_OK on success
 */
int rlc_espnow_add_peer(const uint8_t *peer_mac);

/**
 * Register receive and send callbacks.
 */
void rlc_espnow_register_recv_cb(rlc_espnow_recv_cb_t cb);
void rlc_espnow_register_send_cb(rlc_espnow_send_cb_t cb);

/**
 * Send a raw message to the peer.
 *
 * @param peer_mac  Destination MAC (6 bytes)
 * @param data      Message buffer (header + payload)
 * @param len       Length in bytes
 * @return ESP_OK on success
 */
int rlc_espnow_send(const uint8_t *peer_mac, const uint8_t *data, int len);

/**
 * De-initialise ESP-NOW.
 */
void rlc_espnow_deinit(void);
