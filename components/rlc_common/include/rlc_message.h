/**
 * RLC Message Serialisation / Deserialisation
 *
 * Build and parse protocol messages with header, sequence numbers,
 * session tokens, and integrity CRC (CRC32-C Castagnoli).
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "rlc_protocol.h"

/**
 * Build a complete message (header + payload) into the output buffer.
 *
 * @param buf           Output buffer (must be at least RLC_MSG_MAX_SIZE)
 * @param msg_type      Message type
 * @param seq_number    Sequence number for this message
 * @param session_token Current session token (0 during link establishment)
 * @param payload       Payload data (type-specific struct)
 * @param payload_len   Length of payload in bytes
 * @return              Total message length (header + payload), or -1 on error
 */
int rlc_msg_build(uint8_t *buf,
                  uint8_t msg_type,
                  uint32_t seq_number,
                  uint32_t session_token,
                  const void *payload,
                  uint16_t payload_len);

/**
 * Parse a received message buffer into header and payload pointer.
 *
 * @param data          Raw received data
 * @param len           Length of received data
 * @param header_out    Parsed header (output)
 * @param payload_out   Pointer to payload within data (output)
 * @param payload_len   Length of payload (output)
 * @return              true if valid, false if corrupt/too short
 */
bool rlc_msg_parse(const uint8_t *data,
                   int len,
                   rlc_msg_header_t *header_out,
                   const uint8_t **payload_out,
                   uint16_t *payload_len);

/**
 * Compute the CRC32-C integrity check for a command message.
 *
 * CRC input: header_bytes || payload_bytes (excluding CRC field) || integrity_key
 * Polynomial: CRC32-C (Castagnoli) 0x1EDC6F41
 * Initial value: 0xFFFFFFFF, final XOR: 0xFFFFFFFF
 *
 * @param header       Message header bytes (12 bytes)
 * @param header_len   Length of header
 * @param payload      Payload bytes (excluding the CRC field)
 * @param payload_len  Length of payload without CRC
 * @return             CRC32-C value
 */
uint32_t rlc_compute_integrity_crc(const void *header,    uint16_t header_len,
                                   const void *payload,   uint16_t payload_len);

/**
 * Standalone CRC32-C computation (for self-test).
 * Computes CRC32-C over a buffer with standard init/XOR.
 *
 * @param buf   Data buffer
 * @param len   Length of data
 * @return      CRC32-C value
 */
uint32_t rlc_crc32c(const uint8_t *buf, uint32_t len);

/**
 * Validate sequence number (must be strictly greater than last accepted).
 *
 * @param received     Received sequence number
 * @param last         Pointer to last accepted sequence number (updated on success)
 * @return             true if valid
 */
bool rlc_seq_validate(uint32_t received, uint32_t *last);

/**
 * STATUS_UPDATE data-gap detection (FSD §6.4.3).
 *
 * `update_sequence` is a free-running uint16 the base increments on every
 * STATUS_UPDATE. Returns how many frames were lost between `prev` and `now`.
 *
 * Modular arithmetic, so the wrap from 65535 to 0 is a normal step and not a
 * 65535-frame gap (T-U16). A duplicate (`now == prev`) returns 0 — the link
 * layer's replay guard should already have dropped it, and it is not a gap.
 *
 * @param prev  last update_sequence accepted
 * @param now   update_sequence just received
 * @return      number of frames missed (0 when consecutive or duplicate)
 */
uint16_t rlc_update_seq_lost(uint16_t prev, uint16_t now);
