/**
 * RLC Message Serialisation / Deserialisation
 */

#include "rlc_message.h"
#include "rlc_config.h"

#include <string.h>
#include "esp_crc.h"
#include "esp_log.h"

static const char *TAG = "rlc_msg";

int rlc_msg_build(uint8_t *buf,
                  uint8_t msg_type,
                  uint32_t seq_number,
                  uint32_t session_token,
                  const void *payload,
                  uint16_t payload_len)
{
    if (!buf) return -1;
    if (payload_len > 0 && !payload) return -1;
    if (sizeof(rlc_msg_header_t) + payload_len > 250) return -1;  /* ESP-NOW max */

    rlc_msg_header_t header = {
        .protocol_version = RLC_PROTOCOL_VERSION,
        .msg_type         = msg_type,
        .payload_length   = payload_len,
        .sequence_number  = seq_number,
        .session_token    = session_token,
    };

    memcpy(buf, &header, sizeof(header));
    if (payload_len > 0) {
        memcpy(buf + sizeof(header), payload, payload_len);
    }

    return (int)(sizeof(header) + payload_len);
}

bool rlc_msg_parse(const uint8_t *data,
                   int len,
                   rlc_msg_header_t *header_out,
                   const uint8_t **payload_out,
                   uint16_t *payload_len)
{
    if (!data || !header_out || !payload_out || !payload_len) return false;
    if (len < (int)sizeof(rlc_msg_header_t)) return false;

    memcpy(header_out, data, sizeof(rlc_msg_header_t));

    if (header_out->protocol_version != RLC_PROTOCOL_VERSION) {
        ESP_LOGW(TAG, "Unknown protocol version: 0x%02x", header_out->protocol_version);
        return false;
    }

    if ((int)(sizeof(rlc_msg_header_t) + header_out->payload_length) > len) {
        ESP_LOGW(TAG, "Payload length mismatch: declared %u, available %d",
                 header_out->payload_length,
                 len - (int)sizeof(rlc_msg_header_t));
        return false;
    }

    *payload_out = data + sizeof(rlc_msg_header_t);
    *payload_len = header_out->payload_length;
    return true;
}

uint32_t rlc_compute_integrity_crc(const void *payload, uint16_t payload_len)
{
    uint8_t key[] = CMD_INTEGRITY_KEY;

    /* CRC32 over payload + pre-shared key */
    uint32_t crc = esp_crc32_le(0, (const uint8_t *)payload, payload_len);
    crc = esp_crc32_le(crc, key, sizeof(key));

    return crc;
}

bool rlc_seq_validate(uint32_t received, uint32_t *last)
{
    if (!last) return false;
    if (received <= *last) return false;
    *last = received;
    return true;
}
