#pragma once
#include <stdint.h>

/* fire_pulse() energises the channel SPDT relay (NC→NO) for duration_ms
 * using a hardware timer ISR → xTaskNotifyFromISR() → task deactivates relay.
 * If safe_after is non-zero, relay_all_safe() is called after expiry.
 * Returns actual elapsed ms measured by esp_timer_get_time(). */
void hw_fire_timer_init(void);
int  fire_pulse(int ch, uint32_t duration_ms, int safe_after);
void fire_abort(void);   /* signal an in-progress fire_pulse() to stop immediately */
