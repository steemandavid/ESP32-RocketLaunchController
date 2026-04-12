#include "hw_encoder.h"
#include "pin_config.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"

static const char *TAG = "hw_enc";

static volatile int s_count     = 0;
static volatile int s_direction = 0;
static volatile uint8_t s_prev_state = 0;

/*
 * Gray code quadrature lookup table.
 * Index = (prev_AB << 2) | curr_AB, value = step (+1 CW, -1 CCW, 0 invalid).
 * AB encoding: (A << 1) | B
 */
static const int8_t DRAM_ATTR s_enc_table[16] = {
/*  prev\cur:  00   01   10   11  */
/*  00 */       0,  -1,  +1,   0,
/*  01 */      +1,   0,   0,  -1,
/*  10 */      -1,   0,   0,  +1,
/*  11 */       0,  +1,  -1,   0,
};

static void IRAM_ATTR enc_isr(void *arg)
{
    uint8_t a = gpio_get_level(PIN_ENCODER_A);
    uint8_t b = gpio_get_level(PIN_ENCODER_B);
    uint8_t cur = (a << 1) | b;
    uint8_t idx = (s_prev_state << 2) | cur;
    int8_t step = s_enc_table[idx];
    if (step) {
        s_count += step;
        s_direction = step;
    }
    s_prev_state = cur;
}

void hw_encoder_init(void)
{
    /* Configure A/B as inputs with pull-ups and interrupts */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_ENCODER_A) | (1ULL << PIN_ENCODER_B),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&io);

    /* Sample initial state */
    s_prev_state = (gpio_get_level(PIN_ENCODER_A) << 1)
                 | gpio_get_level(PIN_ENCODER_B);

    /* Install GPIO ISR */
    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIN_ENCODER_A, enc_isr, NULL);
    gpio_isr_handler_add(PIN_ENCODER_B, enc_isr, NULL);

    ESP_LOGI(TAG, "Encoder initialised (A=GPIO%d, B=GPIO%d)", PIN_ENCODER_A, PIN_ENCODER_B);
}

int enc_get_count(void)
{
    return s_count;
}

void enc_reset_count(void)
{
    s_count = 0;
}

int enc_get_direction(void)
{
    int d = s_direction;
    s_direction = 0;
    return d;
}
