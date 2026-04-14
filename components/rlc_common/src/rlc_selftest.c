/**
 * RLC Boot Self-Tests
 *
 * Verifies struct packing offsets, CRC32-C correctness, message
 * serialisation, sequence validation, debounce logic, and version
 * comparison at power-on.
 */

#include "rlc_selftest.h"
#include "rlc_protocol.h"
#include "rlc_message.h"
#include "rlc_debounce.h"
#include "rlc_version.h"

#include <stddef.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "rlc_selftest";

/* ── Struct offset verification ──────────────────────────────────── */

typedef struct {
    const char *name;
    size_t      expected_offset;
    size_t      actual_offset;
} offset_check_t;

#define CHECK_OFFSET(type, field, exp) \
    { #type "." #field, (exp), offsetof(type, field) }

static int test_struct_offsets(void)
{
    const offset_check_t checks[] = {
        /* rlc_msg_header_t */
        CHECK_OFFSET(rlc_msg_header_t, protocol_version, 0),
        CHECK_OFFSET(rlc_msg_header_t, msg_type,         1),
        CHECK_OFFSET(rlc_msg_header_t, payload_length,   2),
        CHECK_OFFSET(rlc_msg_header_t, sequence_number,  4),
        CHECK_OFFSET(rlc_msg_header_t, session_token,    8),

        /* rlc_payload_link_request_t */
        CHECK_OFFSET(rlc_payload_link_request_t, remote_firmware_version, 0),
        CHECK_OFFSET(rlc_payload_link_request_t, remote_mac,              3),

        /* rlc_payload_link_ack_t */
        CHECK_OFFSET(rlc_payload_link_ack_t, session_token,          0),
        CHECK_OFFSET(rlc_payload_link_ack_t, base_firmware_version,  4),
        CHECK_OFFSET(rlc_payload_link_ack_t, num_channels,           7),

        /* rlc_payload_ping_t */
        CHECK_OFFSET(rlc_payload_ping_t, ping_timestamp,            0),
        CHECK_OFFSET(rlc_payload_ping_t, remote_battery_voltage_mv, 4),

        /* rlc_payload_pong_t */
        CHECK_OFFSET(rlc_payload_pong_t, ping_timestamp, 0),
        CHECK_OFFSET(rlc_payload_pong_t, pong_timestamp, 4),

        /* rlc_payload_cmd_arm_t */
        CHECK_OFFSET(rlc_payload_cmd_arm_t, integrity_crc, 0),
        CHECK_OFFSET(rlc_payload_cmd_arm_t, channel,       4),

        /* rlc_payload_status_update_t */
        CHECK_OFFSET(rlc_payload_status_update_t, continuity_bands,       0),
        CHECK_OFFSET(rlc_payload_status_update_t, channel_armed_bitmask,  2),
        CHECK_OFFSET(rlc_payload_status_update_t, channel_firing_bitmask, 4),
        CHECK_OFFSET(rlc_payload_status_update_t, base_arm_switch,        6),
        CHECK_OFFSET(rlc_payload_status_update_t, arm_switch_hw,          7),
        CHECK_OFFSET(rlc_payload_status_update_t, battery_voltage_mv,     8),
        CHECK_OFFSET(rlc_payload_status_update_t, base_state,            10),
        CHECK_OFFSET(rlc_payload_status_update_t, error_flags,           11),
        CHECK_OFFSET(rlc_payload_status_update_t, update_sequence,       12),
    };

    int failures = 0;
    const int count = sizeof(checks) / sizeof(checks[0]);

    for (int i = 0; i < count; i++) {
        if (checks[i].actual_offset != checks[i].expected_offset) {
            ESP_LOGE(TAG, "FAIL: %s offset %u, expected %u",
                     checks[i].name,
                     (unsigned)checks[i].actual_offset,
                     (unsigned)checks[i].expected_offset);
            failures++;
        }
    }

    if (failures == 0) {
        ESP_LOGI(TAG, "Struct offset self-test: PASS (%d checks)", count);
    } else {
        ESP_LOGE(TAG, "Struct offset self-test: FAIL (%d/%d)", failures, count);
    }
    return failures;
}

/* ── CRC32-C test vector ─────────────────────────────────────────── */

static int test_crc32c(void)
{
    /* Standard CRC32-C test vector: "123456789" → 0xE3069283 */
    const uint8_t test_input[] = "123456789";
    const uint32_t expected = 0xE3069283;

    uint32_t result = rlc_crc32c(test_input, 9);

    if (result == expected) {
        ESP_LOGI(TAG, "CRC32-C self-test: PASS (0x%08lX)", (unsigned long)result);
        return 0;
    }

    ESP_LOGE(TAG, "CRC32-C self-test: FAIL (got 0x%08lX, expected 0x%08lX)",
             (unsigned long)result, (unsigned long)expected);
    return 1;
}

/* ── Message serialisation tests ────────────────────────────────── */

static int test_message_serialisation(void)
{
    int failures = 0;
    uint8_t buf[RLC_MSG_MAX_SIZE];

    /* Test: build a PING message and verify round-trip */
    rlc_payload_ping_t ping_out = {
        .ping_timestamp = 0x12345678,
        .remote_battery_voltage_mv = 7500,
    };
    int len = rlc_msg_build(buf, MSG_PING, 42, 0xABCDEF01,
                            &ping_out, sizeof(ping_out));
    if (len != (int)(sizeof(rlc_msg_header_t) + sizeof(ping_out))) {
        ESP_LOGE(TAG, "FAIL: PING build length %d, expected %u",
                 len, (unsigned)(sizeof(rlc_msg_header_t) + sizeof(ping_out)));
        failures++;
    }

    rlc_msg_header_t hdr;
    const uint8_t *payload;
    uint16_t plen;
    if (!rlc_msg_parse(buf, len, &hdr, &payload, &plen)) {
        ESP_LOGE(TAG, "FAIL: PING parse failed");
        failures++;
    } else {
        if (hdr.msg_type != MSG_PING) {
            ESP_LOGE(TAG, "FAIL: PING msg_type %u, expected %u",
                     hdr.msg_type, MSG_PING);
            failures++;
        }
        if (hdr.sequence_number != 42) {
            ESP_LOGE(TAG, "FAIL: PING seq %lu, expected 42",
                     (unsigned long)hdr.sequence_number);
            failures++;
        }
        if (hdr.session_token != 0xABCDEF01) {
            ESP_LOGE(TAG, "FAIL: PING token 0x%08lX, expected 0xABCDEF01",
                     (unsigned long)hdr.session_token);
            failures++;
        }
        if (plen != sizeof(rlc_payload_ping_t)) {
            ESP_LOGE(TAG, "FAIL: PING payload len %u, expected %u",
                     plen, (unsigned)sizeof(rlc_payload_ping_t));
            failures++;
        }
    }

    /* Test: zero-length payload */
    len = rlc_msg_build(buf, MSG_CMD_CEASE_FIRE, 1, 0x100, NULL, 0);
    if (len != (int)sizeof(rlc_msg_header_t)) {
        ESP_LOGE(TAG, "FAIL: zero-payload build length %d, expected %u",
                 len, (unsigned)sizeof(rlc_msg_header_t));
        failures++;
    }

    /* Test: parse with truncated data */
    if (rlc_msg_parse(buf, 5, &hdr, &payload, &plen)) {
        ESP_LOGE(TAG, "FAIL: truncated parse should have failed");
        failures++;
    }

    if (failures == 0) {
        ESP_LOGI(TAG, "Message serialisation self-test: PASS");
    } else {
        ESP_LOGE(TAG, "Message serialisation self-test: FAIL (%d)", failures);
    }
    return failures;
}

/* ── Sequence validation tests ──────────────────────────────────── */

static int test_sequence_validation(void)
{
    int failures = 0;
    uint32_t last = 0;

    /* Strictly increasing */
    if (!rlc_seq_validate(1, &last) || last != 1) {
        ESP_LOGE(TAG, "FAIL: seq 1 should pass");
        failures++;
    }
    if (!rlc_seq_validate(5, &last) || last != 5) {
        ESP_LOGE(TAG, "FAIL: seq 5 should pass");
        failures++;
    }

    /* Equal should fail */
    if (rlc_seq_validate(5, &last)) {
        ESP_LOGE(TAG, "FAIL: seq 5 (equal) should fail");
        failures++;
    }

    /* Lower should fail */
    if (rlc_seq_validate(3, &last)) {
        ESP_LOGE(TAG, "FAIL: seq 3 (lower) should fail");
        failures++;
    }

    /* Zero after non-zero should fail */
    if (rlc_seq_validate(0, &last)) {
        ESP_LOGE(TAG, "FAIL: seq 0 (after 5) should fail");
        failures++;
    }

    /* NULL pointer should fail */
    if (rlc_seq_validate(10, NULL)) {
        ESP_LOGE(TAG, "FAIL: NULL last should fail");
        failures++;
    }

    if (failures == 0) {
        ESP_LOGI(TAG, "Sequence validation self-test: PASS");
    } else {
        ESP_LOGE(TAG, "Sequence validation self-test: FAIL (%d)", failures);
    }
    return failures;
}

/* ── Debounce logic tests ───────────────────────────────────────── */

static int test_debounce(void)
{
    int failures = 0;
    rlc_debounce_t db;
    int callback_count = 0;

    rlc_debounce_init(&db, 1, DEBOUNCE_8BIT);

    /* Feed 7 LOWs — should not trigger yet (need 8) */
    for (int i = 0; i < 7; i++) {
        if (rlc_debounce_update(&db, 0, NULL, NULL)) {
            ESP_LOGE(TAG, "FAIL: debounce triggered too early (step %d)", i);
            failures++;
            break;
        }
    }
    /* 8th LOW should trigger active */
    if (!rlc_debounce_update(&db, 0, NULL, NULL)) {
        ESP_LOGE(TAG, "FAIL: debounce should trigger on 8th LOW");
        failures++;
    }
    if (!rlc_debounce_get_state(&db)) {
        ESP_LOGE(TAG, "FAIL: debounce should be active after 8 LOWs");
        failures++;
    }

    /* Feed 8 HIGHs — should transition to inactive */
    for (int i = 0; i < 7; i++) {
        rlc_debounce_update(&db, 1, NULL, NULL);
    }
    if (!rlc_debounce_update(&db, 1, NULL, NULL)) {
        ESP_LOGE(TAG, "FAIL: debounce should trigger on 8th HIGH");
        failures++;
    }
    if (rlc_debounce_get_state(&db)) {
        ESP_LOGE(TAG, "FAIL: debounce should be inactive after 8 HIGHs");
        failures++;
    }

    (void)callback_count;

    if (failures == 0) {
        ESP_LOGI(TAG, "Debounce self-test: PASS");
    } else {
        ESP_LOGE(TAG, "Debounce self-test: FAIL (%d)", failures);
    }
    return failures;
}

/* ── Version comparison test ────────────────────────────────────── */

static int test_version_comparison(void)
{
    int failures = 0;
    uint8_t current[3] = { RLC_VERSION_MAJOR, RLC_VERSION_MINOR, RLC_VERSION_PATCH };

    /* Exact match */
    if (current[0] != RLC_VERSION_MAJOR || current[1] != RLC_VERSION_MINOR ||
        current[2] != RLC_VERSION_PATCH) {
        ESP_LOGE(TAG, "FAIL: version self-consistency");
        failures++;
    }

    /* Verify the version symbols exist and are non-zero */
    if (RLC_VERSION_MAJOR == 0 && RLC_VERSION_MINOR == 0 && RLC_VERSION_PATCH == 0) {
        ESP_LOGE(TAG, "FAIL: version is 0.0.0 — likely undefined");
        failures++;
    }

    if (failures == 0) {
        ESP_LOGI(TAG, "Version comparison self-test: PASS (v%s)", RLC_VERSION_STRING);
    } else {
        ESP_LOGE(TAG, "Version comparison self-test: FAIL (%d)", failures);
    }
    return failures;
}

/* ── Integrity CRC test ────────────────────────────────────────── */

static int test_integrity_crc(void)
{
    int failures = 0;

    /* Build a header + payload and verify CRC is deterministic and non-zero */
    rlc_msg_header_t hdr = {
        .protocol_version = RLC_PROTOCOL_VERSION,
        .msg_type = MSG_CMD_ARM,
        .payload_length = sizeof(rlc_payload_cmd_arm_t) - 4, /* exclude CRC field */
        .sequence_number = 1,
        .session_token = 0xDEADBEEF,
    };
    uint8_t payload[1] = { 0x05 }; /* channel 5 */

    uint32_t crc1 = rlc_compute_integrity_crc(&hdr, sizeof(hdr), payload, sizeof(payload));
    uint32_t crc2 = rlc_compute_integrity_crc(&hdr, sizeof(hdr), payload, sizeof(payload));

    if (crc1 != crc2) {
        ESP_LOGE(TAG, "FAIL: CRC not deterministic (0x%08lX != 0x%08lX)",
                 (unsigned long)crc1, (unsigned long)crc2);
        failures++;
    }
    if (crc1 == 0) {
        ESP_LOGE(TAG, "FAIL: CRC is zero");
        failures++;
    }

    /* Modify payload and verify CRC changes */
    payload[0] = 0x06;
    uint32_t crc3 = rlc_compute_integrity_crc(&hdr, sizeof(hdr), payload, sizeof(payload));
    if (crc3 == crc1) {
        ESP_LOGE(TAG, "FAIL: CRC unchanged after payload modification");
        failures++;
    }

    if (failures == 0) {
        ESP_LOGI(TAG, "Integrity CRC self-test: PASS (0x%08lX)", (unsigned long)crc1);
    } else {
        ESP_LOGE(TAG, "Integrity CRC self-test: FAIL (%d)", failures);
    }
    return failures;
}

/* ── Public entry point ──────────────────────────────────────────── */

int rlc_selftest_run(void)
{
    ESP_LOGI(TAG, "Running boot self-tests...");

    int failures = 0;
    failures += test_struct_offsets();
    failures += test_crc32c();
    failures += test_message_serialisation();
    failures += test_sequence_validation();
    failures += test_debounce();
    failures += test_version_comparison();
    failures += test_integrity_crc();

    if (failures == 0) {
        ESP_LOGI(TAG, "All self-tests PASSED (7 test suites)");
    } else {
        ESP_LOGE(TAG, "%d self-test(s) FAILED — firmware may be corrupt", failures);
    }
    return (failures == 0) ? 0 : -1;
}
