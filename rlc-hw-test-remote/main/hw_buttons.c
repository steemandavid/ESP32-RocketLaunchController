#include "hw_buttons.h"
#include "pin_config.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "hw_btn";

/* Shift-register debounce state */
static uint8_t  s_fire_sr  = 0xFF;   /* 8-bit, start released */
static uint16_t s_arm_sr   = 0xFFFF; /* 16-bit, start DISARMED */
static uint16_t s_enc_sw_sr = 0xFFFF; /* 16-bit, start released */

/* Fresh-press tracking */
static int s_fire_was_released = 1;  /* 1 = previously released */

void hw_buttons_init(void)
{
    uint64_t mask = (1ULL << PIN_FIRE_BUTTON)
                  | (1ULL << PIN_ARM_SWITCH)
                  | (1ULL << PIN_ENCODER_SW);
    gpio_config_t io = {
        .pin_bit_mask = mask,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    ESP_LOGI(TAG, "Button inputs initialised (fire=%d, arm=%d, enc_sw=%d)",
             PIN_FIRE_BUTTON, PIN_ARM_SWITCH, PIN_ENCODER_SW);
}

/* --- Fire button (8-bit shift register) -------------------------------- */

int fire_read_raw(void)
{
    return gpio_get_level(PIN_FIRE_BUTTON);
}

int fire_get_shift_reg(void)
{
    /* Shift current reading into register */
    int raw = gpio_get_level(PIN_FIRE_BUTTON);
    s_fire_sr = (s_fire_sr << 1) | (raw ? 1 : 0);
    return s_fire_sr;
}

int fire_read_debounced(void)
{
    fire_get_shift_reg();
    return (s_fire_sr == 0x00) ? 1 : 0;  /* 0x00 = all LOW = pressed */
}

int fire_fresh_press(void)
{
    int pressed = fire_read_debounced();
    int fresh = 0;
    if (pressed && s_fire_was_released) {
        fresh = 1;
    }
    s_fire_was_released = !pressed;
    return fresh;
}

/* --- Arm switch (16-bit shift register) --------------------------------- */

int arm_read_raw(void)
{
    return gpio_get_level(PIN_ARM_SWITCH);
}

uint16_t arm_get_shift_reg(void)
{
    int raw = gpio_get_level(PIN_ARM_SWITCH);
    s_arm_sr = (s_arm_sr << 1) | (raw ? 1 : 0);
    return s_arm_sr;
}

int arm_read_debounced(void)
{
    arm_get_shift_reg();
    return (s_arm_sr == 0x0000) ? 1 : 0; /* 0x0000 = all LOW = ARMED */
}

/* --- Encoder push button (16-bit shift register) ----------------------- */

int enc_sw_read_raw(void)
{
    return gpio_get_level(PIN_ENCODER_SW);
}

uint16_t enc_sw_get_shift_reg(void)
{
    int raw = gpio_get_level(PIN_ENCODER_SW);
    s_enc_sw_sr = (s_enc_sw_sr << 1) | (raw ? 1 : 0);
    return s_enc_sw_sr;
}

int enc_sw_read_debounced(void)
{
    enc_sw_get_shift_reg();
    return (s_enc_sw_sr == 0x0000) ? 1 : 0; /* pressed = LOW */
}
