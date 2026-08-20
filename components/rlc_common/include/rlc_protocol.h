/**
 * RLC Communication Protocol
 *
 * Message types, header, and payload structures for ESP-NOW communication.
 * All multi-byte integers are little-endian (native ESP32-S3).
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

/* ── Protocol Version ─────────────────────────────────────────── */

#define RLC_PROTOCOL_VERSION  0x01

/* ── Message Types ────────────────────────────────────────────── */

typedef enum {
    MSG_LINK_REQUEST   = 0x01,
    MSG_LINK_ACK       = 0x02,
    MSG_PING           = 0x10,
    MSG_PONG           = 0x11,
    MSG_CMD_ARM        = 0x20,
    MSG_CMD_DISARM     = 0x21,
    MSG_CMD_FIRE       = 0x22,
    MSG_CMD_CEASE_FIRE = 0x23,
    MSG_STATUS_UPDATE  = 0x30,
    MSG_CMD_ACK        = 0x31,
    MSG_CMD_NACK       = 0x32,
} rlc_msg_type_t;

/* ── NACK Reason Codes ────────────────────────────────────────── */

typedef enum {
    NACK_BASE_SWITCH_OFF       = 0x01,
    NACK_REMOTE_SWITCH_MISMATCH = 0x02,
    NACK_INVALID_CHANNEL       = 0x03,
    NACK_NO_CONTINUITY         = 0x04,
    NACK_WRONG_STATE           = 0x05,
    NACK_INTEGRITY_ERROR       = 0x06,
    NACK_SESSION_ERROR         = 0x07,
    NACK_REPLAY_DETECTED       = 0x08,
    NACK_LOW_BATTERY           = 0x09,
    NACK_CHANNEL_ALREADY_ARMED = 0x0A,
    NACK_ARM_SENSE_FAULT       = 0x0B,
    NACK_REMOTE_BATTERY_LOW    = 0x0C,
    NACK_COMM_DEGRADED         = 0x0D,
} rlc_nack_reason_t;

/* ── Error Flags (bitmask in STATUS_UPDATE) ───────────────────── */

#define ERR_VBAT_LOW                    (1 << 0)
#define ERR_VBAT_CRITICAL               (1 << 1)
#define ERR_RELAY_FAULT                 (1 << 2)
/* Bit 3 reserved (was ERR_CONTINUITY_LOST_WHILE_ARMED — removed v1.8) */
#define ERR_COMM_DEGRADED               (1 << 4)
#define ERR_WATCHDOG_RESET              (1 << 5)
#define ERR_INTERNAL                    (1 << 6)

/* ── Continuity Band Classification (§5.4.2) ──────────────────── */
/* Enum values intentionally match 2-bit wire encoding in STATUS_UPDATE */

typedef enum {
    CONT_OPEN     = 0,  /* > 500 Ω or no igniter — blocks arming */
    CONT_GOOD     = 1,  /* 0.5–20 Ω — normal igniter */
    CONT_MARGINAL = 2,  /* 20–500 Ω — high resistance, warning only */
    CONT_SHORT    = 3,  /* < 0.5 Ω — possible wiring fault, info only */
} rlc_continuity_band_t;

/* ── FSM States (transmitted in STATUS_UPDATE) ────────────────── */

typedef enum {
    STATE_BOOT      = 0x00,
    STATE_IDLE      = 0x01,
    STATE_LINKING   = 0x02,   /* Remote only */
    STATE_ARMED     = 0x03,
    STATE_PRE_FIRE  = 0x04,
    STATE_FIRING    = 0x05,
    STATE_POST_FIRE = 0x06,
    STATE_LINK_LOST = 0x07,
    STATE_ERROR     = 0x08,
} rlc_state_t;

/* ── Message Structures ──────────────────────────────────────── */

#pragma pack(push, 1)

typedef struct __attribute__((packed)) {
    uint8_t  protocol_version;
    uint8_t  msg_type;
    uint16_t payload_length;
    uint32_t sequence_number;
    uint32_t session_token;
} rlc_msg_header_t;
_Static_assert(sizeof(rlc_msg_header_t) == 12, "Header size mismatch");

