/*
 * trng.h — Raw entropy capture from the iCE40 TRNG (SPI variant)
 * controlpaths.com | lut7.dev
 *
 * After the FPGA is configured, it acts as a tiny SPI slave that shifts
 * one fresh random bit out on SO per SCK falling edge. The RP2040 reads
 * a continuous byte stream from the slave at the SPI PIO clock rate.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Power-of-two size of the internal ring buffer (in bytes). */
#define TRNG_BUFFER_BYTES   2048u

/* Initialise the TRNG sink. Must be called once after the FPGA has been
 * configured and ice40_attach_spi_master() has been invoked. */
void trng_init(void);

/* Pump the TRNG: pulls a small chunk out of the FPGA over SPI into the
 * ring buffer if there is room. Non-blocking-ish (blocks for the few
 * hundred microseconds the chunk takes on the wire). Skipped while
 * trng_set_paused(true) is in effect (e.g. during flash programming). */
void trng_task(void);

/* Pause / resume the SPI reads. Used by the flash programming path so
 * the master SPI is not driven concurrently from two places. */
void trng_set_paused(bool paused);
bool trng_is_paused(void);

/* Bytes currently buffered (available for trng_read_bytes). */
size_t trng_available_bytes(void);

/* Drain up to n bytes from the buffer into dst. Returns bytes copied. */
size_t trng_read_bytes(uint8_t *dst, size_t n);

/* Diagnostic counters. Either pointer may be NULL. */
void trng_get_stats(uint32_t *bits_seen, uint32_t *overflows);
