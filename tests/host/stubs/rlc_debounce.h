#pragma once
#include <stdbool.h>
/* Stub: the encoder's button path is not under test here. */
typedef struct { int unused; } rlc_debounce_t;
#define DEBOUNCE_8BIT  0
#define DEBOUNCE_16BIT 1
static inline void rlc_debounce_init(rlc_debounce_t *d, int pin, int w)
{ (void)d;(void)pin;(void)w; }
static inline void rlc_debounce_update(rlc_debounce_t *d, int lvl,
                                       void (*cb)(int, bool, void *), void *u)
{ (void)d;(void)lvl;(void)cb;(void)u; }