typedef struct __attribute__((packed)) {
    uint8_t remote_firmware_version[3];  /* [0]=major, [1]=minor, [2]=patch */
    uint8_t remote_mac[6];
} rlc_payload_link_request_t;
_Static_assert(sizeof(rlc_payload_link_request_t) == 9, "LINK_REQUEST size mismatch");

typedef struct __attribute__((packed)) {
    uint32_t session_token;
    uint8_t  base_firmware_version[3];   /* [0]=major, [1]=minor, [2]=patch */
    uint8_t  num_channels;
} rlc_payload_link_ack_t;
_Static_assert(sizeof(rlc_payload_link_ack_t) == 8, "LINK_ACK size mismatch");

typedef struct __attribute__((packed)) {
    uint32_t ping_timestamp;
    uint16_t remote_battery_voltage_mv;
} rlc_payload_ping_t;
_Static_assert(sizeof(rlc_payload_ping_t) == 6, "PING size mismatch");

typedef struct __attribute__((packed)) {
    uint32_t ping_timestamp;
    uint32_t pong_timestamp;
} rlc_payload_pong_t;
_Static_assert(sizeof(rlc_payload_pong_t) == 8, "PONG size mismatch");

/* Command structs: integrity_crc first for natural 4-byte alignment (FSD §6.3.3) */
typedef struct __attribute__((packed)) {
    uint32_t integrity_crc;
    uint8_t  channel;
} rlc_payload_cmd_arm_t;
_Static_assert(sizeof(rlc_payload_cmd_arm_t) == 5, "CMD_ARM size mismatch");

typedef struct __attribute__((packed)) {
    uint32_t integrity_crc;
    uint8_t  channel;
} rlc_payload_cmd_disarm_t;
_Static_assert(sizeof(rlc_payload_cmd_disarm_t) == 5, "CMD_DISARM size mismatch");

typedef struct __attribute__((packed)) {
    uint32_t integrity_crc;
    uint8_t  channel;
} rlc_payload_cmd_fire_t;
_Static_assert(sizeof(rlc_payload_cmd_fire_t) == 5, "CMD_FIRE size mismatch");

typedef struct __attribute__((packed)) {
    uint32_t integrity_crc;
} rlc_payload_cmd_cease_fire_t;
_Static_assert(sizeof(rlc_payload_cmd_cease_fire_t) == 4, "CMD_CEASE_FIRE size mismatch");

typedef struct __attribute__((packed)) {
    uint16_t continuity_bands;          /* 2 bits/ch: 00=OPEN,01=GOOD,10=MARGINAL,11=SHORT */
    uint16_t channel_armed_bitmask;     /* bits 0-7: armed channels */
    uint16_t channel_firing_bitmask;    /* bits 0-7: firing channels */
    /* Two DIFFERENT signals — see FSD §5.4.3 / §5.4.3b. Historically both
     * carried the key switch (debounced and raw), which made the raw copy
     * useless and left the remote unable to see the arm relay at all. */
    uint8_t  base_key_switch;           /* GPIO 42, debounced: key switch position.
                                         * 1 = key turned to ARM. A precondition
                                         * only — the fire path may still be dead. */
    uint8_t  base_arm_sense;            /* GPIO 21, debounced: arm relay COM output.
                                         * 1 = relay contacts closed AND VBAT live on
                                         * the fire path. This is the hazard signal
                                         * and the welded-contact evidence. */
    uint16_t battery_voltage_mv;
    uint8_t  base_state;
    uint8_t  error_flags;
    uint16_t update_sequence;
} rlc_payload_status_update_t;
_Static_assert(sizeof(rlc_payload_status_update_t) == 14, "STATUS_UPDATE size mismatch");

typedef struct __attribute__((packed)) {
    uint8_t  acked_msg_type;
    uint32_t acked_sequence_number;
    uint8_t  channel;
} rlc_payload_cmd_ack_t;
_Static_assert(sizeof(rlc_payload_cmd_ack_t) == 6, "CMD_ACK size mismatch");

typedef struct __attribute__((packed)) {
    uint8_t  nacked_msg_type;
    uint32_t nacked_sequence_number;
    uint8_t  reason_code;
} rlc_payload_cmd_nack_t;
_Static_assert(sizeof(rlc_payload_cmd_nack_t) == 6, "CMD_NACK size mismatch");

#pragma pack(pop)

/* ── Max message size (header + largest payload) ──────────────── */

