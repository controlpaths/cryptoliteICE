/*
 * ice40.c — iCE40UP5K control for CryptoLite-ICE
 * controlpaths.com | lut7.dev
 */

#include "ice40.h"
#include "spi_pio.h"
#include "pins.h"
#include "tusb.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "pico/time.h"

#ifndef CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLK_USB
#define CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLK_USB 0x07
#endif

void ice40_init(void) {
    // CRESET: output, held LOW — keeps iCE40 in reset
    gpio_init(PIN_ICE_RESET);
    gpio_set_dir(PIN_ICE_RESET, GPIO_OUT);
    gpio_put(PIN_ICE_RESET, 0);

    // CDONE: input with pull-down
    gpio_init(PIN_ICE_DONE);
    gpio_set_dir(PIN_ICE_DONE, GPIO_IN);
    gpio_pull_down(PIN_ICE_DONE);

    // ICE_CLK: low until FPGA is booted
    gpio_init(PIN_ICE_CLK);
    gpio_set_dir(PIN_ICE_CLK, GPIO_OUT);
    gpio_put(PIN_ICE_CLK, 0);

    // Status LEDs: output, off
    gpio_init(PIN_LD1);
    gpio_set_dir(PIN_LD1, GPIO_OUT);
    gpio_put(PIN_LD1, 0);

    gpio_init(PIN_LD2);
    gpio_set_dir(PIN_LD2, GPIO_OUT);
    gpio_put(PIN_LD2, 0);
}

void ice40_reset(void) {
    gpio_put(PIN_ICE_RESET, 0);
    busy_wait_us(1);
    gpio_put(PIN_ICE_RESET, 1);
    sleep_ms(2);
}

void ice40_clock_start(void) {
    clock_gpio_init(PIN_ICE_CLK, CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLK_USB, 1);
}

void ice40_clock_stop(void) {
    gpio_init(PIN_ICE_CLK);
    gpio_set_dir(PIN_ICE_CLK, GPIO_OUT);
    gpio_put(PIN_ICE_CLK, 0);
}

bool ice40_is_configured(void) {
    return gpio_get(PIN_ICE_DONE);
}

void ice40_claim_spi(void) {
    gpio_put(PIN_ICE_RESET, 0);
    sleep_ms(1);
    spi_pio_init();
    gpio_init(PIN_ICE_SSn);
    gpio_set_dir(PIN_ICE_SSn, GPIO_OUT);
    gpio_put(PIN_ICE_SSn, 1);
}

bool ice40_boot(void) {
    // 1. Stop PIO SM
    pio_sm_set_enabled(SPI_PIO_BLOCK, SPI_PIO_SM, false);

    // 2. Release all SPI lines to HI-Z (external R8 pulls SS_B HIGH → master SPI mode)
    gpio_init(PIN_ICE_SCK);  gpio_set_dir(PIN_ICE_SCK, GPIO_IN);  gpio_disable_pulls(PIN_ICE_SCK);
    gpio_init(PIN_ICE_SI);   gpio_set_dir(PIN_ICE_SI,  GPIO_IN);  gpio_disable_pulls(PIN_ICE_SI);
    gpio_init(PIN_ICE_SO);   gpio_set_dir(PIN_ICE_SO,  GPIO_IN);  gpio_disable_pulls(PIN_ICE_SO);
    gpio_init(PIN_ICE_SSn);  gpio_set_dir(PIN_ICE_SSn, GPIO_IN);  gpio_disable_pulls(PIN_ICE_SSn);

    // 3. Wait for SS_B to settle via R8
    sleep_ms(2);

    // 4. Pulse CRESET: iCE40 latches master SPI mode when SS_B is HIGH at CRESET↑
    //    Ensure RESET pin is configured as output before driving it.
    gpio_set_function(PIN_ICE_RESET, GPIO_FUNC_SIO);
    gpio_set_dir(PIN_ICE_RESET, GPIO_OUT);
    gpio_put(PIN_ICE_RESET, 0);
    sleep_ms(1);
    gpio_put(PIN_ICE_RESET, 1);

    // 5. Poll CDONE (iCE40 configures from U6 flash autonomously)
    bool cdone = false;
    uint32_t t0 = to_ms_since_boot(get_absolute_time());
    while (to_ms_since_boot(get_absolute_time()) - t0 < ICE40_CDONE_TIMEOUT_MS) {
        if (gpio_get(PIN_ICE_DONE)) {
            cdone = true;
            break;
        }
        tud_task();
    }

    // 6. Start 48 MHz user clock regardless of CDONE
    ice40_clock_start();

    return cdone;
}

