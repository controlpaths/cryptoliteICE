/*
 * ice40.h — iCE40UP5K control: reset, clock, CDONE, LEDs
 * controlpaths.com | lut7.dev
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

// Timeout waiting for CDONE to go high (ms)
#define ICE40_CDONE_TIMEOUT_MS  3000

// Initialise all iCE40 control GPIOs (CRESET, CDONE, ICE_CLK, LEDs).
// CRESET is asserted LOW (FPGA held in reset), clock is disabled, LEDs off.
void ice40_init(void);

// Assert CRESET low for ≥200 ns, then release.
void ice40_reset(void);

// Enable 48 MHz clock on PIN_ICE_CLK via CLK_GPOUT.
void ice40_clock_start(void);

// Disable the clock output on PIN_ICE_CLK.
void ice40_clock_stop(void);

// Returns true when CDONE (PIN_ICE_DONE) is high (FPGA configured).
bool ice40_is_configured(void);

// Assert CRESET low and reclaim the SPI bus for PIO.
// Must be called before ice40_boot() after any previous flash access.
void ice40_claim_spi(void);

// Flash-master boot: releases all SPI pins to HI-Z, starts the 48 MHz clock,
// pulses CRESET, then polls CDONE until the iCE40 finishes auto-configuring.
// Returns true when CDONE goes HIGH, false on timeout.
bool ice40_boot(void);

// Reset the FPGA and let it reconfigure from flash.
// All SPI pins (including CS) are released to HI-Z so the iCE40 drives the
// bus as SPI master.  CRESET is held LOW for reset_ms milliseconds.
// Returns true when CDONE goes HIGH, false on timeout.
bool ice40_reboot(uint32_t reset_ms);

// Attach the SPI PIO master to the configured FPGA slave.
// Unlike ice40_claim_spi(), this leaves CRESET HIGH so the FPGA stays
// configured. CS_B is driven HIGH so the external flash on the same bus
// stays de-selected — only the FPGA's SO is sampled while clocking.
void ice40_attach_spi_master(void);

// LED control
void led_set(uint8_t led_pin, bool on);
void led_blink(uint8_t led_pin, uint32_t period_ms);
