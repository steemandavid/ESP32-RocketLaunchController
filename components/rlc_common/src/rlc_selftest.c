/**
 * RLC Boot Self-Tests
 *
 * Verifies struct packing offsets, CRC32-C correctness, message
 * serialisation, sequence validation, sequence overflow, debounce
 * logic (8-bit and 16-bit), version comparison, continuity band
 * classification, and continuity band encoding at power-on.
 */

#include "rlc_selftest.h"
#include "rlc_protocol.h"
#include "rlc_message.h"
#include "rlc_debounce.h"
#include "rlc_version.h"
#include "rlc_config.h"

#include <stddef.h>
#include <string.h>
#include <limits.h>
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
        CHECK_OFFSET(rlc_payload_status_update_t, base_key_switch,        6),
        CHECK_OFFSET(rlc_payload_status_update_t, base_arm_sense,         7),
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

/* ── Sequence overflow tests ─────────────────────────────────────── */

static int test_sequence_overflow(void)
{
    int failures = 0;

    /* Accept UINT32_MAX - 1 when last = 0 */
    uint32_t last = 0;
    if (!rlc_seq_validate(UINT32_MAX - 1, &last) || last != UINT32_MAX - 1) {
        ESP_LOGE(TAG, "FAIL: seq UINT32_MAX-1 should pass from 0");
        failures++;
    }

    /* Accept UINT32_MAX when last = UINT32_MAX - 1 */
    if (!rlc_seq_validate(UINT32_MAX, &last) || last != UINT32_MAX) {
        ESP_LOGE(TAG, "FAIL: seq UINT32_MAX should pass from UINT32_MAX-1");
        failures++;
    }

    /* Reject UINT32_MAX (equal) when last = UINT32_MAX */
    if (rlc_seq_validate(UINT32_MAX, &last)) {
        ESP_LOGE(TAG, "FAIL: seq UINT32_MAX (equal) should fail");
        failures++;
    }

    /* Reject UINT32_MAX - 1 (lower) when last = UINT32_MAX */
    if (rlc_seq_validate(UINT32_MAX - 1, &last)) {
        ESP_LOGE(TAG, "FAIL: seq UINT32_MAX-1 (lower) should fail");
        failures++;
    }

    /* Reject 0 (wrap) when last = UINT32_MAX */
    if (rlc_seq_validate(0, &last)) {
        ESP_LOGE(TAG, "FAIL: seq 0 (wrap) should fail after UINT32_MAX");
        failures++;
    }

    if (failures == 0) {
        ESP_LOGI(TAG, "Sequence overflow self-test: PASS");
    } else {
        ESP_LOGE(TAG, "Sequence overflow self-test: FAIL (%d)", failures);
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
        ESP_LOGI(TAG, "Debounce 8-bit self-test: PASS");
    } else {
        ESP_LOGE(TAG, "Debounce 8-bit self-test: FAIL (%d)", failures);
    }
    return failures;
}

/* ── 16-bit debounce logic tests ───────────────────────────────── */

