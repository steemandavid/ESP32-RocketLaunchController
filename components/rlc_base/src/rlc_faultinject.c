/**
 * RLC Fault Injection Implementation — see rlc_faultinject.h.
 *
 * TEST BUILDS ONLY. Guarded whole-file so an accidental addition to
 * CMakeLists in a production build still compiles to nothing.
 */

#include "rlc_faultinject.h"

#if CONFIG_RLC_FAULT_INJECTION

#warning "CONFIG_RLC_FAULT_INJECTION is ON - this firmware deliberately lies to the remote and is NOT safe for live use"

#include "rlc_config.h"
#include "rlc_base_fsm.h"
#include "rlc_status_update.h"
#include "rlc_fsm_events.h"

#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>

static const char *TAG = "rlc_fi";

/* volatile: written by the console task, read by status_update_task and the
 * FSM task. Single-writer, and each is a single machine word, so no lock is
 * needed — but they must not be cached. */
static volatile bool    s_suppress_status = false;
/* Report base_state as IDLE in STATUS_UPDATE while the base is really in
 * ERROR. Keeps error_flags truthful, so the remote can still name the fault
 * from its cache when the NACK arrives. */
static volatile bool    s_lie_state       = false;
static volatile bool    s_wrong_ch_armed  = false;
static volatile bool    s_relay_fault     = false;
static volatile uint8_t s_wrong_ch_last   = 0;

bool fault_inject_suppress_status(void)
{
    return s_suppress_status;
}

bool fault_inject_lie_state(void)
{
    return s_lie_state;
}

bool fault_inject_relay_fault(void)
{
    return s_relay_fault;
}

bool fault_inject_take_wrong_channel(uint8_t *ch)
{
    if (!s_wrong_ch_armed || ch == NULL) return false;
    s_wrong_ch_armed = false;          /* one-shot */

    uint8_t orig = *ch;
    /* Any channel that is not the real one. Wrap inside 1..NUM_CHANNELS so the
     * remote sees a *plausible* channel — a value out of range might be
     * rejected by a bounds check before the mismatch check ever runs, which
     * would test the wrong thing. */
    *ch = (uint8_t)((orig % NUM_CHANNELS) + 1);
    if (*ch == orig) *ch = (uint8_t)((orig % NUM_CHANNELS) + 2);
    s_wrong_ch_last = *ch;

    ESP_LOGE(TAG, "INJECT: ARM ACK channel %u -> %u (T-A13)", orig, *ch);
    return true;
}

/* ESP_LOG rather than printf: it routes through the logging path the rest of
 * the firmware already uses, keeps the output in timestamp order with
 * everything else in the capture, and costs less stack than stdio. */
static void print_state(void)
{
    ESP_LOGW(TAG, "[FI] STATUS_UPDATE suppression : %s  (T-A11)",
             s_suppress_status ? "ON - remote's cached status is ageing out" : "off");
    ESP_LOGW(TAG, "[FI] wrong-channel ARM ACK     : %s  (T-A13)",
             s_wrong_ch_armed  ? "ARMED - fires on the next ARM ACK"         : "off");
    ESP_LOGW(TAG, "[FI] reported ERR_RELAY_FAULT   : %s",
             s_relay_fault ? "ON" : "off");
    ESP_LOGW(TAG, "[FI] keys: s = suppression, a = wrong-channel, "
                  "e = force ERROR w/ fresh cache, ? = this");
}

