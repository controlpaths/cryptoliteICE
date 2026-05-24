/*
 * led_status.h — Two-LED visual status machine for CryptoLite-RP
 * controlpaths.com | lut7.dev
 *
 * Encodes the board state on D5 (LD1) + D6 (LD2):
 *
 *   STATE_BOOT       → both LEDs off
 *   STATE_NO_FPGA    → LD1 solid on, LD2 off            (FPGA not configured)
 *   STATE_IDLE       → LD2 solid on, LD1 off            (FPGA ready)
 *   STATE_BUSY       → LD1+LD2 fast blink in phase      (RNG transfer active)
 *   STATE_PROGRAM    → LD1+LD2 alternating              (flashing FPGA)
 *   STATE_ERROR      → LD1 fast blink, LD2 off          (sticky error)
 */

#pragma once

#include <stdbool.h>

typedef enum {
    LED_STATE_BOOT = 0,
    LED_STATE_NO_FPGA,
    LED_STATE_IDLE,
    LED_STATE_BUSY,
    LED_STATE_PROGRAM,
    LED_STATE_ERROR,
} led_state_t;

void led_status_init(void);
void led_status_set(led_state_t state);
led_state_t led_status_get(void);
void led_status_task(void);
