#pragma once

void hw_encoder_init(void);
int  enc_get_count(void);
void enc_reset_count(void);
int  enc_get_direction(void);    /* last step: +1 CW, -1 CCW, 0 none */