static void fi_console_task(void *arg)
{
    (void)arg;
    for (;;) {
        int c = getchar();
        if (c == EOF) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }

        switch (c) {
        case 's':
            s_suppress_status = !s_suppress_status;
            ESP_LOGE(TAG, "INJECT: STATUS_UPDATE suppression %s (T-A11)",
                     s_suppress_status ? "ON" : "off");
            print_state();
            break;
        case 'a':
            s_wrong_ch_armed = true;
            ESP_LOGE(TAG, "INJECT: wrong-channel ARM ACK armed (T-A13)");
            print_state();
            break;
        case 'e':
            /* Drive the base into ERROR while keeping the remote's cached
             * status showing a healthy IDLE — the only way to exercise the
             * NACK_BASE_ERROR (0x0E) path.
             *
             * The remote's local guard refuses to arm whenever its cached
             * status says ERROR, so in normal operation it never sends the
             * command and the base never gets to answer. The gap where the
             * base is in ERROR but the remote does not yet know is real (the
             * base can fail between status updates) but lasts under 100 ms,
             * which is not a test.
             *
             * Rather than suppressing status and racing a 4 s freshness
             * window — which is a race, not a test — this falsifies
             * base_state to IDLE in every subsequent STATUS_UPDATE. The
             * remote's cache stays fresh and healthy-looking indefinitely, so
             * it will send an ARM whenever the operator asks, with no time
             * pressure. error_flags is left TRUTHFUL, so when the NACK comes
             * back the remote can still name the actual fault from its
             * cache — which is the behaviour under test. */
            s_lie_state = true;
            if (base_fsm_get_queue()) {
                rlc_fsm_event_t ev = {0};
                ev.type = EVT_BATTERY_CRITICAL;   /* existing terminal path */
                (void)xQueueSend(base_fsm_get_queue(), &ev, pdMS_TO_TICKS(10));
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            status_update_trigger();
            ESP_LOGE(TAG, "INJECT: base forced to ERROR; STATUS_UPDATE keeps "
                          "reporting IDLE so the remote will still send ARM "
                          "-> NACK 0x0E. No time limit.");
            break;
        case 'w':
            s_relay_fault = !s_relay_fault;
            status_update_trigger();
            ESP_LOGE(TAG, "INJECT: reported ERR_RELAY_FAULT (welded relay) %s",
                     s_relay_fault ? "ON" : "off");
            print_state();
            break;
        case '?':
            print_state();
            break;
        default:
            break;   /* ignore stray bytes, including the CR from ENTER */
        }
    }
}

void fault_inject_init(void)
{
    /* stdin is non-blocking without the VFS driver, so getchar() would spin
     * returning EOF. Same setup as tools/armgate-test. */
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    if (uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0) == ESP_OK) {
        uart_vfs_dev_use_driver(UART_NUM_0);
        uart_vfs_dev_port_set_rx_line_endings(UART_NUM_0, ESP_LINE_ENDINGS_CR);
        uart_vfs_dev_port_set_tx_line_endings(UART_NUM_0, ESP_LINE_ENDINGS_CRLF);
    } else {
        ESP_LOGE(TAG, "UART0 driver install failed — injection console unavailable");
        return;
    }

    printf("\n"
      "***********************************************************\n"
      "*  FAULT INJECTION BUILD - NOT SAFE FOR LIVE USE           *\n"
      "*                                                         *\n"
      "*  This firmware can deliberately withhold STATUS_UPDATE   *\n"
      "*  and emit a wrong channel in an ARM ACK, to exercise     *\n"
      "*  FSD tests T-A11 and T-A13 which cannot be induced from  *\n"
      "*  outside the firmware.                                   *\n"
      "*                                                         *\n"
      "*  Reflash a normal build (./build_base.sh flash) before   *\n"
      "*  any further testing or any live use.                    *\n"
      "***********************************************************\n");
    print_state();

    /* 8192, not the 3072 this first had. ESP-IDF's stdio is stack-hungry —
     * printf() in print_state() overflowed 3072 and rebooted the base, which
     * silently cleared every injection flag and made T-A11 look like a
     * firmware failure when it was this task dying. The boot banner survived
     * only because it runs on app_main's much larger stack. */
    if (xTaskCreatePinnedToCore(fi_console_task, "fi_console", 8192, NULL,
                                2, NULL, 0) != pdPASS) {
        ESP_LOGE(TAG, "injection console task create FAILED");
        return;
    }
    ESP_LOGE(TAG, "FAULT INJECTION ACTIVE — see banner above");
}

#endif /* CONFIG_RLC_FAULT_INJECTION */
