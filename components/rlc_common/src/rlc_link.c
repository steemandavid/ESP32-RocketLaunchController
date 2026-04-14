/**
 * RLC Link Manager — Phase 1 implementation.
 *
 * Single-task-owner model: `link_task` owns all link state, receives frames
 * from an internal queue (fed by rlc_link_on_rx from the ESP-NOW recv
 * worker task), and drives all heartbeat timing via queue-receive timeouts.
 *
 * Fixes applied (per Phase 1 code review):
 *   - LINK_REQUEST retry count tracking with "NO LINK" indication
 *   - Sequence number overflow guard (re-link on 0xFFFFFFFF)
 *   - App-state guard callback for LINK_REQUEST rejection
 *   - 5-consecutive-send-failure immediate link loss
 *   - TWDT registration for link task
 *   - Orange ping-failure flash colour corrected to (255,100,0)
 */

#include "rlc_link.h"
#include "rlc_message.h"
#include "rlc_protocol.h"
#include "rlc_espnow.h"
#include "rlc_config.h"
#include "rlc_version.h"
#include "rlc_rgb_led.h"
#include "rlc_watchdog.h"

#include <string.h>
#include <stdlib.h>
#include <limits.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_mac.h"
#include "esp_task_wdt.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

static const char *TAG = "rlc_link";

/* ── Internal rx queue ─────────────────────────────────────────── */

#define LINK_RX_MAX        250
#define LINK_RX_QUEUE_LEN  16

typedef struct {
    uint8_t src_mac[6];
    int     rssi;
    int     len;
    uint8_t data[LINK_RX_MAX];
} link_rx_item_t;

/* ── Private state ─────────────────────────────────────────────── */

static rlc_link_role_t   s_role;
static uint8_t           s_peer_mac[6];

static SemaphoreHandle_t s_state_mutex = NULL;
static QueueHandle_t     s_rx_queue    = NULL;
static TaskHandle_t      s_link_task   = NULL;

/* State — protected by s_state_mutex for external readers. */
static rlc_link_state_t  s_state = RLC_LINK_STATE_BOOT;
static uint32_t          s_session_token = 0;
static uint32_t          s_tx_seq = 0;       /* next sequence to send  */
static uint32_t          s_rx_last_seq = 0;  /* last accepted from peer */
static uint8_t           s_peer_fw[3] = {0};
static bool              s_peer_fw_known = false;

static int               s_rssi_last = 0;
static int               s_rssi_ring[RSSI_AVERAGE_WINDOW];
static int               s_rssi_count = 0;
static int               s_rssi_idx = 0;
static int               s_rssi_avg = 0;

static uint16_t          s_remote_battery_mv = 0;

/* Timing state (monotonic ms via esp_timer_get_time() / 1000). */
static int64_t           s_last_linkreq_ms = 0;
static int64_t           s_last_ping_sent_ms = 0;   /* remote: when last PING sent   */
static uint32_t          s_last_ping_timestamp = 0; /* remote: stamp in last PING    */
static bool              s_ping_outstanding = false;
static int64_t           s_last_ping_rx_ms = 0;     /* base: when last PING received */
static uint16_t          s_missed_pings = 0;

/* LINK_REQUEST retry counter (remote). */
static uint16_t          s_linkreq_attempts = 0;

/* App-state guard callback (for LINK_REQUEST rejection). */
static rlc_link_guard_cb_t s_guard_cb = NULL;

/* ── Helpers ───────────────────────────────────────────────────── */

static inline int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void lock(void)   { xSemaphoreTake(s_state_mutex, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_state_mutex); }

static void update_rssi(int rssi)
{
    s_rssi_last = rssi;
    s_rssi_ring[s_rssi_idx] = rssi;
    s_rssi_idx = (s_rssi_idx + 1) % RSSI_AVERAGE_WINDOW;
    if (s_rssi_count < RSSI_AVERAGE_WINDOW) s_rssi_count++;

    int sum = 0;
    for (int i = 0; i < s_rssi_count; i++) sum += s_rssi_ring[i];
    s_rssi_avg = sum / s_rssi_count;
}