#define RLC_MSG_MAX_SIZE  (sizeof(rlc_msg_header_t) + sizeof(rlc_payload_status_update_t))

/* ── Helper: human-readable error flag names ──────────────────── */

/**
 * Name of a SINGLE error flag bit. Pass one bit, not a mask.
 * Every bit 0-7 resolves to something, so an unexpected flag is still
 * reported rather than silently ignored.
 */
static inline const char *rlc_error_flag_str(uint8_t flag)
{
    switch (flag) {
        case ERR_VBAT_LOW:       return "VBAT LOW";
        case ERR_VBAT_CRITICAL:  return "VBAT CRITICAL";
        case ERR_RELAY_FAULT:    return "RELAY FAULT";
        case (1 << 3):           return "RESERVED BIT3";   /* freed in v1.8 */
        case ERR_COMM_DEGRADED:  return "COMM DEGRADED";
        case ERR_WATCHDOG_RESET: return "WATCHDOG RESET";
        case ERR_INTERNAL:       return "INTERNAL FAULT";
        case (1 << 7):           return "UNDEFINED BIT7";
        default:                 return "UNKNOWN";
    }
}

/** Number of flags set in the mask. */
static inline int rlc_error_flags_count(uint8_t flags)
{
    int n = 0;
    for (int b = 0; b < 8; b++) if (flags & (1u << b)) n++;
    return n;
}

/**
 * Name of the n-th set flag (0-based), or NULL when n is out of range.
 * Lets a caller with one line of screen cycle through several flags.
 */
static inline const char *rlc_error_flag_nth(uint8_t flags, int n)
{
    for (int b = 0; b < 8; b++) {
        if (!(flags & (1u << b))) continue;
        if (n-- == 0) return rlc_error_flag_str((uint8_t)(1u << b));
    }
    return NULL;
}

/**
 * Format every set flag into `buf` as a comma-separated list, e.g.
 * "VBAT CRITICAL, RELAY FAULT". Writes "NONE" for an empty mask.
 * Always NUL-terminates. Deliberately avoids stdio so this header stays
 * dependency-light for the shared protocol layer.
 *
 * @return buf, for convenient use inside a log call
 */
static inline const char *rlc_error_flags_str(uint8_t flags, char *buf, size_t len)
{
    if (!buf || len == 0) return "";
    size_t w = 0;
    for (int b = 0; b < 8 && w + 1 < len; b++) {
        if (!(flags & (1u << b))) continue;
        if (w > 0) {
            const char *sep = ", ";
            while (*sep && w + 1 < len) buf[w++] = *sep++;
        }
        const char *name = rlc_error_flag_str((uint8_t)(1u << b));
        while (*name && w + 1 < len) buf[w++] = *name++;
    }
    if (w == 0) {
        const char *none = "NONE";
        while (*none && w + 1 < len) buf[w++] = *none++;
    }
    buf[w] = '\0';
    return buf;
}

/* ── Helper: human-readable NACK reason strings ───────────────── */

static inline const char *rlc_nack_reason_str(uint8_t reason)
{
    switch (reason) {
        case NACK_BASE_SWITCH_OFF:        return "BASE KEY OFF";
        case NACK_REMOTE_SWITCH_MISMATCH: return "REMOTE KEY MISMATCH";
        case NACK_INVALID_CHANNEL:        return "INVALID CHANNEL";
        case NACK_NO_CONTINUITY:          return "NO CONTINUITY";
        case NACK_WRONG_STATE:            return "WRONG STATE";
        case NACK_INTEGRITY_ERROR:        return "INTEGRITY ERROR";
        case NACK_SESSION_ERROR:          return "SESSION ERROR";
        case NACK_REPLAY_DETECTED:        return "REPLAY DETECTED";
        case NACK_LOW_BATTERY:            return "LOW BATTERY";
        case NACK_CHANNEL_ALREADY_ARMED:  return "ALREADY ARMED";
        case NACK_ARM_SENSE_FAULT:        return "ARM SENSE FAULT";
        case NACK_REMOTE_BATTERY_LOW:     return "REMOTE BATTERY LOW";
        case NACK_COMM_DEGRADED:         return "COMM DEGRADED";
        default:                          return "UNKNOWN ERROR";
    }
}
