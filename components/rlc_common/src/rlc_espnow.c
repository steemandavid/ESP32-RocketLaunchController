/**
 * RLC ESP-NOW Communication Driver
 */

#include "rlc_espnow.h"
#include "rlc_config.h"

#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"

static const char *TAG = "rlc_espnow";

static rlc_espnow_recv_cb_t s_recv_cb = NULL;
static rlc_espnow_send_cb_t s_send_cb = NULL;

static void espnow_recv_cb(const esp_now_recv_info_t *recv_info,
                           const uint8_t *data, int data_len)
{
    if (s_recv_cb && recv_info) {
        int rssi = recv_info->rx_ctrl->rssi;
        s_recv_cb(recv_info->src_addr, data, data_len, rssi);
    }
}

static void espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    if (s_send_cb) {
        s_send_cb(mac_addr, status == ESP_NOW_SEND_SUCCESS);
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

    /* Set Wi-Fi channel */
    ESP_ERROR_CHECK(esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE));

    /* Disable power saving for low latency */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    /* Initialise ESP-NOW */
    ESP_ERROR_CHECK(esp_now_init());

    /* Set PMK for encryption */
    uint8_t pmk[] = ESPNOW_PMK;
    ESP_ERROR_CHECK(esp_now_set_pmk(pmk));

    /* Register callbacks */
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));
    ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));

    ESP_LOGI(TAG, "ESP-NOW initialised on channel %d", WIFI_CHANNEL);
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
        ESP_LOGE(TAG, "Failed to add peer: %s", esp_err_to_name(ret));
        return -1;
    }

    ESP_LOGI(TAG, "Peer added: %02x:%02x:%02x:%02x:%02x:%02x",
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

int rlc_espnow_send(const uint8_t *peer_mac, const uint8_t *data, int len)
{
    esp_err_t ret = esp_now_send(peer_mac, data, len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Send failed: %s", esp_err_to_name(ret));
        return -1;
    }
    return 0;
}

void rlc_espnow_deinit(void)
{
    esp_now_deinit();
    esp_wifi_stop();
    esp_wifi_deinit();
    ESP_LOGI(TAG, "ESP-NOW de-initialised");
}
