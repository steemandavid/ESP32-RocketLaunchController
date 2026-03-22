/**
 * RLC Message Serialisation / Deserialisation
 *
 * Build and parse protocol messages with header, sequence numbers,
 * session tokens, and integrity CRC.
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
 * Compute the CRC32 integrity check for a command payload.
 *
 * @param payload      Payload bytes (excluding the CRC field)
 * @param payload_len  Length of payload without CRC
 * @return             CRC32 value
 */
uint32_t rlc_compute_integrity_crc(const void *payload, uint16_t payload_len);

/**
 * Validate sequence number (must be strictly greater than last accepted).
 *
 * @param received     Received sequence number
 * @param last         Pointer to last accepted sequence number (updated on success)
 * @return             true if valid
 */
bool rlc_seq_validate(uint32_t received, uint32_t *last);
