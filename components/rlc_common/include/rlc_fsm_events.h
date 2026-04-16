/**
 * RLC FSM Event Types
 *
 * Shared event definitions for the base and remote state machines.
 * Events are delivered via a FreeRTOS queue owned by the state machine task.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "rlc_protocol.h"

/* ── Event Types ───────────────────────────────────────────────── */

typedef enum {
    /* Received frame events (from link_task) */
    EVT_CMD_ARM          = 0x01,
    EVT_CMD_FIRE         = 0x02,
    EVT_CMD_DISARM       = 0x03,
    EVT_CMD_CEASE_FIRE   = 0x04,
    EVT_CMD_ACK          = 0x05,
    EVT_CMD_NACK         = 0x06,
    EVT_STATUS_UPDATE    = 0x07,

    /* Local I/O events */
    EVT_ARM_SENSE_CHANGED   = 0x10,
    EVT_ARM_SENSE_FAULT     = 0x11,
    EVT_ARM_SWITCH_CHANGED  = 0x12,
    EVT_FIRE_BUTTON_PRESSED = 0x13,
    EVT_FIRE_BUTTON_RELEASED = 0x14,
    EVT_ENCODER_LONG_PRESS  = 0x15,
    EVT_ENCODER_SHORT_PRESS = 0x16,
    EVT_ENCODER_ROTATE      = 0x17,
    EVT_KEY_SWITCH_CHANGED  = 0x18,

    /* Timer / internal events */
    EVT_FIRE_PULSE_DONE   = 0x20,
    EVT_PREFIRE_TIMEOUT   = 0x21,
    EVT_ARM_TIMEOUT       = 0x22,
    EVT_POSTFIRE_COOLDOWN = 0x23,
    EVT_ACK_TIMEOUT       = 0x24,

    /* Link events */
    EVT_LINK_LOST       = 0x30,
    EVT_LINK_ESTABLISHED = 0x31,
    EVT_LINK_RECOVERED  = 0x32,

    /* Battery events */
    EVT_BATTERY_CRITICAL = 0x40,
} rlc_fsm_event_type_t;

/* ── Event Structure ───────────────────────────────────────────── */

typedef struct {
    rlc_fsm_event_type_t type;

    union {
        /* CMD_ARM / CMD_FIRE / CMD_DISARM payload */
        struct {
            uint8_t  channel;          /* Channel number (1-8, or 0xFF for DISARM all) */
            uint32_t seq_number;       /* Sequence number from header */
            uint32_t integrity_crc;    /* CRC from payload (already verified by link_task) */
            int64_t  received_ms;      /* Wire-receive timestamp (captured in ESP-NOW callback) */
        } cmd;

        /* CMD_ACK */
        struct {
            uint8_t  acked_msg_type;
            uint32_t acked_seq_number;
            uint8_t  channel;
        } ack;

        /* CMD_NACK */
        struct {
            uint8_t  nacked_msg_type;
            uint32_t nacked_seq_number;
            uint8_t  reason_code;
        } nack;

        /* STATUS_UPDATE (remote receives from base) */
        struct {
            rlc_payload_status_update_t status;
        } status_update;

        /* Arm sense / arm switch state change */
        struct {
            bool armed;
        } arm_state;

        /* Encoder channel change */
        struct {
            uint8_t channel;    /* New selected channel (1-8) */
        } encoder;

    } data;
} rlc_fsm_event_t;

/* ── Queue Configuration ───────────────────────────────────────── */

#define FSM_EVENT_QUEUE_LEN  16

/* ── Task Notification Bits ────────────────────────────────────── */

#define FIRE_NOTIFY_BIT   0x01U
#define FIRE_ABORT_BIT    0x02U
