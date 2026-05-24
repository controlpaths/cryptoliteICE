/*
 * spi_pio.h — PIO-based SPI driver for CryptoLite-ICE
 * SPI Mode 0, MSB first
 * controlpaths.com | lut7.dev
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "hardware/pio.h"

// PIO instance and state machine used by this driver
#define SPI_PIO_BLOCK   pio0
#define SPI_PIO_SM      0

// SCK frequency for iCE40 flash operations
#define SPI_PIO_FREQ_HZ 1000000

// Initialise PIO SPI. Must be called once before any transfer.
void spi_pio_init(void);

// Write len bytes from src. MISO is discarded.
void spi_pio_write(const uint8_t *src, size_t len);

// Read len bytes into dst. Sends 0x00 on MOSI.
void spi_pio_read(uint8_t *dst, size_t len);

// Full-duplex: write src and simultaneously read into dst.
void spi_pio_write_read(const uint8_t *src, uint8_t *dst, size_t len);