bool ice40_reboot(uint32_t reset_ms) {
    // 1. Stop PIO SM if it happens to be running
    pio_sm_set_enabled(SPI_PIO_BLOCK, SPI_PIO_SM, false);

    // 2. Stop the user clock before touching the FPGA
    ice40_clock_stop();

    // 3. Release all SPI pins to HI-Z, no internal pulls.
    //    External R8 (10k) keeps SS_B HIGH so iCE40 enters master SPI mode.
    //    CS must not be driven by RP2040 during configuration so the FPGA
    //    can control the pin to read the flash autonomously.
    gpio_init(PIN_ICE_SCK);  gpio_set_dir(PIN_ICE_SCK, GPIO_IN);  gpio_disable_pulls(PIN_ICE_SCK);
    gpio_init(PIN_ICE_SI);   gpio_set_dir(PIN_ICE_SI,  GPIO_IN);  gpio_disable_pulls(PIN_ICE_SI);
    gpio_init(PIN_ICE_SO);   gpio_set_dir(PIN_ICE_SO,  GPIO_IN);  gpio_disable_pulls(PIN_ICE_SO);
    gpio_init(PIN_ICE_SSn);  gpio_set_dir(PIN_ICE_SSn, GPIO_IN);  gpio_disable_pulls(PIN_ICE_SSn);

    // 4. Wait for SS_B to settle to HIGH via R8 before asserting CRESET
    sleep_ms(2);

    // 5. Assert CRESET LOW for the requested duration, then release
    gpio_set_function(PIN_ICE_RESET, GPIO_FUNC_SIO);
    gpio_set_dir(PIN_ICE_RESET, GPIO_OUT);
    gpio_put(PIN_ICE_RESET, 0);
    sleep_ms(reset_ms);
    gpio_put(PIN_ICE_RESET, 1);

    // 6. Poll CDONE, keep USB alive while waiting
    bool cdone = false;
    uint32_t t0 = to_ms_since_boot(get_absolute_time());
    while (to_ms_since_boot(get_absolute_time()) - t0 < ICE40_CDONE_TIMEOUT_MS) {
        if (gpio_get(PIN_ICE_DONE)) {
            cdone = true;
            break;
        }
        tud_task();
    }

    // 7. Restart the 48 MHz user clock
    ice40_clock_start();

    return cdone;
}

void ice40_attach_spi_master(void) {
    /* FPGA stays configured (CRESET HIGH). Re-init the PIO SPI block and
     * keep the external flash de-selected by driving CS_B HIGH from the
     * RP2040. The FPGA slave ignores SS and shifts on every SCK edge. */
    spi_pio_init();
    gpio_init(PIN_ICE_SSn);
    gpio_set_dir(PIN_ICE_SSn, GPIO_OUT);
    gpio_put(PIN_ICE_SSn, 1);
}

void led_set(uint8_t led_pin, bool on) {
    gpio_put(led_pin, on ? 1 : 0);
}

void led_blink(uint8_t led_pin, uint32_t period_ms) {
    static uint32_t last_toggle_ld1 = 0;
    static uint32_t last_toggle_ld2 = 0;
    static bool state_ld1 = false;
    static bool state_ld2 = false;

    uint32_t now = to_ms_since_boot(get_absolute_time());
    bool     *state = (led_pin == PIN_LD1) ? &state_ld1 : &state_ld2;
    uint32_t *last  = (led_pin == PIN_LD1) ? &last_toggle_ld1 : &last_toggle_ld2;

    if (now - *last >= period_ms / 2) {
        *state = !(*state);
        gpio_put(led_pin, *state ? 1 : 0);
        *last = now;
    }
}
