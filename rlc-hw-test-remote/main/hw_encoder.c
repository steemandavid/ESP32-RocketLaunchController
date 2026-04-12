#include "hw_encoder.h"
#include "pin_config.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"

static const char *TAG = "hw_enc";

static volatile int s_count = 0;
static volatile int s_direction = 0;

void IRAM_ATTR enc_isr(void *arg)
{
    int a = gpio_get_level(PIN_ENCODER_A);
    int b = gpio_get_level(PIN_ENCODER_B);
    s_direction = a ^ b ? 1 : -1;
    s_count += s_direction;
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
    return s_direction;
}