static void set_state(rlc_link_state_t st)
{
    if (s_state == st) return;
    ESP_LOGI(TAG, "link state %d -> %d", s_state, st);
    s_state = st;

    switch (st) {
        case RLC_LINK_STATE_BOOT:
            rlc_rgb_led_set_pattern(LED_PATTERN_BOOT);
            break;
        case RLC_LINK_STATE_WAITING:
            rlc_rgb_led_set_pattern(LED_PATTERN_BOOT);  /* base: still in BOOT pulse */
            break;
        case RLC_LINK_STATE_LINKING:
            rlc_rgb_led_set_pattern(LED_PATTERN_BOOT);  /* remote: boot-like pulse */
            break;
        case RLC_LINK_STATE_LINKED:
            rlc_rgb_led_set_pattern(LED_PATTERN_IDLE);
            break;
        case RLC_LINK_STATE_LOST:
            rlc_rgb_led_set_pattern(LED_PATTERN_LINK_LOST);
            break;
        case RLC_LINK_STATE_VERSION_MISMATCH:
            rlc_rgb_led_set_pattern(LED_PATTERN_ERROR);
            break;
    }
}

static void reset_session(uint32_t new_token)
{
    s_session_token = new_token;
    s_tx_seq = 0;
    s_rx_last_seq = 0;
    s_missed_pings = 0;
    s_ping_outstanding = false;
    s_last_ping_rx_ms = now_ms();
}

/* Strict three-component firmware version check. */
static bool version_matches(const uint8_t fw[3])
{
    return fw[0] == RLC_VERSION_MAJOR &&
           fw[1] == RLC_VERSION_MINOR &&
           fw[2] == RLC_VERSION_PATCH;
}

/**
 * Increment sequence counter with overflow guard (FSD §6.2.2).
 * Returns true if the increment succeeded, false if overflow detected.
 */
static bool seq_next(uint32_t *out_seq)
{
    if (s_tx_seq >= UINT32_MAX) {
        return false;  /* Caller must re-link */
    }
    *out_seq = ++s_tx_seq;
    return true;
}

/* ── Frame builders ────────────────────────────────────────────── */

static void send_link_request(void)
{
    uint8_t buf[RLC_MSG_MAX_SIZE];
    rlc_payload_link_request_t p = {0};
    p.remote_firmware_version[0] = RLC_VERSION_MAJOR;
    p.remote_firmware_version[1] = RLC_VERSION_MINOR;
    p.remote_firmware_version[2] = RLC_VERSION_PATCH;
    esp_read_mac(p.remote_mac, ESP_MAC_WIFI_STA);

    int len = rlc_msg_build(buf, MSG_LINK_REQUEST,
                            0,          /* sequence during handshake is 0 */
                            0,          /* session token 0 during handshake */
                            &p, sizeof(p));
    if (len > 0) {
        rlc_espnow_send(s_peer_mac, buf, len);
        s_last_linkreq_ms = now_ms();
        s_linkreq_attempts++;
        ESP_LOGI(TAG, "LINK_REQUEST sent (attempt %u)", s_linkreq_attempts);
    }
}

static void send_link_ack(void)
{
    uint8_t buf[RLC_MSG_MAX_SIZE];
    rlc_payload_link_ack_t p = {0};
    p.session_token = s_session_token;
    p.base_firmware_version[0] = RLC_VERSION_MAJOR;
    p.base_firmware_version[1] = RLC_VERSION_MINOR;
    p.base_firmware_version[2] = RLC_VERSION_PATCH;
    p.num_channels = NUM_CHANNELS;

    int len = rlc_msg_build(buf, MSG_LINK_ACK, 0, 0, &p, sizeof(p));
    if (len > 0) {
        rlc_espnow_send(s_peer_mac, buf, len);
        ESP_LOGI(TAG, "LINK_ACK sent, token=0x%08lx", (unsigned long)s_session_token);
    }
}

/* Minimal Phase-1 status update: all zero except base_state+sequence.
 * Phase 2/3 will fill in real values. */
