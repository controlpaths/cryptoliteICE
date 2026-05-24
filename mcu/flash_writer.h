/*
 * flash_writer.h — Bitstream programming over USB Vendor for CryptoLite-RP
 * controlpaths.com | lut7.dev
 *
 * Three-phase programming flow driven by the host:
 *
 *   FLASH_BEGIN  → reset FPGA, claim SPI, init flash, erase region 0..size
 *   FLASH_DATA   → write a chunk at offset
 *   FLASH_END    → verify, release SPI, reboot FPGA, return CDONE result
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef enum {
    FW_OK = 0,
    FW_ERR_BUSY,
    FW_ERR_FLASH_ID,
    FW_ERR_NOT_BEGUN,
    FW_ERR_RANGE,
    FW_ERR_VERIFY,
    FW_ERR_TIMEOUT,
} fw_status_t;

/* Begin a programming session. Holds CRESET LOW, claims the SPI bus,
 * initialises the flash driver and erases sectors 0..total_size. */
fw_status_t flash_writer_begin(uint32_t total_size);

/* Write `len` bytes at `offset` into the flash. Caller must have called
 * flash_writer_begin() first. offset+len must fit within total_size. */
fw_status_t flash_writer_data(uint32_t offset, const uint8_t *data, size_t len);

/* Finish the session: verify the written region against `expected_crc32`
 * (set crc32 to 0 to skip verification), release SPI, reboot the FPGA.
 * Outputs CDONE result in *cdone_out (true if FPGA configured). */
fw_status_t flash_writer_end(uint32_t expected_crc32, bool *cdone_out);

bool flash_writer_in_progress(void);
