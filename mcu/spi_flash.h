/*
 * spi_flash.h — W25Q32 SPI flash driver for CryptoLite-ICE
 * Operates over PIO SPI on iCE40 flash (U6)
 * controlpaths.com | lut7.dev
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// W25Q32 geometry
#define FLASH_PAGE_SIZE         256U
#define FLASH_SECTOR_SIZE       4096U
#define FLASH_BLOCK_SIZE        65536U
#define FLASH_TOTAL_SIZE        (4U * 1024U * 1024U)

// Expected JEDEC ID for W25Q32JV (EF 40 16)
#define FLASH_JEDEC_MANUF       0xEF
#define FLASH_JEDEC_TYPE        0x40
#define FLASH_JEDEC_CAPACITY    0x16

extern uint8_t g_flash_jedec[3];
extern bool    g_flash_miso_idle;
extern uint8_t g_flash_sr1;
extern uint8_t g_flash_sr2;

// Initialise flash driver. Returns true if W25Q32 JEDEC ID is detected.
bool flash_init(void);

// Clear block-protection bits in SR1 so erase/program commands are honored.
// flash_write() calls this internally; flash_writer.c calls it once before
// a multi-sector erase pass.
void flash_unprotect(void);

// Read len bytes from addr.
void flash_read(uint32_t addr, uint8_t *buf, size_t len);

// Erase 4 KB sector containing addr (rounds down to sector boundary).
void flash_erase_sector(uint32_t addr);

// Erase 64 KB block containing addr (rounds down to block boundary).
void flash_erase_block(uint32_t addr);

// Program up to FLASH_PAGE_SIZE bytes at page-aligned addr.
void flash_write_page(uint32_t addr, const uint8_t *data, size_t len);

// High-level write: erase required sectors then program page by page.
void flash_write(uint32_t addr, const uint8_t *data, size_t len);

// Read back and compare. Stores address of first mismatch in *fail_addr.
bool flash_verify(uint32_t addr, const uint8_t *data, size_t len, uint32_t *fail_addr);