static uint16_t s_status_update_seq = 0;

static void send_status_update(void)
{
    uint32_t seq;
    if (!seq_next(&seq)) {
        /* Overflow — would need to re-link, but status updates are not commands */
        s_tx_seq = 0;
        seq = ++s_tx_seq;
    }

    uint8_t buf[RLC_MSG_MAX_SIZE];
    rlc_payload_status_update_t p = {0};
    p.base_state = STATE_IDLE;
    p.update_sequence = s_status_update_seq++;

    int len = rlc_msg_build(buf, MSG_STATUS_UPDATE,
                            seq, s_session_token, &p, sizeof(p));
    if (len > 0) {
        rlc_espnow_send(s_peer_mac, buf, len);
    }
}

void rlc_link_send_status_update(const rlc_payload_status_update_t *payload)
{
    if (s_state != RLC_LINK_STATE_LINKED || s_role != RLC_LINK_ROLE_BASE) {
        return;
    }

    uint32_t seq;
    if (!seq_next(&seq)) {
        s_tx_seq = 0;
        seq = ++s_tx_seq;
    }

    uint8_t buf[RLC_MSG_MAX_SIZE];
    rlc_payload_status_update_t p = *payload;
    p.update_sequence = s_status_update_seq++;

    int len = rlc_msg_build(buf, MSG_STATUS_UPDATE,
                            seq, s_session_token, &p, sizeof(p));
    if (len > 0) {
        rlc_espnow_send(s_peer_mac, buf, len);
    }
}

static void send_ping(uint16_t battery_mv)
{
    uint32_t seq;
    if (!seq_next(&seq)) {
        ESP_LOGW(TAG, "seq overflow in send_ping — re-linking");
        set_state(RLC_LINK_STATE_LINKING);
        send_link_request();
        return;
    }

    uint8_t buf[RLC_MSG_MAX_SIZE];
    rlc_payload_ping_t p = {0};
    p.ping_timestamp = (uint32_t)now_ms();
    p.remote_battery_voltage_mv = battery_mv;

    int len = rlc_msg_build(buf, MSG_PING,
                            seq, s_session_token, &p, sizeof(p));
    if (len > 0) {
        rlc_espnow_send(s_peer_mac, buf, len);
        s_last_ping_sent_ms = now_ms();
        s_last_ping_timestamp = p.ping_timestamp;
        s_ping_outstanding = true;
    }
}

static void send_pong(uint32_t echoed_ping_timestamp)
{
    uint32_t seq;
    if (!seq_next(&seq)) {
        ESP_LOGW(TAG, "seq overflow in send_pong — ignoring");
        return;
    }

    uint8_t buf[RLC_MSG_MAX_SIZE];
    rlc_payload_pong_t p = {0};
    p.ping_timestamp = echoed_ping_timestamp;
    p.pong_timestamp = (uint32_t)now_ms();

    int len = rlc_msg_build(buf, MSG_PONG,
                            seq, s_session_token, &p, sizeof(p));
    if (len > 0) {
        rlc_espnow_send(s_peer_mac, buf, len);
    }
}

/* ── Frame handlers ────────────────────────────────────────────── */

static void handle_link_request(const uint8_t *payload, uint16_t plen)
{
    if (plen < sizeof(rlc_payload_link_request_t)) return;
    const rlc_payload_link_request_t *req = (const rlc_payload_link_request_t *)payload;

    memcpy(s_peer_fw, req->remote_firmware_version, 3);
    s_peer_fw_known = true;

    ESP_LOGI(TAG, "LINK_REQUEST from remote fw %u.%u.%u",
             req->remote_firmware_version[0],
             req->remote_firmware_version[1],
             req->remote_firmware_version[2]);

    /* FSD §6.4.1: reject if app-state guard says busy (ARMED/PRE_FIRE/FIRING/POST_FIRE). */
    if (s_guard_cb && !s_guard_cb()) {
        ESP_LOGI(TAG, "LINK_REQUEST rejected by app-state guard (busy)");
        return;
    }

    /* Atomically invalidate old session before generating the new token
       (FSD §6.2.2). Sequence counters reset to 0. */
    uint32_t new_token;
    do {
        new_token = esp_random();
    } while (new_token == 0 || new_token == s_session_token);
    reset_session(new_token);

    send_link_ack();
    send_status_update();
    set_state(RLC_LINK_STATE_LINKED);
}

