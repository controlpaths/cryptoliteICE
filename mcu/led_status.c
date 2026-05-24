/*
 * led_status.c — Two-LED visual status machine for CryptoLite-RP
 * controlpaths.com | lut7.dev
 */

#include "led_status.h"
#include "ice40.h"
#include "pins.h"
#include "pico/time.h"

static led_state_t _state = LED_STATE_BOOT;

/* Each entry into LED_STATE_BUSY arms _busy_until. While now() < _busy_until,
 * incoming led_status_set(LED_STATE_IDLE) requests are ignored. This makes
 * sub-millisecond random commands actually visible — the blink runs for at
 * least BUSY_MIN_HOLD_MS, which a long burst of commands keeps re-arming. */
#define BUSY_MIN_HOLD_MS  300u
static uint32_t _busy_until = 0;

void led_status_init(void) {
    led_set(PIN_LD1, false);
    led_set(PIN_LD2, false);
    _state = LED_STATE_BOOT;
}

void led_status_set(led_state_t state) {
    uint32_t now = to_ms_since_boot(get_absolute_time());

    if (state == LED_STATE_BUSY) {
        _busy_until = now + BUSY_MIN_HOLD_MS;
    }
    /* While the BUSY hold is in effect, swallow IDLE transitions. The
     * pending IDLE will be applied by led_status_task() once the timer
     * expires. */
    if (state == LED_STATE_IDLE
            && _state == LED_STATE_BUSY
            && (int32_t)(now - _busy_until) < 0) {
        return;
    }

    if (state == _state) {
        return;
    }
    _state = state;

    /* Apply the steady-state pattern immediately so the change is visible
     * before the next led_status_task() tick. */
    switch (_state) {
        case LED_STATE_BOOT:
            led_set(PIN_LD1, false);
            led_set(PIN_LD2, false);
            break;
        case LED_STATE_NO_FPGA:
            led_set(PIN_LD1, true);
            led_set(PIN_LD2, false);
            break;
        case LED_STATE_IDLE:
            led_set(PIN_LD1, false);
            led_set(PIN_LD2, true);
            break;
        default:
            /* Blink-driven states: handled in led_status_task(). */
            break;
    }
}

led_state_t led_status_get(void) {
    return _state;
}

void led_status_task(void) {
    static uint32_t last_toggle = 0;
    static bool     phase = false;

    uint32_t now = to_ms_since_boot(get_absolute_time());

    /* Honour the BUSY hold timer: once it expires and nothing has re-armed
     * it, drop back to IDLE. */
    if (_state == LED_STATE_BUSY && (int32_t)(now - _busy_until) >= 0) {
        led_status_set(LED_STATE_IDLE);
    }

    switch (_state) {
        case LED_STATE_BUSY: {
            /* Both LEDs blink in phase, ~10 Hz */
            if (now - last_toggle >= 50) {
                phase = !phase;
                led_set(PIN_LD1, phase);
                led_set(PIN_LD2, phase);
                last_toggle = now;
            }
            break;
        }
        case LED_STATE_PROGRAM: {
            /* Alternating, ~5 Hz */
            if (now - last_toggle >= 100) {
                phase = !phase;
                led_set(PIN_LD1, phase);
                led_set(PIN_LD2, !phase);
                last_toggle = now;
            }
            break;
        }
        case LED_STATE_ERROR: {
            /* LD1 fast blink, LD2 off */
            if (now - last_toggle >= 75) {
                phase = !phase;
                led_set(PIN_LD1, phase);
                led_set(PIN_LD2, false);
                last_toggle = now;
            }
            break;
        }
        default:
            /* Steady state — nothing to do. */
            break;
    }
}
