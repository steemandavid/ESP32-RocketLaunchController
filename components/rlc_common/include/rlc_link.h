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
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
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
    uint16_t         missed_pings;     /* consecutive failures in current window.
                                        * NOTE: stops incrementing once the link
                                        * is LOST — it is a miss counter, not an
                                        * elapsed-time measure. Use
                                        * ms_since_contact for "how long ago". */
    uint32_t         ms_since_contact; /* since the last well-formed frame from
                                        * the peer; 0 if never in contact */
    uint16_t         ping_rtt_ms;      /* last PING->PONG round-trip, 0 if unknown */
    uint16_t         linkreq_attempts; /* LINK_REQUEST retry count (remote) */
    uint8_t          peer_fw[3];       /* major, minor, patch reported by peer */
    bool             peer_fw_known;
    uint8_t          last_reject;      /* rlc_link_reject_reason_t, 0 = none.
                                        * Why the base last refused a
                                        * handshake, so the remote can say so
                                        * instead of retrying in silence. */
} rlc_link_status_t;

/**
 * Guard callback for LINK_REQUEST rejection during safety-critical states.
 * Return TRUE when the application is busy (ARMED/PRE_FIRE/FIRING/
 * POST_FIRE) — the LINK_REQUEST is then silently ignored (FSD §6.4.1).
 * Return false to accept. If not set (NULL), all LINK_REQUESTs are accepted.
 */
typedef bool (*rlc_link_guard_cb_t)(void);

/**
 * Base-only hook: asked to push a fresh STATUS_UPDATE right after a
 * successful LINK_REQUEST handshake, so the remote gets real state instead
 * of waiting up to one STATUS_UPDATE_INTERVAL_MS (m8 — this replaced a
 * placeholder frame that hardcoded base_state = STATE_IDLE).
 *
 * Runs on link_task with the link state mutex held, so it MUST NOT block,
 * take locks, or send. Setting a flag (status_update_trigger) is the
 * intended implementation.
 */
typedef void (*rlc_link_status_request_cb_t)(void);

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
 * received_ms is the wire-receive timestamp captured in the ESP-NOW recv
 * callback (C3, FSD §6.4.1b) — never re-stamped here.
 */
void rlc_link_on_rx(const uint8_t *src_mac,
                    const uint8_t *data,
                    int len,
                    int rssi,
                    int64_t received_ms);

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
 * Milliseconds since the last well-formed frame was received from the peer,
 * or -1 if the peer has never been heard from.
 *
 * Unlike rlc_link_status_t.ms_since_contact this never conflates "never" with
 * "just now" — required by the base's PRE_FIRE→FIRING heartbeat-freshness
 * guard (FSD §7.2.4 guard 2).
 */
int64_t rlc_link_ms_since_contact(void);

/**
 * Number of fire channels the base reported in LINK_ACK (FSD §8.2.2).
 * Remote side only; returns this build's NUM_CHANNELS until a handshake has
 * completed, and is clamped to NUM_CHANNELS in any case.
 */
uint8_t rlc_link_get_peer_num_channels(void);

/**
 * Remote battery voltage (mV) carried in PING messages. Set by the
 * application periodically; 0 means "unknown" until first battery sample.
 */
void rlc_link_set_remote_battery_mv(uint16_t mv);

/**
 * Set the guard callback for LINK_REQUEST rejection.
 * When set, the callback is invoked before processing a LINK_REQUEST.
 * If it returns true (busy), the request is silently ignored.
 * This allows the application state machine to block session resets
 * during ARMED/PRE_FIRE/FIRING/POST_FIRE (FSD §6.4.1).
 */
void rlc_link_set_guard(rlc_link_guard_cb_t cb);

/** Set the post-handshake status-push hook. See rlc_link_status_request_cb_t. */
void rlc_link_set_status_request_cb(rlc_link_status_request_cb_t cb);

/**
 * Send a STATUS_UPDATE with a pre-populated payload.
 * For use by the base unit's status_update_task to send real I/O data.
 * Only call when linked (does nothing otherwise).
 */
void rlc_link_send_status_update(const rlc_payload_status_update_t *payload);

/* ── Phase 3: Command Queue Integration ─────────────────────────── */

/**
 * Register the state machine's command queue.
 * The link manager will forward received command messages (CMD_ARM, CMD_FIRE,
 * CMD_DISARM, CMD_CEASE_FIRE, CMD_ACK, CMD_NACK) and STATUS_UPDATE to this
 * queue. The queue is owned by the state machine task.
 */
void rlc_link_register_cmd_queue(QueueHandle_t q);

/**
 * Send a command message (remote -> base).
 * Uses the link's session token and the caller-supplied sequence number.
 * The caller must obtain the seq via rlc_link_next_seq() before computing
 * the integrity CRC so that CRC and message header agree on the seq.
 * @return 0 on success, -1 on error or not linked.
 */
int rlc_link_send_cmd(uint8_t msg_type, uint32_t seq, const void *payload, uint16_t payload_len);

/**
 * Get the current session token (for CRC computation on remote side).
 */
uint32_t rlc_link_get_session_token(void);

/**
 * Get the next TX sequence number (thread-safe).
 * Returns 0 on counter wrap; a 0 return is not a usable seq and the caller
 * MUST abort the send (the anti-replay check compares seq numbers, so a
 * wrapped counter cannot produce a monotonic sequence). The re-link is not
 * the caller's job: this function has already dropped the link to LOST
 * before returning 0 (m6).
 */
uint32_t rlc_link_next_seq(void);

/**
 * Check if the link is healthy enough for safety-critical operations.
 * Returns false if ERR_COMM_DEGRADED should be set (>30% ping failures
 * in the last HEARTBEAT_WINDOW_SIZE pings).
 */
bool rlc_link_is_healthy(void);
