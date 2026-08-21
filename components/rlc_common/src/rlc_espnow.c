/**
 * RLC ESP-NOW Communication Driver
 *
 * Wi-Fi + ESP-NOW initialisation, encrypted peer registration, send/recv.
 *
 * Receive path decoupling (FSD §6.4.1b):
 *   The Wi-Fi recv callback runs in Wi-Fi task context and must return
 *   quickly. Frames are copied into a FreeRTOS queue (depth >= 16) and
 *   drained by a dedicated worker task that invokes the user callback
 *   in task context. Frames larger than RLC_ESPNOW_RX_MAX are dropped.
 *
 * Send failure tracking (FSD §6.4.1a):
 *   5 consecutive ESP-NOW send callback failures trigger immediate
 *   link loss via the send_failure_cb callback.
 */

#include "rlc_espnow.h"
#include "rlc_config.h"

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "rlc_espnow";

#define RLC_ESPNOW_RX_MAX        250
#define RLC_ESPNOW_RX_QUEUE_LEN  16
#define ESPNOW_SEND_FAIL_THRESHOLD 5

typedef struct {
    uint8_t src_mac[6];
    int     rssi;
    int     len;
    int64_t received_ms;   /* C3: stamped in espnow_recv_cb, at wire time */
    uint8_t data[RLC_ESPNOW_RX_MAX];
} rlc_espnow_rx_item_t;

static rlc_espnow_recv_cb_t s_recv_cb = NULL;
static rlc_espnow_send_cb_t s_send_cb = NULL;

/* FSD §6.4.1a: send failure tracking */
static rlc_espnow_send_failure_cb_t s_send_failure_cb = NULL;
static volatile int s_consecutive_send_failures = 0;
/* m7: cumulative failures since boot — the diagnostic the removed per-failure
 * ESP_LOGW used to provide, without logging from Wi-Fi task context. */
static volatile uint32_t s_send_failure_total = 0;

static QueueHandle_t s_rx_queue = NULL;
static TaskHandle_t  s_rx_task  = NULL;

static void espnow_recv_cb(const esp_now_recv_info_t *recv_info,
                           const uint8_t *data, int data_len)
{
    if (!recv_info || !data || data_len <= 0) return;
    if (data_len > RLC_ESPNOW_RX_MAX) {
        ESP_LOGW(TAG, "rx oversize (%d), dropped", data_len);
        return;
    }
    if (!s_rx_queue) return;

    rlc_espnow_rx_item_t item;
    memcpy(item.src_mac, recv_info->src_addr, 6);
    item.rssi = recv_info->rx_ctrl ? recv_info->rx_ctrl->rssi : 0;
    item.len  = data_len;
    /* C3 (§6.4.1b): stamp at wire-receive time, before the frame enters the
     * queue, so downstream freshness logic never counts queue drain latency
     * as airtime. */
    item.received_ms = esp_timer_get_time() / 1000;
    memcpy(item.data, data, data_len);

    /* esp_now_recv_cb runs in Wi-Fi task context (not ISR).
     * Use xQueueSend, not xQueueSendFromISR. */
    if (xQueueSend(s_rx_queue, &item, 0) != pdTRUE) {
        ESP_LOGW(TAG, "rx queue full, dropped");
    }
}

static void espnow_send_cb(const uint8_t *mac, esp_now_send_status_t status)
{
    if (status == ESP_NOW_SEND_SUCCESS) {
        s_consecutive_send_failures = 0;
    } else {
        s_consecutive_send_failures++;
        /* m7: no ESP_LOGW here. This runs in Wi-Fi task context, and the
         * per-failure line fired hardest exactly when the link was already
         * struggling — logging takes the stdout lock and can block on a full
         * UART buffer. The running total is exposed via
         * rlc_espnow_get_send_failure_total() and printed by each unit's
         * housekeeping status line instead; the threshold crossing is logged
         * once, on link_task. */
        s_send_failure_total++;

        if (s_consecutive_send_failures >= ESPNOW_SEND_FAIL_THRESHOLD &&
            s_send_failure_cb) {
            /* 2.7: Wi-Fi task context — the callback must not block (no
             * mutexes, no timed queue sends, no logging). The link manager
             * only latches a flag and performs the link-loss transition on
             * its own task. The link-loss line is logged there, once. */
            s_send_failure_cb();
            s_consecutive_send_failures = 0;
        }
    }

    if (s_send_cb) {
        s_send_cb(mac, status == ESP_NOW_SEND_SUCCESS);
    }
}

