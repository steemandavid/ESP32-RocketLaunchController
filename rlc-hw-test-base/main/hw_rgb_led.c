#include "hw_rgb_led.h"
#include "pin_config.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "hw_rgb_led";

/* WS2812 timing (ns) */
#define WS2812_T0H_NS   300
#define WS2812_T0L_NS   900
#define WS2812_T1H_NS   900
#define WS2812_T1L_NS   300
#define WS2812_RESET_US 50

static rmt_channel_handle_t s_rmt_chan    = NULL;
static rmt_encoder_handle_t s_led_encoder = NULL;
static uint8_t              s_brightness  = 255;

/* ---------- RMT bytes encoder for WS2812 ---------- */
typedef struct {
    rmt_encoder_t       base;
    rmt_encoder_t      *bytes_encoder;
    rmt_encoder_t      *copy_encoder;
    rmt_symbol_word_t   reset_code;
    int                 state;
} ws2812_encoder_t;

static size_t ws2812_encode(rmt_encoder_t *encoder, rmt_channel_handle_t chan,
                             const void *data, size_t data_size,
                             rmt_encode_state_t *ret_state)
{
    ws2812_encoder_t *ws = __containerof(encoder, ws2812_encoder_t, base);
    rmt_encode_state_t sess = RMT_ENCODING_RESET;
    size_t encoded = 0;

    if (ws->state == 0) {
        encoded += ws->bytes_encoder->encode(ws->bytes_encoder, chan,
                                             data, data_size, &sess);
        if (sess & RMT_ENCODING_COMPLETE) {
            ws->state = 1;
            sess      = RMT_ENCODING_RESET;
        }
        if (sess & RMT_ENCODING_MEM_FULL) {
            *ret_state = RMT_ENCODING_MEM_FULL;
            return encoded;
        }
    }
    if (ws->state == 1) {
        encoded += ws->copy_encoder->encode(ws->copy_encoder, chan,
                                            &ws->reset_code,
                                            sizeof(ws->reset_code), &sess);
        if (sess & RMT_ENCODING_COMPLETE) {
            ws->state   = RMT_ENCODING_RESET;
            *ret_state  = RMT_ENCODING_COMPLETE;
        }
    }
    return encoded;
}

static esp_err_t ws2812_del(rmt_encoder_t *encoder)
{
    ws2812_encoder_t *ws = __containerof(encoder, ws2812_encoder_t, base);
    rmt_del_encoder(ws->bytes_encoder);
    rmt_del_encoder(ws->copy_encoder);
    free(ws);
    return ESP_OK;
}

static esp_err_t ws2812_reset(rmt_encoder_t *encoder)
{
    ws2812_encoder_t *ws = __containerof(encoder, ws2812_encoder_t, base);
    rmt_encoder_reset(ws->bytes_encoder);
    rmt_encoder_reset(ws->copy_encoder);
    ws->state = 0;
    return ESP_OK;
}

static esp_err_t new_ws2812_encoder(rmt_encoder_handle_t *out)
{
    ws2812_encoder_t *ws = calloc(1, sizeof(*ws));
    if (!ws) return ESP_ERR_NO_MEM;

    ws->base.encode = ws2812_encode;
    ws->base.del    = ws2812_del;
    ws->base.reset  = ws2812_reset;

    rmt_bytes_encoder_config_t bec = {
        .bit0 = { .level0 = 1, .duration0 = WS2812_T0H_NS / 10,
                  .level1 = 0, .duration1 = WS2812_T0L_NS / 10 },
        .bit1 = { .level0 = 1, .duration0 = WS2812_T1H_NS / 10,
                  .level1 = 0, .duration1 = WS2812_T1L_NS / 10 },
        .flags.msb_first = 1,
    };
    ESP_ERROR_CHECK(rmt_new_bytes_encoder(&bec, &ws->bytes_encoder));

    rmt_copy_encoder_config_t cec = {};
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&cec, &ws->copy_encoder));

    uint32_t reset_ticks = WS2812_RESET_US * 1000 / 10; /* duration in 10 ns units */
    ws->reset_code = (rmt_symbol_word_t){
        .level0    = 0, .duration0 = reset_ticks / 2,
        .level1    = 0, .duration1 = reset_ticks / 2,
    };
    *out = &ws->base;
    return ESP_OK;
}

/* ---------- Public API ---------- */

void hw_rgb_led_init(void)
{
    rmt_tx_channel_config_t tx_cfg = {
        .gpio_num            = PIN_RGB_LED,
        .clk_src             = RMT_CLK_SRC_DEFAULT,
        .resolution_hz       = 10 * 1000 * 1000, /* 10 MHz → 100 ns per tick */
        .mem_block_symbols   = 64,
        .trans_queue_depth   = 4,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_cfg, &s_rmt_chan));
    ESP_ERROR_CHECK(new_ws2812_encoder(&s_led_encoder));
    ESP_ERROR_CHECK(rmt_enable(s_rmt_chan));

    led_off();
    ESP_LOGI(TAG, "RGB LED (WS2812) initialised on GPIO %d", PIN_RGB_LED);
}

void led_set(uint8_t r, uint8_t g, uint8_t b)
{
    /* Apply brightness scaling */
    r = (uint8_t)((r * s_brightness) / 255);
    g = (uint8_t)((g * s_brightness) / 255);
    b = (uint8_t)((b * s_brightness) / 255);

    /* WS2812 GRB byte order */
    uint8_t grb[3] = { g, r, b };

    rmt_transmit_config_t tx_cfg = { .loop_count = 0 };
    rmt_transmit(s_rmt_chan, s_led_encoder, grb, sizeof(grb), &tx_cfg);
    rmt_tx_wait_all_done(s_rmt_chan, pdMS_TO_TICKS(100));
}

void led_off(void)
{
    led_set(0, 0, 0);
}

void led_set_brightness(uint8_t brightness)
{
    s_brightness = brightness;
}

void led_test(void)
{
    /* FSD §11.1 status patterns — 2 s each */
    printf("Blue pulse (DISARMED)...\r\n");
    for (int i = 0; i < 4; i++) {
        led_set(0, 0, 255);
        vTaskDelay(pdMS_TO_TICKS(250));
        led_off();
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    printf("Green solid (LINK_OK)...\r\n");
    led_set(0, 255, 0);
    vTaskDelay(pdMS_TO_TICKS(2000));

    printf("Red blink (ARMED)...\r\n");
    for (int i = 0; i < 4; i++) {
        led_set(255, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(250));
        led_off();
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    printf("Yellow blink (LINK_LOST)...\r\n");
    for (int i = 0; i < 4; i++) {
        led_set(255, 255, 0);
        vTaskDelay(pdMS_TO_TICKS(250));
        led_off();
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    printf("Red triple flash (ERROR)...\r\n");
    for (int rep = 0; rep < 2; rep++) {
        for (int i = 0; i < 3; i++) {
            led_set(255, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
            led_off();
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        vTaskDelay(pdMS_TO_TICKS(600));
    }

    led_off();
    printf("LED test complete.\r\n");
}