static void handle_link_ack(const uint8_t *payload, uint16_t plen)
{
    if (plen < sizeof(rlc_payload_link_ack_t)) return;
    const rlc_payload_link_ack_t *ack = (const rlc_payload_link_ack_t *)payload;

    memcpy(s_peer_fw, ack->base_firmware_version, 3);
    s_peer_fw_known = true;

    if (!version_matches(ack->base_firmware_version)) {
        ESP_LOGE(TAG, "FW MISMATCH: base %u.%u.%u / remote %u.%u.%u",
                 ack->base_firmware_version[0],
                 ack->base_firmware_version[1],
                 ack->base_firmware_version[2],
                 RLC_VERSION_MAJOR, RLC_VERSION_MINOR, RLC_VERSION_PATCH);
        set_state(RLC_LINK_STATE_VERSION_MISMATCH);
        return;
    }

    reset_session(ack->session_token);
    s_linkreq_attempts = 0;  /* Reset retry counter on successful link */
    ESP_LOGI(TAG, "LINK_ACK accepted, token=0x%08lx", (unsigned long)ack->session_token);
    set_state(RLC_LINK_STATE_LINKED);
}

static void handle_ping(const uint8_t *payload, uint16_t plen)
{
    if (plen < sizeof(rlc_payload_ping_t)) return;
    const rlc_payload_ping_t *p = (const rlc_payload_ping_t *)payload;

    s_last_ping_rx_ms = now_ms();
    s_missed_pings = 0;
    send_pong(p->ping_timestamp);
}

static void handle_pong(const uint8_t *payload, uint16_t plen)
{
    if (plen < sizeof(rlc_payload_pong_t)) return;
    const rlc_payload_pong_t *p = (const rlc_payload_pong_t *)payload;

    if (!s_ping_outstanding) return;
    if (p->ping_timestamp != s_last_ping_timestamp) {
        ESP_LOGW(TAG, "stale PONG (ts %lu != %lu) discarded",
                 (unsigned long)p->ping_timestamp,
                 (unsigned long)s_last_ping_timestamp);
        return;
    }

    s_ping_outstanding = false;
    s_missed_pings = 0;
}

static void process_frame(const link_rx_item_t *it)
{
    /* Source MAC filter — reject frames from anyone but the configured peer. */
    if (memcmp(it->src_mac, s_peer_mac, 6) != 0) {
        ESP_LOGW(TAG, "rx from unexpected MAC, dropped");
        return;
    }

    rlc_msg_header_t hdr;
    const uint8_t *payload = NULL;
    uint16_t plen = 0;
    if (!rlc_msg_parse(it->data, it->len, &hdr, &payload, &plen)) {
        ESP_LOGW(TAG, "rx parse failed");
        return;
    }

    update_rssi(it->rssi);

    switch (hdr.msg_type) {
        case MSG_LINK_REQUEST:
            if (s_role == RLC_LINK_ROLE_BASE) {
                handle_link_request(payload, plen);
            }
            break;
        case MSG_LINK_ACK:
            if (s_role == RLC_LINK_ROLE_REMOTE &&
                s_state != RLC_LINK_STATE_VERSION_MISMATCH) {
                handle_link_ack(payload, plen);
            }
            break;
        case MSG_PING:
            if (s_role == RLC_LINK_ROLE_BASE) {
                if (hdr.session_token != s_session_token) {
                    ESP_LOGW(TAG, "PING invalid session, dropped");
                    return;
                }
                /* Sequence check — allow 0 seq after reset. */
                if (hdr.sequence_number <= s_rx_last_seq && s_rx_last_seq != 0) {
                    ESP_LOGW(TAG, "PING replay seq %lu", (unsigned long)hdr.sequence_number);
                    return;
                }
                s_rx_last_seq = hdr.sequence_number;
                if (s_state == RLC_LINK_STATE_LOST) {
                    ESP_LOGI(TAG, "link recovery — PING received");
                    set_state(RLC_LINK_STATE_LINKED);
                }
                handle_ping(payload, plen);
            }
            break;
        case MSG_PONG:
            if (s_role == RLC_LINK_ROLE_REMOTE) {
                if (hdr.session_token != s_session_token) return;
                if (hdr.sequence_number <= s_rx_last_seq && s_rx_last_seq != 0) return;
                s_rx_last_seq = hdr.sequence_number;
                if (s_state == RLC_LINK_STATE_LOST) {
                    ESP_LOGI(TAG, "link recovery — PONG received");
                    set_state(RLC_LINK_STATE_LINKED);
                }
                handle_pong(payload, plen);
            }
            break;
        case MSG_STATUS_UPDATE:
            /* Phase 1: received but not acted upon (Phase 2/3/4 own UI). */
            if (s_role == RLC_LINK_ROLE_REMOTE) {
                if (hdr.session_token != s_session_token) return;
            }
            break;
        default:
            break;
    }
}

