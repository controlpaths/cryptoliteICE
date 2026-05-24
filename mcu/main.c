/*
 * main.c — CryptoLite-RP firmware entry point
 * Vendor-only USB device controlled by criptolite-ice.py.
 * controlpaths.com | lut7.dev
 */

#include "tusb.h"
#include "pico/stdlib.h"

#include "pins.h"
#include "ice40.h"
#include "led_status.h"
#include "trng.h"
#include "rng_pipeline.h"
#include "protocol.h"

int main(void) {
    /* GPIOs first: CRESET LOW, CDONE input, ICE_CLK off, LEDs off. */
    ice40_init();
    led_status_init();

    /* USB up before anything that might block — the host has a 1 s
     * enumeration window. */
    tusb_init();
    protocol_init();

    /* Claim SPI with FPGA in reset, then let it auto-configure from U6. */
    ice40_claim_spi();
    bool configured = ice40_boot();

    if (configured) {
        /* Re-attach the SPI master to the now-running FPGA slave so the
         * TRNG sampler can read random bytes out of it. */
        ice40_attach_spi_master();
        rng_pipeline_init();
        led_status_set(LED_STATE_IDLE);
    } else {
        led_status_set(LED_STATE_NO_FPGA);
    }

    while (true) {
        tud_task();
        protocol_task();
        trng_task();
        rng_pipeline_task();
        led_status_task();
    }
}