/* 5.8: deliberately NOT registered with the TWDT (FSD §9.6). It blocks on
 * portMAX_DELAY with nothing to do between frames, so it cannot feed a
 * watchdog while idle — subscribing it would guarantee a spurious panic on
 * any quiet link. Liveness of the receive path is covered by link_task's own
 * TWDT registration plus the PING drought / missed-ping detection, which is
 * the layer that can actually tell "no frames" from "frames not delivered". */
static void espnow_rx_task(void *arg)
{
    (void)arg;
    rlc_espnow_rx_item_t item;
    while (1) {
        if (xQueueReceive(s_rx_queue, &item, portMAX_DELAY) == pdTRUE) {
            if (s_recv_cb) {
                s_recv_cb(item.src_mac, item.data, item.len, item.rssi,
                          item.received_ms);
            }
        }
    }
}

int rlc_espnow_init(void)
{
    /* Initialise NVS (required by Wi-Fi) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Initialise networking stack */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Wi-Fi init in station mode */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Pin to the configured Wi-Fi channel so ESP-NOW never wanders. */
    ESP_ERROR_CHECK(esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE));

    /* Disable power saving for low-latency heartbeats. */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    /* Initialise ESP-NOW */
    ESP_ERROR_CHECK(esp_now_init());

    /* Set PMK for encryption (LMK configured per-peer on add). */
    uint8_t pmk[] = ESPNOW_PMK;
    ESP_ERROR_CHECK(esp_now_set_pmk(pmk));

    /* Create rx queue + worker task before registering callback
       so no frame can arrive before the queue exists. */
    s_rx_queue = xQueueCreate(RLC_ESPNOW_RX_QUEUE_LEN, sizeof(rlc_espnow_rx_item_t));
    if (!s_rx_queue) {
        ESP_LOGE(TAG, "rx queue alloc failed");
        return -1;
    }
    if (xTaskCreate(espnow_rx_task, "espnow_rx", 4096, NULL, 8, &s_rx_task) != pdPASS) {
        ESP_LOGE(TAG, "rx task create failed");
        return -1;
    }

    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));
    ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));

    uint8_t mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    ESP_LOGI(TAG, "ESP-NOW init ch %d, MAC %02x:%02x:%02x:%02x:%02x:%02x",
             WIFI_CHANNEL, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return 0;
}

int rlc_espnow_add_peer(const uint8_t *peer_mac)
{
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, peer_mac, 6);
    peer.channel = WIFI_CHANNEL;
    peer.encrypt = true;

    uint8_t lmk[] = ESPNOW_LMK;
    memcpy(peer.lmk, lmk, 16);

    esp_err_t ret = esp_now_add_peer(&peer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "add_peer failed: %s", esp_err_to_name(ret));
        return -1;
    }

    ESP_LOGI(TAG, "peer added: %02x:%02x:%02x:%02x:%02x:%02x",
             peer_mac[0], peer_mac[1], peer_mac[2],
             peer_mac[3], peer_mac[4], peer_mac[5]);
    return 0;
}

void rlc_espnow_register_recv_cb(rlc_espnow_recv_cb_t cb)
{
    s_recv_cb = cb;
}

void rlc_espnow_register_send_cb(rlc_espnow_send_cb_t cb)
{
    s_send_cb = cb;
}

void rlc_espnow_register_send_failure_cb(rlc_espnow_send_failure_cb_t cb)
{
    s_send_failure_cb = cb;
}

uint32_t rlc_espnow_get_send_failure_total(void)
{
    return s_send_failure_total;
}

int rlc_espnow_send(const uint8_t *peer_mac, const uint8_t *data, int len)
{
    esp_err_t ret = esp_now_send(peer_mac, data, len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "send failed: %s", esp_err_to_name(ret));
        return -1;
    }
    return 0;
}

void rlc_espnow_deinit(void)
{
    esp_now_deinit();
    esp_wifi_stop();
    esp_wifi_deinit();
    if (s_rx_task) {
        vTaskDelete(s_rx_task);
        s_rx_task = NULL;
    }
    if (s_rx_queue) {
        vQueueDelete(s_rx_queue);
        s_rx_queue = NULL;
    }
    ESP_LOGI(TAG, "ESP-NOW de-initialised");
}