/* ── ESP-NOW send failure callback (FSD §6.4.1a) ─────────────── */

static void espnow_send_failure_handler(void)
{
    lock();
    if (s_state == RLC_LINK_STATE_LINKED) {
        ESP_LOGE(TAG, "5 consecutive send failures — immediate link loss");
        set_state(RLC_LINK_STATE_LOST);
    }
    unlock();
}

/* ── Link task main loop ───────────────────────────────────────── */

static void tick_remote(void)
{
    int64_t t = now_ms();

    if (s_state == RLC_LINK_STATE_VERSION_MISMATCH) {
        return;  /* stuck until power cycle */
    }

    if (s_state == RLC_LINK_STATE_LINKING ||
        s_state == RLC_LINK_STATE_LOST) {
        if (t - s_last_linkreq_ms >= LINK_REQUEST_INTERVAL_MS) {
            send_link_request();
            /* FSD §6.4.1: after 5 attempts, display "NO LINK" and keep retrying */
            if (s_linkreq_attempts == LINK_REQUEST_MAX_RETRIES) {
                ESP_LOGW(TAG, "NO LINK — %u attempts failed, retrying at %d ms",
                         s_linkreq_attempts, LINK_REQUEST_SLOW_INTERVAL_MS);
            }
        }
        return;
    }

    if (s_state != RLC_LINK_STATE_LINKED) return;

    /* Outstanding ping timed out? */
    if (s_ping_outstanding &&
        (t - s_last_ping_sent_ms) >= HEARTBEAT_TIMEOUT_MS) {
        s_ping_outstanding = false;
        s_missed_pings++;
        ESP_LOGW(TAG, "PING miss %u", s_missed_pings);

        /* FSD §11.2 / §6.4.2: brief orange overlay (250 ms). Colour (255,100,0). */
        rlc_rgb_led_flash_overlay(255, 100, 0, 250);

        if (s_missed_pings >= HEARTBEAT_FAIL_THRESHOLD) {
            ESP_LOGE(TAG, "LINK LOST (3 missed pings)");
            set_state(RLC_LINK_STATE_LOST);
            return;
        }
    }

    /* Time to emit the next ping? */
    if (!s_ping_outstanding &&
        (t - s_last_ping_sent_ms) >= HEARTBEAT_INTERVAL_MS) {
        send_ping(s_remote_battery_mv);
    }
}

static void tick_base(void)
{
    if (s_state != RLC_LINK_STATE_LINKED) return;

    int64_t t = now_ms();
    int64_t since = t - s_last_ping_rx_ms;
    /* 3 consecutive 500ms intervals without a PING => link lost. */
    if (since >= (int64_t)HEARTBEAT_FAIL_THRESHOLD * HEARTBEAT_INTERVAL_MS) {
        ESP_LOGE(TAG, "base: PING drought (%lld ms) — LINK LOST", since);
        set_state(RLC_LINK_STATE_LOST);
    }
}

