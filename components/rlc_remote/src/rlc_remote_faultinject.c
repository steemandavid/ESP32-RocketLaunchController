/**
 * RLC Remote Fault Injection Implementation — see rlc_remote_faultinject.h.
 *
 * TEST BUILDS ONLY. Guarded whole-file so an accidental addition to
 * CMakeLists in a production build still compiles to nothing.
 */

#include "rlc_remote_faultinject.h"

#if CONFIG_RLC_REMOTE_FAULT_INJECTION

#warning "CONFIG_RLC_REMOTE_FAULT_INJECTION is ON - this firmware can force itself into ERROR and is NOT safe for live use"

#include "rlc_remote_fsm.h"
#include "rlc_fsm_events.h"
#include "rlc_link.h"

#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>

static const char *TAG = "rlc_rfi";

static void print_state(void)
{
    ESP_LOGW(TAG, "[RFI] keys: d = DISPLAY FAULT, b = BATTERY CRITICAL, "
                  "l = force LINK_REQUEST, c = corrupt next command, ? = this");
    ESP_LOGW(TAG, "[RFI] both latch a terminal ERROR — power cycle to clear");
}

/* Post an event to the remote FSM queue. Blocking send with a short timeout:
 * these are deliberate test injections, and silently dropping one would look
 * like the fault path failing rather than the queue being full. */
static void post_event(uint8_t type, const char *label)
{
    QueueHandle_t q = remote_fsm_get_queue();
    if (!q) {
        ESP_LOGE(TAG, "INJECT: FSM queue not up yet — %s ignored", label);
        return;
    }
    rlc_fsm_event_t ev = {0};
    ev.type = type;
    if (xQueueSend(q, &ev, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGE(TAG, "INJECT: FSM queue full — %s DROPPED", label);
        return;
    }
    ESP_LOGE(TAG, "INJECT: %s posted", label);
}

static void rfi_console_task(void *arg)
{
    (void)arg;
    while (1) {
        int c = getchar();
        if (c == EOF) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }

        switch (c) {
        case 'd':
            /* Same event the 5 s panel-ID re-read posts on two consecutive
             * bad reads (DS-01 / FSD §5.5.6). Reaching it for real means
             * pulling the display flex on a live remote (T-S10b). */
            post_event(EVT_DISPLAY_FAULT, "EVT_DISPLAY_FAULT (-> ERROR)");
            break;
        case 'b':
            /* Terminal battery path. Reaching it for real needs a bench
             * supply taking the pack below REMOTE_VBAT_CRITICAL_MV. */
            post_event(EVT_BATTERY_CRITICAL, "EVT_BATTERY_CRITICAL (-> ERROR)");
            break;
        case 'l':
            /* T-S09: tick_remote() only sends LINK_REQUEST in LINKING or LOST,
             * so a linked remote never emits one — and rebooting to force the
             * issue takes ~1.9 s, by which time the base has hit link loss at
             * 1.5 s and disarmed. This is the only way to put a LINK_REQUEST
             * in front of the base's app-state guard while it is still ARMED. */
            ESP_LOGE(TAG, "INJECT: forcing a LINK_REQUEST while linked (T-S09)");
            rlc_link_force_link_request();
            break;
        case 'c':
            /* T-S05: corrupt one bit of the next outgoing COMMAND, after
             * rlc_msg_build() has computed its integrity CRC. The base's
             * receive path checks that CRC on CMD_* frames only
             * (rlc_link.c), so this is the direction that reaches the guard
             * and produces NACK 0x06 per App D.3. Corrupting a base->remote
             * ACK instead only makes the remote miss a confirmation. */
            ESP_LOGE(TAG, "INJECT: next outgoing COMMAND will be corrupted "
                          "(T-S05) — expect NACK 0x06 from the base");
            rlc_link_corrupt_next_tx();
            break;
        case '?':
            print_state();
            break;
        default:
            break;   /* ignore stray bytes, including the CR from ENTER */
        }
    }
}

void remote_fault_inject_init(void)
{
    /* stdin is non-blocking without the VFS driver, so getchar() would spin
     * returning EOF. Same setup as the base harness. */
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
      "*  REMOTE FAULT INJECTION BUILD - NOT SAFE FOR LIVE USE    *\n"
      "*                                                         *\n"
      "*  This firmware can drive itself into a terminal ERROR    *\n"
      "*  on a keystroke, to exercise the REMOTE FAULT paths that *\n"
      "*  are otherwise reachable only by pulling the display     *\n"
      "*  flex or flattening the pack.                            *\n"
      "*                                                         *\n"
      "*  Reflash a normal build (./build_remote.sh flash) before *\n"
      "*  any further testing or any live use.                    *\n"
      "***********************************************************\n");
    print_state();

    /* 8192 to match the base harness: ESP-IDF's stdio is stack-hungry and
     * printf() in print_state() overflowed 3072 there, rebooting the unit and
     * silently clearing every injection. */
    if (xTaskCreatePinnedToCore(rfi_console_task, "rfi_console", 8192, NULL,
                                2, NULL, 0) != pdPASS) {
        ESP_LOGE(TAG, "injection console task create FAILED");
        return;
    }
    ESP_LOGE(TAG, "REMOTE FAULT INJECTION ACTIVE — see banner above");
}

#endif /* CONFIG_RLC_REMOTE_FAULT_INJECTION */