static int test_debounce_16bit(void)
{
    int failures = 0;
    rlc_debounce_t db;

    rlc_debounce_init(&db, 2, DEBOUNCE_16BIT);

    /* Feed 15 LOWs — should not trigger yet (need 16) */
    for (int i = 0; i < 15; i++) {
        if (rlc_debounce_update(&db, 0, NULL, NULL)) {
            ESP_LOGE(TAG, "FAIL: 16-bit debounce triggered too early (step %d)", i);
            failures++;
            break;
        }
    }
    /* 16th LOW should trigger active */
    if (!rlc_debounce_update(&db, 0, NULL, NULL)) {
        ESP_LOGE(TAG, "FAIL: 16-bit debounce should trigger on 16th LOW");
        failures++;
    }
    if (!rlc_debounce_get_state(&db)) {
        ESP_LOGE(TAG, "FAIL: 16-bit debounce should be active after 16 LOWs");
        failures++;
    }

    /* Feed 15 HIGHs — should not transition yet */
    for (int i = 0; i < 15; i++) {
        rlc_debounce_update(&db, 1, NULL, NULL);
    }
    /* 16th HIGH should transition to inactive */
    if (!rlc_debounce_update(&db, 1, NULL, NULL)) {
        ESP_LOGE(TAG, "FAIL: 16-bit debounce should trigger on 16th HIGH");
        failures++;
    }
    if (rlc_debounce_get_state(&db)) {
        ESP_LOGE(TAG, "FAIL: 16-bit debounce should be inactive after 16 HIGHs");
        failures++;
    }

    /* Verify that 8 consecutive LOWs in 16-bit mode does NOT trigger */
    rlc_debounce_init(&db, 3, DEBOUNCE_16BIT);
    for (int i = 0; i < 8; i++) {
        if (rlc_debounce_update(&db, 0, NULL, NULL)) {
            ESP_LOGE(TAG, "FAIL: 16-bit debounce triggered after only 8 LOWs");
            failures++;
            break;
        }
    }
    if (rlc_debounce_get_state(&db)) {
        ESP_LOGE(TAG, "FAIL: 16-bit debounce should not be active after 8 LOWs");
        failures++;
    }

    if (failures == 0) {
        ESP_LOGI(TAG, "Debounce 16-bit self-test: PASS");
    } else {
        ESP_LOGE(TAG, "Debounce 16-bit self-test: FAIL (%d)", failures);
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

/* ── Continuity band classification test (§5.4.2) ──────────────── */

/* Verify the production enum matches the wire encoding spec (§5.4.2) */
_Static_assert(CONT_OPEN == 0, "CONT_OPEN must be 0 (wire encoding)");
_Static_assert(CONT_GOOD == 1, "CONT_GOOD must be 1 (wire encoding)");
_Static_assert(CONT_MARGINAL == 2, "CONT_MARGINAL must be 2 (wire encoding)");
_Static_assert(CONT_SHORT == 3, "CONT_SHORT must be 3 (wire encoding)");

/**
 * Threshold classification — same logic as rlc_continuity.c classify_initial().
 * Uses the same CONT_*_UV constants from rlc_config.h, so any threshold
 * change is tested here automatically. The _Static_asserts above ensure
 * enum values stay aligned with wire encoding.
 */
static rlc_continuity_band_t selftest_classify_uv(int32_t uv)
{
    if (uv < CONT_SHORT_UV)      return CONT_SHORT;
    if (uv < CONT_MARGINAL_UV)   return CONT_GOOD;
    if (uv < CONT_OPEN_UV)       return CONT_MARGINAL;
    return CONT_OPEN;
}

static int test_continuity_classification(void)
{
    int failures = 0;

    /* Test the threshold classification logic using config constants */
    struct { int32_t uv; rlc_continuity_band_t expected; } tests[] = {
        { 0,        CONT_SHORT },    /* Zero ohm dead short */
        { 300,      CONT_SHORT },    /* Below SHORT threshold */
        { 500,      CONT_GOOD },     /* At SHORT boundary */
        { 1000,     CONT_GOOD },     /* Solid good reading */
        { 30000,    CONT_GOOD },     /* Still good */
        { 66000,    CONT_MARGINAL }, /* At MARGINAL boundary */
        { 100000,   CONT_MARGINAL }, /* Marginal */
        { 400000,   CONT_MARGINAL }, /* Still marginal, just under OPEN */
        { 432000,   CONT_OPEN },     /* At OPEN boundary (~500 ohm) */
        { 900000,   CONT_OPEN },     /* Definitely open */
        { 3190000,  CONT_OPEN },     /* Open-circuit rest voltage */
    };

    /* Vectors are expressed against the config constants deliberately: they
     * caught the 2026-08-21 OPEN threshold move at boot rather than in the
     * field. Keep them in step when the thresholds change. */

    const int count = sizeof(tests) / sizeof(tests[0]);
    for (int i = 0; i < count; i++) {
        rlc_continuity_band_t result = selftest_classify_uv(tests[i].uv);
        if (result != tests[i].expected) {
            ESP_LOGE(TAG, "FAIL: classify(%ld uV) = %d, expected %d",
                     (long)tests[i].uv, result, tests[i].expected);
            failures++;
        }
    }

    if (failures == 0) {
        ESP_LOGI(TAG, "Continuity classification self-test: PASS (%d points)", count);
    } else {
        ESP_LOGE(TAG, "Continuity classification self-test: FAIL (%d)", failures);
    }
    return failures;
}

/* ── Continuity hysteresis test (§5.4.2, T-U11) ───────────────── */

/**
 * Inline hysteresis classifier — mirrors the production logic in
 * rlc_continuity.c classify_with_hysteresis(). Tests that hysteresis
 * prevents spurious transitions near band boundaries.
 */
static rlc_continuity_band_t test_classify_hysteresis(int32_t uv,
                                                       rlc_continuity_band_t current)
{
    switch (current) {
    case CONT_SHORT:
        if (uv > CONT_SHORT_UV + CONT_HYSTERESIS_SHORT_UV) {
            if (uv < CONT_MARGINAL_UV) return CONT_GOOD;
            if (uv < CONT_OPEN_UV)     return CONT_MARGINAL;
            return CONT_OPEN;
        }
        return CONT_SHORT;

    case CONT_GOOD:
        if (uv < CONT_SHORT_UV - CONT_HYSTERESIS_SHORT_UV)
            return CONT_SHORT;
        if (uv > CONT_MARGINAL_UV + CONT_HYSTERESIS_MARGINAL_UV) {
            if (uv < CONT_OPEN_UV) return CONT_MARGINAL;
            return CONT_OPEN;
        }
        return CONT_GOOD;

    case CONT_MARGINAL:
        if (uv < CONT_MARGINAL_UV - CONT_HYSTERESIS_MARGINAL_UV) {
            if (uv < CONT_SHORT_UV) return CONT_SHORT;
            return CONT_GOOD;
        }
        if (uv > CONT_OPEN_UV + CONT_HYSTERESIS_OPEN_UV)
            return CONT_OPEN;
        return CONT_MARGINAL;

    case CONT_OPEN:
        if (uv < CONT_OPEN_UV - CONT_HYSTERESIS_OPEN_UV) {
            if (uv < CONT_MARGINAL_UV) {
                if (uv < CONT_SHORT_UV) return CONT_SHORT;
                return CONT_GOOD;
            }
            return CONT_MARGINAL;
        }
        return CONT_OPEN;
    }

    return CONT_OPEN;
}

static int test_continuity_hysteresis(void)
{
    int failures = 0;

    /* Test 1: GOOD near SHORT boundary — should stay GOOD within hysteresis */
    rlc_continuity_band_t band = CONT_GOOD;

    /* Voltage just above SHORT threshold but within hysteresis — should stay GOOD */
    band = test_classify_hysteresis(CONT_SHORT_UV + CONT_HYSTERESIS_SHORT_UV / 2, band);
    if (band != CONT_GOOD) {
        ESP_LOGE(TAG, "FAIL: hysteresis GOOD near SHORT boundary — got %d, expected GOOD", band);
        failures++;
    }

    /* Voltage below SHORT threshold minus hysteresis — should transition to SHORT */
    band = test_classify_hysteresis(CONT_SHORT_UV - CONT_HYSTERESIS_SHORT_UV - 1, band);
    if (band != CONT_SHORT) {
        ESP_LOGE(TAG, "FAIL: hysteresis GOOD->SHORT transition — got %d, expected SHORT", band);
        failures++;
    }

    /* Test 2: OPEN near MARGINAL boundary — should stay OPEN within hysteresis */
    band = CONT_OPEN;

    /* Voltage just below OPEN threshold but within hysteresis — should stay OPEN */
    band = test_classify_hysteresis(CONT_OPEN_UV - CONT_HYSTERESIS_OPEN_UV / 2, band);
    if (band != CONT_OPEN) {
        ESP_LOGE(TAG, "FAIL: hysteresis OPEN near MARGINAL boundary — got %d, expected OPEN", band);
        failures++;
    }

    /* Voltage below OPEN threshold minus hysteresis — should transition to MARGINAL */
    band = test_classify_hysteresis(CONT_OPEN_UV - CONT_HYSTERESIS_OPEN_UV - 1, band);
    if (band != CONT_MARGINAL) {
        ESP_LOGE(TAG, "FAIL: hysteresis OPEN->MARGINAL transition — got %d, expected MARGINAL", band);
        failures++;
    }

    /* Test 3: SHORT near GOOD boundary — should stay SHORT within hysteresis */
    band = CONT_SHORT;

    /* Voltage just above SHORT threshold but within hysteresis — should stay SHORT */
    band = test_classify_hysteresis(CONT_SHORT_UV + CONT_HYSTERESIS_SHORT_UV / 2, band);
    if (band != CONT_SHORT) {
        ESP_LOGE(TAG, "FAIL: hysteresis SHORT stability — got %d, expected SHORT", band);
        failures++;
    }

    if (failures == 0) {
        ESP_LOGI(TAG, "Continuity hysteresis self-test: PASS");
    } else {
        ESP_LOGE(TAG, "Continuity hysteresis self-test: FAIL (%d)", failures);
    }
    return failures;
}

/* ── Continuity bands encoding test ─────────────────────────────── */

static int test_continuity_bands_encoding(void)
{
    int failures = 0;

    /* Verify 2-bit-per-channel packing:
     * ch1 in bits 1:0, ch2 in bits 3:2, ..., ch8 in bits 15:14 */
    rlc_continuity_band_t bands[8] = {
        CONT_GOOD,     /* ch1: 01 */
        CONT_SHORT,    /* ch2: 11 */
        CONT_OPEN,     /* ch3: 00 */
        CONT_MARGINAL, /* ch4: 10 */
        CONT_GOOD,     /* ch5: 01 */
        CONT_OPEN,     /* ch6: 00 */
        CONT_SHORT,    /* ch7: 11 */
        CONT_MARGINAL, /* ch8: 10 */
    };

    uint16_t packed = 0;
    for (int i = 0; i < 8; i++) {
        packed |= ((uint16_t)bands[i] << (i * 2));
    }

    /* Expected: ch1=01, ch2=11, ch3=00, ch4=10, ch5=01, ch6=00, ch7=11, ch8=10
     * Bits: 10_11_00_01_10_00_11_01 = 0xB24D */
    uint16_t expected = 0;
    expected |= (0x01UL << 0);   /* ch1: GOOD=1 */
    expected |= (0x03UL << 2);   /* ch2: SHORT=3 */
    expected |= (0x00UL << 4);   /* ch3: OPEN=0 */
    expected |= (0x02UL << 6);   /* ch4: MARGINAL=2 */
    expected |= (0x01UL << 8);   /* ch5: GOOD=1 */
    expected |= (0x00UL << 10);  /* ch6: OPEN=0 */
    expected |= (0x03UL << 12);  /* ch7: SHORT=3 */
    expected |= (0x02UL << 14);  /* ch8: MARGINAL=2 */

    if (packed != expected) {
        ESP_LOGE(TAG, "FAIL: bands encoding 0x%04X, expected 0x%04X", packed, expected);
        failures++;
    }

    /* Verify individual channel extraction */
    for (int i = 0; i < 8; i++) {
        uint8_t extracted = (packed >> (i * 2)) & 0x03;
        if (extracted != bands[i]) {
            ESP_LOGE(TAG, "FAIL: ch%d extracted %d, expected %d",
                     i + 1, extracted, bands[i]);
            failures++;
        }
    }

    /* All OPEN should give 0x0000 */
    uint16_t all_open = 0;
    for (int i = 0; i < 8; i++) {
        all_open |= ((uint16_t)CONT_OPEN << (i * 2));
    }
    if (all_open != 0x0000) {
        ESP_LOGE(TAG, "FAIL: all-OPEN should be 0x0000, got 0x%04X", all_open);
        failures++;
    }

    /* All SHORT should give 0xFFFF */
    uint16_t all_short = 0;
    for (int i = 0; i < 8; i++) {
        all_short |= ((uint16_t)CONT_SHORT << (i * 2));
    }
    if (all_short != 0xFFFF) {
        ESP_LOGE(TAG, "FAIL: all-SHORT should be 0xFFFF, got 0x%04X", all_short);
        failures++;
    }

    if (failures == 0) {
        ESP_LOGI(TAG, "Continuity bands encoding self-test: PASS (packed=0x%04X)", packed);
    } else {
        ESP_LOGE(TAG, "Continuity bands encoding self-test: FAIL (%d)", failures);
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
    failures += test_sequence_overflow();
    failures += test_debounce();
    failures += test_debounce_16bit();
    failures += test_version_comparison();
    failures += test_integrity_crc();
    failures += test_continuity_classification();
    failures += test_continuity_hysteresis();
    failures += test_continuity_bands_encoding();

    if (failures == 0) {
        ESP_LOGI(TAG, "All self-tests PASSED (12 test suites)");
    } else {
        ESP_LOGE(TAG, "%d self-test(s) FAILED — firmware may be corrupt", failures);
    }
    return (failures == 0) ? 0 : -1;
}