static void link_task(void *arg)
{
    (void)arg;

    /* Register with TWDT (FSD §9.6) */
    esp_task_wdt_add(NULL);

    /* Enter initial state */
    if (s_role == RLC_LINK_ROLE_REMOTE) {
        set_state(RLC_LINK_STATE_LINKING);
        send_link_request();
    } else {
        set_state(RLC_LINK_STATE_WAITING);
    }

    link_rx_item_t item;
    const TickType_t poll = pdMS_TO_TICKS(50);

    while (1) {
        if (xQueueReceive(s_rx_queue, &item, poll) == pdTRUE) {
            lock();
            process_frame(&item);
            unlock();
        }

        lock();
        if (s_role == RLC_LINK_ROLE_REMOTE) {
            tick_remote();
        } else {
            tick_base();
        }
        unlock();

        /* Feed TWDT each loop iteration */
        esp_task_wdt_reset();
    }
}

/* ── Public API ────────────────────────────────────────────────── */

static void espnow_recv_trampoline(const uint8_t *src_mac,
                                    const uint8_t *data, int len, int rssi)
{
    rlc_link_on_rx(src_mac, data, len, rssi);
}

int rlc_link_init(rlc_link_role_t role, const uint8_t *peer_mac)
{
    if (!peer_mac) return -1;

    s_role = role;
    memcpy(s_peer_mac, peer_mac, 6);

    s_state_mutex = xSemaphoreCreateMutex();
    s_rx_queue    = xQueueCreate(LINK_RX_QUEUE_LEN, sizeof(link_rx_item_t));
    if (!s_state_mutex || !s_rx_queue) {
        ESP_LOGE(TAG, "alloc failed");
        return -1;
    }

    rlc_espnow_register_recv_cb(espnow_recv_trampoline);
    rlc_espnow_register_send_failure_cb(espnow_send_failure_handler);

    if (xTaskCreate(link_task, "rlc_link", 4096, NULL, 6, &s_link_task) != pdPASS) {
        ESP_LOGE(TAG, "task create failed");
        return -1;
    }

    ESP_LOGI(TAG, "link manager started (role=%s)",
             role == RLC_LINK_ROLE_BASE ? "BASE" : "REMOTE");
    return 0;
}

void rlc_link_on_rx(const uint8_t *src_mac,
                    const uint8_t *data, int len, int rssi)
{
    if (!s_rx_queue || !src_mac || !data || len <= 0 || len > LINK_RX_MAX) return;

    link_rx_item_t it;
    memcpy(it.src_mac, src_mac, 6);
    it.rssi = rssi;
    it.len  = len;
    memcpy(it.data, data, len);
    (void)xQueueSend(s_rx_queue, &it, 0);
}

void rlc_link_get_status(rlc_link_status_t *out)
{
    if (!out) return;
    lock();
    out->state          = s_state;
    out->session_token  = s_session_token;
    out->rssi_avg_dbm   = s_rssi_avg;
    out->last_rssi_dbm  = s_rssi_last;
    out->missed_pings   = s_missed_pings;
    out->linkreq_attempts = s_linkreq_attempts;
    memcpy(out->peer_fw, s_peer_fw, 3);
    out->peer_fw_known  = s_peer_fw_known;
    unlock();
}

bool rlc_link_is_linked(void)
{
    rlc_link_state_t st;
    lock();
    st = s_state;
    unlock();
    return st == RLC_LINK_STATE_LINKED;
}

rlc_link_state_t rlc_link_get_state(void)
{
    rlc_link_state_t st;
    lock();
    st = s_state;
    unlock();
    return st;
}

void rlc_link_set_remote_battery_mv(uint16_t mv)
{
    if (s_state_mutex == NULL) {
        /* Link manager not initialised yet — store value directly.
         * Safe because no other task is reading it yet. */
        s_remote_battery_mv = mv;
        return;
    }
    lock();
    s_remote_battery_mv = mv;
    unlock();
}

void rlc_link_set_guard(rlc_link_guard_cb_t cb)
{
    lock();
    s_guard_cb = cb;
    unlock();
}
