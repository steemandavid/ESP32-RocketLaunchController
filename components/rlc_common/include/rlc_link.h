/**
 * RLC Link Manager — Phase 1 (Foundation and Communication)
 *
 * Handles ESP-NOW link establishment and heartbeat for both Base and
 * Remote units. The module is role-parameterised via rlc_link_role_t.
 *
 * Responsibilities:
 *   - LINK_REQUEST / LINK_ACK handshake with strict MAJOR.MINOR.PATCH check
 *   - Session token generation (base) and storage (remote)
 *   - Per-peer sequence counter management (reset to 0 on new session)
 *   - PING (remote->base) / PONG (base->remote) at 500 ms
 *   - 3-missed-ping link loss detection, recovery on PING/PONG return
 *   - RSSI tracking (3-frame moving average)
 *   - Firmware version mismatch lock-out
 *   - 5-consecutive-send-failure immediate link loss (FSD §6.4.1a)
 *   - App-state guard callback for LINK_REQUEST rejection (FSD §6.4.1)
 *
 * Thread model:
 *   A single `link_task` owns the link state. Incoming frames arrive via
 *   the rlc_espnow recv worker task, which forwards them by calling
 *   rlc_link_on_rx(). That function copies the frame into a queue the
 *   link_task owns, so all state is mutated on one task.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "rlc_protocol.h"

typedef enum {
    RLC_LINK_ROLE_BASE,
    RLC_LINK_ROLE_REMOTE,
} rlc_link_role_t;

typedef enum {
    RLC_LINK_STATE_BOOT = 0,
    RLC_LINK_STATE_LINKING,           /* Remote: sending LINK_REQUESTs */
    RLC_LINK_STATE_WAITING,           /* Base: waiting for LINK_REQUEST */
    RLC_LINK_STATE_LINKED,            /* Handshake done, heartbeats active */
    RLC_LINK_STATE_LOST,              /* Three missed heartbeats */
    RLC_LINK_STATE_VERSION_MISMATCH,  /* Remote only: stuck until reboot */
} rlc_link_state_t;

typedef struct {
    rlc_link_state_t state;
    uint32_t         session_token;
    int              rssi_avg_dbm;     /* average of last 3 frames, 0 if unknown */
    int              last_rssi_dbm;
    uint16_t         missed_pings;     /* consecutive failures in current window */
    uint16_t         linkreq_attempts; /* LINK_REQUEST retry count (remote) */
    uint8_t          peer_fw[3];       /* major, minor, patch reported by peer */
    bool             peer_fw_known;
} rlc_link_status_t;

/**
 * Guard callback for LINK_REQUEST rejection during safety-critical states.
 * Return false to silently ignore the LINK_REQUEST (FSD §6.4.1).
 * If not set (NULL), all LINK_REQUESTs are accepted.
 */
typedef bool (*rlc_link_guard_cb_t)(void);

/**
 * Initialise the link manager. Must be called AFTER rlc_espnow_init()
 * and after the peer has been added via rlc_espnow_add_peer().
 *
 * Creates an internal rx queue + link task. The task drives all timers
 * (LINK_REQUEST retry, PING interval, pong timeout).
 *
 * @param role       RLC_LINK_ROLE_BASE or RLC_LINK_ROLE_REMOTE
 * @param peer_mac   6-byte MAC of the other unit
 * @return 0 on success
 */
int rlc_link_init(rlc_link_role_t role, const uint8_t *peer_mac);

/**
 * Receive-side entry point, called by the ESP-NOW recv worker task.
 * Forwards the frame to the link task's queue for processing.
 */
void rlc_link_on_rx(const uint8_t *src_mac,
                    const uint8_t *data,
                    int len,
                    int rssi);

/**
 * Snapshot current link status (thread-safe, mutex-protected).
 */
void rlc_link_get_status(rlc_link_status_t *out);

/**
 * Convenience helpers.
 */
bool rlc_link_is_linked(void);
rlc_link_state_t rlc_link_get_state(void);

/**
 * Remote battery voltage (mV) carried in PING messages. Set by the
 * application periodically; 0 means "unknown" until first battery sample.
 */
void rlc_link_set_remote_battery_mv(uint16_t mv);

/**
 * Set the guard callback for LINK_REQUEST rejection.
 * When set, the callback is invoked before processing a LINK_REQUEST.
 * If it returns false, the request is silently ignored.
 * This allows the application state machine to block session resets
 * during ARMED/PRE_FIRE/FIRING/POST_FIRE (FSD §6.4.1).
 */
void rlc_link_set_guard(rlc_link_guard_cb_t cb);
