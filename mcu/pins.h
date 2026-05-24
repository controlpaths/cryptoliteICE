/*
 * pins.h — GPIO pin assignments for CryptoLite-ICE (RP2040 + iCE40UP5K)
 * controlpaths.com | lut7.dev
 */

#pragma once

// ── SPI to iCE40 flash (U6: W25Q32) — driven by PIO ──────────────────────
// TODO: these pins are general-purpose GPIO, not the RP2040 hardware SPI
//       (SPI0/SPI1) peripheral pins, so the firmware is forced to bit-bang
//       SPI through PIO (see spi_pio.c / spi_pio.pio). Reassign the SPI bus to
//       hardware-SPI-capable pins in a future board revision to free the PIO
//       and simplify the driver.
#define PIN_ICE_SI      8   // MOSI  → iCE40 SPI_SI  (Pin 17) & U6 SI
#define PIN_ICE_SO      11  // MISO  ← iCE40 SPI_SO  (Pin 14) & U6 SO
#define PIN_ICE_SSn     12  // CS    → iCE40 SPI_SS_B (Pin 16) & U6 CS  (active LOW)
#define PIN_ICE_SCK     13  // CLK   → iCE40 SPI_SCK (Pin 15) & U6 SCK

// ── iCE40 control signals ─────────────────────────────────────────────────
#define PIN_ICE_RESET   18  // CRESET_B  (active LOW, controlled by RP2040)
#define PIN_ICE_CLK     25  // FPGA main clock (48 MHz via CLK_GPOUT)
#define PIN_ICE_DONE    29  // CDONE — HIGH when FPGA is configured

// ── Application bus (iCE40 ↔ RP2040, general purpose) ────────────────────
#define PIN_ICE_APP0    7   // iCE40 IOB_18a (Pin 10)
#define PIN_ICE_APP1    6   // iCE40 IOB_20a (Pin 11)

// ── Status LEDs (active HIGH) ─────────────────────────────────────────────
#define PIN_LD1         2   // LED LD1
#define PIN_LD2         3   // LED LD2
